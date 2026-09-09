package one.espos.client

/**
 * BoardFlash — прошивка ота_0 по WiFi через SD (фаза 2). ПРЯМЕ з'єднання з платою
 * (той самий Wi-Fi): логін PIN-ом -> POST /fw/stage (multipart -> SD) -> POST /fw/apply
 * (ребут у factory-updater). Довгі таймаути (заливка ~2.2МБ + флеш).
 */
import kotlinx.serialization.json.*
import okhttp3.*
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.RequestBody.Companion.toRequestBody
import okio.BufferedSink
import java.io.File
import java.util.concurrent.TimeUnit

object BoardFlash {
    private val http = OkHttpClient.Builder()
        .connectTimeout(10, TimeUnit.SECONDS)
        .writeTimeout(5, TimeUnit.MINUTES)
        .readTimeout(5, TimeUnit.MINUTES)
        .build()
    private val json = Json { ignoreUnknownKeys = true }
    private val plain = "text/plain".toMediaType()
    private fun norm(h: String) = if (h.startsWith("http")) h else "http://$h"

    fun login(host: String, pin: String): Boolean = runCatching {
        val body = """{"pin":"$pin"}""".toRequestBody(plain)
        http.newCall(Request.Builder().url(norm(host) + "/api/login").post(body).build()).execute().use { r ->
            val o = runCatching { json.parseToJsonElement(r.body?.string().orEmpty()).jsonObject }.getOrNull()
            o?.get("ok")?.jsonPrimitive?.booleanOrNull ?: false
        }
    }.getOrDefault(false)

    /** Сирий GET до плати (для тест-раннера: перевірити, що ендпоінт відповідає). null=нема. */
    fun get(host: String, path: String): String? = runCatching {
        http.newCall(Request.Builder().url(norm(host) + path).get().build()).execute().use { r ->
            if (!r.isSuccessful) return null
            r.body?.string()
        }
    }.getOrNull()

    fun status(host: String): JsonObject? = runCatching {
        http.newCall(Request.Builder().url(norm(host) + "/fw/status").get().build()).execute().use { r ->
            if (!r.isSuccessful) return null
            json.parseToJsonElement(r.body?.string().orEmpty()).jsonObject
        }
    }.getOrNull()

    /** Залити образ на SD плати. onProgress 0..100. */
    fun stage(host: String, file: File, md5: String, onProgress: (Int) -> Unit): Boolean = runCatching {
        val total = file.length()
        val fileBody = object : RequestBody() {
            override fun contentType() = "application/octet-stream".toMediaType()
            override fun contentLength() = total
            override fun writeTo(sink: BufferedSink) {
                file.inputStream().use { inp ->
                    val buf = ByteArray(1 shl 15); var sent = 0L; var n: Int
                    while (inp.read(buf).also { n = it } >= 0) {
                        sink.write(buf, 0, n); sent += n
                        if (total > 0) onProgress(((sent * 100) / total).toInt())
                    }
                }
            }
        }
        val mp = MultipartBody.Builder().setType(MultipartBody.FORM)
            .addFormDataPart("md5", md5)
            .addFormDataPart("file", "firmware.bin", fileBody)
            .build()
        http.newCall(Request.Builder().url(norm(host) + "/fw/stage").post(mp).build()).execute().use { r ->
            val o = runCatching { json.parseToJsonElement(r.body?.string().orEmpty()).jsonObject }.getOrNull()
            r.isSuccessful && (o?.get("ok")?.jsonPrimitive?.booleanOrNull ?: false)
        }
    }.getOrDefault(false)

    /** Ребут у factory-updater (плата запише ота_0 з SD). Зв'язок обірветься. */
    fun apply(host: String): Boolean = runCatching {
        http.newCall(Request.Builder().url(norm(host) + "/fw/apply").post("".toRequestBody(plain)).build()).execute().use { r ->
            val o = runCatching { json.parseToJsonElement(r.body?.string().orEmpty()).jsonObject }.getOrNull()
            r.isSuccessful && (o?.get("ok")?.jsonPrimitive?.booleanOrNull ?: false)
        }
    }.getOrDefault(false)
}
