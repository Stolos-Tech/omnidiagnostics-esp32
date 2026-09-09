package one.espos.flash

import java.security.MessageDigest

/**
 * EspFlasher — реалізація протоколу ROM-завантажувача ESP32 (як в esptool) поверх
 * SerialLink. Пише прошивку НАПРЯМУ в app-партицію через USB — тому НЕ потрібні дві
 * OTA-партиції (проблема 4МБ обходиться повністю). Ланцюг:
 *   reset→bootloader (DTR/RTS)  ·  SYNC  ·  (verify chip)  ·  SPI_ATTACH  ·
 *   FLASH_BEGIN(erase)  ·  FLASH_DATA×N (uncompressed, ROM 1024-байт блоки)  ·
 *   SPI_FLASH_MD5 (звірка)  ·  FLASH_END  ·  hard-reset у застосунок.
 *
 * Свідомо БЕЗ stub-loader (не вимагає вшитого бінарного блоба на плату) — повільніше
 * на етапі erase, зате самодостатньо й передбачувано. Швидкість добираємо підняттям
 * бодрейту після SYNC.
 *
 * Джерела істини: espressif/esptool (BSD). Значення команд/магій — публічні константи.
 */
class EspFlasher(
    private val link: SerialLink,
    private val log: (String) -> Unit = {},
) {
    class FlashError(msg: String) : Exception(msg)

    companion object {
        // команди ROM-завантажувача
        private const val FLASH_BEGIN = 0x02
        private const val FLASH_DATA = 0x03
        private const val FLASH_END = 0x04
        private const val SYNC = 0x08
        private const val READ_REG = 0x0A
        private const val SPI_ATTACH = 0x0D
        private const val CHANGE_BAUDRATE = 0x0F
        private const val SPI_FLASH_MD5 = 0x13

        private const val CHECKSUM_SEED = 0xEF
        private const val ROM_BLOCK = 1024            // FLASH_WRITE_SIZE для ROM (без stub)
        private const val STATUS_BYTES = 2            // ROM ESP32 додає 2 байти статусу

        // ідентифікація чипа: READ_REG(CHIP_DETECT_MAGIC_REG) == магія ESP32
        private const val CHIP_MAGIC_REG = 0x40001000L
        private const val ESP32_MAGIC = 0x00F01D83L

        // стандартний зсув app-партиції ESP32 (bootloader/parttable лишаємо недоторканими)
        const val APP_OFFSET = 0x10000
        const val SYNC_BAUD = 115200
        const val FLASH_BAUD = 460800                 // підняти після SYNC для швидкого data-етапу
    }

    // ── публічний вхід ─────────────────────────────────────────────────────────
    /**
     * Прошити [image] у [offset]. [onProgress] — 0..100 (data-етап). Кидає FlashError
     * при будь-якому збої (виклик обгортає у try й показує користувачу).
     */
    fun flash(image: ByteArray, offset: Int = APP_OFFSET, highSpeed: Boolean = true,
              onProgress: (Int) -> Unit = {}) {
        if (image.isEmpty()) throw FlashError("порожній образ прошивки")
        link.setBaud(SYNC_BAUD)
        connect()
        verifyChip()
        spiAttach()
        if (highSpeed) changeBaud(FLASH_BAUD)
        flashImage(image, offset, onProgress)
        verifyMd5(image, offset)
        flashEnd()
        hardReset()
        log("✓ ГОТОВО · плата перезавантажується у нову прошивку")
    }

    // ── reset у завантажувач + SYNC ────────────────────────────────────────────
    private fun connect() {
        log("reset → bootloader…")
        var synced = false
        outer@ for (attempt in 1..5) {
            classicReset()
            drain(120)                                  // прогорнути boot-текст ROM
            // на один reset — кілька SYNC (як esptool), перш ніж перезаходити
            for (s in 1..7) if (trySync()) { synced = true; break@outer }
            log("SYNC спроба $attempt невдала, повтор reset…")
        }
        if (!synced) throw FlashError("плата не відповідає (SYNC). Перевір OTG-кабель і що плата у режимі завантаження.")
        drain(50)
        log("SYNC ✓ · ROM-завантажувач на зв'язку")
    }

    /** Класичний auto-reset (як esptool ClassicReset): EN через RTS, GPIO0 через DTR. */
    private fun classicReset() {
        link.setControlLines(dtr = false, rts = true)   // IO0=HIGH, EN=LOW (у скиданні)
        sleep(100)
        link.setControlLines(dtr = true, rts = false)   // IO0=LOW, EN=HIGH (вихід зі скидання у boot)
        sleep(50)
        link.setControlLines(dtr = false, rts = false)  // відпустити IO0
        link.purge()
    }

    private fun trySync(): Boolean {
        val data = ByteArray(36)
        data[0] = 0x07; data[1] = 0x07; data[2] = 0x12; data[3] = 0x20
        for (i in 4 until 36) data[i] = 0x55
        writePacket(SYNC, data, 0)
        // перша відповідь — власне SYNC; ROM шле ще кілька, зчитаємо/відкинемо
        val r = readPacket(150) ?: return false
        if (r.isEmpty() || (r[0].toInt() and 0xFF) != SYNC) {
            // інколи перший кадр — сміття; спробуємо ще один
            val r2 = readPacket(150) ?: return false
            if (r2.isEmpty() || (r2[0].toInt() and 0xFF) != SYNC) return false
        }
        repeat(7) { readPacket(60) }                    // злити «хвіст» SYNC-відповідей
        return true
    }

    // ── ідентифікація / SPI ────────────────────────────────────────────────────
    private fun verifyChip() {
        val magic = readReg(CHIP_MAGIC_REG)
        if (magic == ESP32_MAGIC) log("чип: ESP32 ✓ (magic 0x%08X)".format(magic))
        else log("⚠ невідомий чип (magic 0x%08X) — не ESP32? продовжую обережно".format(magic))
    }

    private fun readReg(addr: Long): Long {
        val d = leWord(addr.toInt())
        val (value, _) = command(READ_REG, d, 0, 500)
        return value and 0xFFFFFFFFL
    }

    private fun spiAttach() {
        // ROM ESP32: 8 байтів аргументу (4 hspi-flags=0 + 4 резерв=0)
        checkCommand("SPI_ATTACH", SPI_ATTACH, ByteArray(8), 0, 3000)
    }

    private fun changeBaud(baud: Int) {
        try {
            // ROM: <new_baud> <old_baud=0>
            checkCommand("CHANGE_BAUDRATE", CHANGE_BAUDRATE, leWord(baud) + leWord(0), 0, 3000)
            link.setBaud(baud)
            sleep(60); link.purge()
            log("бодрейт → $baud")
        } catch (e: Exception) {
            log("⚠ не вдалось підняти бодрейт, лишаю $SYNC_BAUD")
            link.setBaud(SYNC_BAUD)
        }
    }

    // ── запис образу ───────────────────────────────────────────────────────────
    private fun flashImage(image: ByteArray, offset: Int, onProgress: (Int) -> Unit) {
        val numBlocks = (image.size + ROM_BLOCK - 1) / ROM_BLOCK
        // erase виконується всередині FLASH_BEGIN — на ROM повільно (~30с/МБ)
        val eraseTimeout = 12000 + (image.size.toLong() * 30000 / (1024 * 1024)).toInt()
        log("erase ${fmtKb(image.size)} (≈ до ${eraseTimeout / 1000}с)…")
        val begin = leWord(image.size) + leWord(numBlocks) + leWord(ROM_BLOCK) + leWord(offset)
        checkCommand("FLASH_BEGIN", FLASH_BEGIN, begin, 0, eraseTimeout)

        log("запис ${fmtKb(image.size)} у 0x%X · $numBlocks блоків".format(offset))
        var seq = 0
        var pos = 0
        while (pos < image.size) {
            val len = minOf(ROM_BLOCK, image.size - pos)
            val block = ByteArray(ROM_BLOCK) { 0xFF.toByte() }   // добивка 0xFF (як esptool)
            System.arraycopy(image, pos, block, 0, len)
            val header = leWord(ROM_BLOCK) + leWord(seq) + leWord(0) + leWord(0)
            val chk = checksum(block)
            checkCommand("FLASH_DATA#$seq", FLASH_DATA, header + block, chk, 3000)
            seq++; pos += len
            onProgress((pos.toLong() * 100 / image.size).toInt())
        }
        log("data ✓ · записано $seq блоків")
    }

    private fun verifyMd5(image: ByteArray, offset: Int) {
        val local = MessageDigest.getInstance("MD5").digest(image)
            .joinToString("") { "%02x".format(it) }
        val d = leWord(offset) + leWord(image.size) + leWord(0) + leWord(0)
        val (_, body) = command(SPI_FLASH_MD5, d, 0, 8000)
        // ROM повертає 32 hex-символи (+ статус-байти вже відрізані у command())
        val remote = body.take(32).toByteArray().toString(Charsets.US_ASCII).lowercase()
        if (remote.length >= 32 && remote == local) log("MD5 ✓ $local")
        else throw FlashError("MD5 НЕ ЗБІГСЯ (плата=$remote / образ=$local) — прошивку не підтверджено")
    }

    private fun flashEnd() {
        // arg 0 = запустити код застосунку (не лишатись у завантажувачі)
        try { checkCommand("FLASH_END", FLASH_END, leWord(0), 0, 2000) } catch (_: Exception) {}
    }

    private fun hardReset() {
        link.setControlLines(dtr = false, rts = true)   // EN=LOW
        sleep(120)
        link.setControlLines(dtr = false, rts = false)  // EN=HIGH → старт
    }

    // ── SLIP-транспорт ─────────────────────────────────────────────────────────
    /** Відправити пакет-команду й повернути (value, body-без-статус-байтів). */
    private fun command(op: Int, data: ByteArray, chk: Int, timeoutMs: Int): Pair<Long, ByteArray> {
        writePacket(op, data, chk)
        val deadline = System.currentTimeMillis() + timeoutMs
        while (System.currentTimeMillis() < deadline) {
            val pkt = readPacket((deadline - System.currentTimeMillis()).toInt().coerceIn(50, timeoutMs))
                ?: continue
            if (pkt.size < 8) continue
            val dir = pkt[0].toInt() and 0xFF
            val cmd = pkt[1].toInt() and 0xFF
            if (dir != 0x01 || cmd != op) continue          // не наша відповідь — читаємо далі
            val size = (pkt[2].toInt() and 0xFF) or ((pkt[3].toInt() and 0xFF) shl 8)
            val value = leToLong(pkt, 4)
            val body = if (pkt.size >= 8 + size) pkt.copyOfRange(8, 8 + size) else pkt.copyOfRange(8, pkt.size)
            if (body.size >= STATUS_BYTES) {
                val status = body[body.size - STATUS_BYTES].toInt() and 0xFF
                val err = body[body.size - STATUS_BYTES + 1].toInt() and 0xFF
                if (status != 0) throw FlashError("команда 0x%02X: помилка $status/$err".format(op))
                return value to body.copyOfRange(0, body.size - STATUS_BYTES)
            }
            return value to body
        }
        throw FlashError("таймаут відповіді на 0x%02X".format(op))
    }

    private fun checkCommand(name: String, op: Int, data: ByteArray, chk: Int, timeoutMs: Int) {
        try { command(op, data, chk, timeoutMs) }
        catch (e: FlashError) { throw FlashError("$name: ${e.message}") }
    }

    private fun writePacket(op: Int, data: ByteArray, chk: Int) {
        val hdr = ByteArray(8)
        hdr[0] = 0x00; hdr[1] = op.toByte()
        hdr[2] = (data.size and 0xFF).toByte(); hdr[3] = ((data.size shr 8) and 0xFF).toByte()
        hdr[4] = (chk and 0xFF).toByte(); hdr[5] = ((chk shr 8) and 0xFF).toByte()
        hdr[6] = ((chk shr 16) and 0xFF).toByte(); hdr[7] = ((chk shr 24) and 0xFF).toByte()
        link.write(slipEncode(hdr + data))
    }

    // рухомий буфер для збірки SLIP-кадрів з байтового потоку
    private val rx = ArrayDeque<Byte>()
    private fun readPacket(timeoutMs: Int): ByteArray? {
        val deadline = System.currentTimeMillis() + timeoutMs.coerceAtLeast(1)
        while (true) {
            val frame = extractFrame()
            if (frame != null) return frame
            if (System.currentTimeMillis() >= deadline) return null
            val chunk = link.read(512, (deadline - System.currentTimeMillis()).toInt().coerceIn(1, timeoutMs))
            for (b in chunk) rx.addLast(b)
            if (chunk.isEmpty()) sleep(2)
        }
    }

    /** Витягти наступний повний SLIP-кадр (між 0xC0…0xC0), розкодований. */
    private fun extractFrame(): ByteArray? {
        // відкинути все до першого 0xC0
        while (rx.isNotEmpty() && (rx.first().toInt() and 0xFF) != 0xC0) rx.removeFirst()
        if (rx.isEmpty()) return null
        // знайти закриваючий 0xC0
        val it = rx.iterator(); it.next()                 // пропустити стартовий 0xC0
        var idx = 1; var end = -1
        while (it.hasNext()) { if ((it.next().toInt() and 0xFF) == 0xC0) { end = idx; break }; idx++ }
        if (end < 0) return null                           // кадр ще не повний
        val raw = ArrayList<Byte>(end)
        repeat(end + 1) { raw.add(rx.removeFirst()) }       // з'їсти кадр включно з обома 0xC0
        // розкодувати вміст (між першим і останнім 0xC0)
        val out = ArrayList<Byte>(raw.size)
        var i = 1
        while (i < raw.size - 1) {
            val b = raw[i].toInt() and 0xFF
            if (b == 0xDB && i + 1 < raw.size - 1) {
                val n = raw[i + 1].toInt() and 0xFF
                when (n) { 0xDC -> out.add(0xC0.toByte()); 0xDD -> out.add(0xDB.toByte()); else -> out.add(raw[i]) }
                i += 2
            } else { out.add(raw[i]); i++ }
        }
        return out.toByteArray()
    }

    private fun drain(ms: Int) {
        val end = System.currentTimeMillis() + ms
        while (System.currentTimeMillis() < end) { link.read(512, 20); }
        rx.clear()
    }

    // ── дрібні хелпери ─────────────────────────────────────────────────────────
    private fun slipEncode(data: ByteArray): ByteArray {
        val out = ArrayList<Byte>(data.size + 8)
        out.add(0xC0.toByte())
        for (b in data) when (b.toInt() and 0xFF) {
            0xC0 -> { out.add(0xDB.toByte()); out.add(0xDC.toByte()) }
            0xDB -> { out.add(0xDB.toByte()); out.add(0xDD.toByte()) }
            else -> out.add(b)
        }
        out.add(0xC0.toByte())
        return out.toByteArray()
    }

    private fun checksum(data: ByteArray): Int {
        var c = CHECKSUM_SEED
        for (b in data) c = c xor (b.toInt() and 0xFF)
        return c
    }

    private fun leWord(v: Int) = byteArrayOf(
        (v and 0xFF).toByte(), ((v shr 8) and 0xFF).toByte(),
        ((v shr 16) and 0xFF).toByte(), ((v shr 24) and 0xFF).toByte())

    private fun leToLong(a: ByteArray, off: Int): Long =
        (a[off].toLong() and 0xFF) or ((a[off + 1].toLong() and 0xFF) shl 8) or
        ((a[off + 2].toLong() and 0xFF) shl 16) or ((a[off + 3].toLong() and 0xFF) shl 24)

    private fun fmtKb(b: Int) = "%.1f КБ".format(b / 1024.0)
    private fun sleep(ms: Long) { try { Thread.sleep(ms) } catch (_: InterruptedException) {} }
    private fun sleep(ms: Int) = sleep(ms.toLong())
}
