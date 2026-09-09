package one.espos.app

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.MenuBook
import androidx.compose.material3.*
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.draw.clip
import kotlinx.coroutines.launch
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import android.content.Context
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import androidx.lifecycle.viewmodel.compose.viewModel
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlinx.serialization.json.*
import one.espos.app.ui.*
import one.espos.client.*

/**
 * MainViewModel — клієнт + стан. Уся мережа на IO. Тягне СИРІ дані (спектр, статус-історію),
 * а малює телефон (APEX-HUD) — так плата розвантажена, а візуалу можна скільки завгодно.
 */
class MainViewModel : ViewModel() {
    var lang by mutableStateOf(Lang.EN)
    fun toggleLang() { lang = if (lang == Lang.UK) Lang.EN else Lang.UK }
    private fun tr(uk: String, en: String) = if (lang == Lang.UK) uk else en
    var host by mutableStateOf("netprobe.local"); var pin by mutableStateOf("")   // mDNS-дефолт: переживає зміну DHCP-IP + чиста установка одразу знаходить плату
    // gateway-режим: доступ до плати з будь-якої мережі через шлюз (Tailscale). PIN схований на сервері.
    var useGateway by mutableStateOf(false)
    // USB-режим: керування платою по OTG-кабелю (провідний канал — плата не відпадає під час аналізу).
    var useUsb by mutableStateOf(false)
    var usbLabel by mutableStateOf("")
    private var usbLink: one.espos.client.UsbLink? = null
    // Реле плата→сервер через телефон: телефон на нормальному WiFi, плата по USB -> шлемо телеметрію на сервер.
    var relayEnabled by mutableStateOf(false)
    var relayMsg by mutableStateOf("")
    var gwHost by mutableStateOf("100.80.30.64:8443"); var gwToken by mutableStateOf("")
    var autoConnect by mutableStateOf(false)   // запам'ятати + авто-вхід на старті
    private var gatewayMode = false
    var connected by mutableStateOf(false); var msg by mutableStateOf("")
    var linkBusy by mutableStateOf(false)   // плата зайнята аналізом / перепід'єднання
    var status by mutableStateOf<DeviceStatus?>(null)
    var mirror by mutableStateOf(MirrorScreen("", emptyList(), null))
    var archive by mutableStateOf<List<ArchiveItem>>(emptyList())
    var scripts by mutableStateOf<List<ScriptFile>>(emptyList())
    var scriptName by mutableStateOf(""); var scriptOut by mutableStateOf<String?>(null)
    var themes by mutableStateOf<List<String>>(emptyList()); var themeCurrent by mutableStateOf(0)
    var sdMounted by mutableStateOf(false); var sdStatusLine by mutableStateOf("")

    // сирі дані для графіків (малюємо в додатку)
    var subghzDb by mutableStateOf<List<Int>>(emptyList()); var subghzMhz by mutableStateOf<List<Int>>(emptyList())
    var subghzPresent by mutableStateOf(false)
    var nrfCounts by mutableStateOf<List<Int>>(emptyList()); var nrfPresent by mutableStateOf(false)
    var battHist by mutableStateOf<List<Int>>(emptyList()); var rssiHist by mutableStateOf<List<Int>>(emptyList())

    private var client: EspOsClient? = null
    private fun io(block: () -> Unit) = viewModelScope.launch {
        try { withContext(Dispatchers.IO) { block() } } catch (e: Exception) { msg = e.message ?: "error" }
    }

    // Динамічний per-link seed (HMAC challenge-response). Зберігається в Prefs (sandbox-приват).
    var hmacSeed by mutableStateOf<String?>(null)
    var appContext: android.content.Context? = null   // application-context для персистенції seed
    // Логін: якщо є seed -> HMAC (динамічно, без PIN); інакше -> PIN, і при першому успіху
    // провізуємо seed (enroll). PIN лишається фолбеком → локаут неможливий.
    private fun authLogin(c: EspOsClient): Boolean {
        val seed = hmacSeed
        if (!seed.isNullOrBlank() && c.loginHmac(seed)) return true   // HMAC ок -> без PIN
        val ok = c.login(pin)                                         // PIN фолбек/бутстреп
        if (ok) {                                                     // (пере)провізувати seed і ОДРАЗУ персист
            val s = c.enroll()                                        // (connect async → Prefs.save@кнопці біжить раніше)
            if (s != null && s.length == 64 && s != hmacSeed) {
                hmacSeed = s
                appContext?.let { Prefs.save(it, this) }
            }
        }
        return ok
    }

    fun connect() = io {
        if (useGateway) {
            val c = EspOsClient(gwHost, token = gwToken); val ok = c.health()
            connected = ok; gatewayMode = ok; msg = if (ok) "GATEWAY UP" else tr("шлюз/токен: помилка", "gateway/token failed")
            if (ok) { client = c; refreshDevice(c); startPolling() }
        } else {
            // Стійкий конект: пробуємо збережений host, далі mDNS-ім'я (переживає зміну DHCP-IP
            // плати), далі SoftAP плати (стабільний). Перший, що логіниться, — робочий; запам'ятовуємо.
            val cands = listOf(host, "netprobe.local", "192.168.4.1").filter { it.isNotBlank() }.distinct()
            var okHost: String? = null; var cli: EspOsClient? = null
            for (h in cands) { val c = EspOsClient(h); if (authLogin(c)) { okHost = h; cli = c; break } }
            connected = okHost != null; gatewayMode = false
            if (okHost != null) {
                client = cli
                if (okHost != host) { host = okHost; appContext?.let { Prefs.save(it, this) } }
                msg = "LINK UP · $okHost"; refreshDevice(cli); startPolling()
            } else {
                msg = tr("не досягти плати ($host / netprobe.local / AP). Перевір: телефон на WiFi роутера, або під'єднайся до точки «OmniDiag-Setup» і став host 192.168.4.1.",
                         "board unreachable ($host / netprobe.local / AP). Check: phone on the router's WiFi, or join AP 'OmniDiag-Setup' and set host 192.168.4.1.")
            }
        }
    }
    fun refreshDevice(c: EspOsClient? = client) = io { val cc = c ?: return@io; status = cc.status(); mirror = cc.mirror() }

    // USB-керування: відкрити OTG-лінк, залогінитись PIN-ом по serial, стрімити лог плати.
    fun connectUsb(ctx: android.content.Context) {
        val devs = one.espos.flash.UsbSerial.list(ctx)
        if (devs.isEmpty()) { msg = tr("USB: плату не знайдено (OTG-кабель?)", "USB: board not found (OTG cable?)"); return }
        val d = devs.first(); usbLabel = d.label
        one.espos.flash.UsbSerial.requestPermission(ctx, d.driver.device) { ok ->
            if (!ok) { msg = tr("USB: дозвіл відхилено", "USB: permission denied"); return@requestPermission }
            io {
                try {
                    val link = one.espos.client.UsbLink.open(ctx, d.driver) { line ->
                        logLines = (logLines + line).takeLast(200)
                    }
                    usbLink = link
                    val c = EspOsClient(one.espos.client.UsbTransport(link))
                    val okLogin = authLogin(c)   // USB — довірений канал, ідеальний для enroll seed
                    connected = okLogin; gatewayMode = false
                    msg = if (okLogin) "USB LINK UP" else tr("USB: вхід не вдався (PIN?)", "USB: login failed (PIN?)")
                    if (okLogin) { client = c; refreshDevice(c); startPolling() }
                    else { link.close(); usbLink = null }
                } catch (e: Exception) { msg = e.message ?: "USB error"; usbLink?.close(); usbLink = null }
            }
        }
    }
    fun disconnect() {
        connected = false
        usbLink?.close(); usbLink = null
        client = null
    }

    private var polling = false
    private fun startPolling() { if (polling) return; polling = true; viewModelScope.launch {
        var fails = 0
        while (connected) {
            val ok = withContext(Dispatchers.IO) { runCatching {
                val s = client?.status() ?: return@runCatching false
                status = s
                battHist = (battHist + s.mv).takeLast(60)
                rssiHist = (rssiHist + s.rssi).takeLast(60)
                true
            }.getOrDefault(false) }
            if (ok) {
                fails = 0
                if (linkBusy) {                       // щойно відновились -> оновити дзеркало (плата могла змінити екран)
                    linkBusy = false
                    withContext(Dispatchers.IO) { runCatching { mirror = client?.mirror() ?: mirror } }
                }
            } else {
                fails++
                if (fails >= 2) linkBusy = true        // 2 поспіль невдалі -> плата зайнята/недосяжна
                if (fails >= 12) {                     // ~18с глухо -> НЕ висіти «connected+busy»: розірвати
                    connected = false; linkBusy = false
                    airLive = false; netLive = false; spectrumLive = false  // гасимо live-скани: чесні чіпи + нуль шансів повторно ронити плату на реконекті
                    msg = tr("втрачено зв'язок із платою — перепід'єднайся (IP міг змінитись)",
                             "lost link to board — reconnect (IP may have changed)")
                }
            }
            // USB — провідний канал: опитуємо частіше (реалтайм, без ризику «відпадіння»).
            // Gateway (Tailscale REST-poll) — рідше, щоб не глушити тунель.
            delay(when { useUsb -> 1000L; linkBusy -> 1500L; gatewayMode -> 4000L; else -> 3000L })
        }
        polling = false; linkBusy = false
    } }

    fun cmd(btn: String? = null, idx: Int? = null, back: Boolean = false) = io {
        val c = client ?: return@io
        if (ws != null && wsConnected) { ws!!.send(btn = btn, idx = idx, back = back); return@io }
        c.cmd(btn = btn, idx = idx, back = back)
        Thread.sleep(350)
        // Команда могла запустити довгий аналіз -> mirror/status можуть впасти; не «падаємо»,
        // а вмикаємо busy — полінг сам відновить дзеркало по завершенні.
        val ok = runCatching { mirror = c.mirror(); status = c.status(); true }.getOrDefault(false)
        if (!ok) linkBusy = true
    }

    fun loadArchive() = io { archive = client?.archive() ?: emptyList() }
    fun loadApps() = io { scripts = client?.scriptList() ?: emptyList() }
    fun runScript(s: ScriptFile) = io {
        scriptName = "▸ ${s.name}"; scriptOut = "running…"
        val r = client?.runScript(s.name, s.cat)
        val ok = r?.get("ok")?.jsonPrimitive?.booleanOrNull ?: false
        val out = r?.get("out")?.jsonPrimitive?.content ?: ""
        val err = r?.get("error")?.jsonPrimitive?.content
        scriptOut = if (ok) out.ifBlank { "(ok · no output)" } else "ERROR: ${err ?: "?"}\n$out"
    }
    fun viewScript(s: ScriptFile) = io {
        scriptName = s.name; scriptOut = "loading…"
        scriptOut = client?.scriptSource(s.name, s.cat)?.ifBlank { tr("(порожній / не знайдено)", "(empty / not found)") } ?: tr("(нема зв'язку)", "(no link)")
    }
    fun closeScript() { scriptOut = null }

    // ── #2 IDE скриптів: редактор .be у додатку (створити/редагувати/зберегти/запустити) ──
    var editorOpen by mutableStateOf(false)
    var editorName by mutableStateOf(""); var editorCat by mutableStateOf("misc")
    var editorSource by mutableStateOf(""); var editorMsg by mutableStateOf("")
    var editorIsNew by mutableStateOf(false); var editorSaving by mutableStateOf(false)
    val scriptCats = listOf("system", "net", "gpio", "uno", "misc")
    private val newScriptTemplate =
        "# новий скрипт ESP32-OS (Berry)\n" +
        "# app_draw() малює кадр (240x135), app_button(id) — реакція на кнопку\n" +
        "def app_draw()\n" +
        "  display_clear()\n" +
        "  display_text(6, 10, \"Hello\", 0xFFFF)\n" +
        "end\n" +
        "def app_button(id)\n" +
        "end\n"
    fun openEditor(s: ScriptFile) = io {
        editorIsNew = false; editorName = s.name; editorCat = s.cat; editorMsg = ""
        editorSource = tr("завантаження…", "loading…"); editorOpen = true
        editorSource = client?.scriptSource(s.name, s.cat)?.ifBlank { newScriptTemplate } ?: ""
    }
    fun newScript() {
        editorIsNew = true; editorName = ""; editorCat = "misc"
        editorSource = newScriptTemplate; editorMsg = ""; editorOpen = true
    }
    fun closeEditor() { editorOpen = false; editorMsg = "" }
    private fun editorFileName(): String {
        var n = editorName.trim()
        if (n.isEmpty()) n = "script"
        if (!n.endsWith(".be")) n += ".be"
        return n
    }
    fun saveEditor(thenRun: Boolean = false) = io {
        if (useUsb) { editorMsg = tr("збереження — лише по WiFi/gateway (USB рядковий)", "save only over WiFi/gateway (USB is line-based)"); return@io }
        val c = client ?: return@io
        editorSaving = true; editorMsg = tr("збереження…", "saving…")
        val fn = editorFileName()
        val ok = c.saveScript(fn, editorCat, editorSource)
        editorSaving = false
        if (!ok) { editorMsg = tr("не збереглося (перевір ім'я/зв'язок)", "save failed (name/link?)"); return@io }
        editorName = fn; editorIsNew = false; editorMsg = tr("збережено ✓", "saved ✓")
        loadApps()
        if (thenRun) { editorOpen = false; runScript(ScriptFile(fn, editorCat)) }
    }
    fun deleteEditorScript() = io {
        if (editorIsNew) { editorOpen = false; return@io }
        client?.deleteScript(editorFileName(), editorCat); editorOpen = false; loadApps()
    }

    // --- SD / живлення / лог плати ---
    var sdMsg by mutableStateOf("")
    fun sdMount() = io { client?.sdMount(); sdMsg = "mount…"; loadTools() }
    fun sdUnmount() = io { client?.sdUnmount(); sdMsg = "unmounted"; loadTools() }
    fun sdSelftest() = io { sdMsg = if (client?.sdSelftest() == true) "selftest OK" else "selftest FAIL"; loadTools() }
    fun reboot() = io { client?.reboot(); connected = false; msg = "rebooting…" }
    fun passive() = io { client?.passive() }

    var logLines by mutableStateOf<List<String>>(emptyList()); private var logSince = 0
    private var logPolling = false
    fun startLog() { if (useUsb) return; if (logPolling) return; logPolling = true; viewModelScope.launch {   // USB: лог стрімиться по serial (onLog), не поллимо
        while (connected) {
            withContext(Dispatchers.IO) { runCatching {
                val r = client?.log(logSince) ?: return@runCatching
                logSince = r["total"]?.jsonPrimitive?.intOrNull ?: logSince
                val lines = r["log"]?.jsonArray?.map { it.jsonPrimitive.content } ?: emptyList()
                if (lines.isNotEmpty()) logLines = (logLines + lines).takeLast(200)
            } }
            delay(1500)
        }
        logPolling = false
    } }
    fun loadTools() = io {
        val c = client ?: return@io
        val sd = c.sdStatus(); sdMounted = sd["mounted"]?.jsonPrimitive?.booleanOrNull ?: false
        sdStatusLine = sd["status"]?.jsonPrimitive?.content ?: ""
        val t = c.themes(); themes = t["themes"]?.jsonArray?.map { it.jsonPrimitive.content } ?: emptyList()
        themeCurrent = t["current"]?.jsonPrimitive?.intOrNull ?: 0
        loadSpectrum(c); loadSysinfo(c); loadWifi(c); loadModules(c)   // loadServer прибрано: сервер поллиться незалежно (startServerPoll)
    }
    var sysTemp by mutableStateOf(0f); var coolState by mutableStateOf(0); var coolDuty by mutableStateOf(0)
    var powerTotal by mutableStateOf(0); var powerBd by mutableStateOf<List<Pair<String, Int>>>(emptyList())
    fun loadSysinfo(c: EspOsClient? = client) = io {
        val cc = c ?: return@io
        val si = cc.sysinfo()
        sysTemp = si["temp_c"]?.jsonPrimitive?.floatOrNull ?: 0f
        val cl = si["cooling"]?.jsonObject
        coolState = cl?.get("state")?.jsonPrimitive?.intOrNull ?: 0
        coolDuty = cl?.get("fan_duty")?.jsonPrimitive?.intOrNull ?: 0
        val p = si["power"]?.jsonObject
        powerTotal = p?.get("ma")?.jsonPrimitive?.intOrNull ?: 0
        powerBd = listOf("base", "backlight", "wifi", "radios", "sd", "bt")
            .map { it to (p?.get(it)?.jsonPrimitive?.intOrNull ?: 0) }
    }
    // RF-waterfall: історія спектрів (новіші в кінці, ~50 рядків).
    var nrfHistory by mutableStateOf<List<List<Int>>>(emptyList())
    var subghzHistory by mutableStateOf<List<List<Int>>>(emptyList())
    var spectrumLive by mutableStateOf(false); private var spectrumStreaming = false
    fun loadSpectrum(c: EspOsClient? = client) = io {
        val cc = c ?: return@io
        val sg = cc.subghzSpectrum()
        subghzPresent = sg["present"]?.jsonPrimitive?.booleanOrNull ?: false
        subghzDb = sg["db"]?.jsonArray?.mapNotNull { it.jsonPrimitive.intOrNull } ?: emptyList()
        subghzMhz = sg["mhz"]?.jsonArray?.mapNotNull { it.jsonPrimitive.intOrNull } ?: emptyList()
        if (subghzDb.isNotEmpty()) subghzHistory = (subghzHistory + listOf(subghzDb)).takeLast(50)
        val nr = cc.nrfSpectrum()
        nrfPresent = nr["present"]?.jsonPrimitive?.booleanOrNull ?: false
        nrfCounts = nr["ch"]?.jsonArray?.mapNotNull { it.jsonPrimitive.intOrNull } ?: emptyList()
        if (nrfCounts.isNotEmpty()) nrfHistory = (nrfHistory + listOf(nrfCounts)).takeLast(50)
    }
    fun toggleSpectrumLive() { spectrumLive = !spectrumLive; if (spectrumLive) startSpectrumStream() }

