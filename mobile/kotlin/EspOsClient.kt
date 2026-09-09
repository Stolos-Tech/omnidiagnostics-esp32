package one.espos.client

/**
 * EspOsClient — Kotlin-порт board-API ESP32·OS (1:1 з mobile/espos_client.py, який
 * перевірений наживо). Структура за Flipper Android `bridge:connection`:
 *
 *   Transport (OkHttp) -> Session (PIN) -> feature suspend fun-и (типізовані ендпоінти)
 *
 * 4-таб модель Flipper (BottomBarTabEnum: DEVICE/ARCHIVE/APPS/TOOLS):
 *   DEVICE   -> version/status/mirror/cmd
 *   ARCHIVE  -> archive/report            (типізовано за категоріями, як FlipperKeyType)
 *   APPS     -> scripts                    (Berry .be = FAP-аналог)
 *   TOOLS    -> subghzSpectrum/nrfSpectrum/sd*
 *
 * Транспорт — HTTP:80 (REST). Для стріму екрана мобілка може поллити mirror() або відкрити
 * WS:81 (окремо). BLE-транспорт — за тим самим інтерфейсом Transport, коли/якщо знадобиться
 * (див. mobile_arch артефакт: transport-абстракція підтверджена сорсом Flipper).
 *
 * Порт свідомо тримає api/impl-стиль: інтерфейс EspOsApi (нижче) — це "api"-модуль,
 * EspOsClient — "impl". У реальному Android-проєкті рознести по Gradle-модулях.
 */

import kotlinx.serialization.Serializable
import kotlinx.serialization.json.*
import okhttp3.*
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.RequestBody.Companion.toRequestBody

@Serializable data class ArchiveItem(val name: String, val cat: String)
@Serializable data class DeviceStatus(
    val sta: Boolean = false, val ap: Boolean = false,
    val rssi: Int = 0, val mv: Int = 0, val usb: Boolean = false,
)
data class MirrorScreen(val page: String, val items: List<String>, val status: JsonObject?)

/** "api"-шар: контракт, від якого залежать фічі UI (не від impl). */
interface EspOsApi {
    suspend fun version(): JsonObject
    suspend fun login(pin: String): Boolean
    suspend fun status(): DeviceStatus
    suspend fun mirror(): MirrorScreen
    suspend fun cmd(btn: String? = null, idx: Int? = null, back: Boolean = false,
                    text: String? = null, value: String? = null): JsonObject
    suspend fun archive(): List<ArchiveItem>
    suspend fun report(name: String): String
    suspend fun subghzSpectrum(): JsonObject
    suspend fun themes(): JsonObject
    suspend fun setTheme(i: Int): JsonObject
}

/** "impl"-шар: транспорт + сесія + типізовані виклики. */
class EspOsClient(host: String) : EspOsApi {
    private val base = "http://$host"
    private val http = OkHttpClient()
    private val json = Json { ignoreUnknownKeys = true }
    private val plain = "text/plain".toMediaType()   // тіло POST МУСИТЬ бути text/plain (arg("plain"))

    private fun get(path: String): JsonElement {
        val req = Request.Builder().url(base + path).get().build()
        http.newCall(req).execute().use { r ->
            val body = r.body?.string().orEmpty()
            return runCatching { json.parseToJsonElement(body) }
                .getOrElse { JsonPrimitive(body) }
        }
    }

    private fun post(path: String, body: JsonObject? = null): JsonElement {
        val payload = (body?.toString() ?: "").toRequestBody(plain)
        val req = Request.Builder().url(base + path).post(payload).build()
        http.newCall(req).execute().use { r ->
            val txt = r.body?.string().orEmpty()
            return runCatching { json.parseToJsonElement(txt) }.getOrElse { JsonPrimitive(txt) }
        }
    }

    override suspend fun version() = get("/api/version").jsonObject

    override suspend fun login(pin: String): Boolean =
        post("/api/login", buildJsonObject { put("pin", pin) })
            .jsonObject["ok"]?.jsonPrimitive?.booleanOrNull ?: false

    override suspend fun status(): DeviceStatus =
        json.decodeFromJsonElement(get("/api/status"))

    override suspend fun mirror(): MirrorScreen {
        val arr = get("/api/mirror").jsonArray
        var page = ""; var items = emptyList<String>(); var status: JsonObject? = null
        for (el in arr) {
            val o = el.jsonObject
            if (o.containsKey("items")) {
                page = o["page"]?.jsonPrimitive?.content ?: ""
                items = o["items"]?.jsonArray?.map { it.jsonPrimitive.content } ?: emptyList()
            } else if (o.containsKey("status")) status = o["status"]?.jsonObject
        }
        return MirrorScreen(page, items, status)
    }

    override suspend fun cmd(btn: String?, idx: Int?, back: Boolean,
                            text: String?, value: String?): JsonObject =
        post("/api/cmd", buildJsonObject {
            btn?.let { put("btn", it) }
            idx?.let { put("idx", it) }
            if (back) put("back", true)
            text?.let { put("text", it); put("value", value ?: "") }
        }).jsonObject

    override suspend fun archive(): List<ArchiveItem> =
        get("/api/archive").jsonObject["items"]?.jsonArray
            ?.map { json.decodeFromJsonElement<ArchiveItem>(it) } ?: emptyList()

    override suspend fun report(name: String): String {
        val el = get("/reports/get?f=$name")
        return (el as? JsonObject)?.get("_text")?.jsonPrimitive?.content
            ?: (el as? JsonPrimitive)?.content ?: ""
    }

    override suspend fun subghzSpectrum() = get("/api/subghz/spectrum").jsonObject
    override suspend fun themes() = get("/theme").jsonObject
    override suspend fun setTheme(i: Int) = post("/theme?i=$i").jsonObject
}
