package one.espos.client

/**
 * UsbLink — керування платою по USB-OTG кабелю (БЕЗ WiFi). Говорить із серійним мостом
 * плати (serial_command): рядок "REQ <id> <METHOD> <path> <body>" -> "RES <id> <status> <json>".
 * Провідний канал = плата НЕ відпадає під час важкого аналізу (на відміну від WiFi).
 * Некадрові рядки (лог плати) стрімляться в onLog у реалтаймі.
 *
 * UsbTransport загортає це під BoardTransport -> EspOsClient працює як завжди.
 */
import kotlinx.serialization.json.*
import one.espos.flash.SerialLink
import one.espos.flash.UsbSerial
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger

class UsbLink(private val link: SerialLink, private val onLog: (String) -> Unit = {}) {
    @Volatile private var running = true
    private val seq = AtomicInteger(1)
    // БУФЕРИЗОВАНА черга (не SynchronousQueue!): RES може прилетіти ДО того, як потік
    // запиту дійде до poll() — з SynchronousQueue offer() без споживача губить відповідь
    // (інтермітентний таймаут). LinkedBlockingQueue приймає завжди.
    private val pending = ConcurrentHashMap<Int, LinkedBlockingQueue<Pair<Int, String>>>()
    private val reader = Thread { readLoop() }.apply { isDaemon = true; start() }

    private fun readLoop() {
        val sb = StringBuilder()
        while (running) {
            val chunk = link.read(1024, 60)
            if (chunk.isEmpty()) continue
            sb.append(String(chunk, Charsets.US_ASCII))
            var nl = sb.indexOf("\n")
            while (nl >= 0) {
                val line = sb.substring(0, nl).trimEnd('\r')
                sb.delete(0, nl + 1)
                if (line.isNotEmpty()) onLine(line)
                nl = sb.indexOf("\n")
            }
            if (sb.length > 16384) sb.setLength(0)   // захист від сміття
        }
    }

    private fun onLine(line: String) {
        if (line.startsWith("RES ")) {
            val p = line.split(" ", limit = 4)               // RES id status json
            val id = p.getOrNull(1)?.toIntOrNull() ?: return
            val status = p.getOrNull(2)?.toIntOrNull() ?: 0
            val body = p.getOrNull(3) ?: "{}"
            pending.remove(id)?.offer(status to body)
        } else {
            onLog(line)                                      // лог плати -> у консоль додатка
        }
    }

    private val writeLock = Any()

    /** Синхронний REQ/RES. Повертає (status, jsonText). Кидає при таймауті. */
    fun request(method: String, path: String, body: String = "", timeoutMs: Long = 8000): Pair<Int, String> {
        val id = seq.getAndIncrement()
        val q = LinkedBlockingQueue<Pair<Int, String>>()
        pending[id] = q
        try {
            val b = if (body.isBlank()) "" else " $body"
            val frame = "REQ $id $method $path$b\n".toByteArray(Charsets.US_ASCII)
            // Polling (1с) і команди йдуть різними корутинами -> запис МУСИТЬ бути
            // атомарним, інакше байти REQ перемішаються й ламають протокол.
            synchronized(writeLock) { link.write(frame) }
            return q.poll(timeoutMs, TimeUnit.MILLISECONDS)
                ?: throw java.io.IOException("USB timeout: $method $path")
        } finally { pending.remove(id) }
    }

    fun close() { running = false; try { reader.interrupt() } catch (_: Exception) {}; link.close() }

    companion object {
        /** Відкрити USB-лінк на платі (115200, БЕЗ reset — control-режим, не bootloader). */
        fun open(ctx: android.content.Context, driver: com.hoho.android.usbserial.driver.UsbSerialDriver,
                 onLog: (String) -> Unit): UsbLink {
            val link = UsbSerial.open(ctx, driver, 115200)
            link.setControlLines(dtr = false, rts = false)   // EN high, GPIO0 high -> плата працює, не скидається
            return UsbLink(link, onLog)
        }
    }
}

/** BoardTransport поверх UsbLink: get/post -> REQ/RES; JSON парситься тут. */
class UsbTransport(private val usb: UsbLink) : BoardTransport {
    private val json = Json { ignoreUnknownKeys = true }
    private fun parse(s: String): JsonElement = runCatching { json.parseToJsonElement(s) }.getOrElse { JsonPrimitive(s) }

    // Важкі скани (netscan робить ARP-скан підмережі ~15-30с; airscan/spectrum блокують
    // serial-цикл) — даємо ЩЕДРИЙ таймаут, інакше запит на залізі не встигає.
    private fun timeoutFor(path: String): Long =
        if (path.contains("netscan") || path.contains("airscan") || path.contains("spectrum") || path.contains("devscan") || path.contains("calibrate")) 30000 else 8000

    override fun get(path: String): JsonElement {
        val (_, body) = usb.request("GET", path, timeoutMs = timeoutFor(path))
        return parse(body)
    }
    override fun post(path: String, body: JsonObject?): JsonElement {
        val (_, resp) = usb.request("POST", path, body?.toString() ?: "")
        return parse(resp)
    }
    override fun getText(path: String): String = usb.request("GET", path).second
    override fun postOk(path: String): Boolean {
        val (status, _) = usb.request("POST", path)
        return status in 200..299
    }
    // УВАГА: serial-міст рядковий (\n = кінець кадру) -> багаторядковий текст (скрипти)
    // по USB НЕ передається коректно. Збереження скриптів роби по WiFi/gateway.
    override fun postText(path: String, text: String): Boolean {
        if (text.contains('\n')) return false           // не корумпуємо: багаторядкове не по USB
        val (status, _) = usb.request("POST", path, text)
        return status in 200..299
    }
}
