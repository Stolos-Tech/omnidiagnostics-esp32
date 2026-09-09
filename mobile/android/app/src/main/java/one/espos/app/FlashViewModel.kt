package one.espos.app

import android.app.Application
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlinx.serialization.json.*
import one.espos.client.BoardFlash
import one.espos.client.Updater
import one.espos.flash.EspFlasher
import one.espos.flash.UsbSerial
import java.io.File
import java.security.MessageDigest

/**
 * FlashViewModel — прошивка ПЛАТИ через USB-OTG з телефона (task: flash-over-phone).
 * Пише образ НАПРЯМУ в app-партицію (обхід проблеми 4МБ/dual-OTA — дві партиції не
 * потрібні). Джерело образу: сервер (/firmware.bin + /api/firmware з SHA-256).
 *
 * Кроки в UI: 1) SCAN USB (детект плати+дозвіл) · 2) отримати firmware з сервера ·
 * 3) FLASH (reset→bootloader→запис→MD5→reset). Усе на IO, весь стан — у полях нижче.
 */
class FlashViewModel(app: Application) : AndroidViewModel(app) {
    // виявлений USB-пристрій (перший підхожий драйвер)
    var usbLabel by mutableStateOf("")
    var usbReady by mutableStateOf(false)
    private var driver: com.hoho.android.usbserial.driver.UsbSerialDriver? = null

    // маніфест прошивки з сервера
    var fwVersion by mutableStateOf("")
    var fwSha by mutableStateOf("")
    var fwSize by mutableStateOf(0L)
    var fwNotes by mutableStateOf("")
    var fwOffset by mutableStateOf(EspFlasher.APP_OFFSET)
    // Тести оновлення (з маніфесту): Triple(name, board-path, expect-substring). Прогоняємо
    // після прошивки; сервер видаляє їх після успіху (щоб не роздувати оновлення).
    var fwTests by mutableStateOf<List<Triple<String, String, String>>>(emptyList())
    var testsPassed by mutableStateOf(false)

    // адреса плати для WiFi-шляху (синхронізується з MainViewModel host/pin)
    var boardHost by mutableStateOf("")
    var boardPin by mutableStateOf("")
    var uk by mutableStateOf(false)                      // мова (синк з MainViewModel.lang)
    private fun tr(u: String, e: String) = if (uk) u else e
    // одна сесія логів на цикл (завантаж→прошивка→тести); дзеркалиться на сервер.
    private var sessionId: String = ""
    private fun ensureSession(title: String) { if (sessionId.isBlank()) sessionId = LogStore.newId(); sessionTitle = title }
    private var sessionTitle = ""

    // прогрес/стан
    var busy by mutableStateOf(false)
    var stage by mutableStateOf("")           // людиночитний етап
    var dlProgress by mutableStateOf(-1)       // завантаження з сервера, 0..100
    var flProgress by mutableStateOf(-1)       // запис у флеш, 0..100
    var lines by mutableStateOf<List<String>>(emptyList())
    var done by mutableStateOf(false)
    var error by mutableStateOf("")

    private fun log(s: String) { lines = (lines + s).takeLast(120) }
    private fun ctx() = getApplication<Application>().applicationContext

    /** Крок 1: знайти плату на USB і попросити дозвіл. */
    fun scanUsb() {
        error = ""; usbReady = false; driver = null; usbLabel = ""
        val devs = UsbSerial.list(ctx())
        if (devs.isEmpty()) { usbLabel = tr("не знайдено (під'єднай плату OTG-кабелем)", "not found (attach board via OTG cable)"); return }
        val d = devs.first()
        driver = d.driver; usbLabel = d.label
        UsbSerial.requestPermission(ctx(), d.driver.device) { ok ->
            usbReady = ok
            usbLabel = if (ok) d.label + tr(" · дозволено", " · granted") else d.label + tr(" · дозвіл відхилено", " · denied")
        }
    }

