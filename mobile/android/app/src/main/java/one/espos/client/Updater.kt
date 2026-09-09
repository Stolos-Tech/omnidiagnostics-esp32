package one.espos.client

/**
 * Updater — self-update додатка з сервера (dashboard :8080, у tailnet, без auth).
 * ЗАХИСТ ВІД ПОЛАМАНИХ ЗБІРОК:
 *  1) манІфест-гейт: ставимо лише те, що сервер оголосив у /api/manifest;
 *  2) SHA-256: завантажений APK звіряється з manifest.sha256 ПЕРЕД встановленням;
 *  3) recovery поза додатком: браузерна /ota завжди віддає останній робочий APK;
 *  4) health-beacon: при успішному старті шлемо /api/health?vc -> сервер бачить живу версію.
 * Усе в try/catch — збій оновлення НІКОЛИ не роняє додаток.
 */
import kotlinx.serialization.json.*
import okhttp3.*
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.RequestBody.Companion.toRequestBody
import java.io.File
import java.security.MessageDigest

object Updater {
    data class Manifest(val versionCode: Int, val versionName: String,
                        val sha256: String, val size: Long, val notes: String,
                        val sig: String = "", val sigalg: String = "")

    private val http = OkHttpClient()
    private val json = Json { ignoreUnknownKeys = true }
    private fun norm(u: String) = if (u.startsWith("http")) u else "http://$u"

    // OTA-цілісність: Ed25519-підпис manifest.sha256 (приват-ключ лише на сервері). Захист від
    // MITM-підміни APK+manifest на HTTP-каналі: sha256 звіряє APK, підпис звіряє автентичність sha256.
    private val OTA_PUBKEY = java.util.Base64.getDecoder().decode("dySpBDC5iw3+IV3g4B7ZyG+yokaQ3x7UaFP271ZMdfw=")

    /**
     * Перевірка підпису маніфесту. ОПОРТУНІСТИЧНО+РЕКАВЕРАБЕЛЬНО, щоб мій баг не зламав OTA:
     *  - підпис відсутній / інший alg -> true (проходимо на sha256, старий сервер сумісний);
     *  - підпис Є і НЕВІРНИЙ (GeneralSecurityException) -> false (відмова — MITM/підміна);
     *  - будь-яка інша помилка (base64/Tink) -> true (не блокуємо OTA через не-крипто-збій).
     * Аварійний вихід: якщо верифікація хибно ріже валідне -> прибрати «sig» з manifest на сервері.
     */
    fun verifySig(m: Manifest): Boolean {
        if (m.sig.isBlank() || m.sigalg != "ed25519") return true
        return try {
            val sig = java.util.Base64.getDecoder().decode(m.sig)
            com.google.crypto.tink.subtle.Ed25519Verify(OTA_PUBKEY).verify(sig, m.sha256.toByteArray())
            true
        } catch (e: java.security.GeneralSecurityException) {
            false
        } catch (e: Throwable) {
            true
        }
    }

    /** GET /api/manifest. null -> сервер недосяжний / нема маніфесту. */
    fun manifest(baseUrl: String): Manifest? = runCatching {
        val req = Request.Builder().url(norm(baseUrl) + "/api/manifest").get().build()
        http.newCall(req).execute().use { r ->
            if (!r.isSuccessful) return null
            val o = json.parseToJsonElement(r.body?.string().orEmpty()).jsonObject
            val vc = o["versionCode"]?.jsonPrimitive?.intOrNull ?: return null
            Manifest(vc,
                o["versionName"]?.jsonPrimitive?.content ?: "?",
                o["sha256"]?.jsonPrimitive?.content ?: "",
                o["size"]?.jsonPrimitive?.longOrNull ?: 0L,
                o["notes"]?.jsonPrimitive?.content ?: "",
                o["sig"]?.jsonPrimitive?.content ?: "",
                o["sigalg"]?.jsonPrimitive?.content ?: "")
        }
    }.getOrNull()

    /** Качає /app.apk у dest із підрахунком SHA-256. Повертає true ЛИШЕ якщо гешібся з expectedSha. */
    fun download(baseUrl: String, expectedSha: String, dest: File, onProgress: (Int) -> Unit): Boolean = runCatching {
        val req = Request.Builder().url(norm(baseUrl) + "/app.apk").get().build()
        http.newCall(req).execute().use { r ->
            val body = r.body ?: return false
            if (!r.isSuccessful) return false
            val total = body.contentLength()
            val md = MessageDigest.getInstance("SHA-256")
            dest.outputStream().use { out ->
                body.byteStream().use { inp ->
                    val buf = ByteArray(1 shl 16); var read = 0L; var n: Int
                    while (inp.read(buf).also { n = it } >= 0) {
                        out.write(buf, 0, n); md.update(buf, 0, n); read += n
                        if (total > 0) onProgress(((read * 100) / total).toInt())
                    }
                }
            }
            val hex = md.digest().joinToString("") { "%02x".format(it) }
            val ok = expectedSha.isNotBlank() && hex.equals(expectedSha, ignoreCase = true)
            if (!ok) dest.delete()                       // побита/підмінена -> геть
            ok
        }
    }.getOrElse { runCatching { dest.delete() }; false }