    // ── LIVE-автооновлення важких сканів (AIR SCAN / HOSTS) — м'який інтервал, щоб не глушити плату ──
    var airLive by mutableStateOf(false); private var airLiveRunning = false
    fun toggleAirLive() { airLive = !airLive; if (airLive) startAirLive() }
    private fun startAirLive() {
        if (airLiveRunning) return; airLiveRunning = true
        viewModelScope.launch { while (airLive && connected) { airscan(); delay(12000) }; airLiveRunning = false }
    }
    var netLive by mutableStateOf(false); private var netLiveRunning = false
    fun toggleNetLive() { netLive = !netLive; if (netLive) startNetLive() }
    private fun startNetLive() {
        if (netLiveRunning) return; netLiveRunning = true
        viewModelScope.launch { while (netLive && connected) { netscan(); delay(20000) }; netLiveRunning = false }
    }
    private fun startSpectrumStream() {
        if (spectrumStreaming) return; spectrumStreaming = true
        viewModelScope.launch {
            while (spectrumLive && connected) {
                withContext(Dispatchers.IO) { runCatching { loadSpectrumSync() } }
                delay(1500)
            }
            spectrumStreaming = false
        }
    }
    private fun loadSpectrumSync() {
        val cc = client ?: return
        val nr = cc.nrfSpectrum()
        nrfPresent = nr["present"]?.jsonPrimitive?.booleanOrNull ?: false
        nrfCounts = nr["ch"]?.jsonArray?.mapNotNull { it.jsonPrimitive.intOrNull } ?: emptyList()
        if (nrfCounts.isNotEmpty()) nrfHistory = (nrfHistory + listOf(nrfCounts)).takeLast(50)
        val sg = cc.subghzSpectrum()
        subghzPresent = sg["present"]?.jsonPrimitive?.booleanOrNull ?: false
        subghzDb = sg["db"]?.jsonArray?.mapNotNull { it.jsonPrimitive.intOrNull } ?: emptyList()
        if (subghzDb.isNotEmpty()) subghzHistory = (subghzHistory + listOf(subghzDb)).takeLast(50)
    }
    fun setTheme(i: Int) = io { client?.setTheme(i); themeCurrent = i }

    var reportName by mutableStateOf(""); var reportContent by mutableStateOf("")
    fun openReport(n: String) = io { reportName = n; reportContent = client?.report(n) ?: "" }
    fun closeReport() { reportName = ""; reportContent = "" }
    fun deleteCurrentReport() = io { val n = reportName; reportName = ""; reportContent = ""; client?.deleteReport(n); loadArchive() }

    // Збережені WiFi-мережі (менеджер): список, auto-toggle, forget.
    var wifiNets by mutableStateOf<List<Triple<Int, String, Boolean>>>(emptyList())
    fun loadWifi(c: EspOsClient? = client) = io {
        wifiNets = (c ?: return@io).wifiSaved()["nets"]?.jsonArray?.map {
            val o = it.jsonObject
            Triple(o["i"]?.jsonPrimitive?.intOrNull ?: 0,
                   o["ssid"]?.jsonPrimitive?.content ?: "",
                   (o["auto"]?.jsonPrimitive?.intOrNull ?: 0) == 1)
        } ?: emptyList()
    }
    fun wifiAuto(i: Int, on: Boolean) = io { client?.wifiAuto(i, on); loadWifi() }
    fun wifiForget(i: Int) = io { client?.wifiForget(i); loadWifi() }

    // Інвентар мережі (ARP-скан): ip, mac, vendor, gw.
    var netHosts by mutableStateOf<List<List<String>>>(emptyList())
    var netScanning by mutableStateOf(false)
    fun netscan() = io {
        netScanning = true
        val r = client?.netscan()
        netHosts = r?.get("hosts")?.jsonArray?.map {
            val o = it.jsonObject
            listOf(o["ip"]?.jsonPrimitive?.content ?: "", o["mac"]?.jsonPrimitive?.content ?: "",
                   o["vendor"]?.jsonPrimitive?.content ?: "?", if (o["gw"] != null) "gw" else "")
        }?.sortedBy { it[0].split(".").lastOrNull()?.toIntOrNull() ?: 0 } ?: emptyList()
        netScanning = false
    }

    // AIR SCAN — WiFi-мережі + evil-twin детект.
    var airAps by mutableStateOf<List<List<String>>>(emptyList())   // ssid, bssid, rssi, ch, enc, twin
    var airTwins by mutableStateOf(0); var airOpen by mutableStateOf(0); var airWeak by mutableStateOf(0)
    var airScanning by mutableStateOf(false)
    fun airscan() = io {
        airScanning = true
        val r = client?.airscan()
        if (r?.get("throttled")?.jsonPrimitive?.booleanOrNull == true) { airScanning = false; return@io }  // тримаємо попередні дані
        airTwins = r?.get("twins")?.jsonPrimitive?.intOrNull ?: 0
        airOpen = r?.get("open")?.jsonPrimitive?.intOrNull ?: 0
        airWeak = r?.get("weak")?.jsonPrimitive?.intOrNull ?: 0
        airAps = r?.get("aps")?.jsonArray?.map {
            val o = it.jsonObject
            listOf(o["ssid"]?.jsonPrimitive?.content ?: "?", o["bssid"]?.jsonPrimitive?.content ?: "",
                   (o["rssi"]?.jsonPrimitive?.intOrNull ?: 0).toString(), (o["ch"]?.jsonPrimitive?.intOrNull ?: 0).toString(),
                   o["enc"]?.jsonPrimitive?.content ?: "?",
                   if (o["twin"] != null) "twin" else "", if (o["open"] != null) "open" else if (o["weak"] != null) "weak" else "")
        }?.sortedByDescending { it[2].toIntOrNull() ?: -120 } ?: emptyList()
        airScanning = false
    }

    // Автодетект модулів обох плат (ESP + UNO) для графічного відображення.
    // ── Config-фіча: профіль модулів (які на якій платі) — адаптація прошивки без перекомпіляції ──
    val profEspKeys = listOf("nrf24", "cc1101", "sd", "bt")
    val profUnoKeys = listOf("enabled", "joy", "rc522", "dht", "pot", "reed")
    var profEsp by mutableStateOf(profEspKeys.associateWith { true })
    var profUno by mutableStateOf(profUnoKeys.associateWith { true })
    var profLoaded by mutableStateOf(false); var profMsg by mutableStateOf("")
    // Опційні апаратні power-enable GPIO (до MOSFET/load-switch) для реального відключення рейки.
    // Порожньо/"" = нема (логічне вимкнення). Ключі в профілі: esp.<module>_pwr.
    val profPwrKeys = listOf("nrf24", "sd", "cc1101")
    var profPwr by mutableStateOf(profPwrKeys.associateWith { "" })
    fun loadProfile() = io {
        val p = client?.getProfile() ?: return@io
        val esp = p["esp"]?.jsonObject; val uno = p["uno"]?.jsonObject
        if (esp != null || uno != null) {
            profEsp = profEspKeys.associateWith { esp?.get(it)?.jsonPrimitive?.booleanOrNull ?: true }
            profUno = profUnoKeys.associateWith { uno?.get(it)?.jsonPrimitive?.booleanOrNull ?: true }
            profPwr = profPwrKeys.associateWith { esp?.get(it + "_pwr")?.jsonPrimitive?.intOrNull?.toString() ?: "" }
            profLoaded = true
        }
    }
    fun toggleProf(section: String, key: String) {
        if (section == "esp") profEsp = profEsp.toMutableMap().apply { this[key] = !(this[key] ?: true) }
        else profUno = profUno.toMutableMap().apply { this[key] = !(this[key] ?: true) }
    }
    fun setPwrGpio(key: String, gpio: String) { profPwr = profPwr.toMutableMap().apply { this[key] = gpio.filter { it.isDigit() }.take(2) } }
    fun saveProfile() = io {
        val json = buildJsonObject {
            put("board", "esp32-t-display")
            put("esp", buildJsonObject {
                profEsp.forEach { (k, v) -> put(k, v) }
                profPwr.forEach { (k, g) -> g.toIntOrNull()?.let { put(k + "_pwr", it) } }  // лише задані
            })
            put("uno", buildJsonObject { profUno.forEach { (k, v) -> put(k, v) } })
        }.toString()
        profMsg = if (client?.setProfile(json) == true) tr("профіль збережено ✓", "profile saved ✓") else tr("не збережено", "save failed")
        loadModules()
    }

    // AP-фіча: назва точки доступу плати + вимикач (керується по кабелю/WiFi).
    var apSsid by mutableStateOf(""); var apEnabled by mutableStateOf(true)
    var apUp by mutableStateOf(false); var apClients by mutableStateOf(0)
    var apLoaded by mutableStateOf(false); var apMsg by mutableStateOf("")
    fun loadAp() = io {
        val a = client?.getAp() ?: return@io
        if (a.containsKey("ssid")) {
            apSsid = a["ssid"]?.jsonPrimitive?.content ?: ""
            apEnabled = a["enabled"]?.jsonPrimitive?.booleanOrNull ?: true
            apUp = a["up"]?.jsonPrimitive?.booleanOrNull ?: false
            apClients = a["clients"]?.jsonPrimitive?.intOrNull ?: 0
            apLoaded = true
        }
    }
    fun saveApSsid(name: String) = io {
        val s = name.trim()
        if (s.isEmpty() || s.length > 32) { apMsg = tr("назва: 1..32 символи", "name: 1..32 chars"); return@io }
        apMsg = if (client?.setApSsid(s) == true) tr("назву збережено ✓ (перепідключіться)", "name saved ✓ (reconnect)")
                else tr("не збережено", "save failed")
        loadAp()
    }
    fun toggleAp(on: Boolean) = io {
        apMsg = if (client?.setApEnabled(on) == true)
                    (if (on) tr("точку ввімкнено ✓", "AP enabled ✓") else tr("точку вимкнено ✓ (STA-only)", "AP disabled ✓ (STA-only)"))
                else tr("не змінено", "toggle failed")
        loadAp()
    }

    // RF-калібратор: live-аналіз модуля + безпечна оптимізація WiFi TX-потужності.
    var rf by mutableStateOf<JsonObject?>(null)
    var calibResult by mutableStateOf<JsonObject?>(null)
    var calibBusy by mutableStateOf(false); var calibMsg by mutableStateOf("")
    var calibMode by mutableStateOf("")   // "optimize" | "boost" — для точного пояснення
    fun loadRf() = io { rf = client?.rfStat() }

    // Безпечний режим пінів (high-Z): захист голих ніжок від КЗ, коли плата без модулів.
    // При активації плата робить КЗ-перевірку (drive-verify) кожного піна: закорочені на рейку —
    // не драйвить і відкочує у safe; hwSafeBlocked = перелік таких пінів.
    var hwSafe by mutableStateOf(true); var hwSafeBusy by mutableStateOf(false)
    var hwSafeBlocked by mutableStateOf<List<String>>(emptyList())
    private fun readHwSafe(r: JsonObject) {
        r["safe"]?.jsonPrimitive?.booleanOrNull?.let { hwSafe = it }
        hwSafeBlocked = r["blocked"]?.jsonArray?.mapNotNull { it.jsonPrimitive.contentOrNull } ?: emptyList()
    }
    fun loadHwSafe() = io { client?.hwSafeGet()?.let { readHwSafe(it) } }
    fun setHwSafe(on: Boolean) = io {
        hwSafeBusy = true
        val r = client?.hwSafeSet(on)
        hwSafeBusy = false
        if (r == null || r.isEmpty()) { msg = tr("не вдалося (потрібен вхід)", "failed (login needed)"); return@io }
        readHwSafe(r)
        msg = when {
            hwSafe && !on && hwSafeBlocked.isNotEmpty() ->
                tr("АКТИВАЦІЮ ЗАБЛОКОВАНО — КЗ на: ${hwSafeBlocked.joinToString(", ")}",
                   "ACTIVATION BLOCKED — short on: ${hwSafeBlocked.joinToString(", ")}")
            hwSafe -> tr("піни вимкнено — безпечно", "pins off — safe")
            else -> tr("піни активовано ✓ (КЗ не виявлено)", "pins active ✓ (no short found)")
        }
    }
    // Діагностика розводки: софт-«продзвонка» пінів + активна перевірка SPI-шини.
    var diagBusy by mutableStateOf(false)
    var diagPins by mutableStateOf<List<Triple<Int, String, String>>>(emptyList())  // gpio,name,state
    var diagBridges by mutableStateOf<List<String>>(emptyList())                    // "A–B"
    var diagSpi by mutableStateOf("")                                                // вердикт SPI
    fun runPinScan() = io {
        diagBusy = true
        val r = client?.pinScan()
        diagBusy = false
        if (r == null || r.isEmpty()) { msg = tr("продзвонка не вдалась (вхід?)", "pinscan failed (login?)"); return@io }
        diagPins = r["pins"]?.jsonArray?.mapNotNull {
            val o = it.jsonObject
            Triple(o["gpio"]?.jsonPrimitive?.intOrNull ?: return@mapNotNull null,
                   o["name"]?.jsonPrimitive?.contentOrNull ?: "",
                   o["state"]?.jsonPrimitive?.contentOrNull ?: "")
        } ?: emptyList()
        diagBridges = r["bridges"]?.jsonArray?.mapNotNull {
            val o = it.jsonObject; "${o["a"]?.jsonPrimitive?.contentOrNull}–${o["b"]?.jsonPrimitive?.contentOrNull}"
        } ?: emptyList()
        msg = tr("продзвонка готова", "pinscan done")
    }
    fun runSpiTest() = io {
        diagBusy = true
        val r = client?.spiTest()
        diagBusy = false
        val nrf = r?.get("nrf24")?.jsonObject
        diagSpi = nrf?.get("verdict")?.jsonPrimitive?.contentOrNull
            ?: tr("SPI-тест не вдався (вхід?)", "spitest failed (login?)")
    }
    // Апаратний self-test: список перевірок name/status/detail + підсумок.
    var selfTestBusy by mutableStateOf(false)
    var selfChecks by mutableStateOf<List<Triple<String, String, String>>>(emptyList())  // name,status,detail
    var selfSummary by mutableStateOf("")
    fun runSelfTest() = io {
        selfTestBusy = true
        val r = client?.selfTest()
        selfTestBusy = false
        if (r == null || r.isEmpty()) { selfSummary = tr("self-test не вдався (вхід?)", "self-test failed (login?)"); return@io }
        selfChecks = r["checks"]?.jsonArray?.mapNotNull {
            val o = it.jsonObject
            Triple(o["name"]?.jsonPrimitive?.contentOrNull ?: return@mapNotNull null,
                   o["status"]?.jsonPrimitive?.contentOrNull ?: "",
                   o["detail"]?.jsonPrimitive?.contentOrNull ?: "")
        } ?: emptyList()
        val p = r["pass"]?.jsonPrimitive?.intOrNull ?: 0
        val w = r["warn"]?.jsonPrimitive?.intOrNull ?: 0
        val f = r["fail"]?.jsonPrimitive?.intOrNull ?: 0
        selfSummary = "✓$p  !$w  ✗$f"
    }
    // UNO-діагностика: лінк/пробінг/вітали/модулі/датчики/RFID + людський вердикт.
    var unoDiagBusy by mutableStateOf(false)
    var unoVerdict by mutableStateOf("")
    var unoLine by mutableStateOf("")                 // короткий рядок вітал/датчиків
    var unoModsDiag by mutableStateOf<List<String>>(emptyList())  // "name present"
    fun runUnoDiag() = io {
        unoDiagBusy = true
        val r = client?.unoDiag()
        unoDiagBusy = false
        if (r == null || r.isEmpty()) { unoVerdict = tr("UNO-діагностика не вдалась (вхід?)", "unodiag failed (login?)"); return@io }
        unoVerdict = r["verdict"]?.jsonPrimitive?.contentOrNull ?: ""
        val vit = r["vitals"]?.jsonObject; val sn = r["sensors"]?.jsonObject
        val probe = r["probe"]?.jsonObject
        val ram = vit?.get("free_ram")?.jsonPrimitive?.intOrNull ?: -1
        val up = vit?.get("uptime_s")?.jsonPrimitive?.intOrNull ?: 0
        val resp = probe?.get("responded")?.jsonPrimitive?.booleanOrNull ?: false
        unoLine = buildString {
            append(if (resp) "probe:✓ " else "probe:✗ ")
            if (ram >= 0) append("RAM ${ram}B · up ${up}s · ")
            sn?.get("pot")?.jsonPrimitive?.intOrNull?.let { append("pot $it ") }
            sn?.get("temp_c")?.jsonPrimitive?.doubleOrNull?.let { append("· ${it}°C ") }
        }.trim()
        unoModsDiag = r["modules"]?.jsonArray?.mapNotNull {
            val o = it.jsonObject
            val nm = o["name"]?.jsonPrimitive?.contentOrNull ?: return@mapNotNull null
            val pr = o["present"]?.jsonPrimitive?.booleanOrNull ?: false
            "$nm ${if (pr) "✓" else "✗"}"
        } ?: emptyList()
    }

    fun calibrate() = runCalib(null, "optimize")   // авто: економія струму/тепла
    fun boost() = runCalib(19.5, "boost")          // макс безпечна TX: дальність/стабільність
    private fun runCalib(dbm: Double?, mode: String) = io {
        calibBusy = true; calibResult = null; calibMode = mode
        calibMsg = if (mode == "boost") tr("піднімаю потужність…", "raising power…")
                   else tr("калібрую… вимір лінка (~3с)", "calibrating… link check (~3s)")
        calibResult = client?.calibrateApply(dbm)
        rf = client?.rfStat()
        calibBusy = false; calibMsg = ""
    }