    /** Крок 2: підтягнути маніфест прошивки з сервера. */
    fun refreshFirmware(serverUrl: String) = io {
        stage = tr("маніфест прошивки…", "firmware manifest…")
        val m = Updater.firmwareManifest(serverUrl)
        if (m == null) { stage = tr("нема /api/firmware на сервері", "no /api/firmware on server"); return@io }
        fwVersion = m["versionName"]?.jsonPrimitive?.content ?: "?"
        fwSha = m["sha256"]?.jsonPrimitive?.content ?: ""
        fwSize = m["size"]?.jsonPrimitive?.longOrNull ?: 0L
        fwNotes = m["notes"]?.jsonPrimitive?.content ?: ""
        fwOffset = m["offset"]?.jsonPrimitive?.intOrNull ?: EspFlasher.APP_OFFSET
        fwTests = m["tests"]?.jsonArray?.mapNotNull {
            val o = it.jsonObject
            val name = o["name"]?.jsonPrimitive?.content ?: return@mapNotNull null
            Triple(name, o["path"]?.jsonPrimitive?.content ?: "", o["expect"]?.jsonPrimitive?.content ?: "")
        } ?: emptyList()
        stage = tr("прошивка v$fwVersion готова (${fwSize / 1024} КБ)", "firmware v$fwVersion ready (${fwSize / 1024} KB)")
    }

    /** Кнопка 1: лише СКАЧАТИ прошивку з сервера у кеш (SHA-256), без прошивки. */
    fun downloadOnly(serverUrl: String) = io {
        sessionId = ""; lines = emptyList(); testsPassed = false   // нове СКАЧАТИ = нова сесія логів
        ensureSession(tr("Оновлення v$fwVersion", "Update v$fwVersion"))
        busy = true; error = ""; dlProgress = 0
        stage = tr("завантаження прошивки…", "downloading firmware…")
        val dest = File(ctx().cacheDir, "firmware.bin")
        val ok = Updater.downloadFirmware(serverUrl, fwSha, dest) { p -> dlProgress = p }
        dlProgress = -1; busy = false
        if (ok) { log(tr("✓ прошивка ${dest.length() / 1024} КБ у кеші · SHA-256 ✓", "✓ firmware ${dest.length() / 1024} KB cached · SHA-256 ✓")); stage = tr("готово до заливки", "ready to flash") }
        else log(tr("✗ завантаження/SHA не пройшло", "✗ download/SHA failed"))
        pushSession(serverUrl)
    }

    /** Кнопка 3: прогнати тести оновлення проти плати. Успіх -> сервер видаляє тести. */
    fun runTests(serverUrl: String) = io {
        ensureSession(tr("Оновлення прошивки", "Firmware update"))
        if (boardHost.isBlank()) { log(tr("✗ тести: нема адреси плати (під'єднайся)", "✗ tests: no board address")); pushSession(serverUrl); return@io }
        busy = true; stage = tr("тести…", "tests…")
        var pass = 0; val total = fwTests.size
        if (total == 0) log(tr("· у цьому оновленні тестів нема", "· no tests in this update"))
        for ((name, path, expect) in fwTests) {
            val body = one.espos.client.BoardFlash.get(boardHost, path)
            val ok = body != null && (expect.isBlank() || body.contains(expect))
            if (ok) pass++
            log((if (ok) "✓ " else "✗ ") + name + (if (!ok) " (" + tr("нема '$expect'", "missing '$expect'") + ")" else ""))
        }
        testsPassed = total > 0 && pass == total
        stage = tr("тести: $pass/$total", "tests: $pass/$total")
        if (testsPassed) {
            log(tr("✓ усі тести пройдено — прошу сервер видалити тести оновлення", "✓ all tests passed — asking server to drop update tests"))
            Updater.firmwareTestsClear(serverUrl)
        }
        busy = false
        pushSession(serverUrl)
    }

    /** Зберегти поточні рядки як сесію (телефон) + віддзеркалити на сервер (коли бачимо). */
    private fun pushSession(serverUrl: String) {
        if (sessionId.isBlank()) return
        LogStore.append(ctx(), sessionId, sessionTitle, lines)
        val payload = buildJsonObject {
            put("id", sessionId); put("title", sessionTitle)
            put("lines", buildJsonArray { lines.forEach { add(it) } })
        }
        Updater.logSessionIngest(serverUrl, payload.toString())
    }

