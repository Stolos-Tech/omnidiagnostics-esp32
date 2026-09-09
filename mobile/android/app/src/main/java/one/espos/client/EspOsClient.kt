package one.espos.client

/**
 * EspOsClient — клієнт board-API ESP32·OS (порт з mobile/espos_client.py, перевіреного наживо).
 * Структура за Flipper `bridge:connection`: Transport(OkHttp) -> Session(PIN) -> feature-методи.
 * Виклики блокуючі (OkHttp .execute) — кликати з Dispatchers.IO (див. MainViewModel).
 */
import kotlinx.serialization.Serializable
import kotlinx.serialization.json.*
import okhttp3.*
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.RequestBody.Companion.toRequestBody

@Serializable data class ArchiveItem(val name: String, val cat: String)
data class ScriptFile(val name: String, val cat: String)
@Serializable data class DeviceStatus(
    val sta: Boolean = false, val ap: Boolean = false,
    val rssi: Int = 0, val mv: Int = 0, val usb: Boolean = false,
)
data class MirrorScreen(val page: String, val items: List<String>, val status: JsonObject?)

/**
 * BoardTransport — абстракція каналу до плати. Дає підмінити HTTP (WiFi/gateway) на
 * USB-serial (REQ/RES по OTG-кабелю) БЕЗ зміни feature-методів EspOsClient. get/post —
 * JSON; getText — сирий текст; postOk — 2xx-подібний результат control-дій.
 */
interface BoardTransport {
    fun get(path: String): JsonElement
    fun post(path: String, body: JsonObject? = null): JsonElement
    fun getText(path: String): String
    fun postOk(path: String): Boolean
    /** POST із СИРИМ text/plain тілом (напр. збереження .be скрипта -> arg("plain")). */
    fun postText(path: String, text: String): Boolean
}

// token != null -> gateway-режим: base = шлюз (напр. 100.80.30.64:8443), кожен запит несе
// заголовок X-Gateway-Token; PIN не потрібен (шлюз сам логінить плату). Шляхи ті самі —
// gateway прозоро проксіює /<path> на плату. WS(:81) через шлюз не йде (лише REST-poll).
class HttpTransport(host: String, private val token: String? = null) : BoardTransport {
    private val base = if (host.startsWith("http")) host else "http://$host"
    // Connect ЩЕДРИЙ (10с) — Tailscale з мобільного встановлює тунель 5-8с; занадто жорсткий
    // connect ламав під'єднання. Read помірний (9с) — щоб детектити «плата зайнята аналізом»
    // (з'єднання є, але відповіді нема) і показувати банер замість мовчазного зависання.
    private val http = OkHttpClient.Builder()
        .connectTimeout(4, java.util.concurrent.TimeUnit.SECONDS)   // LAN конект миттєвий; мертвий кандидат падає швидко
        .readTimeout(9, java.util.concurrent.TimeUnit.SECONDS)
        .retryOnConnectionFailure(true)
        .build()
    private val json = Json { ignoreUnknownKeys = true }
    private val plain = "text/plain".toMediaType()   // POST-тіло МУСИТЬ бути text/plain (arg("plain"))

    private fun Request.Builder.auth(): Request.Builder =
        if (token != null) header("X-Gateway-Token", token) else this

    override fun get(path: String): JsonElement {
        val req = Request.Builder().url(base + path).get().auth().build()
        http.newCall(req).execute().use { r ->
            val body = r.body?.string().orEmpty()
            return runCatching { json.parseToJsonElement(body) }.getOrElse { JsonPrimitive(body) }
        }
    }
    override fun post(path: String, body: JsonObject?): JsonElement {
        val payload = (body?.toString() ?: "").toRequestBody(plain)
        val req = Request.Builder().url(base + path).post(payload).auth().build()
        http.newCall(req).execute().use { r ->
            val txt = r.body?.string().orEmpty()
            return runCatching { json.parseToJsonElement(txt) }.getOrElse { JsonPrimitive(txt) }
        }
    }
    // GET, що повертає сирий текст (ендпоінти text/plain: /reports/get, /fs/get, control-POST).
    override fun getText(path: String): String {
        val req = Request.Builder().url(base + path).get().auth().build()
        http.newCall(req).execute().use { r -> return r.body?.string().orEmpty() }
    }
    // POST без JSON-тіла (control-дії sd/sys/wifi/theme); повертає true на 2xx.
    override fun postOk(path: String): Boolean {
        val req = Request.Builder().url(base + path).post("".toRequestBody(plain)).auth().build()
        http.newCall(req).execute().use { r -> return r.isSuccessful }
    }
    override fun postText(path: String, text: String): Boolean {
        val req = Request.Builder().url(base + path).post(text.toRequestBody(plain)).auth().build()
        http.newCall(req).execute().use { r -> return r.isSuccessful }
    }
}

