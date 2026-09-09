package one.espos.flash

/**
 * SerialLink — тонкий контракт послідовного порту для флешера. Відв'язує протокол
 * esptool (EspFlasher) від конкретної USB-serial реалізації (UsbSerial поверх
 * usb-serial-for-android). Так протокол можна тестувати й на «фейковому» лінку.
 *
 * read() повертає ДО maxLen байтів (може бути порожньо — це не помилка, просто
 * ще нема даних у межах timeoutMs). Флешер сам збирає SLIP-кадри з потоку.
 */
interface SerialLink {
    fun write(data: ByteArray)
    fun read(maxLen: Int, timeoutMs: Int): ByteArray
    fun setControlLines(dtr: Boolean, rts: Boolean)
    fun setBaud(baud: Int)
    fun purge()
    fun close()
}
