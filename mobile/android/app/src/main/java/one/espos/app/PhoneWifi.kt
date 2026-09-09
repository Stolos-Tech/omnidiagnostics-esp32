package one.espos.app

/**
 * PhoneWifi — збір збережених WiFi телефона для експорту на сервер (task 7).
 * УВАГА (обмеження Android): з Android 10+ звичайний застосунок НЕ має доступу до
 * паролів збережених мереж. Тому:
 *   1) SSID відомих мереж — best-effort через WifiManager (може бути порожньо на 11+);
 *   2) паролі — або root-читання wpa_supplicant (якщо є su), або ручний ввід у UI.
 * Експортуємо все, що вдалось зібрати; PSK може бути порожнім.
 */
import android.content.Context
import android.net.wifi.WifiManager
import java.io.BufferedReader
import java.io.InputStreamReader

data class PhoneNet(val ssid: String, var psk: String = "", val enc: String = "")

object PhoneWifi {

    /** SSID відомих мереж (без паролів на сучасному Android). */
    fun knownSsids(ctx: Context): List<String> = runCatching {
        val wm = ctx.applicationContext.getSystemService(Context.WIFI_SERVICE) as WifiManager
        @Suppress("DEPRECATION")
        wm.configuredNetworks?.mapNotNull { it.SSID?.trim('"') }?.filter { it.isNotBlank() } ?: emptyList()
    }.getOrDefault(emptyList())

    /** Спроба root-читання wpa_supplicant (працює лише на rooted-пристроях). */
    fun rootReadWpa(): List<PhoneNet> = runCatching {
        val paths = listOf(
            "/data/misc/wifi/wpa_supplicant.conf",
            "/data/misc/apexdata/com.android.wifi/WifiConfigStore.xml"
        )
        val out = ArrayList<PhoneNet>()
        for (p in paths) {
            val proc = Runtime.getRuntime().exec(arrayOf("su", "-c", "cat $p"))
            val txt = BufferedReader(InputStreamReader(proc.inputStream)).readText()
            proc.waitFor()
            if (txt.isBlank()) continue
            // wpa_supplicant.conf: network={ ssid="X" psk="Y" }
            Regex("""network=\{(.*?)\}""", RegexOption.DOT_MATCHES_ALL).findAll(txt).forEach { m ->
                val blk = m.groupValues[1]
                val ssid = Regex("""ssid="([^"]*)"""").find(blk)?.groupValues?.get(1) ?: return@forEach
                val psk = Regex("""psk="?([^"\n]*)"?""").find(blk)?.groupValues?.get(1) ?: ""
                val enc = if (blk.contains("key_mgmt=NONE")) "OPEN" else "WPA2"
                out += PhoneNet(ssid, psk, enc)
            }
            // WifiConfigStore.xml: <string name="SSID">"X"</string> ... <string name="PreSharedKey">"Y"</string>
            Regex("""<WifiConfiguration>(.*?)</WifiConfiguration>""", RegexOption.DOT_MATCHES_ALL).findAll(txt).forEach { m ->
                val blk = m.groupValues[1]
                val ssid = Regex("""name="SSID">&quot;?([^<&]*)""").find(blk)?.groupValues?.get(1)?.trim('"') ?: return@forEach
                val psk = Regex("""name="PreSharedKey">&quot;?([^<&]*)""").find(blk)?.groupValues?.get(1)?.trim('"') ?: ""
                out += PhoneNet(ssid, psk, if (psk.isBlank()) "OPEN" else "WPA2")
            }
            if (out.isNotEmpty()) break
        }
        out.distinctBy { it.ssid }
    }.getOrDefault(emptyList())

    /** Об'єднана best-effort вибірка: root -> інакше самі SSID. */
    fun collect(ctx: Context): List<PhoneNet> {
        val root = rootReadWpa()
        if (root.isNotEmpty()) return root
        return knownSsids(ctx).map { PhoneNet(it, "", "") }
    }
}