class EspOsClient(private val t: BoardTransport) {
    /** Сумісний конструктор HTTP/gateway (host[, token]) — решта коду не змінюється. */
    constructor(host: String, token: String? = null) : this(HttpTransport(host, token))

    private val json = Json { ignoreUnknownKeys = true }
    private fun get(path: String) = t.get(path)
    private fun post(path: String, body: JsonObject? = null) = t.post(path, body)
    private fun getText(path: String) = t.getText(path)
    private fun postOk(path: String) = t.postOk(path)
    // URL-кодування значення query (імена файлів/категорій можуть містити пробіли).
    private fun enc(s: String): String = java.net.URLEncoder.encode(s, "UTF-8")

    /** Перевірка шлюзу: /_gw/health повертає {"gateway":"ok",...}. 401 (невірний токен) -> не "ok". */
    fun health(): Boolean = runCatching {
        (get("/_gw/health") as? JsonObject)?.get("gateway")?.jsonPrimitive?.content == "ok"
    }.getOrElse { false }

    fun version(): JsonObject = get("/api/version").jsonObject
    fun login(pin: String): Boolean =
        post("/api/login", buildJsonObject { put("pin", pin) })
            .jsonObject["ok"]?.jsonPrimitive?.booleanOrNull ?: false

    // ── Challenge-response HMAC (динамічний логін без передачі seed) ──
    private fun hexToBytes(h: String) = ByteArray(h.length / 2) { h.substring(it * 2, it * 2 + 2).toInt(16).toByte() }
    private fun hmacSha256Hex(keyHex: String, msgHex: String): String {
        val mac = javax.crypto.Mac.getInstance("HmacSHA256")
        mac.init(javax.crypto.spec.SecretKeySpec(hexToBytes(keyHex), "HmacSHA256"))
        return mac.doFinal(hexToBytes(msgHex)).joinToString("") { "%02x".format(it.toInt() and 0xFF) }
    }
    /** Бутстреп: отримати seed (лише ПІСЛЯ успішного PIN-логіну). Найкраще по USB/довіреному каналу. */
    fun enroll(): String? = runCatching { (get("/api/enroll") as? JsonObject)?.get("seed")?.jsonPrimitive?.contentOrNull }.getOrNull()
    /** Динамічний логін: nonce з /api/challenge -> HMAC-SHA256(seed,nonce) -> /api/login. Seed не передається. */
    fun loginHmac(seedHex: String): Boolean = runCatching {
        val nonce = (get("/api/challenge") as? JsonObject)?.get("nonce")?.jsonPrimitive?.contentOrNull
        if (nonce.isNullOrBlank()) false else {
            val resp = hmacSha256Hex(seedHex, nonce)
            (post("/api/login", buildJsonObject { put("nonce", nonce); put("hmac", resp) }) as? JsonObject)
                ?.get("ok")?.jsonPrimitive?.booleanOrNull ?: false
        }
    }.getOrDefault(false)

    fun status(): DeviceStatus = json.decodeFromJsonElement(get("/api/status"))