    // Low-RAM: хостинг WiFi+веб НА СЕСІЮ (ребут повертає). ~+45 КБ heap коли вимкнено.
    var hostingOn by mutableStateOf(true); var hostingMsg by mutableStateOf("")
    fun toggleHosting(on: Boolean) = io {
        val r = client?.setHosting(on) ?: return@io
        val ok = r["ok"]?.jsonPrimitive?.booleanOrNull ?: false
        val heapKb = r["heap"]?.jsonPrimitive?.intOrNull?.let { it / 1024 }
        if (ok) hostingOn = on
        hostingMsg = when {
            !ok -> tr("не змінено", "failed")
            on  -> tr("хостинг увімкнено ✓", "hosting on ✓")
            else -> tr("хостинг вимкнено ✓  ${heapKb?.let { "вільно ${it} КБ" } ?: "+~45 КБ"}; ребут поверне",
                       "hosting off ✓  ${heapKb?.let { "free ${it} KB" } ?: "+~45KB"}; reboot restores")
        }
    }

    var espModules by mutableStateOf<Map<String, Boolean>>(emptyMap())
    var unoConnected by mutableStateOf(false)
    var unoModules by mutableStateOf<List<Triple<String, String, Boolean>>>(emptyList())  // name, type, present
    var unoSensors by mutableStateOf<Map<String, String>>(emptyMap())
    var unoFreeRam by mutableStateOf(-1); var unoUptime by mutableStateOf(0)
    fun loadModules(c: EspOsClient? = client) = io {
        val m = (c ?: return@io).modules()
        val esp = m["esp"]?.jsonObject
        espModules = listOf("nrf24", "cc1101", "sd", "wifi_sta", "soft_ap", "bt")
            .associateWith { esp?.get(it)?.jsonPrimitive?.booleanOrNull ?: false }
        val uno = m["uno"]?.jsonObject
        unoConnected = uno?.get("connected")?.jsonPrimitive?.booleanOrNull ?: false
        unoFreeRam = uno?.get("free_ram")?.jsonPrimitive?.intOrNull ?: -1
        unoUptime = uno?.get("uptime_s")?.jsonPrimitive?.intOrNull ?: 0
        unoModules = uno?.get("modules")?.jsonArray?.map {
            val o = it.jsonObject
            Triple(o["name"]?.jsonPrimitive?.content ?: "?", o["type"]?.jsonPrimitive?.content ?: "",
                   o["present"]?.jsonPrimitive?.booleanOrNull ?: false)
        } ?: emptyList()
        unoSensors = uno?.get("sensors")?.jsonObject?.mapValues { it.value.jsonPrimitive.content } ?: emptyMap()
    }

    var wsConnected by mutableStateOf(false); private var ws: WsStream? = null
    fun startWs() {
        if (gatewayMode) { msg = "WS: use direct mode"; return }   // :81 не проксіюється шлюзом -> REST-poll
        ws?.close(); ws = WsStream(host, pin, onScreen = { mirror = it }, onState = { wsConnected = it }); ws!!.connect()
    }
    fun stopWs() { ws?.close(); ws = null }

    var discovered by mutableStateOf<List<Pair<String, String>>>(emptyList()); private var disc: Discovery? = null
    fun discover(ctx: android.content.Context) {
        disc?.stop(); discovered = emptyList()
        disc = Discovery(ctx) { name, h, _ -> discovered = (discovered + (name to h)).distinctBy { it.second } }; disc!!.start()
    }
    override fun onCleared() { stopWs(); disc?.stop(); usbLink?.close() }

    // ── Реле плата→сервер через телефон ──────────────────────────────────────────
    // Телефон на нормальному WiFi + плата по USB: телефон тягне телеметрію плати й
    // шле на сервер (/api/board/ingest). Так сервер бачить плату БЕЗ спільної мережі.
    private var relayRunning = false
    fun toggleRelay() { relayEnabled = !relayEnabled; if (relayEnabled) startRelay() }
    private fun startRelay() {
        if (relayRunning) return; relayRunning = true
        viewModelScope.launch {
            while (relayEnabled && connected) {
                val ok = withContext(Dispatchers.IO) { runCatching {
                    val c = client ?: return@runCatching false
                    val payload = buildJsonObject {
                        put("status", c.status().let { buildJsonObject {
                            put("rssi", it.rssi); put("mv", it.mv); put("sta", it.sta); put("usb", it.usb) } })
                        put("sysinfo", c.sysinfo())
                        put("via", "usb-phone-relay")
                    }
                    Updater.boardIngest(updateUrl, payload.toString())
                }.getOrDefault(false) }
                relayMsg = if (ok) tr("реле → сервер: ok", "relay → server: ok") else tr("реле: сервер недосяжний", "relay: server unreachable")
                delay(5000)
            }
            relayRunning = false
        }
    }

    // --- Self-update (OTA) із захистом від поламаних збірок ---
    var updateUrl by mutableStateOf("100.80.30.64:8080")
    var myVc by mutableStateOf(0)
    var updateStatus by mutableStateOf("")
    var updateAvail by mutableStateOf<Updater.Manifest?>(null)
    var updateProgress by mutableStateOf(-1)
    var readyApk by mutableStateOf<File?>(null)
    fun checkUpdate() = io {
        updateStatus = "checking…"; updateAvail = null; readyApk = null
        val m = Updater.manifest(updateUrl)
        updateStatus = when {
            m == null -> tr("сервер offline / нема маніфесту", "server offline / no manifest")
            m.versionCode > myVc -> tr("є оновлення: v${m.versionName} (build ${m.versionCode})", "update available: v${m.versionName} (build ${m.versionCode})")
            else -> tr("актуальна версія (v$myVc)", "up to date (v$myVc)")
        }
        if (m != null && m.versionCode > myVc) updateAvail = m
    }
    fun downloadUpdate(dir: File) = io {
        val m = updateAvail ?: return@io
        if (!Updater.verifySig(m)) {   // Ed25519: підпис Є і невірний -> підміна маніфесту, стоп
            updateStatus = tr("ПІДПИС OTA НЕВІРНИЙ — скасовано (можлива підміна). Recovery: /ota у браузері.",
                              "OTA SIGNATURE INVALID — aborted (possible tampering). Recovery: /ota in a browser.")
            readyApk = null; return@io
        }
        updateProgress = 0; updateStatus = tr("завантаження…", "downloading…")
        val dest = File(dir, "update.apk")
        val ok = Updater.download(updateUrl, m.sha256, dest) { p -> updateProgress = p }
        updateProgress = -1
        if (ok) { updateStatus = tr("SHA-256 ✓ — встановлення…", "SHA-256 ✓ — installing…"); readyApk = dest }
        else { updateStatus = tr("SHA-256 НЕ ЗБІГСЯ — скасовано. Recovery: відкрий /ota у браузері.", "SHA-256 MISMATCH — aborted. Recovery: open /ota in a browser."); readyApk = null }
    }
    fun beacon() = io { if (myVc > 0) Updater.beacon(updateUrl, myVc) }

    // Моніторинг сервера (dashboard /api/server) — ресурси stolos у додатку.
    var serverStats by mutableStateOf<JsonObject?>(null)
    fun loadServer() = io { serverStats = Updater.serverStats(updateUrl) }
    // Статус сервера — ОКРЕМА система від плати: поллимо незалежно (потрібен, коли плата вимкнена).
    private var serverPolling = false
    fun startServerPoll() {
        if (serverPolling) return; serverPolling = true
        viewModelScope.launch {
            while (true) {
                withContext(Dispatchers.IO) { runCatching { serverStats = Updater.serverStats(updateUrl) } }
                kotlinx.coroutines.delay(8000)
            }
        }
    }

    // ── Надсилання повного набору зібраних даних/звітів у Telegram-бота (через сервер /api/notify) ──
    var botBusy by mutableStateOf(false); var botMsg by mutableStateOf("")
    private fun buildDataReport(): String {
        val sb = StringBuilder()
        sb.append("=== ESP32-OS · ЗІБРАНІ ДАНІ ===\n")
        sb.append("WiFi: ${airAps.size} мереж (twins ${airTwins}, open ${airOpen}, wep ${airWeak})\n")
        airAps.sortedByDescending { it[2].toIntOrNull() ?: -120 }.forEach {
            val flags = (if (it[5] == "twin") " TWIN" else "") +
                (if (it.getOrElse(6){""} == "open") " OPEN" else if (it.getOrElse(6){""} == "weak") " WEP" else "")
            sb.append("  ${it[2]}dBm ch${it[3]} ${it[4]} ${it[0]} (${it[1]})$flags\n")
        }
        sb.append("\nLAN: ${netHosts.size} хостів\n")
        netHosts.forEach { sb.append("  ${it[0]}  ${it[2]}  ${it[1]}${if (it[3]=="gw") " [GW]" else ""}\n") }
        val busy = nrfCounts.mapIndexed { i, c -> i to c }.filter { it.second > 0 }.sortedByDescending { it.second }
        if (busy.isNotEmpty()) sb.append("\n2.4G зайняті канали: " + busy.take(40).joinToString(" ") { "ch${it.first}(${it.second})" } + "\n")
        val peaks = subghzDb.mapIndexed { i, d -> i to d }.sortedByDescending { it.second }.take(15).filter { it.second > -110 }
        if (peaks.isNotEmpty()) sb.append("\nSub-GHz піки: " + peaks.joinToString(" ") { "${subghzMhz.getOrElse(it.first){0}}MHz:${it.second}dBm" } + "\n")
        deviceIntel?.let { di ->
            val ip = di["ip"]?.jsonPrimitive?.contentOrNull ?: ""
            if (ip.isNotBlank()) sb.append("\nОстання розвідка: $ip — ${di["category"]?.jsonPrimitive?.contentOrNull ?: ""}\n")
        }
        if (logLines.isNotEmpty()) { sb.append("\n=== ЛОГ (${logLines.size}) ===\n"); logLines.forEach { sb.append("$it\n") } }
        return sb.toString()
    }
    fun sendDataToBot() = io {
        botBusy = true
        val ok = Updater.notifyBot(updateUrl, "ESP32-OS Дані", buildDataReport())
        botMsg = if (ok) tr("надіслано в бота ✓", "sent to bot ✓") else tr("не вдалось (сервер?)", "failed (server?)")
        botBusy = false
    }
    fun sendSelfTestToBot() = io {
        botBusy = true
        val sb = StringBuilder("=== ESP32-OS · SELF-TEST ===\n$selfSummary\n")
        selfChecks.forEach { (n, st, d) -> sb.append("[${st.uppercase()}] $n: $d\n") }
        if (unoVerdict.isNotBlank()) sb.append("UNO: $unoVerdict\n")
        if (diagSpi.isNotBlank()) sb.append("SPI: $diagSpi\n")
        val ok = Updater.notifyBot(updateUrl, "ESP32-OS Self-Test", sb.toString())
        botMsg = if (ok) tr("надіслано в бота ✓", "sent to bot ✓") else tr("не вдалось (сервер?)", "failed (server?)")
        botBusy = false
    }

    // ── Глибока розвідка пристрою (сервер сканує IP: порти/hostname/UPnP/NetBIOS/OUI-виробник) ──
    var deviceIntel by mutableStateOf<JsonObject?>(null)
    var intelBusy by mutableStateOf(false)
    var intelTitle by mutableStateOf("")
    fun scanDeviceIntel(mac: String, ip: String) = io {
        intelBusy = true; intelTitle = "INTEL · $ip"; deviceIntel = JsonObject(emptyMap())
        // Спершу — АВТОНОМНО з ПЛАТИ (/api/devscan): порти/сервіси/категорія без сервера.
        // Фолбек — серверний devintel (багатший: vendor/hostname/UPnP/NetBIOS), якщо є зв'язок.
        val fromBoard = if (connected) runCatching { client?.devScan(ip) }.getOrNull() else null
        val boardOk = fromBoard != null && fromBoard["error"] == null &&
                      (fromBoard.containsKey("category") || fromBoard.containsKey("ports"))
        deviceIntel = if (boardOk) fromBoard
            else Updater.deviceScan(updateUrl, mac, ip) ?: fromBoard
                ?: JsonObject(mapOf("error" to JsonPrimitive(tr("нема зв'язку ні з платою, ні з сервером", "no board or server link"))))
        intelBusy = false
    }
    fun closeIntel() { deviceIntel = null }

    // ── ТАРГЕТ: спільний вибраний пристрій для трансферу даних між інструментами ──
    var targetIp by mutableStateOf(""); var targetMac by mutableStateOf(""); var targetLabel by mutableStateOf("")
    fun setTarget(ip: String, mac: String, label: String) { targetIp = ip; targetMac = mac; targetLabel = label }
    fun clearTarget() { targetIp = ""; targetMac = ""; targetLabel = "" }
}

// board=true -> вкладка керує ПЛАТОЮ (тягне дані по лінку); false -> app/server-native,
// працює й коли плата офлайн. Використовується, щоб банер «плата зайнята/перепідключення»
// не з'являвся на екранах, що плати не потребують (SERVER/LIBRARY/HELP) — жодного залипання.
// Розділи меню (drawer): кожен — окреме повноекранне вікно. board=true -> показувати банер лінку
// плати. Порядок = порядок у меню (за логікою: плати -> тести -> аналіз -> сервер -> калібр ->
// модулі/піни -> дані -> скрипти -> налаштування -> довідка).
private enum class Sec(val board: Boolean, val icon: ImageVector) {
    BOARDS(true, Icons.Filled.DeveloperBoard),
    TESTS(true, Icons.Filled.BugReport),
    SCREEN(true, Icons.Filled.Smartphone),
    SERVER(false, Icons.Filled.Storage),
    CALIB(true, Icons.Filled.Tune),
    MODULES(true, Icons.Filled.Memory),
    DATA(false, Icons.Filled.Folder),
    SCRIPTS(true, Icons.Filled.Code),
    SETTINGS(false, Icons.Filled.Settings),
    HELP(false, Icons.AutoMirrored.Filled.MenuBook),
}

