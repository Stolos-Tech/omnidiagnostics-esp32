package one.espos.client

/**
 * Discovery — знаходить плату в локальній мережі через mDNS (Android NsdManager).
 * Плата анонсує `esp32os` як `_http._tcp` на :80 (drivers/mdns_service). Замість ручного
 * вводу IP — авто-дискавері. Аналог "device discovery" у Flipper (там BLE-скан).
 *
 * Використання:
 *   val d = Discovery(context) { name, host, port -> ... }
 *   d.start(); ... d.stop()
 */
import android.content.Context
import android.net.nsd.NsdManager
import android.net.nsd.NsdServiceInfo

// resolveService / NsdServiceInfo.host deprecated з API 34, але заміна (registerServiceInfoCallback)
// потребує API 34+, а ми тримаємо minSdk 26 -> лишаємо старий шлях зі свідомим suppress.
@Suppress("DEPRECATION")
class Discovery(
    context: Context,
    private val onFound: (name: String, host: String, port: Int) -> Unit,
) {
    private val nsd = context.getSystemService(Context.NSD_SERVICE) as NsdManager
    private var listener: NsdManager.DiscoveryListener? = null

    fun start() {
        val l = object : NsdManager.DiscoveryListener {
            override fun onServiceFound(service: NsdServiceInfo) {
                // Резолвимо всі _http._tcp — фінальний фільтр (esp32os/netprobe) у UI.
                nsd.resolveService(service, resolver())
            }
            override fun onServiceLost(service: NsdServiceInfo) {}
            override fun onDiscoveryStarted(serviceType: String) {}
            override fun onDiscoveryStopped(serviceType: String) {}
            override fun onStartDiscoveryFailed(serviceType: String, errorCode: Int) {}
            override fun onStopDiscoveryFailed(serviceType: String, errorCode: Int) {}
        }
        listener = l
        nsd.discoverServices("_http._tcp.", NsdManager.PROTOCOL_DNS_SD, l)
    }

    private fun resolver() = object : NsdManager.ResolveListener {
        override fun onServiceResolved(service: NsdServiceInfo) {
            val host = service.host?.hostAddress ?: return
            onFound(service.serviceName, host, service.port)
        }
        override fun onResolveFailed(service: NsdServiceInfo, errorCode: Int) {}
    }

    fun stop() {
        listener?.let { runCatching { nsd.stopServiceDiscovery(it) } }
        listener = null
    }
}