    /** Крок 3: завантажити образ і прошити плату через USB. */
    fun flash(serverUrl: String) = io {
        if (driver == null) { error = tr("спершу SCAN USB", "SCAN USB first"); return@io }
        if (!usbReady) { error = tr("нема дозволу на USB-пристрій", "no USB device permission"); return@io }
        ensureSession(tr("Оновлення прошивки", "Firmware update"))
        busy = true; done = false; error = ""; flProgress = -1

        // 2a. завантажити firmware.bin (SHA-256)
        stage = tr("завантаження прошивки…", "downloading firmware…"); dlProgress = 0
        val dest = File(ctx().cacheDir, "firmware.bin")
        val ok = Updater.downloadFirmware(serverUrl, fwSha, dest) { p -> dlProgress = p }
        dlProgress = -1
        if (!ok || !dest.exists()) { error = tr("завантаження/SHA-256 не пройшло", "download/SHA-256 failed"); busy = false; return@io }
        log("firmware.bin ${dest.length() / 1024} " + tr("КБ", "KB") + " · SHA-256 ✓")

        // 2b. прошити
        var link: one.espos.flash.SerialLink? = null
        try {
            stage = tr("з'єднання з платою по USB…", "connecting to board over USB…")
            link = UsbSerial.open(ctx(), driver!!, EspFlasher.SYNC_BAUD)
            val image = dest.readBytes()
            val flasher = EspFlasher(link) { s -> log(s) }
            stage = tr("прошивка…", "flashing…")
            flasher.flash(image, fwOffset, highSpeed = true) { p -> flProgress = p }
            flProgress = 100
            stage = tr("готово ✓", "done ✓"); done = true
        } catch (e: Exception) {
            error = e.message ?: tr("збій прошивки", "flash failed")
            log("✗ " + error)
            stage = tr("збій", "failed")
        } finally {
            link?.close(); busy = false
        }
        pushSession(serverUrl)
    }

    /** Фаза 2: прошивка по WiFi через SD плати (без кабелю). Плата має бути на OTA-таблиці
     *  (factory-updater) + SD змонтована. */
    fun wifiFlash(serverUrl: String) = io {
        if (boardHost.isBlank()) { error = tr("нема адреси плати (під'єднайся на вкладці DEVICE)", "no board address (connect on the DEVICE tab)"); return@io }
        busy = true; done = false; error = ""; lines = emptyList(); flProgress = -1

        stage = tr("завантаження прошивки…", "downloading firmware…"); dlProgress = 0
        val dest = File(ctx().cacheDir, "firmware.bin")
        val ok = Updater.downloadFirmware(serverUrl, fwSha, dest) { p -> dlProgress = p }
        dlProgress = -1
        if (!ok || !dest.exists()) { error = tr("завантаження/SHA-256 не пройшло", "download/SHA-256 failed"); busy = false; return@io }
        val md5 = md5hex(dest)
        log("firmware.bin ${dest.length() / 1024} " + tr("КБ", "KB"))

        stage = tr("логін на плату…", "logging in to board…")
        if (!BoardFlash.login(boardHost, boardPin)) { error = tr("логін на плату не вдався (PIN?)", "board login failed (PIN?)"); busy = false; return@io }
        val st = BoardFlash.status(boardHost)
        if (st?.get("factory")?.jsonPrimitive?.booleanOrNull != true) {
            error = tr("на платі нема factory-updater — спершу USB-міграція на OTA-таблицю", "no factory-updater on board — do the USB migration to the OTA table first"); busy = false; return@io
        }
        if (st["sd"]?.jsonPrimitive?.booleanOrNull != true) {
            error = tr("SD не змонтована на платі (TOOLS → SD mount)", "SD not mounted on board (TOOLS → SD mount)"); busy = false; return@io
        }

        stage = tr("заливка на SD плати…", "uploading to board SD…"); flProgress = 0
        if (!BoardFlash.stage(boardHost, dest, md5) { p -> flProgress = p }) {
            error = tr("заливка на SD не вдалась", "SD upload failed"); busy = false; return@io
        }
        flProgress = 100
        log(tr("образ на SD ✓", "image on SD ✓"))
        stage = tr("ребут у updater…", "rebooting into updater…")
        BoardFlash.apply(boardHost)
        done = true; stage = tr("плата прошивається з SD ✓", "board is flashing from SD ✓")
        log(tr("✓ apply надіслано — плата пише ота_0 з SD і перезавантажується", "✓ apply sent — board writes ota_0 from SD and reboots"))
        busy = false
    }

    private fun md5hex(f: File): String {
        val md = MessageDigest.getInstance("MD5")
        f.inputStream().use { inp -> val b = ByteArray(1 shl 16); var n: Int; while (inp.read(b).also { n = it } >= 0) md.update(b, 0, n) }
        return md.digest().joinToString("") { "%02x".format(it) }
    }

    private fun io(block: suspend () -> Unit) = viewModelScope.launch {
        try { withContext(Dispatchers.IO) { block() } }
        catch (e: Exception) { error = e.message ?: "error"; busy = false }
    }
}