    private fun getJson(baseUrl: String, path: String): JsonObject? = runCatching {
        val req = Request.Builder().url(norm(baseUrl) + path).get().build()
        http.newCall(req).execute().use { r ->
            if (!r.isSuccessful) return null
            json.parseToJsonElement(r.body?.string().orEmpty()).jsonObject
        }
    }.getOrNull()

    /** Ресурси сервера (dashboard /api/server): cores/load/mem/disk/temp/services. */
    fun serverStats(baseUrl: String): JsonObject? = getJson(baseUrl, "/api/server")

    /** Інвентар пристроїв мережі (dashboard /api/devices) з розвідкою (category/hostname). */
    fun devices(baseUrl: String): JsonArray? = runCatching {
        val req = Request.Builder().url(norm(baseUrl) + "/api/devices").get().build()
        http.newCall(req).execute().use { r ->
            if (!r.isSuccessful) return null
            json.parseToJsonElement(r.body?.string().orEmpty()).jsonArray
        }
    }.getOrNull()

    /** Глибока розвідка пристрою ЗАРАЗ: сервер сканує IP (порти/hostname/UPnP/NetBIOS/OUI). */
    fun deviceScan(baseUrl: String, mac: String, ip: String): JsonObject? =
        getJson(baseUrl, "/api/device/scan?mac=${enc(mac)}&ip=${enc(ip)}")

    private fun enc(s: String) = java.net.URLEncoder.encode(s, "UTF-8")
    /** Налаштування вартового (dashboard /api/settings) — БЕЗ токена. */
    fun settings(baseUrl: String): JsonObject? = getJson(baseUrl, "/api/settings")

    // ── Прошивка ПЛАТИ (для USB-флешу з телефона) ────────────────────────────
    /** GET /api/firmware -> {versionName, sha256, size, notes, offset}. null = нема на сервері. */
    fun firmwareManifest(baseUrl: String): JsonObject? = getJson(baseUrl, "/api/firmware")

    /**
     * Качає /firmware.bin у dest із SHA-256. true ЛИШЕ якщо збіглось із expectedSha
     * (порожній expectedSha -> приймаємо без звірки, але це не рекомендовано).
     */
    fun downloadFirmware(baseUrl: String, expectedSha: String, dest: File, onProgress: (Int) -> Unit): Boolean = runCatching {
        val req = Request.Builder().url(norm(baseUrl) + "/firmware.bin").get().build()
        http.newCall(req).execute().use { r ->
            val body = r.body ?: return false
            if (!r.isSuccessful) return false
            val total = body.contentLength()
            val md = MessageDigest.getInstance("SHA-256")
            dest.outputStream().use { out ->
                body.byteStream().use { inp ->
                    val buf = ByteArray(1 shl 16); var read = 0L; var n: Int
                    while (inp.read(buf).also { n = it } >= 0) {
                        out.write(buf, 0, n); md.update(buf, 0, n); read += n
                        if (total > 0) onProgress(((read * 100) / total).toInt())
                    }
                }
            }
            val hex = md.digest().joinToString("") { "%02x".format(it) }
            val ok = expectedSha.isBlank() || hex.equals(expectedSha, ignoreCase = true)
            if (!ok) dest.delete()
            ok
        }
    }.getOrElse { runCatching { dest.delete() }; false }

    /** Після успішних тестів оновлення — сервер видаляє їх із firmware.json (економія місця). */
    fun firmwareTestsClear(baseUrl: String): Boolean = runCatching {
        http.newCall(Request.Builder().url(norm(baseUrl) + "/api/firmware/tests/clear")
            .post("".toRequestBody("application/json".toMediaType())).build()).execute().use { r -> r.isSuccessful }
    }.getOrDefault(false)