class MainActivity : ComponentActivity() {
    override fun onCreate(s: Bundle?) { super.onCreate(s); setContent { ApexTheme { App() } } }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun App(vm: MainViewModel = viewModel()) {
    var sec by remember { mutableStateOf(Sec.BOARDS) }
    val drawer = rememberDrawerState(DrawerValue.Closed)
    val scope = rememberCoroutineScope()
    val appCtx = LocalContext.current
    // task 4: окреме серверне середовище — власна VM, синхронізована з gateway-креденшалами
    val avm = remember { AdminViewModel(appCtx.applicationContext as android.app.Application) }
    // прошивка плати через USB-OTG з телефона (окрема VM: USB-host + esptool-протокол)
    val fvm = remember { FlashViewModel(appCtx.applicationContext as android.app.Application) }
    LaunchedEffect(vm.gwToken, vm.updateUrl) { avm.token = vm.gwToken; avm.serverBase = vm.updateUrl }
    LaunchedEffect(vm.host, vm.pin) { fvm.boardHost = vm.host; fvm.boardPin = vm.pin }
    LaunchedEffect(vm.lang) { fvm.uk = vm.lang == Lang.UK; avm.uk = vm.lang == Lang.UK }
    LaunchedEffect(Unit) {
        vm.appContext = appCtx.applicationContext                // для персистенції seed поза UI
        Prefs.load(appCtx, vm)                                   // запам'ятані host/token/режим
        LogStore.autoCleanByPref(appCtx)                         // авточистка старих сесій логів (retention)
        vm.myVc = AppUpdate.currentVersionCode(appCtx); vm.beacon()
        vm.startServerPoll()                                     // статус сервера незалежно від плати
        if (vm.autoConnect && !vm.connected) {                   // авто-вхід за токеном/PIN
            val ready = if (vm.useGateway) vm.gwToken.isNotBlank() else vm.pin.isNotBlank()
            if (ready) vm.connect()
        }
    }
    CompositionLocalProvider(LocalLang provides vm.lang) {
    ModalNavigationDrawer(drawerState = drawer, drawerContent = {
        DrawerContent(sec) { s -> sec = s; scope.launch { drawer.close() }
            when (s) { Sec.BOARDS -> vm.loadTools(); Sec.SERVER -> avm.refreshServer(); Sec.DATA -> vm.loadArchive(); Sec.SCRIPTS -> vm.loadApps(); else -> {} } }
    }) {
    Scaffold(
        containerColor = Apex.Bg,
        topBar = {
            Column(Modifier.background(Apex.Bg).padding(horizontal = 14.dp, vertical = 8.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Icon(Icons.Filled.Menu, "menu", tint = Apex.Accent, modifier = Modifier.size(24.dp).clickable { scope.launch { drawer.open() } })
                    Spacer(Modifier.width(10.dp))
                    Box(Modifier.size(8.dp).background(if (vm.connected) Apex.Accent else Apex.Bad, CircleShape))
                    Spacer(Modifier.width(8.dp))
                    Column {
                        Text("ESP32·OS", color = Apex.Ink, fontFamily = FontFamily.Monospace, fontWeight = FontWeight.SemiBold, fontSize = 15.sp)
                        Text("// MISSION CONTROL", color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 8.sp, letterSpacing = 2.sp)
                    }
                    Spacer(Modifier.weight(1f))
                    var clock by remember { mutableStateOf("") }
                    LaunchedEffect(Unit) { val f = SimpleDateFormat("HH:mm:ss", Locale.US); while (true) { clock = f.format(Date()); delay(1000) } }
                    Text(clock, color = Apex.Ink2, fontFamily = FontFamily.Monospace, fontSize = 12.sp, letterSpacing = 1.sp)
                    Spacer(Modifier.width(10.dp))
                    Text(if (vm.lang == Lang.UK) "УКР" else "ENG", color = Apex.Accent, fontFamily = FontFamily.Monospace, fontSize = 12.sp,
                        modifier = Modifier.border(1.dp, Apex.Edge, RoundedCornerShape(4.dp)).clickable { vm.toggleLang() }.padding(horizontal = 7.dp, vertical = 2.dp))
                }
                val s = vm.status
                CodedBar("// LINK ${if (vm.connected) "WIFI-STA" else "OFFLINE"} · RSSI ${s?.rssi ?: 0}dBm · ${s?.mv ?: 0}mV ${if (s?.usb == true) "· USB" else ""}",
                    ok = vm.connected)
                // APEX readiness-strip (сигнатурний елемент mission-control).
                Row(Modifier.padding(top = 4.dp), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    StatusTag("LINK", if (vm.connected) 0 else 2)
                    if (vm.sysTemp > 0f) StatusTag("PWR", vm.coolState)
                    if (vm.sysTemp > 0f) StatusTag("TEMP ${vm.sysTemp.toInt()}°", vm.coolState)
                    StatusTag("SD", if (vm.sdMounted) 0 else 1)
                }
            }
        }
    ) { pad ->
        Column(Modifier.padding(pad).fillMaxSize().padding(horizontal = 12.dp)) {
            // Банер плати + USB-реле — лише на board-розділах (не на сервер/дані/налашт/довідка).
            if (vm.linkBusy && sec.board) BusyBanner(if (vm.lang == Lang.UK) "// ПЛАТА ЗАЙНЯТА · АНАЛІЗ · ПЕРЕПІД'ЄДНАННЯ…" else "// BOARD BUSY · ANALYSIS · RECONNECTING…")
            if (vm.connected && vm.useUsb && sec.board) UsbRelayBar(vm)
            when (sec) {
                Sec.BOARDS -> BoardsScreen(vm)      // моніторинг ESP+UNO + аналіз-інструменти
                Sec.TESTS -> TestsScreen(vm)
                Sec.SCREEN -> ScreenLauncherScreen(vm)  // мірор екрана плати (screen-launcher)
                Sec.SERVER -> ServerScreen(avm, fvm, vm.updateUrl)
                Sec.CALIB -> CalibrationScreen(vm)
                Sec.MODULES -> ModulesPinsScreen(vm)
                Sec.DATA -> ArchiveScreen(vm, avm)
                Sec.SCRIPTS -> AppsScreen(vm)
                Sec.SETTINGS -> SettingsScreen(vm)
                Sec.HELP -> HelpScreen(vm, fvm)
            }
        }
    }
    }   // ModalNavigationDrawer
    }   // CompositionLocalProvider
}

/* ===== Drawer-меню: список усіх розділів (кожен — окреме вікно) ===== */
@Composable
private fun DrawerContent(current: Sec, onSelect: (Sec) -> Unit) {
    val uk = LocalLang.current == Lang.UK
    ModalDrawerSheet(drawerContainerColor = Apex.Panel, modifier = Modifier.width(268.dp)) {
        Column(Modifier.padding(horizontal = 12.dp, vertical = 16.dp)) {
            Text("ESP32·OS", color = Apex.Ink, fontFamily = FontFamily.Monospace, fontWeight = FontWeight.SemiBold, fontSize = 16.sp)
            Text("// MISSION CONTROL", color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 8.sp, letterSpacing = 2.sp, modifier = Modifier.padding(bottom = 12.dp))
            Box(Modifier.fillMaxWidth().height(1.dp).background(Apex.Edge))
            Spacer(Modifier.height(8.dp))
            Sec.values().forEach { s ->
                val on = s == current
                Row(Modifier.fillMaxWidth().clip(RoundedCornerShape(8.dp)).clickable { onSelect(s) }
                    .background(if (on) Apex.AccentSoft else androidx.compose.ui.graphics.Color.Transparent)
                    .padding(horizontal = 10.dp, vertical = 11.dp), verticalAlignment = Alignment.CenterVertically) {
                    Icon(s.icon, null, Modifier.size(20.dp), tint = if (on) Apex.Accent else Apex.Ink2)
                    Spacer(Modifier.width(12.dp))
                    Text(L(s.name.lowercase()), color = if (on) Apex.Accent else Apex.Ink, fontFamily = FontFamily.Monospace, fontSize = 13.sp, fontWeight = if (on) FontWeight.SemiBold else FontWeight.Normal, letterSpacing = 0.5.sp)
                }
            }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun UsbRelayBar(vm: MainViewModel) {
    val uk = LocalLang.current == Lang.UK
    Row(Modifier.fillMaxWidth().padding(bottom = 8.dp).background(Apex.Panel, RoundedCornerShape(8.dp))
        .border(1.dp, Apex.Edge, RoundedCornerShape(8.dp)).padding(horizontal = 10.dp, vertical = 6.dp),
        verticalAlignment = Alignment.CenterVertically) {
        Icon(Icons.Filled.Usb, null, Modifier.size(16.dp), tint = Apex.Accent)
        Spacer(Modifier.width(8.dp))
        Column(Modifier.weight(1f)) {
            Text(if (uk) "USB-ЛІНК · провідний" else "USB LINK · wired", color = Apex.Accent, fontFamily = FontFamily.Monospace, fontSize = 11.sp)
            Text(if (vm.relayEnabled) vm.relayMsg.ifBlank { if (uk) "реле активне →" else "relay on →" }
                 else (if (uk) "реле плата→сервер вимкнено" else "board→server relay off"),
                color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 9.sp)
        }
        FilterChip(vm.relayEnabled, { vm.toggleRelay() },
            { Text(if (uk) "РЕЛЕ→СЕРВЕР" else "RELAY→SERVER", fontSize = 9.sp) })
        Spacer(Modifier.width(6.dp))
        Text("✕", color = Apex.Bad, fontFamily = FontFamily.Monospace, fontSize = 15.sp, modifier = Modifier.clickable { vm.disconnect() })
    }
}

@Composable
private fun ConnectBar(vm: MainViewModel) {
    val ctx = LocalContext.current
    val uk = LocalLang.current == Lang.UK
    HudCard(L("connect"), when { vm.useUsb -> if (uk) "по USB-кабелю" else "over USB cable"; vm.useGateway -> L("gw_hint"); else -> L("connect_hint") }) {
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            FilterChip(!vm.useGateway && !vm.useUsb, { vm.useGateway = false; vm.useUsb = false }, { Text(L("direct")) })
            FilterChip(vm.useGateway, { vm.useGateway = true; vm.useUsb = false }, { Text(L("gateway")) })
            FilterChip(vm.useUsb, { vm.useUsb = true; vm.useGateway = false }, { Text("USB") })
        }
        Spacer(Modifier.height(8.dp))
        if (vm.useUsb) {
            Text(if (uk) "Провідне керування по OTG-кабелю: плата НЕ відпадає під час аналізу, лог наживо. Телефон може бути на своєму WiFi. PIN — той самий, що на екрані плати (статичний)."
                 else "Wired control over OTG cable: the board never drops during analysis, live log. Phone stays on its own WiFi. PIN — same as on the board screen (static).",
                color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 10.sp)
            Spacer(Modifier.height(6.dp))
            OutlinedTextField(vm.pin, { vm.pin = it }, label = { Text(L("pin")) }, singleLine = true, modifier = Modifier.width(140.dp))
            if (vm.usbLabel.isNotEmpty()) Text(vm.usbLabel, color = Apex.Ink2, fontFamily = FontFamily.Monospace, fontSize = 10.sp, modifier = Modifier.padding(top = 4.dp))
        } else if (vm.useGateway) {
            OutlinedTextField(vm.gwHost, { vm.gwHost = it }, label = { Text(L("gw_url")) }, singleLine = true, modifier = Modifier.fillMaxWidth())
            Spacer(Modifier.height(6.dp))
            OutlinedTextField(vm.gwToken, { vm.gwToken = it }, label = { Text(L("token")) }, singleLine = true, modifier = Modifier.fillMaxWidth())
        } else {
            Row {
                OutlinedTextField(vm.host, { vm.host = it }, label = { Text(L("host")) }, singleLine = true, modifier = Modifier.weight(1f))
                Spacer(Modifier.width(8.dp))
                OutlinedTextField(vm.pin, { vm.pin = it }, label = { Text(L("pin")) }, singleLine = true, modifier = Modifier.width(104.dp))
            }
            // #14: пряме під'єднання до РІДНОЇ точки плати (без спільної мережі)
            Row(Modifier.fillMaxWidth().padding(top = 6.dp), verticalAlignment = Alignment.CenterVertically) {
                OutlinedButton({ vm.host = "192.168.4.1" }, contentPadding = androidx.compose.foundation.layout.PaddingValues(horizontal = 10.dp, vertical = 2.dp)) {
                    Icon(Icons.Filled.Wifi, null, Modifier.size(14.dp)); Spacer(Modifier.width(4.dp))
                    Text(if (LocalLang.current == Lang.UK) "AP ПЛАТИ" else "BOARD AP", fontSize = 10.sp)
                }
                Spacer(Modifier.width(8.dp))
                Text(if (LocalLang.current == Lang.UK) "WiFi «OmniDiag-Setup» → 192.168.4.1\n(пароль на екрані плати)" else "join 'OmniDiag-Setup' → 192.168.4.1",
                    color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 9.sp)
            }
        }
        Row(Modifier.fillMaxWidth().padding(top = 8.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Button({ if (vm.useUsb) vm.connectUsb(ctx) else vm.connect(); Prefs.save(ctx, vm) }, Modifier.weight(1f)) {
                Text(if (vm.useUsb) (if (uk) "USB-КОНЕКТ" else "USB CONNECT") else L("connect"))
            }
            if (!vm.useGateway && !vm.useUsb) OutlinedButton({ vm.discover(ctx) }) { Icon(Icons.Filled.Wifi, null); Spacer(Modifier.width(4.dp)); Text(L("find")) }
        }
        Row(Modifier.fillMaxWidth().padding(top = 4.dp), verticalAlignment = Alignment.CenterVertically) {
            FilterChip(vm.autoConnect, { vm.autoConnect = !vm.autoConnect; Prefs.save(ctx, vm) },
                { Text(if (LocalLang.current == Lang.UK) "ЗАПАМ'ЯТАТИ + АВТО-ВХІД" else "REMEMBER + AUTO-CONNECT", fontSize = 10.sp) })
        }
        vm.discovered.forEach { (n, h) -> Text("· $n @ $h", Modifier.fillMaxWidth().clickable { vm.host = h }.padding(vertical = 3.dp), color = Apex.Accent, fontFamily = FontFamily.Monospace, fontSize = 12.sp) }
        if (vm.msg.isNotEmpty()) Text(vm.msg, color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 11.sp)
    }
}

/* ===== ІНСТРУМЕНТИ (TOOLS) = screen-launcher: мірор екрана плати + навігація ===== */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun ScreenLauncherScreen(vm: MainViewModel) {
    val lang = LocalLang.current
    LaunchedEffect(vm.connected) { if (vm.connected) { vm.refreshDevice(); vm.startLog() } }
    LazyColumn {
        if (!vm.connected) item { ConnectBar(vm); Spacer(Modifier.height(12.dp)) }
        item {
            HudCard("${L("screen")} · ${vm.mirror.page}", if (vm.wsConnected) L("live") else L("poll")) {
                if (!vm.connected) Text(if (lang == Lang.UK) "під'єднай плату, щоб керувати її екраном" else "connect the board to drive its screen",
                    color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 11.sp)
                vm.mirror.items.forEachIndexed { i, it ->
                    Row(Modifier.fillMaxWidth().clickable { vm.cmd(idx = i) }.padding(vertical = 9.dp),
                        verticalAlignment = Alignment.CenterVertically) {
                        Text(it, color = Apex.Ink, fontFamily = FontFamily.Monospace, fontSize = 14.sp, modifier = Modifier.weight(1f))
                        Text("›", color = Apex.Accent, fontFamily = FontFamily.Monospace, fontSize = 16.sp)
                    }
                }
                Spacer(Modifier.height(6.dp))
                // Мінімальний D-pad-фолбек для апок-дій (де кнопка = дія, а не вибір пункту).
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    OutlinedButton({ vm.cmd(back = true) }, Modifier.weight(1f)) { Icon(Icons.AutoMirrored.Filled.ArrowBack, null, Modifier.size(18.dp)) }
                    OutlinedButton({ vm.cmd(btn = "S5") }, Modifier.weight(1.4f)) { Text(if (lang == Lang.UK) "ДІЯ" else "ACT", fontSize = 12.sp) }
                    FilterChip(vm.wsConnected, { if (vm.wsConnected) vm.stopWs() else vm.startWs() }, { Text(if (vm.wsConnected) L("live") else "WS") })
                }
            }
        }
    }
}

/** Консоль (лог плати) — для розділу ПЛАТИ. */
@Composable
private fun ConsoleCard(vm: MainViewModel) {
    val uk = LocalLang.current == Lang.UK
    HudCard(if (uk) "КОНСОЛЬ" else "CONSOLE", if (uk) "лог плати" else "board log") {
        if (vm.logLines.isEmpty())
            Text(L("collecting"), color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 11.sp)
        Column(Modifier.heightIn(max = 260.dp).verticalScroll(rememberScrollState())) {
            vm.logLines.takeLast(120).forEach { line ->
                Text(line, color = logColor(line), fontFamily = FontFamily.Monospace, fontSize = 10.5.sp, lineHeight = 13.sp)
            }
        }
    }
}

/** Діагностика UNO (окремо від статусу, в одному розділі з ESP): активний пробінг + вердикт. */
@Composable
private fun UnoDiagCard(vm: MainViewModel) {
    val uk = LocalLang.current == Lang.UK
    HudCard(if (uk) "ДІАГНОСТИКА UNO" else "UNO DIAGNOSTIC", if (vm.unoDiagBusy) (if (uk) "діаг…" else "diag…") else "") {
        Text(if (uk) "Активна перевірка лінку UNO: шле SCAN, слухає UART, звіряє відповідь. Показує пробінг, вітали й CAP-модулі."
             else "Active UNO link check: sends SCAN, listens on UART, verifies the reply. Shows probe, vitals and CAP modules.",
             color = Apex.Ink2, fontFamily = FontFamily.Monospace, fontSize = 10.sp, lineHeight = 14.sp)
        Spacer(Modifier.height(8.dp))
        Button({ vm.runUnoDiag() }, Modifier.fillMaxWidth(), enabled = vm.connected && !vm.unoDiagBusy) {
            Text(if (vm.unoDiagBusy) "…" else (if (uk) "ЗАПУСТИТИ ДІАГНОСТИКУ UNO" else "RUN UNO DIAGNOSTIC"), fontSize = 12.sp, fontFamily = FontFamily.Monospace)
        }
        if (vm.unoVerdict.isNotBlank()) {
            val ok = vm.unoLine.contains("probe:✓")
            Text("UNO: " + vm.unoVerdict, color = if (ok) Apex.Accent else Apex.Warn,
                fontFamily = FontFamily.Monospace, fontSize = 11.sp, lineHeight = 15.sp, modifier = Modifier.padding(top = 6.dp))
            if (vm.unoLine.isNotBlank()) Text(vm.unoLine, color = Apex.Ink2, fontFamily = FontFamily.Monospace, fontSize = 10.5.sp, modifier = Modifier.padding(top = 2.dp))
            vm.unoModsDiag.forEach { Text("  $it", color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 10.5.sp) }
        }
    }
}

/** Статус/моніторинг плати ESP32: модулі, температура, живлення, охолодження. */
@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun EspBoardCard(vm: MainViewModel) {
    val uk = LocalLang.current == Lang.UK
    HudCard("ESP32 T-Display", if (vm.connected) (if (uk) "онлайн" else "online") else (if (uk) "офлайн" else "offline")) {
        FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            val m = vm.espModules
            StatusTag("nRF24", if (m["nrf24"] == true) 0 else 2)
            StatusTag("CC1101", if (m["cc1101"] == true) 0 else 1)
            StatusTag("SD", if (m["sd"] == true) 0 else 2)
            StatusTag("WiFi", if (m["wifi_sta"] == true) 0 else 2)
            StatusTag("AP", if (m["soft_ap"] == true) 0 else 1)
            StatusTag("BT", if (m["bt"] == true) 0 else 1)
        }
        Spacer(Modifier.height(8.dp))
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            if (vm.sysTemp > 0f) StatusTag("TEMP ${vm.sysTemp.toInt()}°C", vm.coolState)
            StatusTag((if (uk) "ФЕН " else "FAN ") + "${vm.coolDuty * 100 / 255}%", if (vm.coolDuty > 0) 0 else 1)
            if (vm.powerTotal > 0) StatusTag("${vm.powerTotal}mA", 0)
        }
    }
}

/** Моніторинг Arduino UNO (рівень ESP): лінк, вітали, CAP-модулі, датчики. Sensor Shield v5.0. */
@Composable
private fun UnoBoardCard(vm: MainViewModel) {
    val uk = LocalLang.current == Lang.UK
    HudCard("Arduino UNO R3 · Sensor Shield v5.0",
        if (vm.unoConnected) (if (uk) "онлайн" else "online") else (if (uk) "не підключено" else "not connected")) {
        Text(if (uk) "UNO на Sensor Shield v5.0 — піни розведені у 3-контактні гнізда (V/G/S). Модулі й датчики нижче — те, що фактично заведено та відповідає по лінку UART (GPIO37/22)."
             else "UNO on Sensor Shield v5.0 — pins broken out to 3-pin (V/G/S) sockets. Modules and sensors below are what's actually wired and reporting over the UART link (GPIO37/22).",
             color = Apex.Ink2, fontFamily = FontFamily.Monospace, fontSize = 10.sp, lineHeight = 14.sp)
        Spacer(Modifier.height(8.dp))
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            StatusTag(if (vm.unoConnected) "LINK" else "NO LINK", if (vm.unoConnected) 0 else 2)
            if (vm.unoFreeRam >= 0) StatusTag("RAM ${vm.unoFreeRam}B", 0)
            if (vm.unoUptime > 0) StatusTag("UP ${vm.unoUptime}s", 0)
        }
        if (vm.unoModules.isNotEmpty()) {
            Spacer(Modifier.height(8.dp))
            Text(if (uk) "МОДУЛІ (CAP):" else "MODULES (CAP):", color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 9.sp)
            vm.unoModules.forEach { (name, type, present) ->
                Row(Modifier.fillMaxWidth().padding(vertical = 2.dp), verticalAlignment = Alignment.CenterVertically) {
                    Text(name, color = Apex.Ink, fontFamily = FontFamily.Monospace, fontSize = 11.sp, modifier = Modifier.weight(1f))
                    Text(type, color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 9.sp, modifier = Modifier.padding(end = 8.dp))
                    StatusTag(if (present) "OK" else "N/A", if (present) 0 else 1)
                }
            }
        }
        if (vm.unoSensors.isNotEmpty()) {
            Spacer(Modifier.height(8.dp))
            Text(if (uk) "ДАТЧИКИ:" else "SENSORS:", color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 9.sp)
            vm.unoSensors.forEach { (k, v) ->
                Row(Modifier.fillMaxWidth().padding(vertical = 2.dp)) {
                    Text(k, color = Apex.Ink2, fontFamily = FontFamily.Monospace, fontSize = 11.sp, modifier = Modifier.weight(1f))
                    Text(v, color = Apex.Accent, fontFamily = FontFamily.Monospace, fontSize = 11.sp)
                }
            }
        }
        if (!vm.unoConnected) Text(if (uk) "UNO мовчить — перевір UART (GPIO37-RX/GPIO22-TX @9600), живлення 5V, спільну GND. Повна діагностика — у розділі ТЕСТИ."
                                   else "UNO silent — check UART (GPIO37-RX/GPIO22-TX @9600), 5V power, common GND. Full diagnostic in TESTS.",
            color = Apex.Warn, fontFamily = FontFamily.Monospace, fontSize = 9.5.sp, lineHeight = 13.sp, modifier = Modifier.padding(top = 6.dp))
    }
}

