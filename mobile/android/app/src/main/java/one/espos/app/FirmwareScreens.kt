package one.espos.app

import android.content.Context
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlinx.serialization.json.*
import one.espos.app.ui.*
import one.espos.client.Updater

/* ===== ПРОШИВКА ПЛАТИ — 3 кнопки в розділі оновлень (HELP) ===== */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun FirmwareUpdateCard(fvm: FlashViewModel, serverUrl: String) {
    val uk = LocalLang.current == Lang.UK
    fun t(u: String, e: String) = if (uk) u else e
    var showLogs by remember { mutableStateOf(false) }
    LaunchedEffect(serverUrl) { if (serverUrl.isNotBlank()) fvm.refreshFirmware(serverUrl) }

    HudCard(t("ПРОШИВКА ПЛАТИ", "BOARD FIRMWARE"), if (fvm.fwVersion.isNotBlank()) "fw v${fvm.fwVersion}" else "USB-OTG") {
        if (fvm.fwSize > 0) Text("v${fvm.fwVersion} · ${fvm.fwSize / 1024} КБ" + (if (fvm.fwTests.isNotEmpty()) " · ${fvm.fwTests.size} " + t("тестів", "tests") else ""),
            color = Apex.Ink2, fontFamily = FontFamily.Monospace, fontSize = 10.sp)
        if (fvm.fwNotes.isNotBlank()) Text(fvm.fwNotes, color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 9.sp)
        Spacer(Modifier.height(8.dp))
        // 3 кнопки: скачати / залити / тести
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            OutlinedButton({ fvm.downloadOnly(serverUrl) }, Modifier.weight(1f), enabled = !fvm.busy,
                contentPadding = PaddingValues(horizontal = 4.dp, vertical = 2.dp)) {
                Text(t("СКАЧАТИ", "DOWNLOAD"), fontSize = 10.sp)
            }
            OutlinedButton({ if (!fvm.usbReady) fvm.scanUsb() else fvm.flash(serverUrl) }, Modifier.weight(1f), enabled = !fvm.busy,
                contentPadding = PaddingValues(horizontal = 4.dp, vertical = 2.dp)) {
                Text(if (!fvm.usbReady) t("USB-СКАН", "USB SCAN") else t("ЗАЛИТИ", "FLASH"), fontSize = 10.sp, color = Apex.Accent)
            }
            OutlinedButton({ fvm.runTests(serverUrl) }, Modifier.weight(1f), enabled = !fvm.busy,
                contentPadding = PaddingValues(horizontal = 4.dp, vertical = 2.dp)) {
                Text(t("ТЕСТИ", "TESTS"), fontSize = 10.sp)
            }
        }
        if (fvm.usbLabel.isNotEmpty()) Text(fvm.usbLabel, color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 9.sp, modifier = Modifier.padding(top = 3.dp))
        if (fvm.dlProgress in 0..100) { Spacer(Modifier.height(6.dp)); LinearProgressIndicator(progress = { fvm.dlProgress / 100f }, modifier = Modifier.fillMaxWidth(), color = Apex.Accent, trackColor = Apex.Edge) }
        if (fvm.flProgress in 0..100) { Spacer(Modifier.height(6.dp)); Text(t("флеш ${fvm.flProgress}%", "flash ${fvm.flProgress}%"), color = Apex.Accent, fontFamily = FontFamily.Monospace, fontSize = 9.sp); LinearProgressIndicator(progress = { fvm.flProgress / 100f }, modifier = Modifier.fillMaxWidth(), color = Apex.Accent, trackColor = Apex.Edge) }
        if (fvm.stage.isNotEmpty()) Text(fvm.stage, color = Apex.Ink2, fontFamily = FontFamily.Monospace, fontSize = 10.sp, modifier = Modifier.padding(top = 4.dp))
        if (fvm.error.isNotEmpty()) Text("✗ ${fvm.error}", color = Apex.Bad, fontFamily = FontFamily.Monospace, fontSize = 10.sp)

        // вікно логів (сесія прошивки/тестів)
        if (fvm.lines.isNotEmpty()) {
            Spacer(Modifier.height(6.dp))
            Column(Modifier.fillMaxWidth().heightIn(max = 160.dp).verticalScroll(rememberScrollState())
                .border(1.dp, Apex.Edge, RoundedCornerShape(6.dp)).padding(8.dp)) {
                fvm.lines.forEach { l -> Text(l, color = if (l.startsWith("✗")) Apex.Bad else Apex.Ink2, fontFamily = FontFamily.Monospace, fontSize = 9.5.sp, lineHeight = 12.sp) }
            }
        }
        Spacer(Modifier.height(6.dp))
        Text(t("ЛОГИ СЕСІЙ ▸ (телефон + сервер)", "SESSION LOGS ▸ (phone + server)"),
            color = Apex.Accent, fontFamily = FontFamily.Monospace, fontSize = 11.sp,
            modifier = Modifier.clickable { showLogs = true })
    }
    if (showLogs) LogsDialog(serverUrl) { showLogs = false }
}

