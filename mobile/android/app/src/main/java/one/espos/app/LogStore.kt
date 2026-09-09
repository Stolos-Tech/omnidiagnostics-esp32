package one.espos.app

import android.content.Context
import kotlinx.serialization.json.*
import java.io.File

/**
 * LogStore — база сесійних логів НА ТЕЛЕФОНІ (файлова, filesDir/logsessions/<id>.json).
 * Сесія = одна активність (прошивка/тести/під'єднання): id (дата-час), title, ts, lines[].
 * Авточистка за retention (день/тиждень/місяць × N | ніколи), щоб не засмічувати пам'ять.
 * Дзеркало на сервері — окремо (Updater.logs*), тут лише локальна база.
 */
object LogStore {
    data class Meta(val id: String, val title: String, val ts: Long, val lines: Int, val bytes: Long, val pinned: Boolean = false, val compressed: Boolean = false)
    data class Session(val id: String, val title: String, val ts: Long, val lines: List<String>, val pinned: Boolean = false)

    private fun dir(ctx: Context): File = File(ctx.filesDir, "logsessions").apply { mkdirs() }
    private fun file(ctx: Context, id: String) = File(dir(ctx), sanitize(id) + ".json")
    private fun sanitize(id: String) = id.filter { it.isLetterOrDigit() || it == '-' || it == '_' }.take(64).ifBlank { "session" }

    fun newId(): String {
        val f = java.text.SimpleDateFormat("yyyyMMdd-HHmmss", java.util.Locale.US)
        return f.format(java.util.Date())
    }

    /** Дописати рядки в сесію (створює, якщо нема). Зберігає прапорець pinned. */
    fun append(ctx: Context, id: String, title: String, newLines: List<String>) {
        if (newLines.isEmpty() && !file(ctx, id).exists()) return
        val existing = get(ctx, id)
        val merged = ((existing?.lines ?: emptyList()) + newLines).takeLast(3000)
        val obj = buildJsonObject {
            put("id", sanitize(id)); put("title", title.ifBlank { existing?.title ?: "" })
            put("ts", System.currentTimeMillis())
            put("pinned", existing?.pinned ?: false)
            put("lines", buildJsonArray { merged.forEach { add(it) } })
        }
        runCatching { file(ctx, id).writeText(obj.toString()) }
    }

    fun list(ctx: Context): List<Meta> = runCatching {
        dir(ctx).listFiles { f -> f.extension == "json" }?.mapNotNull { f ->
            runCatching {
                val o = Json.parseToJsonElement(f.readText()).jsonObject
                Meta(o["id"]?.jsonPrimitive?.content ?: f.nameWithoutExtension,
                    o["title"]?.jsonPrimitive?.content ?: "",
                    o["ts"]?.jsonPrimitive?.longOrNull ?: f.lastModified(),
                    o["lines"]?.jsonArray?.size ?: 0, f.length(),
                    o["pinned"]?.jsonPrimitive?.booleanOrNull ?: false)
            }.getOrNull()
        }?.sortedByDescending { it.ts } ?: emptyList()
    }.getOrDefault(emptyList())

    fun get(ctx: Context, id: String): Session? = runCatching {
        val f = file(ctx, id); if (!f.exists()) return null
        val o = Json.parseToJsonElement(f.readText()).jsonObject
        Session(o["id"]?.jsonPrimitive?.content ?: id, o["title"]?.jsonPrimitive?.content ?: "",
            o["ts"]?.jsonPrimitive?.longOrNull ?: 0,
            o["lines"]?.jsonArray?.map { it.jsonPrimitive.content } ?: emptyList(),
            o["pinned"]?.jsonPrimitive?.booleanOrNull ?: false)
    }.getOrNull()

    /** Закріпити/відкріпити сесію (закріплена — виняток з авточистки). */
    fun setPinned(ctx: Context, id: String, pinned: Boolean) {
        val s = get(ctx, id) ?: return
        val obj = buildJsonObject {
            put("id", s.id); put("title", s.title); put("ts", s.ts); put("pinned", pinned)
            put("lines", buildJsonArray { s.lines.forEach { add(it) } })
        }
        runCatching { file(ctx, id).writeText(obj.toString()) }
    }

    /** Експорт сесії у .txt (кеш) — повертає File для share-інтенту. */
    fun exportTxt(ctx: Context, id: String): File? {
        val s = get(ctx, id) ?: return null
        val out = File(ctx.cacheDir, "exports").apply { mkdirs() }.let { File(it, "${sanitize(id)}.txt") }
        val header = "ESP32-OS session ${s.id}\n${s.title}\n" +
            java.text.SimpleDateFormat("yyyy-MM-dd HH:mm:ss", java.util.Locale.US).format(java.util.Date(s.ts)) +
            "\n${"-".repeat(40)}\n"
        return runCatching { out.writeText(header + s.lines.joinToString("\n")); out }.getOrNull()
    }

    fun delete(ctx: Context, id: String) { runCatching { file(ctx, id).delete() } }
    /** Очистити все, КРІМ закріплених. */
    fun clearAll(ctx: Context) { runCatching { list(ctx).filter { !it.pinned }.forEach { file(ctx, it.id).delete() } } }

    /** Видалити НЕзакріплені сесії, старіші за retentionMillis (0 = ніколи). К-ть видалених. */
    fun autoClean(ctx: Context, retentionMillis: Long): Int {
        if (retentionMillis <= 0) return 0
        val cutoff = System.currentTimeMillis() - retentionMillis
        var n = 0
        list(ctx).forEach { m ->
            if (!m.pinned && m.ts < cutoff) { if (file(ctx, m.id).delete()) n++ }
        }
        return n
    }

    // ── Retention (авточистка): зберігаємо в днях (0 = ніколи). ──
    private fun prefs(ctx: Context) = ctx.getSharedPreferences("espos_logs", Context.MODE_PRIVATE)
    fun retentionDays(ctx: Context): Int = prefs(ctx).getInt("retention_days", 0)
    fun setRetentionDays(ctx: Context, days: Int) { prefs(ctx).edit().putInt("retention_days", days).apply() }
    /** Викликати на старті: чистить телефонну базу за збереженим retention. */
    fun autoCleanByPref(ctx: Context): Int = autoClean(ctx, retentionDays(ctx) * 86_400_000L)
}