/* ===== ТЕСТИ (діагностика заліза) ===== */
@Composable
private fun TestsScreen(vm: MainViewModel) {
    LaunchedEffect(vm.connected) { if (vm.connected) vm.loadHwSafe() }
    LazyColumn { item { DiagCard(vm); Spacer(Modifier.height(12.dp)) } }
}

/* ===== КАЛІБРУВАННЯ (розрахунки + TX-калібр) ===== */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun CalibrationScreen(vm: MainViewModel) {
    val uk = LocalLang.current == Lang.UK
    var smithOpen by remember { mutableStateOf(false) }
    if (smithOpen) {
        Column(Modifier.fillMaxSize()) {
            Row(Modifier.fillMaxWidth().clickable { smithOpen = false }.padding(vertical = 12.dp), verticalAlignment = Alignment.CenterVertically) {
                Text(if (uk) "‹ НАЗАД" else "‹ BACK", color = Apex.Accent, fontFamily = FontFamily.Monospace, fontSize = 13.sp, fontWeight = FontWeight.SemiBold)
                Spacer(Modifier.weight(1f))
                Text(if (uk) "ДІАГРАМА СМІТА" else "SMITH CHART", color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 11.sp, letterSpacing = 1.5.sp)
            }
            Box(Modifier.fillMaxWidth().height(1.dp).background(Apex.Edge))
            LazyColumn(Modifier.padding(top = 10.dp)) { item { SmithCard() } }
        }
        return
    }
    LazyColumn {
        item {
            Row(Modifier.fillMaxWidth().padding(bottom = 12.dp)
                .background(Apex.Panel, RoundedCornerShape(12.dp)).border(1.dp, Apex.Accent, RoundedCornerShape(12.dp))
                .clickable { smithOpen = true }.padding(15.dp), verticalAlignment = Alignment.CenterVertically) {
                Column(Modifier.weight(1f)) {
                    Text(if (uk) "◈ ДІАГРАМА СМІТА" else "◈ SMITH CHART", color = Apex.Accent, fontFamily = FontFamily.Monospace, fontSize = 13.sp, fontWeight = FontWeight.SemiBold, letterSpacing = 1.sp)
                    Text(if (uk) "імпеданс антен · узгодження · жива математика" else "antenna impedance · matching · live math", color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 10.sp, modifier = Modifier.padding(top = 3.dp))
                }
                Text("›", color = Apex.Accent, fontFamily = FontFamily.Monospace, fontSize = 20.sp)
            }
        }
        item { RfCalibCard(vm) }
    }
}

/* ===== МОДУЛІ ТА ПІНИ (профіль/живлення модулів + SAFE-режим) ===== */
@Composable
private fun ModulesPinsScreen(vm: MainViewModel) {
    LaunchedEffect(vm.connected) { if (vm.connected) vm.loadHwSafe() }
    LazyColumn {
        item { ConfigCard(vm); Spacer(Modifier.height(12.dp)) }
        item { HwSafeCard(vm); Spacer(Modifier.height(12.dp)) }
    }
}

/* ===== НАЛАШТУВАННЯ (з'єднання, мова, точка доступу, хостинг, ключ, дисплей, збережені мережі) ===== */
@OptIn(ExperimentalMaterial3Api::class, ExperimentalLayoutApi::class)
@Composable
private fun SettingsScreen(vm: MainViewModel) {
    val lang = LocalLang.current; val uk = lang == Lang.UK
    LaunchedEffect(vm.connected) { if (vm.connected) { vm.loadAp(); vm.loadTools() } }
    LazyColumn {
        item {
            HudCard(if (uk) "З'ЄДНАННЯ ТА МОВА" else "CONNECTION & LANGUAGE", "") {
                Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                    Text(if (uk) "Мова інтерфейсу" else "UI language", color = Apex.Ink, fontFamily = FontFamily.Monospace, fontSize = 12.sp, modifier = Modifier.weight(1f))
                    Text(if (uk) "УКР" else "ENG", color = Apex.Accent, fontFamily = FontFamily.Monospace, fontSize = 13.sp,
                        modifier = Modifier.border(1.dp, Apex.Edge, RoundedCornerShape(4.dp)).clickable { vm.toggleLang() }.padding(horizontal = 10.dp, vertical = 3.dp))
                }
                Spacer(Modifier.height(8.dp))
                OutlinedTextField(vm.updateUrl, { vm.updateUrl = it }, singleLine = true,
                    label = { Text(if (uk) "Сервер оновлень / даних" else "Update / data server") },
                    textStyle = androidx.compose.ui.text.TextStyle(fontFamily = FontFamily.Monospace, fontSize = 13.sp),
                    modifier = Modifier.fillMaxWidth())
            }
        }
        item { ApCard(vm); Spacer(Modifier.height(12.dp)) }
        item { HostingCard(vm); Spacer(Modifier.height(12.dp)) }
        item { AuthKeyCard(vm); Spacer(Modifier.height(12.dp)) }
        item {
            HudCard(L("theme"), L("board_palette")) {
                if (vm.themes.isEmpty())
                    Text(if (!vm.connected) (if (uk) "під'єднай плату, щоб змінити палітру її дисплея" else "connect the board to change its display palette")
                         else (if (uk) "палітри вантажаться…" else "loading palettes…"),
                         color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 11.sp, lineHeight = 15.sp)
                else FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                    vm.themes.forEachIndexed { i, name ->
                        if (i == vm.themeCurrent) Button({ vm.setTheme(i) }) { Text(name, fontSize = 11.sp) }
                        else OutlinedButton({ vm.setTheme(i) }) { Text(name, fontSize = 11.sp) }
                    }
                }
            }
            Spacer(Modifier.height(12.dp))
        }
        item {
            HudCard(if (uk) "WIFI-МЕРЕЖІ ПЛАТИ" else "BOARD WIFI", if (vm.connected) "${vm.wifiNets.size} saved" else "—") {
                if (vm.wifiNets.isEmpty())
                    Text(if (!vm.connected) (if (uk) "під'єднай плату, щоб побачити її збережені мережі" else "connect the board to see its saved networks")
                         else (if (uk) "плата не має збережених мереж" else "board has no saved networks"),
                         color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 11.sp, lineHeight = 15.sp)
                vm.wifiNets.forEach { (i, ssid, auto) ->
                    Row(Modifier.fillMaxWidth().padding(vertical = 5.dp), verticalAlignment = Alignment.CenterVertically) {
                        Text(ssid, color = Apex.Ink, fontFamily = FontFamily.Monospace, fontSize = 12.sp, modifier = Modifier.weight(1f))
                        FilterChip(auto, { vm.wifiAuto(i, !auto) }, { Text("AUTO", fontSize = 9.sp) })
                        Spacer(Modifier.width(8.dp))
                        TapIcon("✕", tint = Apex.Bad, fontSize = 15.sp, onClick = { vm.wifiForget(i) })
                    }
                }
            }
        }
    }
}

/** Діагностика розводки: софт-«продзвонка» пінів (стан + КЗ) та активна перевірка SPI-шини nRF.
 *  Дає повторювати перевірку з телефона під час перепайки — без ПК. */
@Composable
private fun DiagCard(vm: MainViewModel) {
    val uk = LocalLang.current == Lang.UK
    HudCard(if (uk) "ДІАГНОСТИКА РОЗВОДКИ" else "WIRING DIAGNOSTIC",
            if (vm.diagBusy) (if (uk) "скан…" else "scanning…") else "") {
        Text(if (uk) "«Продзвонити» — стан кожного піна (FLOAT = нічого не тримає; HIGH/LOW_pull = під'єднано) + КЗ між пінами. «SPI-тест» — чи жива шина nRF (читання регістрів)."
             else "\"Ring out\" — each pin's state (FLOAT = nothing holds it; HIGH/LOW_pull = connected) + pin-to-pin shorts. \"SPI test\" — is the nRF bus alive (register read).",
             color = Apex.Ink2, fontFamily = FontFamily.Monospace, fontSize = 11.5.sp, lineHeight = 15.sp)
        Spacer(Modifier.height(10.dp))
        // Апаратний self-test — один тап, повна картина заліза.
        Button({ vm.runSelfTest() }, Modifier.fillMaxWidth(), enabled = vm.connected && !vm.selfTestBusy) {
            Text(if (vm.selfTestBusy) "…" else (if (uk) "SELF-TEST ЗАЛІЗА" else "HARDWARE SELF-TEST"),
                fontSize = 12.sp, fontFamily = FontFamily.Monospace)
        }
        if (vm.selfChecks.isNotEmpty()) {
            Spacer(Modifier.height(6.dp))
            if (vm.selfSummary.isNotBlank()) Text(vm.selfSummary, color = Apex.Ink, fontFamily = FontFamily.Monospace, fontSize = 12.sp, fontWeight = FontWeight.SemiBold, modifier = Modifier.padding(bottom = 4.dp))
            vm.selfChecks.forEach { (name, status, detail) ->
                val col = when (status) { "ok" -> Apex.Accent; "warn" -> Apex.Warn; else -> Apex.Bad }
                val mk = when (status) { "ok" -> "✓"; "warn" -> "!"; else -> "✗" }
                Row(Modifier.fillMaxWidth().padding(vertical = 1.dp)) {
                    Text(mk, color = col, fontFamily = FontFamily.Monospace, fontSize = 11.sp, modifier = Modifier.width(16.dp))
                    Text(name, color = Apex.Ink, fontFamily = FontFamily.Monospace, fontSize = 11.sp, modifier = Modifier.width(78.dp))
                    Text(detail, color = Apex.Ink2, fontFamily = FontFamily.Monospace, fontSize = 10.5.sp, modifier = Modifier.weight(1f))
                }
            }
            OutlinedButton({ vm.sendSelfTestToBot() }, Modifier.fillMaxWidth().padding(top = 4.dp), enabled = !vm.botBusy) {
                Text(if (uk) "SELF-TEST У БОТА" else "SELF-TEST TO BOT", fontSize = 10.sp, fontFamily = FontFamily.Monospace)
            }
            if (vm.botMsg.isNotBlank()) Text(vm.botMsg, color = Apex.Accent, fontFamily = FontFamily.Monospace, fontSize = 9.5.sp)
        }
        Spacer(Modifier.height(10.dp))
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            OutlinedButton({ vm.runPinScan() }, Modifier.weight(1f), enabled = vm.connected && !vm.diagBusy) {
                Text(if (uk) "ПРОДЗВОНИТИ" else "RING OUT", fontSize = 11.sp, fontFamily = FontFamily.Monospace)
            }
            OutlinedButton({ vm.runSpiTest() }, Modifier.weight(1f), enabled = vm.connected && !vm.diagBusy) {
                Text(if (uk) "SPI-ТЕСТ" else "SPI TEST", fontSize = 11.sp, fontFamily = FontFamily.Monospace)
            }
        }
        Spacer(Modifier.height(6.dp))
        OutlinedButton({ vm.runUnoDiag() }, Modifier.fillMaxWidth(), enabled = vm.connected && !vm.unoDiagBusy) {
            Text(if (vm.unoDiagBusy) "…" else (if (uk) "ДІАГНОСТИКА UNO" else "UNO DIAGNOSTIC"), fontSize = 11.sp, fontFamily = FontFamily.Monospace)
        }
        if (vm.unoVerdict.isNotBlank()) {
            val ok = vm.unoLine.contains("probe:✓")
            Text("UNO: " + vm.unoVerdict, color = if (ok) Apex.Accent else Apex.Warn,
                fontFamily = FontFamily.Monospace, fontSize = 11.sp, lineHeight = 15.sp, modifier = Modifier.padding(top = 6.dp))
            if (vm.unoLine.isNotBlank()) Text(vm.unoLine, color = Apex.Ink2, fontFamily = FontFamily.Monospace, fontSize = 10.5.sp, modifier = Modifier.padding(top = 2.dp))
            vm.unoModsDiag.forEach { Text("  $it", color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 10.5.sp) }
        }
        if (vm.diagPins.isNotEmpty()) {
            Spacer(Modifier.height(10.dp))
            vm.diagPins.forEach { (gpio, name, state) ->
                val col = when {
                    state.startsWith("HIGH") || state.startsWith("LOW_") -> Apex.Accent  // щось тримає -> під'єднано
                    state == "FLOAT" -> Apex.Warn
                    else -> Apex.Muted                                                     // input-only "LOW?"
                }
                Row(Modifier.fillMaxWidth().padding(vertical = 2.dp)) {
                    Text("GPIO${gpio}", color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 11.sp, modifier = Modifier.width(58.dp))
                    Text(name, color = Apex.Ink, fontFamily = FontFamily.Monospace, fontSize = 11.sp, modifier = Modifier.weight(1f))
                    Text(state, color = col, fontFamily = FontFamily.Monospace, fontSize = 11.sp, fontWeight = FontWeight.SemiBold)
                }
            }
            Text(if (vm.diagBridges.isEmpty()) (if (uk) "КЗ між пінами: немає ✓" else "pin shorts: none ✓")
                 else (if (uk) "⚠ КЗ/мостики: " else "⚠ shorts/bridges: ") + vm.diagBridges.joinToString(", "),
                 color = if (vm.diagBridges.isEmpty()) Apex.Accent else Apex.Bad,
                 fontFamily = FontFamily.Monospace, fontSize = 11.sp, modifier = Modifier.padding(top = 6.dp))
        }
        if (vm.diagSpi.isNotBlank()) Text("SPI: " + vm.diagSpi,
            color = if (vm.diagSpi.contains("OK")) Apex.Accent else Apex.Bad,
            fontFamily = FontFamily.Monospace, fontSize = 11.sp, modifier = Modifier.padding(top = 6.dp))
        if (!vm.connected) Text(if (uk) "під'єднай плату, щоб продзвонити" else "connect the board to ring out",
            color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 10.sp, modifier = Modifier.padding(top = 4.dp))
    }
}
/** Безпечний режим пінів: свіч вимикає драйв усіх GPIO модулів (high-Z) — голі ніжки не коротнуть. */
@Composable
private fun HwSafeCard(vm: MainViewModel) {
    val uk = LocalLang.current == Lang.UK
    var confirmActivate by remember { mutableStateOf(false) }
    HudCard(if (uk) "БЕЗПЕЧНИЙ РЕЖИМ ПІНІВ" else "SAFE PIN MODE",
            if (vm.hwSafe) (if (uk) "піни off" else "pins off") else (if (uk) "активні" else "active")) {
        Text(if (uk) "Увімкнено → усі GPIO модулів у high-Z: голі ніжки плати без модулів не закоротять. При активації плата авто-перевіряє кожен пін (drive-verify) і НЕ подає струм на закорочені на рейку. Дисплей / кнопки / WiFi працюють."
             else "On → all module GPIOs go high-Z: bare pins can't short with no modules attached. On activation the board auto-checks each pin (drive-verify) and won't drive any that's shorted to a rail. Display / buttons / WiFi keep working.",
             color = Apex.Ink2, fontFamily = FontFamily.Monospace, fontSize = 11.5.sp, lineHeight = 15.sp)
        Spacer(Modifier.height(10.dp))
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Text(if (vm.hwSafe) (if (uk) "Піни вимкнено (безпечно)" else "Pins off (safe)")
                 else (if (uk) "Піни активні" else "Pins active"),
                 color = if (vm.hwSafe) Apex.Accent else Apex.Warn, fontFamily = FontFamily.Monospace, fontSize = 12.sp,
                 fontWeight = FontWeight.SemiBold, modifier = Modifier.weight(1f))
            Switch(checked = vm.hwSafe, enabled = vm.connected && !vm.hwSafeBusy,
                onCheckedChange = { wantSafe -> if (wantSafe) vm.setHwSafe(true) else confirmActivate = true },
                colors = SwitchDefaults.colors(checkedThumbColor = Apex.Accent, checkedTrackColor = Apex.AccentSoft))
        }
        if (vm.hwSafeBlocked.isNotEmpty()) Text(
            (if (uk) "⚠ КЗ на рейку — не активовано: " else "⚠ shorted to rail — not activated: ") + vm.hwSafeBlocked.joinToString(", "),
            color = Apex.Bad, fontFamily = FontFamily.Monospace, fontSize = 11.sp, modifier = Modifier.padding(top = 6.dp))
        if (!vm.connected) Text(if (uk) "під'єднай плату, щоб керувати" else "connect the board to control",
            color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 10.sp, modifier = Modifier.padding(top = 4.dp))
    }
    if (confirmActivate) AlertDialog(
        onDismissRequest = { confirmActivate = false }, containerColor = Apex.Panel,
        title = { Text(if (uk) "Активувати піни?" else "Activate pins?", color = Apex.Warn, fontFamily = FontFamily.Monospace, fontSize = 14.sp) },
        text = { Text(if (uk) "Плата спершу авто-перевірить кожен пін і подасть струм ЛИШЕ на не закорочені (закорочені на рейку лишить у high-Z і покаже їх). Продовжити?"
                      else "The board will auto-check each pin and drive ONLY the non-shorted ones (any shorted to a rail stays high-Z and is reported). Continue?",
                      color = Apex.Ink2, fontFamily = FontFamily.Monospace, fontSize = 12.sp) },
        confirmButton = { TextButton({ confirmActivate = false; vm.setHwSafe(false) }) { Text(if (uk) "АКТИВУВАТИ" else "ACTIVATE", color = Apex.Warn) } },
        dismissButton = { TextButton({ confirmActivate = false }) { Text(if (uk) "Скасувати" else "Cancel") } },
    )
}

