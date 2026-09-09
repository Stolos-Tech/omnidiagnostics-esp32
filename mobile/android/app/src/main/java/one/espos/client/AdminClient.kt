package one.espos.client

/**
 * AdminClient — застосунок -> сервер (tasks 3,5,6,7). Ходить на dashboard-базу
 * (:8080 у tailnet) з gateway-токеном. Керування сервером, бібліотека мереж/
 * пристроїв, маршрут до плати через сервер, експорт WiFi телефона.
 */
import kotlinx.serialization.json.*
import okhttp3.*
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.RequestBody.Companion.toRequestBody
import java.util.concurrent.TimeUnit

object AdminClient {
    private val http = OkHttpClient.Builder()
        .connectTimeout(8, TimeUnit.SECONDS).readTimeout(20, TimeUnit.SECONDS).build()
    private val json = Json { ignoreUnknownKeys = true }
    private val JSON = "application/json".toMediaType()
    private fun norm(u: String) = if (u.startsWith("http")) u else "http://$u"

    private fun call(base: String, token: String, path: String, post: Boolean = false, body: String? = null): JsonElement? =
        runCatching {
            val b = Request.Builder().url(norm(base) + path)
            if (token.isNotBlank()) b.header("X-Gateway-Token", token)
            if (post) b.post((body ?: "{}").toRequestBody(JSON)) else b.get()
            http.newCall(b.build()).execute().use { r ->
                val s = r.body?.string().orEmpty()
                if (s.isBlank()) return null
                json.parseToJsonElement(s)
            }
        }.getOrNull()

    // ---- server management (task 3) ----
    fun health(base: String, token: String): JsonObject? = call(base, token, "/admin/health")?.jsonObject
    fun services(base: String, token: String): JsonObject? = call(base, token, "/admin/server/services")?.jsonObject
    fun serviceAction(base: String, token: String, name: String, action: String): JsonObject? =
        call(base, token, "/admin/server/service?name=$name&action=$action", post = true)?.jsonObject
    fun serverReboot(base: String, token: String, mode: String = "reboot"): JsonObject? =
        call(base, token, "/admin/server/reboot?confirm=yes&mode=$mode", post = true)?.jsonObject
    fun getConfig(base: String, token: String): JsonObject? = call(base, token, "/admin/server/config")?.jsonObject
    fun setConfig(base: String, token: String, bodyJson: String): JsonObject? =
        call(base, token, "/admin/server/config", post = true, body = bodyJson)?.jsonObject

    // ---- boards / USB (tasks 1,6) ----
    fun boards(base: String, token: String): JsonArray? = call(base, token, "/admin/boards")?.jsonArray
    fun boardsDiscover(base: String, token: String): JsonObject? =
        call(base, token, "/admin/boards/discover", post = true)?.jsonObject
    fun boardReboot(base: String, token: String, id: Int): JsonObject? =
        call(base, token, "/admin/boards/$id/reboot", post = true)?.jsonObject

    /** Task 6: route ANY board request through the server (USB-bridged when attached). */
    fun boardRoute(base: String, token: String, id: Int, path: String, post: Boolean = false, body: String? = null): JsonElement? =
        call(base, token, "/board/$id" + (if (path.startsWith("/")) path else "/$path"), post, body)

    // ---- network/device Library (task 5) ----
    fun networks(base: String, token: String): JsonArray? = call(base, token, "/lib/networks")?.jsonArray
    fun network(base: String, token: String, id: Int): JsonObject? = call(base, token, "/lib/networks/$id")?.jsonObject
    fun devices(base: String, token: String): JsonArray? = call(base, token, "/lib/devices")?.jsonArray
    fun device(base: String, token: String, id: Int): JsonObject? = call(base, token, "/lib/devices/$id")?.jsonObject
    fun setTrust(base: String, token: String, id: Int, trust: String): JsonObject? =
        call(base, token, "/lib/devices/$id/trust?trust=$trust", post = true)?.jsonObject
    fun setTarget(base: String, token: String, kind: String, id: Int, label: String): JsonObject? =
        call(base, token, "/lib/target?kind=$kind&id=$id&label=${enc(label)}", post = true)?.jsonObject

    // ---- phone wifi export (task 7) ----
    fun importPhoneWifi(base: String, token: String, bodyJson: String): JsonObject? =
        call(base, token, "/admin/phone/wifi/import", post = true, body = bodyJson)?.jsonObject

    private fun enc(s: String) = java.net.URLEncoder.encode(s, "UTF-8")
}