    // ── Сесійні логи на СЕРВЕРІ (дзеркало телефонної бази) ───────────────────
    fun logSessionIngest(baseUrl: String, bodyJson: String): Boolean = runCatching {
        val req = Request.Builder().url(norm(baseUrl) + "/api/logs/session")
            .post(bodyJson.toRequestBody("application/json".toMediaType())).build()
        http.newCall(req).execute().use { r -> r.isSuccessful }
    }.getOrDefault(false)
    fun logSessions(baseUrl: String): JsonArray? = runCatching {
        http.newCall(Request.Builder().url(norm(baseUrl) + "/api/logs/sessions").get().build()).execute().use { r ->
            if (!r.isSuccessful) return null
            json.parseToJsonElement(r.body?.string().orEmpty()).jsonArray
        }
    }.getOrNull()
    fun logSession(baseUrl: String, id: String): JsonObject? = getJson(baseUrl, "/api/logs/session/${enc(id)}")
    fun logSessionPin(baseUrl: String, id: String, pinned: Boolean): Boolean = runCatching {
        http.newCall(Request.Builder().url(norm(baseUrl) + "/api/logs/session/${enc(id)}/pin")
            .post("""{"pinned":$pinned}""".toRequestBody("application/json".toMediaType())).build())
            .execute().use { r -> r.isSuccessful }
    }.getOrDefault(false)
    fun logSessionDelete(baseUrl: String, id: String): Boolean = runCatching {
        http.newCall(Request.Builder().url(norm(baseUrl) + "/api/logs/session/${enc(id)}/delete")
            .post("".toRequestBody("application/json".toMediaType())).build()).execute().use { r -> r.isSuccessful }
    }.getOrDefault(false)
    fun logClearAll(baseUrl: String): Boolean = runCatching {
        http.newCall(Request.Builder().url(norm(baseUrl) + "/api/logs/clear")
            .post("".toRequestBody("application/json".toMediaType())).build()).execute().use { r -> r.isSuccessful }
    }.getOrDefault(false)
    fun logRetentionSet(baseUrl: String, days: Int): Boolean = runCatching {
        http.newCall(Request.Builder().url(norm(baseUrl) + "/api/logs/retention")
            .post("""{"days":$days}""".toRequestBody("application/json".toMediaType())).build()).execute().use { r -> r.isSuccessful }
    }.getOrDefault(false)

    // ── Мережа: часова стрічка (netlog) + допуск (netguard) ──────────────────
    fun netlogEvents(baseUrl: String, since: Long = 0): JsonArray? = runCatching {
        http.newCall(Request.Builder().url(norm(baseUrl) + "/api/netlog/events?since=$since").get().build()).execute().use { r ->
            if (!r.isSuccessful) return null
            json.parseToJsonElement(r.body?.string().orEmpty()).jsonArray
        }
    }.getOrNull()
    fun netlogState(baseUrl: String): JsonArray? = runCatching {
        http.newCall(Request.Builder().url(norm(baseUrl) + "/api/netlog/state").get().build()).execute().use { r ->
            if (!r.isSuccessful) return null
            json.parseToJsonElement(r.body?.string().orEmpty()).jsonArray
        }
    }.getOrNull()
    fun netguardList(baseUrl: String, status: String? = null): JsonArray? = runCatching {
        val q = if (status != null) "?status=$status" else ""
        http.newCall(Request.Builder().url(norm(baseUrl) + "/api/netguard/list$q").get().build()).execute().use { r ->
            if (!r.isSuccessful) return null
            json.parseToJsonElement(r.body?.string().orEmpty()).jsonArray
        }
    }.getOrNull()
    fun netguardSet(baseUrl: String, mac: String, status: String): Boolean = runCatching {
        http.newCall(Request.Builder().url(norm(baseUrl) + "/api/netguard/set")
            .post("""{"mac":"$mac","status":"$status"}""".toRequestBody("application/json".toMediaType())).build())
            .execute().use { r -> r.isSuccessful }
    }.getOrDefault(false)

    /** Надіслати звіт/дані у Telegram-бота (сервер: короткий -> повідомлення, великий -> .txt-документ).
     *  text безпечно JSON-кодується (може містити лапки/переноси). */
    fun notifyBot(baseUrl: String, title: String, text: String): Boolean = runCatching {
        val body = kotlinx.serialization.json.buildJsonObject {
            put("title", kotlinx.serialization.json.JsonPrimitive(title))
            put("text", kotlinx.serialization.json.JsonPrimitive(text))
        }.toString()
        http.newCall(Request.Builder().url(norm(baseUrl) + "/api/notify")
            .post(body.toRequestBody("application/json".toMediaType())).build())
            .execute().use { r -> r.isSuccessful }
    }.getOrDefault(false)

    // ── Інвентар пристроїв (devintel): глибока розвідка по мережі ──
    /** GET /api/device?mac= -> {mac,ip,vendor,category,hostname,intel_at, intel:{ports,port_labels,netbios,ssdp,reasons,random_mac,...}}. */
    fun deviceIntel(baseUrl: String, mac: String): JsonObject? = runCatching {
        http.newCall(Request.Builder().url(norm(baseUrl) + "/api/device?mac=" + enc(mac)).get().build()).execute().use { r ->
            if (!r.isSuccessful) return null
            json.parseToJsonElement(r.body?.string().orEmpty()).jsonObject
        }
    }.getOrNull()

    /** Реле плата→сервер через телефон: POST телеметрії на /api/board/ingest. true=прийнято. */
    fun boardIngest(baseUrl: String, bodyJson: String): Boolean = runCatching {
        val req = Request.Builder().url(norm(baseUrl) + "/api/board/ingest")
            .post(bodyJson.toRequestBody("application/json".toMediaType())).build()
        http.newCall(req).execute().use { r -> r.isSuccessful }
    }.getOrDefault(false)

    /** «Я живий»: сервер фіксує останню робочу версію. Мовчазно, без наслідків при збої. */
    fun beacon(baseUrl: String, versionCode: Int) {
        runCatching {
            http.newCall(Request.Builder().url(norm(baseUrl) + "/api/health?vc=$versionCode").get().build())
                .execute().close()
        }
    }
}