/** Динамічний креденшел (HMAC-seed) — показ/сховати/копіювати. Логін на плату без статичного PIN. */
@Composable
private fun AuthKeyCard(vm: MainViewModel) {
    val uk = LocalLang.current == Lang.UK
    val ctx = LocalContext.current
    var reveal by remember { mutableStateOf(false) }
    val seed = vm.hmacSeed
    HudCard(if (uk) "ДИНАМІЧНИЙ КЛЮЧ" else "DYNAMIC KEY",
            if (!seed.isNullOrBlank()) "HMAC-256" else if (uk) "нема" else "none") {
        if (seed.isNullOrBlank()) {
            Text(if (uk) "Ключ ще не провізовано. Під'єднайся з PIN хоча б раз — додаток автоматично отримає динамічний ключ від плати. Найбезпечніше — по USB (довірений канал)."
                 else "Key not provisioned yet. Connect with the PIN once — the app auto-provisions a dynamic key from the board. USB is the safest (trusted channel).",
                 color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 11.sp, lineHeight = 15.sp)
        } else {
            Text(if (uk) "Провізовано ✓ — вхід без статичного PIN. Кожен логін = свіжий challenge від плати (повтор неможливий)."
                 else "Provisioned ✓ — login without the static PIN. Each login = a fresh challenge from the board (no replay).",
                 color = Apex.Accent, fontFamily = FontFamily.Monospace, fontSize = 10.5.sp, lineHeight = 14.sp)
            Spacer(Modifier.height(8.dp))
            Text(if (reveal) seed.chunked(8).joinToString(" ") else "•".repeat(32),
                 color = Apex.Ink, fontFamily = FontFamily.Monospace, fontSize = 12.sp, lineHeight = 18.sp)
            Spacer(Modifier.height(8.dp))
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedButton({ reveal = !reveal }, Modifier.weight(1f)) {
                    Text(if (reveal) (if (uk) "СХОВАТИ" else "HIDE") else (if (uk) "ПОКАЗАТИ" else "REVEAL"), fontSize = 11.sp)
                }
                OutlinedButton({
                    val cm = ctx.getSystemService(android.content.Context.CLIPBOARD_SERVICE) as android.content.ClipboardManager
                    val clip = android.content.ClipData.newPlainText("hmac-seed", seed)
                    clip.description.extras = android.os.PersistableBundle().apply {
                        putBoolean("android.content.extra.IS_SENSITIVE", true)   // приховати з прев'ю буфера (A13+)
                    }
                    cm.setPrimaryClip(clip)
                    vm.msg = if (uk) "ключ скопійовано" else "key copied"
                }, Modifier.weight(1f)) { Text(if (uk) "КОПІЮВАТИ" else "COPY", fontSize = 11.sp) }
            }
        }
    }
}
internal fun logColor(l: String): Color = when {
    l.contains("FAIL", true) || l.contains("error", true) || l.contains("!!", true) -> Apex.Bad
    l.contains("WARN", true) || l.contains("timeout", true) -> Apex.Warn
    l.startsWith("[") -> Apex.Ink2
    else -> Apex.Ink2
}
private fun rssiLevel(r: Int?): Int = when { r == null -> 0; r > -60 -> 0; r > -80 -> 1; else -> 2 }
private fun upStr(s: Int): String = if (s < 3600) "${s / 60}m ${s % 60}s" else "${s / 3600}h ${s % 3600 / 60}m"

/* ===== АРХІВ = ЗВІТИ + ЛОГИ (сесії, телефон/сервер) + БІБЛІОТЕКА (серверні дані) ===== */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun ArchiveScreen(vm: MainViewModel, avm: AdminViewModel) {
    val uk = LocalLang.current == Lang.UK
    var mode by remember { mutableStateOf(0) }   // 0=звіти 1=логи 2=бібліотека
    LaunchedEffect(mode) { if (mode == 2) avm.refreshLibrary() }
    Column(Modifier.fillMaxSize()) {
        TabRow(selectedTabIndex = mode, containerColor = Apex.Panel2, contentColor = Apex.Accent) {
            Tab(mode == 0, { mode = 0 }) { Text(if (uk) "ЗВІТИ ${vm.archive.size}" else "REPORTS ${vm.archive.size}", fontFamily = FontFamily.Monospace, fontSize = 11.sp, modifier = Modifier.padding(10.dp)) }
            Tab(mode == 1, { mode = 1 }) { Text(if (uk) "ЛОГИ" else "LOGS", fontFamily = FontFamily.Monospace, fontSize = 11.sp, modifier = Modifier.padding(10.dp)) }
            Tab(mode == 2, { mode = 2 }) { Text(if (uk) "БІБЛІОТЕКА" else "LIBRARY", fontFamily = FontFamily.Monospace, fontSize = 11.sp, modifier = Modifier.padding(10.dp)) }
        }
        when (mode) { 1 -> LogsScreen(vm, vm.updateUrl); 2 -> LibraryScreen(avm); else -> ReportsList(vm) }
    }
}

@Composable
private fun ReportsList(vm: MainViewModel) {
    val byCat = vm.archive.groupBy { it.cat }
    LazyColumn {
        byCat.toSortedMap().forEach { (cat, list) ->
            item { HudCard(cat, "${list.size} ${L("items")}") {
                list.forEach { a -> Text("· ${a.name}", Modifier.fillMaxWidth().clickable { vm.openReport(a.name) }.padding(vertical = 5.dp), color = Apex.Ink2, fontFamily = FontFamily.Monospace, fontSize = 12.sp) }
            } }
        }
    }
    if (vm.reportName.isNotEmpty()) AlertDialog(
        onDismissRequest = { vm.closeReport() }, containerColor = Apex.Panel,
        confirmButton = { TextButton({ vm.closeReport() }) { Text(L("close")) } },
        dismissButton = { TextButton({ vm.deleteCurrentReport() }) { Text("DELETE", color = Apex.Bad) } },
        title = { Text(vm.reportName, fontFamily = FontFamily.Monospace, fontSize = 13.sp) },
        text = { LazyColumn(Modifier.heightIn(max = 420.dp)) { item { Text(vm.reportContent, fontFamily = FontFamily.Monospace, fontSize = 11.sp, color = Apex.Ink) } } },
    )
}

/* ===== APPS ===== */
@Composable
private fun AppsScreen(vm: MainViewModel) {
    val uk = LocalLang.current == Lang.UK
    LazyColumn {
        item {
            HudCard(L("scripts"), "berry .be · ${vm.scripts.size}") {
                Row(Modifier.fillMaxWidth().padding(bottom = 4.dp)) {
                    OutlinedButton({ vm.newScript() }, Modifier.weight(1f),
                        contentPadding = androidx.compose.foundation.layout.PaddingValues(horizontal = 10.dp, vertical = 2.dp)) {
                        Icon(Icons.Filled.Add, null, Modifier.size(16.dp)); Spacer(Modifier.width(4.dp))
                        Text(if (uk) "НОВИЙ СКРИПТ" else "NEW SCRIPT", fontSize = 11.sp)
                    }
                }
                if (vm.scripts.isEmpty())
                    Text(L("no_scripts"), color = Apex.Muted, fontFamily = FontFamily.Monospace)
                vm.scripts.forEach { s ->
                    Row(Modifier.fillMaxWidth().padding(vertical = 8.dp), verticalAlignment = Alignment.CenterVertically) {
                        Text(s.name, color = Apex.Ink, fontFamily = FontFamily.Monospace, fontSize = 13.sp,
                            modifier = Modifier.weight(1f).clickable { vm.openEditor(s) })
                        Text(s.cat, color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 10.sp,
                            modifier = Modifier.border(1.dp, Apex.Edge, RoundedCornerShape(4.dp)).padding(horizontal = 6.dp, vertical = 1.dp))
                        Spacer(Modifier.width(12.dp))
                        Text(if (uk) "РЕД" else "EDIT", color = Apex.Ink2, fontFamily = FontFamily.Monospace, fontSize = 11.sp,
                            modifier = Modifier.clickable { vm.openEditor(s) })
                        Spacer(Modifier.width(14.dp))
                        Text("RUN ▸", color = Apex.Accent, fontFamily = FontFamily.Monospace, fontSize = 12.sp, fontWeight = FontWeight.SemiBold,
                            modifier = Modifier.clickable { vm.runScript(s) })
                    }
                }
            }
        }
        item { ToolHelpCard("scripts") }
    }
    if (vm.editorOpen) ScriptEditorDialog(vm, uk)
    if (vm.scriptOut != null) AlertDialog(
        onDismissRequest = { vm.closeScript() }, containerColor = Apex.Panel,
        confirmButton = { TextButton({ vm.closeScript() }) { Text(L("close")) } },
        title = { Text(vm.scriptName, fontFamily = FontFamily.Monospace, fontSize = 13.sp, color = Apex.Accent) },
        text = { LazyColumn(Modifier.heightIn(max = 380.dp)) { item {
            Text(vm.scriptOut ?: "", fontFamily = FontFamily.Monospace, fontSize = 12.sp,
                color = if (vm.scriptOut?.startsWith("ERROR") == true) Apex.Bad else Apex.Ink)
        } } },
    )
}

/* ===== IDE скриптів: редактор .be (створити/редагувати/зберегти/запустити) ===== */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun ScriptEditorDialog(vm: MainViewModel, uk: Boolean) {
    AlertDialog(
        onDismissRequest = { vm.closeEditor() }, containerColor = Apex.Panel,
        title = {
            Text((if (vm.editorIsNew) (if (uk) "НОВИЙ СКРИПТ" else "NEW SCRIPT") else "✎ ${vm.editorName}"),
                fontFamily = FontFamily.Monospace, fontSize = 13.sp, color = Apex.Accent)
        },
        text = {
            Column(Modifier.fillMaxWidth().heightIn(max = 480.dp).verticalScroll(rememberScrollState())) {
                if (vm.editorIsNew) {
                    OutlinedTextField(vm.editorName, { vm.editorName = it }, singleLine = true,
                        label = { Text(if (uk) "ім'я (напр. mytool.be)" else "name (e.g. mytool.be)") },
                        modifier = Modifier.fillMaxWidth())
                    Spacer(Modifier.height(6.dp))
                }
                Text(if (uk) "КАТЕГОРІЯ" else "CATEGORY", color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 9.sp, letterSpacing = 1.sp)
                Row(Modifier.fillMaxWidth().padding(vertical = 4.dp), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                    vm.scriptCats.forEach { c ->
                        FilterChip(vm.editorCat == c, { vm.editorCat = c }, { Text(c, fontSize = 9.sp) })
                    }
                }
                Spacer(Modifier.height(6.dp))
                OutlinedTextField(vm.editorSource, { vm.editorSource = it },
                    label = { Text(if (uk) "код Berry (.be)" else "Berry code (.be)") },
                    textStyle = androidx.compose.ui.text.TextStyle(fontFamily = FontFamily.Monospace, fontSize = 12.sp),
                    modifier = Modifier.fillMaxWidth().heightIn(min = 200.dp, max = 320.dp))
                if (vm.editorMsg.isNotEmpty())
                    Text(vm.editorMsg, color = if (vm.editorMsg.contains("✓")) Apex.Accent else Apex.Warn,
                        fontFamily = FontFamily.Monospace, fontSize = 10.sp, modifier = Modifier.padding(top = 6.dp))
                Text(if (uk) "app_draw() малює кадр 240×135, app_button(id) — реакція. Функції — ПЕРЕД використанням."
                     else "app_draw() renders 240×135, app_button(id) handles keys. Declare functions before use.",
                    color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 9.sp, modifier = Modifier.padding(top = 6.dp))
            }
        },
        confirmButton = {
            Row {
                TextButton({ vm.saveEditor(false) }, enabled = !vm.editorSaving) { Text(if (uk) "ЗБЕРЕГТИ" else "SAVE", color = Apex.Accent) }
                TextButton({ vm.saveEditor(true) }, enabled = !vm.editorSaving) { Text("▸ RUN", color = Apex.Accent) }
            }
        },
        dismissButton = {
            Row {
                if (!vm.editorIsNew) TextButton({ vm.deleteEditorScript() }) { Text("DEL", color = Apex.Bad) }
                TextButton({ vm.closeEditor() }) { Text(L("close")) }
            }
        },
    )
}

/* ===== APP UPDATE — self-update із захистом від поламаних збірок ===== */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun UpdateCard(vm: MainViewModel, ctx: Context, lang: Lang) {
    HudCard(if (lang == Lang.UK) "ОНОВЛЕННЯ ДОДАТКА" else "APP UPDATE", "v${vm.myVc}") {
        OutlinedTextField(vm.updateUrl, { vm.updateUrl = it }, singleLine = true,
            label = { Text(if (lang == Lang.UK) "Сервер оновлень" else "Update server") },
            modifier = Modifier.fillMaxWidth())
        Spacer(Modifier.height(8.dp))
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Button({ vm.checkUpdate() }, Modifier.weight(1f)) { Text(if (lang == Lang.UK) "ПЕРЕВІРИТИ" else "CHECK") }
            if (vm.updateAvail != null) Button(
                { vm.downloadUpdate(ctx.getExternalFilesDir(null) ?: ctx.cacheDir) },
                Modifier.weight(1f), enabled = vm.updateProgress < 0) {
                Text(if (vm.updateProgress in 0..99) "${vm.updateProgress}%" else (if (lang == Lang.UK) "ОНОВИТИ" else "UPDATE"))
            }
        }
        if (vm.updateStatus.isNotEmpty()) Text(vm.updateStatus,
            color = if (vm.updateStatus.contains("НЕ ЗБІГСЯ") || vm.updateStatus.contains("MISMATCH")) Apex.Bad else Apex.Ink2,
            fontFamily = FontFamily.Monospace, fontSize = 11.sp, modifier = Modifier.padding(top = 6.dp))
        vm.updateAvail?.let { if (it.notes.isNotBlank()) Text(it.notes, color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 10.sp) }
        Spacer(Modifier.height(6.dp))
        Text(if (lang == Lang.UK) "Захист: SHA-256 + якщо додаток зламається — відкрий /ota у браузері й переустанови."
             else "Safety: SHA-256 + if the app breaks, open /ota in a browser and reinstall.",
            color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 9.sp)
        Text("Recovery ▸ ${vm.updateUrl}/ota", color = Apex.Accent, fontFamily = FontFamily.Monospace, fontSize = 10.sp,
            modifier = Modifier.clickable { AppUpdate.openRecovery(ctx, vm.updateUrl) }.padding(top = 2.dp))
    }
}

/* ===== HELP — довідка по кожному інструменту (пошук + розкриття) ===== */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun HelpScreen(vm: MainViewModel, fvm: FlashViewModel) {
    val lang = LocalLang.current
    val ctx = LocalContext.current
    var q by remember { mutableStateOf("") }
    var open by remember { mutableStateOf<String?>(null) }
    // Авто-встановлення після успішної SHA-верифікації (recovery — браузерна /ota).
    LaunchedEffect(vm.readyApk) {
        val apk = vm.readyApk ?: return@LaunchedEffect
        if (AppUpdate.canInstall(ctx)) { AppUpdate.install(ctx, apk); vm.readyApk = null }
        else { vm.updateStatus = "дозволь встановлення з цього джерела →"; AppUpdate.requestInstallPermission(ctx) }
    }
    val keys = HELP_ORDER.filter { k ->
        q.isBlank() || toolHelp(k, lang)?.title?.contains(q, ignoreCase = true) == true
    }
    LazyColumn {
        item { UpdateCard(vm, ctx, lang) }
        item { FirmwareUpdateCard(fvm, vm.updateUrl) }
        item {
            OutlinedTextField(q, { q = it }, singleLine = true,
                label = { Text(if (lang == Lang.UK) "Пошук інструмента" else "Search tool") },
                leadingIcon = { Icon(Icons.Filled.Search, null) },
                modifier = Modifier.fillMaxWidth().padding(vertical = 8.dp))
        }
        items(keys) { k ->
            val h = toolHelp(k, lang) ?: return@items
            if (open == k) {
                Box(Modifier.clickable { open = null }) { ToolHelpCard(k) }
            } else {
                Row(Modifier.fillMaxWidth().clickable { open = k }.padding(vertical = 12.dp),
                    verticalAlignment = Alignment.CenterVertically) {
                    Text(h.title, color = Apex.Ink, fontFamily = FontFamily.Monospace, fontSize = 13.sp, modifier = Modifier.weight(1f))
                    Text("›", color = Apex.Accent, fontFamily = FontFamily.Monospace, fontSize = 16.sp)
                }
                Box(Modifier.fillMaxWidth().height(1.dp).background(Apex.Edge))
            }
        }
    }
}

