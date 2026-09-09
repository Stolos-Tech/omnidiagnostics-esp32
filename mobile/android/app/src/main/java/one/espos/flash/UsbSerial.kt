package one.espos.flash

import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbManager
import android.os.Build
import com.hoho.android.usbserial.driver.UsbSerialDriver
import com.hoho.android.usbserial.driver.UsbSerialPort
import com.hoho.android.usbserial.driver.UsbSerialProber

/**
 * UsbSerial — доступ до USB-serial плати з телефона через OTG. Тримає всю специфіку
 * Android USB Host: пошук драйвера (CP210x / CH34x / CH9102 / FTDI — усе, що вміє
 * usb-serial-for-android), рантайм-дозвіл на пристрій, відкриття порту. Віддає
 * SerialLink, який споживає EspFlasher.
 */
object UsbSerial {
    private const val ACTION_PERMISSION = "one.espos.flash.USB_PERMISSION"

    data class Dev(val driver: UsbSerialDriver, val label: String)

    fun list(ctx: Context): List<Dev> {
        val mgr = ctx.getSystemService(Context.USB_SERVICE) as UsbManager
        return UsbSerialProber.getDefaultProber().findAllDrivers(mgr).map {
            val d = it.device
            Dev(it, "${chipName(it)} · %04X:%04X".format(d.vendorId, d.productId))
        }
    }

    private fun chipName(d: UsbSerialDriver): String =
        d.javaClass.simpleName.removeSuffix("SerialDriver").ifBlank { "USB-Serial" }

    fun hasPermission(ctx: Context, device: UsbDevice): Boolean {
        val mgr = ctx.getSystemService(Context.USB_SERVICE) as UsbManager
        return mgr.hasPermission(device)
    }

    /** Запит дозволу на пристрій. cb(true/false) прилетить із системного діалогу. */
    fun requestPermission(ctx: Context, device: UsbDevice, cb: (Boolean) -> Unit) {
        val mgr = ctx.getSystemService(Context.USB_SERVICE) as UsbManager
        if (mgr.hasPermission(device)) { cb(true); return }
        val app = ctx.applicationContext
        val receiver = object : BroadcastReceiver() {
            override fun onReceive(c: Context, i: Intent) {
                if (i.action != ACTION_PERMISSION) return
                app.unregisterReceiver(this)
                val ok = i.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false)
                cb(ok)
            }
        }
        val flags = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S)
            PendingIntent.FLAG_MUTABLE else 0
        val pi = PendingIntent.getBroadcast(app, 0, Intent(ACTION_PERMISSION).setPackage(app.packageName), flags)
        val filter = IntentFilter(ACTION_PERMISSION)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU)
            app.registerReceiver(receiver, filter, Context.RECEIVER_NOT_EXPORTED)
        else @Suppress("UnspecifiedRegisterReceiverFlag") app.registerReceiver(receiver, filter)
        mgr.requestPermission(device, pi)
    }

    /** Відкрити порт першого драйвера. Кидає виняток, якщо нема дозволу / не відкрився. */
    fun open(ctx: Context, driver: UsbSerialDriver, baud: Int = EspFlasher.SYNC_BAUD): SerialLink {
        val mgr = ctx.getSystemService(Context.USB_SERVICE) as UsbManager
        val conn = mgr.openDevice(driver.device)
            ?: throw EspFlasher.FlashError("не вдалось відкрити USB-пристрій (нема дозволу?)")
        val port = driver.ports.first()
        port.open(conn)
        port.setParameters(baud, 8, UsbSerialPort.STOPBITS_1, UsbSerialPort.PARITY_NONE)
        return PortLink(port)
    }

    private class PortLink(private val port: UsbSerialPort) : SerialLink {
        private val readBuf = ByteArray(4096)
        override fun write(data: ByteArray) { port.write(data, 3000) }
        override fun read(maxLen: Int, timeoutMs: Int): ByteArray {
            val n = try { port.read(readBuf, timeoutMs) } catch (_: Exception) { 0 }
            if (n <= 0) return ByteArray(0)
            return readBuf.copyOf(minOf(n, maxLen))
        }
        override fun setControlLines(dtr: Boolean, rts: Boolean) {
            try { port.dtr = dtr } catch (_: Exception) {}
            try { port.rts = rts } catch (_: Exception) {}
        }
        override fun setBaud(baud: Int) {
            try { port.setParameters(baud, 8, UsbSerialPort.STOPBITS_1, UsbSerialPort.PARITY_NONE) } catch (_: Exception) {}
        }
        override fun purge() { try { port.purgeHwBuffers(true, true) } catch (_: Exception) {} }
        override fun close() { try { port.close() } catch (_: Exception) {} }
    }
}