    fun mirror(): MirrorScreen {
        val arr = get("/api/mirror") as? JsonArray ?: return MirrorScreen("", emptyList(), null)
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

    fun cmd(btn: String? = null, idx: Int? = null, back: Boolean = false,
            text: String? = null, value: String? = null): JsonObject =
        post("/api/cmd", buildJsonObject {
            btn?.let { put("btn", it) }
            idx?.let { put("idx", it) }
            if (back) put("back", true)
            text?.let { put("text", it); put("value", value ?: "") }
        }).jsonObject

    fun archive(): List<ArchiveItem> =
        get("/api/archive").jsonObject["items"]?.jsonArray
            ?.map { json.decodeFromJsonElement<ArchiveItem>(it) } ?: emptyList()

    fun report(name: String): String = getText("/reports/get?f=${enc(name)}")
    fun deleteReport(name: String): Boolean = getText("/reports/del?f=${enc(name)}").let { true }

    // Скрипти (APPS): список із категорією (потрібна для запуску/читання), запуск, вихідник.
    fun scriptList(): List<ScriptFile> =
        get("/fs/list").jsonObject["files"]?.jsonArray?.map {
            ScriptFile(it.jsonObject["n"]?.jsonPrimitive?.content ?: "",
                       it.jsonObject["c"]?.jsonPrimitive?.content ?: "misc")
        } ?: emptyList()
    /** Запускає збережений .be на платі; повертає {ok, out, error}. */
    fun runScript(name: String, cat: String): JsonObject =
        post("/script/run?name=${enc(name)}&cat=${enc(cat)}").jsonObject
    fun scriptSource(name: String, cat: String): String =
        getText("/fs/get?name=${enc(name)}&cat=${enc(cat)}")
    /** Зберегти .be на платі (LittleFS). Тіло = сирий текст скрипта (arg("plain")). */
    fun saveScript(name: String, cat: String, source: String): Boolean =
        t.postText("/fs/save?name=${enc(name)}&cat=${enc(cat)}", source)
    fun deleteScript(name: String, cat: String): Boolean =
        getText("/fs/del?name=${enc(name)}&cat=${enc(cat)}").let { true }

    /** Лог плати з курсором: {"log":[рядки],"total":N}. */
    fun log(since: Int): JsonObject = get("/api/log?since=$since").jsonObject

    // Керування SD.
    fun sdMount(): Boolean    = postOk("/sd/mount")
    fun sdUnmount(): Boolean  = postOk("/sd/unmount")
    fun sdSelftest(): Boolean = postOk("/sd/selftest")

    // Живлення пристрою.
    fun reboot(): Boolean = postOk("/sys/reboot")
    fun passive(): Boolean = postOk("/sys/passive")

    // Збережені WiFi-мережі (паролі плата не віддає).
    fun wifiSaved(): JsonObject = get("/wifi/saved").jsonObject
    fun wifiAuto(i: Int, on: Boolean): Boolean = postOk("/wifi/auto?i=$i&on=${if (on) 1 else 0}")
    fun wifiForget(i: Int): Boolean = postOk("/wifi/forget?i=$i")

    /** Config-фіча: профіль модулів (які на якій платі). GET/POST /config/profile. */
    fun getProfile(): JsonObject = runCatching { get("/config/profile").jsonObject }.getOrDefault(JsonObject(emptyMap()))
    fun setProfile(json: String): Boolean = t.postText("/config/profile", json)

    /** AP-фіча: стан точки доступу плати {ssid,enabled,up,channel,clients}. */
    fun getAp(): JsonObject = runCatching { get("/api/ap").jsonObject }.getOrDefault(JsonObject(emptyMap()))
    /** Змінити назву SoftAP (1..32 символів). Тіло = сирий текст ssid (arg("plain")). */
    fun setApSsid(ssid: String): Boolean = t.postText("/api/ap/config", ssid)
    /** Увімкнути/вимкнути SoftAP (вимк = STA-only, точка зникає). */
    fun setApEnabled(on: Boolean): Boolean = postOk("/api/ap/toggle?on=${if (on) 1 else 0}")

    /**
     * Хостинг WiFi+веб (low-RAM режим, НА СЕСІЮ — ребут вмикає назад). off -> /sys/passive
     * (гасить STA+SoftAP+WebServer+WebSocket, звільняє ~45 КБ heap), on -> /sys/resume.
     * Має сенс по USB: вимкнення по WiFi обірве власне з'єднання (назад — ребут/USB).
     * Повертає {ok,wifi,heap} — heap показуємо користувачу.
     */
    fun setHosting(on: Boolean): JsonObject =
        runCatching { post(if (on) "/sys/resume" else "/sys/passive").jsonObject }.getOrDefault(JsonObject(emptyMap()))

    /** Board-native глибокий аналіз пристрою (автономно, без сервера): скан портів +
     *  мітки сервісів + категорія + random-MAC. GET /api/devscan?ip= -> {ip,mac,ports,port_labels,category,...}. */
    fun devScan(ip: String): JsonObject = runCatching { get("/api/devscan?ip=${enc(ip)}").jsonObject }.getOrDefault(JsonObject(emptyMap()))

    /** Live-аналіз RF-модуля: {connected,rssi,tx_dbm,channel,temp,mv,heap,ssid}. Read-only. */
    fun rfStat(): JsonObject = runCatching { get("/api/rfstat").jsonObject }.getOrDefault(JsonObject(emptyMap()))

    /** Безпечний режим пінів: GET {safe:bool, blocked?:[..]}. safe=true → усі піни модулів high-Z. */
    fun hwSafeGet(): JsonObject = runCatching { get("/api/hwsafe").jsonObject }.getOrDefault(JsonObject(emptyMap()))
    /** Перемкнути (auth). Повертає {safe, blocked?}. Якщо просили active, а safe лишився true +
     *  blocked непорожній — активацію відкинув КЗ-детектор (перелічені піни закорочені на рейку). */
    fun hwSafeSet(on: Boolean): JsonObject = runCatching {
        post("/api/hwsafe", buildJsonObject { put("safe", on) }).jsonObject
    }.getOrDefault(JsonObject(emptyMap()))
    /** Софт-«продзвонка» пінів модулів (auth): {pins:[{gpio,name,state}], bridges:[{a,b}]}.
     *  state = FLOAT (нічого не тримає) / HIGH_pull / LOW_pull. bridges = реальні КЗ між пінами. */
    fun pinScan(): JsonObject = runCatching { get("/api/pinscan").jsonObject }.getOrDefault(JsonObject(emptyMap()))
    /** Активна продзвонка SPI (auth): {nrf24:{...,alive,verdict}}. alive=true → шина жива (readback 0x25). */
    fun spiTest(): JsonObject = runCatching { get("/api/spitest").jsonObject }.getOrDefault(JsonObject(emptyMap()))
    /** Повна діагностика Arduino UNO (auth): {link,probe,vitals,modules,sensors,rfid,verdict}. */
    fun unoDiag(): JsonObject = runCatching { get("/api/unodiag").jsonObject }.getOrDefault(JsonObject(emptyMap()))
    /** Апаратний self-test (auth): {checks:[{name,status,detail}], pass,warn,fail}. */
    fun selfTest(): JsonObject = runCatching { get("/api/selftest").jsonObject }.getOrDefault(JsonObject(emptyMap()))

    /** Рекомендація TX (dry-run, без зміни): {cur_dbm,rec_dbm,dir,rssi}. */
    fun calibrateRecommend(): JsonObject = runCatching { get("/api/calibrate").jsonObject }.getOrDefault(JsonObject(emptyMap()))
    /** БЕЗПЕЧНЕ калібрування TX (кламп у enum, валідація+відкат): dbm=null -> авто-рекомендація
     *  (економія струму/тепла); dbm=19.5 -> буст (макс дальність/стабільність). Клампиться на платі.
     *  Повертає {applied,reverted,old_dbm,new_dbm,rssi_before,rssi_after,connected}. */
    fun calibrateApply(dbm: Double? = null): JsonObject = runCatching {
        post("/api/calibrate/apply" + (if (dbm != null) "?dbm=$dbm" else "")).jsonObject
    }.getOrDefault(JsonObject(emptyMap()))

    fun sysinfo(): JsonObject = get("/api/sysinfo").jsonObject
    /** Активний ARP-скан підмережі: {hosts:[{ip,mac,vendor,gw}]}. Блокуюче ~1.5с. */
    fun netscan(): JsonObject = get("/api/netscan").jsonObject
    /** Автодетект модулів обох плат: {esp:{nrf24,cc1101,sd,...}, uno:{connected,modules[],sensors}}. */
    fun modules(): JsonObject = get("/api/modules").jsonObject
    /** Скан WiFi + evil-twin детект: {aps:[{ssid,bssid,rssi,ch,enc,twin}], twins}. Блокуюче ~2-4с. */
    fun airscan(): JsonObject = get("/api/airscan").jsonObject
    fun subghzSpectrum(): JsonObject = get("/api/subghz/spectrum").jsonObject
    fun nrfSpectrum(): JsonObject = get("/api/nrf/spectrum").jsonObject
    fun sdStatus(): JsonObject = get("/api/sd").jsonObject
    fun themes(): JsonObject = get("/theme").jsonObject
    fun setTheme(i: Int): JsonObject = post("/theme?i=$i").jsonObject
}