/* ===== МЕНЮ ЛОГІВ: бази телефона + сервера, перегляд/чистка/retention ===== */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun LogsDialog(serverUrl: String, onClose: () -> Unit) {
    val uk = LocalLang.current == Lang.UK
    fun t(u: String, e: String) = if (uk) u else e
    val ctx = LocalContext.current
    val scope = rememberCoroutineScope()
    var tab by remember { mutableStateOf(0) }               // 0=телефон 1=сервер
    var phone by remember { mutableStateOf(LogStore.list(ctx)) }
    var server by remember { mutableStateOf<List<LogStore.Meta>>(emptyList()) }
    var viewing by remember { mutableStateOf<Pair<String, List<String>>?>(null) }
    fun reloadPhone() { phone = LogStore.list(ctx) }
    fun reloadServer() { scope.launch { server = withContext(Dispatchers.IO) { srvSessions(serverUrl) } } }
    LaunchedEffect(tab) { if (tab == 1) reloadServer() }

    AlertDialog(
        onDismissRequest = onClose, containerColor = Apex.Panel,
        title = { Text(t("ЛОГИ СЕСІЙ", "SESSION LOGS"), fontFamily = FontFamily.Monospace, fontSize = 14.sp, color = Apex.Accent) },
        confirmButton = { TextButton(onClose) { Text(L("close")) } },
        text = {
            Column(Modifier.fillMaxWidth().heightIn(max = 520.dp)) {
                Row(Modifier.fillMaxWidth().padding(bottom = 6.dp), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    FilterChip(tab == 0, { tab = 0 }, { Text(t("ТЕЛЕФОН", "PHONE"), fontSize = 10.sp) })
                    FilterChip(tab == 1, { tab = 1 }, { Text(t("СЕРВЕР", "SERVER"), fontSize = 10.sp) })
                }
                if (tab == 0) RetentionRow(ctx, uk, serverUrl, scope)
                HorizontalDivider(color = Apex.Edge, modifier = Modifier.padding(vertical = 4.dp))
                val items = if (tab == 0) phone else server
                Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                    Text(t("${items.size} сесій", "${items.size} sessions"), color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 10.sp, modifier = Modifier.weight(1f))
                    Text(t("ОЧИСТИТИ ВСЕ", "CLEAR ALL"), color = Apex.Bad, fontFamily = FontFamily.Monospace, fontSize = 10.sp,
                        modifier = Modifier.clickable {
                            if (tab == 0) { LogStore.clearAll(ctx); reloadPhone() }
                            else scope.launch { withContext(Dispatchers.IO) { Updater.logClearAll(serverUrl) }; reloadServer() }
                        })
                }
                Column(Modifier.fillMaxWidth().heightIn(max = 340.dp).verticalScroll(rememberScrollState())) {
                    if (items.isEmpty()) Text(t("порожньо", "empty"), color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 11.sp, modifier = Modifier.padding(vertical = 8.dp))
                    items.forEach { m ->
                        Row(Modifier.fillMaxWidth().padding(vertical = 6.dp), verticalAlignment = Alignment.CenterVertically) {
                            Column(Modifier.weight(1f).clickable {
                                scope.launch {
                                    val lines = withContext(Dispatchers.IO) {
                                        if (tab == 0) LogStore.get(ctx, m.id)?.lines ?: emptyList()
                                        else srvSessionLines(serverUrl, m.id)
                                    }
                                    viewing = m.id to lines
                                }
                            }) {
                                Row(verticalAlignment = Alignment.CenterVertically) {
                                    Text(m.title.ifBlank { m.id }, color = Apex.Ink, fontFamily = FontFamily.Monospace, fontSize = 12.sp)
                                    if (m.compressed) { Spacer(Modifier.width(5.dp)); Text("gz", color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 8.sp, modifier = Modifier.border(1.dp, Apex.Edge, RoundedCornerShape(3.dp)).padding(horizontal = 3.dp)) }
                                }
                                Text(fmtTs(m.ts, tab == 1) + " · ${m.lines} " + t("рядків", "lines"), color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 9.sp)
                            }
                            // 📌 закріпити (виняток з авточистки/стиснення) — тач-таргет ≥44dp
                            TapIcon(if (m.pinned) "📌" else "📍", tint = if (m.pinned) Apex.Accent else Apex.Muted, fontSize = 13.sp, onClick = {
                                if (tab == 0) { LogStore.setPinned(ctx, m.id, !m.pinned); reloadPhone() }
                                else scope.launch { withContext(Dispatchers.IO) { Updater.logSessionPin(serverUrl, m.id, !m.pinned) }; reloadServer() }
                            })
                            // ⬇ експорт у .txt (лише телефон)
                            if (tab == 0) TapIcon("⬇", tint = Apex.Accent, fontSize = 14.sp, onClick = { exportSession(ctx, m.id) })
                            TapIcon("✕", tint = Apex.Bad, fontSize = 14.sp, onClick = {
                                if (tab == 0) { LogStore.delete(ctx, m.id); reloadPhone() }
                                else scope.launch { withContext(Dispatchers.IO) { Updater.logSessionDelete(serverUrl, m.id) }; reloadServer() }
                            })
                        }
                        HorizontalDivider(color = Apex.Edge)
                    }
                }
            }
        },
    )
    viewing?.let { (id, lines) ->
        AlertDialog(
            onDismissRequest = { viewing = null }, containerColor = Apex.Panel,
            confirmButton = { TextButton({ viewing = null }) { Text(L("close")) } },
            title = { Text(id, fontFamily = FontFamily.Monospace, fontSize = 12.sp, color = Apex.Accent) },
            text = { Column(Modifier.fillMaxWidth().heightIn(max = 420.dp).verticalScroll(rememberScrollState())) {
                if (lines.isEmpty()) Text(t("порожньо", "empty"), color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 11.sp)
                lines.forEach { Text(it, color = Apex.Ink2, fontFamily = FontFamily.Monospace, fontSize = 10.sp, lineHeight = 13.sp) }
            } },
        )
    }
}

