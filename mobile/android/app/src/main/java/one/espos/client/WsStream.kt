package one.espos.client

/**
 * WsStream — стрім екрана плати через WebSocket :81 (аналог Flipper screenstreaming
 * feature). Нижча латентність за полінг /api/mirror: плата САМА пушить стан на зміну.
 *
 * Протокол (той самий JSON, що й REST-мірор):
 *   -> надсилаємо {"pin":".."} (auth), далі {"btn"|"idx"|"back"|"text"} (керування)
 *   <- отримуємо {"page":..,"items":[..]} / {"status":{..}} / {"log":[..]}
 *
 * Полінг лишається фолбеком (деякі мережі/пісочниці блокують :81) — див. web index.html.
 */
import kotlinx.serialization.json.*
import okhttp3.*
import okio.ByteString

class WsStream(
    private val host: String,
    private val pin: String,
    private val onScreen: (MirrorScreen) -> Unit,
    private val onStatus: (JsonObject) -> Unit = {},
    private val onState: (Boolean) -> Unit = {},   // true=connected
) {
    private val http = OkHttpClient()
    private val json = Json { ignoreUnknownKeys = true }
    private var ws: WebSocket? = null

    fun connect() {
        val req = Request.Builder().url("ws://$host:81/").build()
        ws = http.newWebSocket(req, object : WebSocketListener() {
            override fun onOpen(webSocket: WebSocket, response: Response) {
                onState(true)
                webSocket.send(buildJsonObject { put("pin", pin) }.toString())   // auth
            }
            override fun onMessage(webSocket: WebSocket, text: String) = handle(text)
            override fun onMessage(webSocket: WebSocket, bytes: ByteString) = handle(bytes.utf8())
            override fun onFailure(webSocket: WebSocket, t: Throwable, response: Response?) = onState(false)
            override fun onClosed(webSocket: WebSocket, code: Int, reason: String) = onState(false)
        })
    }

    private fun handle(text: String) {
        val o = runCatching { json.parseToJsonElement(text).jsonObject }.getOrNull() ?: return
        when {
            o.containsKey("status") -> o["status"]?.jsonObject?.let(onStatus)
            o.containsKey("items")  -> onScreen(
                MirrorScreen(
                    page = o["page"]?.jsonPrimitive?.content ?: "",
                    items = o["items"]?.jsonArray?.map { it.jsonPrimitive.content } ?: emptyList(),
                    status = null,
                )
            )
            // {"log":[..]} — за потреби прокинути в консоль
        }
    }

    fun send(btn: String? = null, idx: Int? = null, back: Boolean = false,
             text: String? = null, value: String? = null) {
        ws?.send(buildJsonObject {
            btn?.let { put("btn", it) }
            idx?.let { put("idx", it) }
            if (back) put("back", true)
            text?.let { put("text", it); put("value", value ?: "") }
        }.toString())
    }

    fun close() { ws?.close(1000, "bye"); ws = null; onState(false) }
}