/* ===== ПЛАТИ (BOARDS) = моніторинг ESP+UNO (статус+діагностика) + аналіз-інструменти ===== */
@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun BoardsScreen(vm: MainViewModel) {
    val lang = LocalLang.current
    val uk = lang == Lang.UK
    var askReboot by remember { mutableStateOf(false) }
    var dataOpen by remember { mutableStateOf(false) }
    LaunchedEffect(vm.connected) { if (vm.connected) { vm.refreshDevice(); vm.startLog(); vm.loadModules(); vm.loadTools() } }
    if (dataOpen) { DataViewerScreen(vm) { dataOpen = false }; return }   // переглядач усіх зібраних даних (офлайн-доступний)
    if (askReboot) AlertDialog(
        onDismissRequest = { askReboot = false }, containerColor = Apex.Panel,
        title = { Text(if (lang == Lang.UK) "Перезавантажити плату?" else "Reboot board?", fontFamily = FontFamily.Monospace, fontSize = 14.sp, color = Apex.Ink) },
        text = { Text(if (lang == Lang.UK) "Зв'язок обірветься до перезапуску." else "The link will drop until it restarts.", fontFamily = FontFamily.Monospace, fontSize = 12.sp, color = Apex.Ink2) },
        confirmButton = { TextButton({ askReboot = false; vm.reboot() }) { Text(if (lang == Lang.UK) "РЕСТАРТ" else "REBOOT", color = Apex.Bad) } },
        dismissButton = { TextButton({ askReboot = false }) { Text(L("close")) } },
    )
    LazyColumn {
        if (!vm.connected) item { ConnectBar(vm); Spacer(Modifier.height(12.dp)) }
        item {
            val s = vm.status
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                StatTile(L("battery"), "${s?.mv ?: "--"}", "mV", modifier = Modifier.weight(1f))
                StatTile(L("rssi"), "${s?.rssi ?: "--"}", "dBm", level = rssiLevel(s?.rssi), modifier = Modifier.weight(1f))
            }
            Spacer(Modifier.height(12.dp))
        }
        item { EspBoardCard(vm); Spacer(Modifier.height(12.dp)) }
        item { UnoBoardCard(vm); Spacer(Modifier.height(12.dp)) }
        item { UnoDiagCard(vm); Spacer(Modifier.height(12.dp)) }
        item { ConsoleCard(vm); Spacer(Modifier.height(12.dp)) }
        item {
            Row(Modifier.fillMaxWidth().padding(bottom = 12.dp)
                .background(Apex.Panel, RoundedCornerShape(12.dp)).border(1.dp, Apex.Accent, RoundedCornerShape(12.dp))
                .clickable { dataOpen = true }.padding(15.dp), verticalAlignment = Alignment.CenterVertically) {
                Column(Modifier.weight(1f)) {
                    Text(if (uk) "◈ ПЕРЕГЛЯДАЧ ДАНИХ" else "◈ DATA VIEWER", color = Apex.Accent, fontFamily = FontFamily.Monospace, fontSize = 13.sp, fontWeight = FontWeight.SemiBold, letterSpacing = 1.sp)
                    Text(if (uk) "усі зібрані дані · повний обсяг · офлайн · описи" else "all collected data · full · offline · legends", color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 10.sp, modifier = Modifier.padding(top = 3.dp))
                }
                Text("${vm.airAps.size + vm.netHosts.size}", color = Apex.Accent, fontFamily = FontFamily.Monospace, fontSize = 16.sp, fontWeight = FontWeight.Bold)
                Text(" ›", color = Apex.Accent, fontFamily = FontFamily.Monospace, fontSize = 20.sp)
            }
        }
        if (vm.targetIp.isNotBlank()) item {
            Row(Modifier.fillMaxWidth().padding(bottom = 8.dp).background(Apex.AccentSoft, RoundedCornerShape(8.dp))
                .border(1.dp, Apex.Accent, RoundedCornerShape(8.dp)).padding(10.dp), verticalAlignment = Alignment.CenterVertically) {
                StatusTag("TGT", 0); Spacer(Modifier.width(8.dp))
                Column(Modifier.weight(1f)) {
                    Text((if (lang == Lang.UK) "ТАРГЕТ: " else "TARGET: ") + vm.targetIp, color = Apex.Accent, fontFamily = FontFamily.Monospace, fontSize = 12.sp, fontWeight = FontWeight.SemiBold)
                    Text(vm.targetLabel.ifBlank { vm.targetMac }, color = Apex.Ink2, fontFamily = FontFamily.Monospace, fontSize = 9.sp)
                }
                // #3: дія над таргетом — глибока розвідка одним тапом (порти/hostname/UPnP/OUI)
                Text(if (lang == Lang.UK) "РОЗВІДКА" else "INTEL", color = Apex.Accent, fontFamily = FontFamily.Monospace,
                    fontSize = 11.sp, fontWeight = FontWeight.SemiBold,
                    modifier = Modifier.clickable { vm.scanDeviceIntel(vm.targetMac, vm.targetIp) }
                        .border(1.dp, Apex.Accent, RoundedCornerShape(4.dp)).padding(horizontal = 6.dp, vertical = 3.dp))
                Spacer(Modifier.width(10.dp))
                Text("✕", color = Apex.Bad, fontFamily = FontFamily.Monospace, fontSize = 15.sp, modifier = Modifier.clickable { vm.clearTarget() })
            }
        }
        item {
            val srv = vm.serverStats
            val svcOk = srv?.get("services")?.jsonObject?.all { it.value.jsonPrimitive.content == "active" } ?: false
            val items = listOf(
                ReadyItem("LINK", if (vm.connected) "up" else "down", if (vm.connected) 0 else 2),
                ReadyItem("POWER", "${vm.powerTotal}mA", vm.coolState),
                ReadyItem("TEMP", "${vm.sysTemp.toInt()}°C", if (vm.sysTemp > 75) 2 else if (vm.sysTemp > 65) 1 else 0),
                ReadyItem("RADIO", if (vm.espModules["nrf24"] == true) "nRF24" else "none",
                    if (vm.espModules["nrf24"] == true || vm.espModules["cc1101"] == true) 0 else 1),
                ReadyItem("SD", if (vm.sdMounted) "ok" else "none", if (vm.sdMounted) 0 else 1),
                ReadyItem("UNO", if (vm.unoConnected) "link" else "off", if (vm.unoConnected) 0 else 2),
                ReadyItem("SERVER", if (srv == null) "offline" else "online", if (svcOk) 0 else if (srv != null) 1 else 2),
            )
            HudCard(if (lang == Lang.UK) "ГОТОВНІСТЬ" else "READINESS", "mission control") {
                ReadinessPanel(items)
            }
        }
        item {
            HudCard(L("system"), L("temp_power")) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Box(Modifier.weight(1f)) {
                        RadialGauge(vm.sysTemp, min = 20f, max = 85f, unit = "°C", level = vm.coolState)
                    }
                    Column(Modifier.weight(1f)) {
                        LeaderRow(L("cooling"), when (vm.coolState) { 2 -> L("crit"); 1 -> L("warn"); else -> L("ok") },
                            null, vm.coolState)
                        LeaderRow(L("fan_pwm"), "${vm.coolDuty * 100 / 255}%")
                        LeaderRow(L("draw"), "${vm.powerTotal} mA")
                    }
                }
                Spacer(Modifier.height(8.dp))
                val total = vm.powerTotal.coerceAtLeast(1)
                vm.powerBd.filter { it.second > 0 }.forEach { (name, ma) ->
                    Column(Modifier.padding(vertical = 3.dp)) {
                        LeaderRow(name, "$ma mA")
                        SegBar(ma * 100 / total)
                    }
                }
                Spacer(Modifier.height(10.dp))
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    OutlinedButton({ vm.passive() }, Modifier.weight(1f)) {
                        Text(if (lang == Lang.UK) "СОН ЕКРАНА" else "SLEEP", fontSize = 11.sp)
                    }
                    OutlinedButton({ askReboot = true }, Modifier.weight(1f)) {
                        Text(if (lang == Lang.UK) "РЕСТАРТ" else "REBOOT", fontSize = 11.sp, color = Apex.Bad)
                    }
                }
                ToolHelpInline("system")
            }
        }
        item {
            val srv = vm.serverStats
            HudCard(if (lang == Lang.UK) "СЕРВЕР · stolos" else "SERVER · stolos",
                if (srv == null) "offline" else "up ${((srv["uptime_s"]?.jsonPrimitive?.intOrNull ?: 0) / 3600)}h") {
                if (srv == null) Text(if (lang == Lang.UK) "нема зв'язку з сервером" else "no server link", color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 11.sp)
                else {
                    val load = srv["load_pct"]?.jsonPrimitive?.intOrNull ?: 0
                    val mem = srv["mem"]?.jsonObject; val disk = srv["disk"]?.jsonObject
                    val memPct = mem?.get("used_pct")?.jsonPrimitive?.intOrNull ?: 0
                    val diskPct = disk?.get("used_pct")?.jsonPrimitive?.intOrNull ?: 0
                    LeaderRow("cpu load", "$load%", null, if (load > 85) 2 else if (load > 60) 1 else 0); SegBar(load)
                    LeaderRow("ram", "${mem?.get("avail_mb")?.jsonPrimitive?.content ?: "?"} MB free"); SegBar(memPct)
                    LeaderRow("disk", "${disk?.get("free_gb")?.jsonPrimitive?.content ?: "?"} GB free"); SegBar(diskPct, 70, 90)
                    srv["cpu_temp_c"]?.jsonPrimitive?.floatOrNull?.let { LeaderRow("cpu temp", "${it.toInt()}°C", null, if (it > 75) 2 else if (it > 65) 1 else 0) }
                    srv["services"]?.jsonObject?.forEach { (k, v) ->
                        val a = v.jsonPrimitive.content == "active"
                        LeaderRow(k.removePrefix("espos-"), v.jsonPrimitive.content, if (a) "OK" else "DOWN", if (a) 0 else 2)
                    }
                }
            }
        }
        item {
            HudCard(L("pinout"), "ESP32 T-Display · ${PINOUT.size} GPIO") {
                PINOUT.forEach { p ->
                    Row(Modifier.fillMaxWidth().padding(vertical = 2.dp)) {
                        Text("G${p.gpio}".padEnd(5), color = Apex.Accent, fontFamily = FontFamily.Monospace,
                            fontSize = 12.sp, fontWeight = FontWeight.Medium)
                        Text(p.fn, color = Apex.Ink, fontFamily = FontFamily.Monospace, fontSize = 12.sp,
                            modifier = Modifier.width(130.dp))
                        if (p.note.isNotEmpty())
                            Text(p.note, color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 10.sp)
                    }
                }
            }
        }
        item {
            val esp = vm.espModules
            val detected = esp.count { it.value }
            HudCard(L("modules_hud"), "$detected/${esp.size.coerceAtLeast(1)} ${if (lang == Lang.UK) "виявлено" else "detected"}") {
                // ── ESP32 T-Display — інтерактивна ізо-плата (реальна будова 51.5×25.5мм) ──
                fun ml(u: String, e: String) = if (lang == Lang.UK) u else e   // двомовні описи модулів
                val espIso = listOf(
                    // пасивна начинка плати (не тапається) — для реалізму будови
                    IsoModule("usb", 0.05f, 0.50f, true, kind = "usb", du = 0.035f, dv = 0.16f, h = 0.05f, selectable = false),  // USB-C, лівий торець
                    IsoModule("hdrT", 0.66f, 0.06f, true, kind = "header", du = 0.28f, dv = 0.02f, h = 0.02f, selectable = false), // верхня гребінка
                    IsoModule("hdrB", 0.66f, 0.94f, true, kind = "header", du = 0.28f, dv = 0.02f, h = 0.02f, selectable = false), // нижня гребінка
                    IsoModule("BOOT", 0.30f, 0.93f, true, kind = "btn", du = 0.022f, dv = 0.03f, h = 0.045f, selectable = false),
                    IsoModule("RST", 0.42f, 0.93f, true, kind = "btn", du = 0.022f, dv = 0.03f, h = 0.045f, selectable = false),
                    IsoModule("ldo", 0.54f, 0.24f, true, kind = "reg", du = 0.03f, dv = 0.04f, h = 0.05f, selectable = false),
                    IsoModule("c1", 0.60f, 0.72f, true, kind = "cap", du = 0.022f, dv = 0.022f, h = 0.07f, selectable = false),
                    IsoModule("jst", 0.93f, 0.83f, true, kind = "conn", du = 0.04f, dv = 0.06f, h = 0.06f, selectable = false),
                    // інтерактивні модулі / детекти
                    IsoModule("ESP32", 0.80f, 0.46f, true, "WROOM-32 · 240MHz", ml("Головний контролер: WiFi/BT, меню, обробка.", "Main controller: WiFi/BT, menu, processing."), "mcu", du = 0.15f, dv = 0.33f, h = 0.10f),
                    IsoModule("TFT", 0.32f, 0.50f, true, "1.14\" ST7789 · 240×135", ml("Дисплей плати (лицьова сторона).", "Board display (front side)."), "disp", du = 0.26f, dv = 0.40f, h = 0.055f),
                    IsoModule("WiFi", 0.965f, 0.46f, esp["wifi_sta"] == true, ml("STA + AP · PCB-антена", "STA + AP · PCB antenna"), ml("Мережа й віддалене керування.", "Network and remote control."), "ant", du = 0.02f, dv = 0.18f, h = 0.03f),
                    IsoModule("nRF24", 0.20f, 0.80f, esp["nrf24"] == true, ml("2.4GHz трансивер", "2.4GHz transceiver"), ml("Сканер зайнятості ефіру (2.4G Analyzer).", "Air-occupancy scanner (2.4G Analyzer)."), "mod", du = 0.09f, dv = 0.10f, h = 0.08f),
                    IsoModule("CC1101", 0.20f, 0.20f, esp["cc1101"] == true, "sub-GHz 300–928MHz", ml("OOK/ASK аналіз і захоплення пультів.", "OOK/ASK analysis and remote capture."), "mod", du = 0.09f, dv = 0.10f, h = 0.08f),
                    IsoModule("SD", 0.06f, 0.82f, esp["sd"] == true, "MicroSD", ml("Логи, звіти, PCAP.", "Logs, reports, PCAP."), "conn", du = 0.04f, dv = 0.09f, h = 0.05f),
                )
                IsoDeviceView("ESP32 T-Display", "${vm.sysTemp.toInt()}°C · ${vm.powerTotal}mA · " + (if (lang == Lang.UK) "тапни модуль" else "tap module"), espIso, aspect = 2.06f)
                LeaderRow(if (lang == Lang.UK) "темп" else "temp", "${vm.sysTemp.toInt()}°C",
                    when (vm.coolState) { 2 -> "CRIT"; 1 -> "WARN"; else -> "OK" }, vm.coolState)
                LeaderRow(if (lang == Lang.UK) "навантаж" else "load", "${vm.powerTotal} mA · fan ${vm.coolDuty * 100 / 255}%")

                Spacer(Modifier.height(10.dp))
                Box(Modifier.fillMaxWidth().height(1.dp).background(Apex.Edge))
                Spacer(Modifier.height(10.dp))

                // ── Arduino UNO R3 — інтерактивна ізо-плата ──
                val potV = vm.unoSensors["pot"] ?: ""
                val unoIso = listOf(
                    // пасивна начинка (реальна будова 68.6×53.4мм: USB-B і живлення на лівому торці)
                    IsoModule("usb", 0.05f, 0.24f, true, kind = "usb", du = 0.06f, dv = 0.13f, h = 0.12f, selectable = false),   // USB-B
                    IsoModule("pwr", 0.05f, 0.72f, true, kind = "jack", du = 0.06f, dv = 0.06f, h = 0.11f, selectable = false),   // барильце живлення
                    IsoModule("16U2", 0.22f, 0.30f, true, kind = "chip", du = 0.05f, dv = 0.06f, h = 0.045f, selectable = false), // USB-чіп
                    IsoModule("xtal", 0.32f, 0.20f, true, kind = "xtal", du = 0.05f, dv = 0.025f, h = 0.05f, selectable = false),  // кварц
                    IsoModule("vreg", 0.26f, 0.80f, true, kind = "reg", du = 0.05f, dv = 0.035f, h = 0.06f, selectable = false),   // регулятор
                    IsoModule("c1", 0.18f, 0.55f, true, kind = "cap", du = 0.03f, dv = 0.03f, h = 0.12f, selectable = false),      // електроліт
                    IsoModule("c2", 0.40f, 0.74f, true, kind = "cap", du = 0.03f, dv = 0.03f, h = 0.12f, selectable = false),
                    IsoModule("hdrT", 0.65f, 0.06f, true, kind = "header", du = 0.30f, dv = 0.02f, h = 0.02f, selectable = false),  // цифрові піни
                    IsoModule("hdrB", 0.50f, 0.94f, true, kind = "header", du = 0.36f, dv = 0.02f, h = 0.02f, selectable = false), // аналог+живлення
                    IsoModule("icsp", 0.93f, 0.50f, true, kind = "header", du = 0.03f, dv = 0.05f, h = 0.03f, selectable = false), // ICSP 2×3
                    IsoModule("rst", 0.10f, 0.06f, true, kind = "btn", du = 0.025f, dv = 0.03f, h = 0.05f, selectable = false),
                    // інтерактивні: чіп + периферія/детекти
                    IsoModule("ATmega", 0.60f, 0.55f, vm.unoConnected, "328P · 16MHz · 2KB SRAM", ml("Копроцесор вводу/датчиків (DIP-28).", "Input/sensor coprocessor (DIP-28)."), "mcu", du = 0.06f, dv = 0.25f, h = 0.06f),
                    IsoModule("joy", 0.82f, 0.24f, vm.unoConnected, "KY-023 · A1/A2/D7", ml("Навігація меню (осі + натиск).", "Menu navigation (axes + press)."), "mod", du = 0.08f, dv = 0.10f, h = 0.08f),
                    IsoModule("RC522", 0.83f, 0.80f, vm.unoModules.any { it.first.contains("rc522", true) && it.third }, "13.56MHz RFID", ml("Читання/аудит Mifare.", "Mifare read/audit."), "mod", du = 0.09f, dv = 0.10f, h = 0.07f),
                    IsoModule("DHT", 0.50f, 0.82f, vm.unoSensors.containsKey("temp_c"), "DHT11 · A5", ml("Темп/вологість: ", "Temp/humidity: ") + "${vm.unoSensors["temp_c"] ?: "?"}°C.", "mod", du = 0.05f, dv = 0.07f, h = 0.08f),
                    IsoModule("pot", 0.50f, 0.22f, vm.unoSensors.containsKey("pot"), ml("потенціометр · A0", "potentiometer · A0"), ml("Аналоговий вхід: ", "Analog input: ") + "$potV / 1023.", "pot", du = 0.05f, dv = 0.05f, h = 0.07f),
                    IsoModule("reed", 0.72f, 0.92f, vm.unoSensors.containsKey("reed"), "reed · D2", ml("Магнітний контакт.", "Magnetic contact."), "conn", du = 0.03f, dv = 0.04f, h = 0.05f),
                )
                val unoSub = if (!vm.unoConnected) (if (lang == Lang.UK) "не на зв'язку" else "offline")
                    else if (vm.unoFreeRam >= 0) "RAM ${vm.unoFreeRam}B · ${upStr(vm.unoUptime)} · " + (if (lang == Lang.UK) "тап" else "tap")
                    else "LINK · " + (if (lang == Lang.UK) "тапни модуль" else "tap module")
                IsoDeviceView("Arduino UNO R3", unoSub, unoIso, aspect = 1.28f)
                if (vm.unoConnected && vm.unoFreeRam >= 0) {
                    LeaderRow(if (lang == Lang.UK) "вільна RAM" else "free RAM", "${vm.unoFreeRam} / 2048 B", null, if (vm.unoFreeRam < 300) 1 else 0)
                    SegBar((2048 - vm.unoFreeRam) * 100 / 2048)
                    LeaderRow("uptime", upStr(vm.unoUptime))
                }
            }
        }
        item {
            HudCard(L("glossary"), L("terms")) {
                (if (lang == Lang.UK) GLOSSARY_UK else GLOSSARY_EN).forEach { (term, def) ->
                    Column(Modifier.padding(vertical = 4.dp)) {
                        Text(term, color = Apex.Accent, fontFamily = FontFamily.Monospace, fontSize = 12.sp,
                            fontWeight = FontWeight.SemiBold)
                        Text(def, color = Apex.Ink2, fontFamily = FontFamily.Monospace, fontSize = 11.sp)
                    }
                }
            }
        }
        item {
            HudCard(L("sub_ghz_spectrum"), if (vm.subghzPresent) "300-928 MHz" else L("no_cc1101")) {
                if (vm.subghzDb.isEmpty()) Text(L("no_data"), color = Apex.Muted, fontFamily = FontFamily.Monospace)
                else SpectrumChart(vm.subghzDb)
                Row(Modifier.fillMaxWidth().padding(top = 4.dp), horizontalArrangement = Arrangement.SpaceBetween) {
                    Text("300", color = Apex.Muted, fontSize = 9.sp, fontFamily = FontFamily.Monospace)
                    Text("433", color = Apex.Muted, fontSize = 9.sp, fontFamily = FontFamily.Monospace)
                    Text("868", color = Apex.Accent, fontSize = 9.sp, fontFamily = FontFamily.Monospace)
                    Text("928", color = Apex.Muted, fontSize = 9.sp, fontFamily = FontFamily.Monospace)
                }
                if (vm.subghzHistory.size > 1) {
                    Spacer(Modifier.height(6.dp))
                    Text(if (lang == Lang.UK) "WATERFALL · час↓" else "WATERFALL · time↓", color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 9.sp, letterSpacing = 1.sp)
                    WaterfallChart(vm.subghzHistory, floor = -120, top = -40)
                }
                ToolHelpInline("subghz_analyzer")
            }
        }
        item {
            HudCard(L("spectrum_24"), if (vm.nrfPresent) "nRF24 · 126 ch" else L("no_nrf24")) {
                Row(Modifier.fillMaxWidth().padding(bottom = 4.dp), horizontalArrangement = Arrangement.End) {
                    FilterChip(vm.spectrumLive, { vm.toggleSpectrumLive() },
                        { Text(if (vm.spectrumLive) "● LIVE" else "LIVE", fontSize = 10.sp) })
                }
                if (vm.nrfCounts.isEmpty()) Text(L("no_data"), color = Apex.Muted, fontFamily = FontFamily.Monospace)
                else SpectrumChart(vm.nrfCounts.map { -120 + it * 90 / 6 })   // counts -> псевдо-dBm для того ж рендера
                if (vm.nrfHistory.size > 1) {
                    Spacer(Modifier.height(6.dp))
                    Text(if (lang == Lang.UK) "WATERFALL · час↓ · 2400→2525 MHz" else "WATERFALL · time↓ · 2400→2525 MHz", color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 9.sp, letterSpacing = 1.sp)
                    WaterfallChart(vm.nrfHistory, floor = 0, top = 6)
                }
                ToolHelpInline("rf24_analyzer")
            }
        }
        item {
            HudCard(L("battery"), L("mv_over_time")) {
                if (vm.battHist.size < 2) Text(L("collecting"), color = Apex.Muted, fontFamily = FontFamily.Monospace)
                else LineChart(vm.battHist, lo = 3300, hi = 4800)
                ToolHelpInline("battery")
            }
        }
        item {
            HudCard(L("sta_rssi"), L("dbm_over_time")) {
                if (vm.rssiHist.size < 2) Text(L("collecting"), color = Apex.Muted, fontFamily = FontFamily.Monospace)
                else LineChart(vm.rssiHist, lo = -95, hi = -30, color = Apex.Warn)
            }
        }
        item {
            HudCard(L("storage"), "SD") {
                LeaderRow(L("sd_card"), if (vm.sdMounted) L("mounted") else L("no_card"),
                    if (vm.sdMounted) L("ok") else L("fail"), if (vm.sdMounted) 0 else 2)
                if (vm.sdMsg.isNotEmpty())
                    Text(vm.sdMsg, color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 11.sp, modifier = Modifier.padding(top = 2.dp))
                Spacer(Modifier.height(8.dp))
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    if (vm.sdMounted) OutlinedButton({ vm.sdUnmount() }, Modifier.weight(1f)) { Text("UNMOUNT", fontSize = 10.sp) }
                    else OutlinedButton({ vm.sdMount() }, Modifier.weight(1f)) { Text("MOUNT", fontSize = 10.sp) }
                    OutlinedButton({ vm.sdSelftest() }, Modifier.weight(1f)) { Text("SELFTEST", fontSize = 10.sp) }
                }
            }
        }
        item {
            HudCard(if (lang == Lang.UK) "МЕРЕЖА · ПРИСТРОЇ" else "NETWORK · HOSTS",
                if (vm.netScanning) "scanning…" else "${vm.netHosts.size} hosts") {
                Row(Modifier.fillMaxWidth().padding(bottom = 6.dp), verticalAlignment = Alignment.CenterVertically) {
                    Button({ vm.netscan() }, Modifier.weight(1f), enabled = !vm.netScanning) {
                        Text(if (vm.netScanning) "…" else (if (lang == Lang.UK) "ARP-СКАН" else "ARP SCAN"), fontSize = 12.sp)
                    }
                    Spacer(Modifier.width(8.dp))
                    FilterChip(vm.netLive, { vm.toggleNetLive() }, { Text(if (vm.netLive) "● LIVE" else "LIVE", fontSize = 10.sp) })
                }
                Text(if (lang == Lang.UK) "тап → розвідка · [TGT] → зробити таргетом (автозаповнення)" else "tap → intel · [TGT] → set as target (autofill)",
                    color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 9.sp, modifier = Modifier.padding(bottom = 4.dp))
                vm.netHosts.forEach { h ->
                    Row(Modifier.fillMaxWidth().clickable { vm.scanDeviceIntel(h[1], h[0]) }.padding(vertical = 4.dp), verticalAlignment = Alignment.CenterVertically) {
                        Text(h[0], color = if (h[3] == "gw") Apex.Warn else Apex.Ink, fontFamily = FontFamily.Monospace, fontSize = 12.sp, modifier = Modifier.width(120.dp))
                        Column(Modifier.weight(1f)) {
                            Text(h[2].ifBlank { "?" }, color = Apex.Ink2, fontFamily = FontFamily.Monospace, fontSize = 11.sp)
                            Text(h[1], color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 9.sp)
                        }
                        if (h[3] == "gw") StatusTag("GW", 1)
                        if (vm.targetMac.equals(h[1], true) && h[1].isNotBlank()) { Spacer(Modifier.width(6.dp)); StatusTag("TARGET", 0) }
                        // миттєвий таргет БЕЗ сканування (автозаповнення ip/mac/vendor зі зібраних даних)
                        Text("[TGT]", color = Apex.Accent, fontFamily = FontFamily.Monospace, fontSize = 11.sp, modifier = Modifier.padding(start = 6.dp)
                            .clickable { vm.setTarget(h[0], h[1], h[2].ifBlank { "host" }) })
                        Text("›", color = Apex.Accent, fontFamily = FontFamily.Monospace, fontSize = 16.sp, modifier = Modifier.padding(start = 8.dp))
                    }
                }
                ToolHelpInline("net_scan")
            }
        }
        item {
            HudCard("AIR SCAN · WiFi",
                if (vm.airScanning) "scanning…" else "${vm.airAps.size} APs · ${vm.airTwins} twins") {
                Row(Modifier.fillMaxWidth().padding(bottom = 6.dp), verticalAlignment = Alignment.CenterVertically) {
                    Button({ vm.airscan() }, Modifier.weight(1f), enabled = !vm.airScanning) {
                        Text(if (vm.airScanning) "…" else (if (lang == Lang.UK) "СКАН ЕФІРУ" else "SCAN AIR"), fontSize = 12.sp)
                    }
                    Spacer(Modifier.width(8.dp))
                    FilterChip(vm.airLive, { vm.toggleAirLive() }, { Text(if (vm.airLive) "● LIVE" else "LIVE", fontSize = 10.sp) })
                    if (vm.airTwins > 0) { Spacer(Modifier.width(8.dp)); StatusTag("EVIL-TWIN ${vm.airTwins}", 2) }
                }
                if (vm.airTwins > 0) Text(if (lang == Lang.UK) "⚠ виявлено клон(и) SSID — можливий evil-twin" else "⚠ SSID clone(s) — possible evil-twin",
                    color = Apex.Bad, fontFamily = FontFamily.Monospace, fontSize = 10.sp, modifier = Modifier.padding(bottom = 2.dp))
                if (vm.airOpen > 0 || vm.airWeak > 0) Text("⚠ " + (if (lang == Lang.UK) "небезпечні: " else "risky: ") + "${vm.airOpen} OPEN · ${vm.airWeak} WEP",
                    color = Apex.Warn, fontFamily = FontFamily.Monospace, fontSize = 10.sp, modifier = Modifier.padding(bottom = 4.dp))
                vm.airAps.forEach { ap ->
                    val twin = ap[5] == "twin"; val risk = ap[6]
                    Row(Modifier.fillMaxWidth().padding(vertical = 4.dp), verticalAlignment = Alignment.CenterVertically) {
                        Column(Modifier.weight(1f)) {
                            Text(ap[0], color = if (twin) Apex.Bad else Apex.Ink, fontFamily = FontFamily.Monospace, fontSize = 12.sp)
                            Text("${ap[4]} · ch${ap[3]} · ${ap[1]}", color = if (risk == "open") Apex.Bad else if (risk == "weak") Apex.Warn else Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 9.sp)
                        }
                        Text("${ap[2]}dBm", color = Apex.Ink2, fontFamily = FontFamily.Monospace, fontSize = 11.sp)
                        if (twin) { Spacer(Modifier.width(6.dp)); StatusTag("TWIN", 2) }
                        else if (risk == "open") { Spacer(Modifier.width(6.dp)); StatusTag("OPEN", 2) }
                        else if (risk == "weak") { Spacer(Modifier.width(6.dp)); StatusTag("WEP", 1) }
                    }
                }
                ToolHelpInline("wifi_analyzer")
            }
        }
    }

    // ── Діалог глибокої розвідки пристрою ──
    val di = vm.deviceIntel
    if (di != null) {
        val err = di["error"]?.jsonPrimitive?.content
        AlertDialog(
            onDismissRequest = { vm.closeIntel() }, containerColor = Apex.Panel,
            title = { Text(vm.intelTitle, fontFamily = FontFamily.Monospace, fontSize = 13.sp, color = Apex.Accent) },
            confirmButton = {
                if (err == null && !vm.intelBusy) TextButton({
                    vm.setTarget(di["ip"]?.jsonPrimitive?.content ?: "", di["mac"]?.jsonPrimitive?.content ?: "",
                        di["category"]?.jsonPrimitive?.content ?: "")
                    vm.closeIntel()
                }) { Text(if (lang == Lang.UK) "ЗРОБИТИ ТАРГЕТОМ" else "SET TARGET", color = Apex.Accent) }
            },
            dismissButton = { TextButton({ vm.closeIntel() }) { Text(L("close")) } },
            text = {
                when {
                    vm.intelBusy -> Text(if (lang == Lang.UK) "розвідка… порти/hostname/UPnP/NetBIOS (до ~10с)" else "scanning…",
                        color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 12.sp)
                    err != null -> Text(err, color = Apex.Bad, fontFamily = FontFamily.Monospace, fontSize = 12.sp)
                    else -> Column(Modifier.heightIn(max = 380.dp).verticalScroll(rememberScrollState())) {
                        Text(di["category"]?.jsonPrimitive?.content ?: "?", color = Apex.Accent,
                            fontFamily = FontFamily.Monospace, fontSize = 13.sp, fontWeight = FontWeight.SemiBold)
                        Spacer(Modifier.height(6.dp))
                        val labels = di["port_labels"]?.jsonObject
                        val ports = di["ports"]?.jsonArray?.joinToString(", ") { p ->
                            val ps = p.jsonPrimitive.content; ps + (labels?.get(ps)?.jsonPrimitive?.content?.let { "/$it" } ?: "")
                        }
                        val ss = di["ssdp"]?.jsonObject
                        val ssv = listOfNotNull(ss?.get("friendlyName")?.jsonPrimitive?.content,
                            ss?.get("manufacturer")?.jsonPrimitive?.content, ss?.get("modelName")?.jsonPrimitive?.content).joinToString(" · ")
                        val http = di["http"]?.jsonObject
                        val web = listOfNotNull(http?.get("server")?.jsonPrimitive?.content, http?.get("title")?.jsonPrimitive?.content).joinToString(" · ")
                        val rows = listOf(
                            "IP" to di["ip"]?.jsonPrimitive?.content,
                            "MAC" to di["mac"]?.jsonPrimitive?.content,
                            (if (lang == Lang.UK) "виробник" else "vendor") to di["vendor"]?.jsonPrimitive?.content,
                            "hostname" to di["hostname"]?.jsonPrimitive?.content,
                            "NetBIOS" to di["netbios"]?.jsonPrimitive?.content,
                            (if (lang == Lang.UK) "порти" else "ports") to (ports?.ifBlank { null } ?: (if (lang == Lang.UK) "закриті / фаєрвол" else "closed / firewalled")),
                            "UPnP" to ssv.ifBlank { null },
                            (if (lang == Lang.UK) "веб" else "web") to web.ifBlank { null },
                        )
                        rows.forEach { (k, v) ->
                            if (!v.isNullOrBlank()) Row(Modifier.fillMaxWidth().padding(vertical = 2.dp)) {
                                Text(k, color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 11.sp, modifier = Modifier.width(96.dp))
                                Text(v, color = Apex.Ink, fontFamily = FontFamily.Monospace, fontSize = 11.sp, modifier = Modifier.weight(1f))
                            }
                        }
                        val reasons = di["reasons"]?.jsonArray?.map { it.jsonPrimitive.content } ?: emptyList()
                        if (reasons.isNotEmpty()) {
                            Spacer(Modifier.height(6.dp))
                            Text(if (lang == Lang.UK) "чому так:" else "why:", color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 10.sp)
                            reasons.forEach { Text("• $it", color = Apex.Ink2, fontFamily = FontFamily.Monospace, fontSize = 10.sp) }
                        }
                    }
                }
            },
        )
    }
}