/* Retention-селектор: одиниця (день/тиждень/місяць/ніколи) × N(1-10). */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
internal fun RetentionRow(ctx: Context, uk: Boolean, serverUrl: String, scope: kotlinx.coroutines.CoroutineScope) {
    fun t(u: String, e: String) = if (uk) u else e
    var days by remember { mutableStateOf(LogStore.retentionDays(ctx)) }
    // визначаємо unit/count із days
    var unit by remember { mutableStateOf(when { days == 0 -> 3; days % 30 == 0 -> 2; days % 7 == 0 -> 1; else -> 0 }) }
    var count by remember { mutableStateOf(when (unit) { 2 -> (days / 30).coerceAtLeast(1); 1 -> (days / 7).coerceAtLeast(1); 0 -> days.coerceAtLeast(1); else -> 1 }) }
    fun apply() {
        days = when (unit) { 0 -> count; 1 -> count * 7; 2 -> count * 30; else -> 0 }
        LogStore.setRetentionDays(ctx, days)
        LogStore.autoCleanByPref(ctx)
        scope.launch { withContext(Dispatchers.IO) { Updater.logRetentionSet(serverUrl, days) } }  // дзеркалимо на сервер
    }
    Column {
        Text(t("АВТОЧИСТКА (телефон + сервер)", "AUTO-CLEAN (phone + server)"), color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 9.sp, letterSpacing = 1.sp)
        Row(Modifier.fillMaxWidth().padding(top = 4.dp), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
            val units = listOf(t("день", "day"), t("тижд", "week"), t("міс", "month"), t("ніколи", "never"))
            units.forEachIndexed { i, label ->
                FilterChip(unit == i, { unit = i; apply() }, { Text(label, fontSize = 9.sp) })
            }
        }
        if (unit != 3) Row(Modifier.padding(top = 4.dp), verticalAlignment = Alignment.CenterVertically) {
            Text("−", color = Apex.Accent, fontFamily = FontFamily.Monospace, fontSize = 20.sp, modifier = Modifier.clickable { if (count > 1) { count--; apply() } }.padding(horizontal = 10.dp))
            Text("$count", color = Apex.Ink, fontFamily = FontFamily.Monospace, fontSize = 15.sp, fontWeight = FontWeight.Bold)
            Text("+", color = Apex.Accent, fontFamily = FontFamily.Monospace, fontSize = 20.sp, modifier = Modifier.clickable { if (count < 10) { count++; apply() } }.padding(horizontal = 10.dp))
            Text(t("зберігати $count " + listOf("дн", "тиж", "міс")[unit], "keep $count " + listOf("d", "w", "mo")[unit]),
                color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 9.sp)
        } else Text(t("логи не видаляються автоматично", "logs never auto-deleted"), color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 9.sp, modifier = Modifier.padding(top = 4.dp))
    }
}

/* helpers для серверних сесій */
internal fun srvSessions(serverUrl: String): List<LogStore.Meta> = runCatching {
    Updater.logSessions(serverUrl)?.map {
        val o = it.jsonObject
        LogStore.Meta(o["id"]?.jsonPrimitive?.content ?: "", o["title"]?.jsonPrimitive?.content ?: "",
            o["ts"]?.jsonPrimitive?.longOrNull ?: 0, o["lines"]?.jsonPrimitive?.intOrNull ?: 0,
            o["bytes"]?.jsonPrimitive?.longOrNull ?: 0,
            o["pinned"]?.jsonPrimitive?.booleanOrNull ?: false,
            o["compressed"]?.jsonPrimitive?.booleanOrNull ?: false)
    } ?: emptyList()
}.getOrDefault(emptyList())

/** Експорт сесії телефона у .txt + share-інтент (зберегти/поділитися). */
internal fun exportSession(ctx: Context, id: String) {
    val f = LogStore.exportTxt(ctx, id) ?: return
    runCatching {
        val uri = androidx.core.content.FileProvider.getUriForFile(ctx, "${ctx.packageName}.fileprovider", f)
        val share = android.content.Intent(android.content.Intent.ACTION_SEND).apply {
            type = "text/plain"; putExtra(android.content.Intent.EXTRA_STREAM, uri)
            putExtra(android.content.Intent.EXTRA_SUBJECT, "ESP32-OS log $id")
            addFlags(android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION or android.content.Intent.FLAG_ACTIVITY_NEW_TASK)
        }
        ctx.startActivity(android.content.Intent.createChooser(share, "Export log").addFlags(android.content.Intent.FLAG_ACTIVITY_NEW_TASK))
    }
}
internal fun srvSessionLines(serverUrl: String, id: String): List<String> = runCatching {
    Updater.logSession(serverUrl, id)?.get("lines")?.jsonArray?.map { it.jsonPrimitive.content } ?: emptyList()
}.getOrDefault(emptyList())
internal fun fmtTs(ts: Long, serverSec: Boolean): String {
    val ms = if (serverSec) ts * 1000 else ts
    return java.text.SimpleDateFormat("yyyy-MM-dd HH:mm", java.util.Locale.US).format(java.util.Date(ms))
}
