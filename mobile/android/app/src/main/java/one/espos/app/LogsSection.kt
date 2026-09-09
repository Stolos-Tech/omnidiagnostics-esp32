package one.espos.app

import android.content.Context
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
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

/**
 * LogsScreen — окремий розділ КЕРУВАННЯ ЛОГАМИ: три джерела (ТЕЛЕФОН | СЕРВЕР | ПЛАТА),
 * ретеншн-таймери, перегляд/pin/експорт/видалення, і трансфер між пристроями системи:
 *   phone→server, server→phone, board→phone (лог плати як сесія).
 * Джерело істини для phone = LogStore; server = api/logs; board = живий лог (vm.logLines).
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun LogsScreen(vm: MainViewModel, serverUrl: String) {
    val uk = LocalLang.current == Lang.UK
    fun t(u: String, e: String) = if (uk) u else e
    val ctx = LocalContext.current
    val scope = rememberCoroutineScope()
    var tab by remember { mutableStateOf(0) }               // 0=телефон 1=сервер 2=плата
    var phone by remember { mutableStateOf(LogStore.list(ctx)) }
    var server by remember { mutableStateOf<List<LogStore.Meta>>(emptyList()) }
    var viewing by remember { mutableStateOf<Pair<String, List<String>>?>(null) }
    var busy by remember { mutableStateOf("") }
    fun reloadPhone() { phone = LogStore.list(ctx) }
    fun reloadServer() { scope.launch { server = withContext(Dispatchers.IO) { srvSessions(serverUrl) } } }
    LaunchedEffect(tab) { if (tab == 1) reloadServer() }

    Column(Modifier.fillMaxSize()) {
        TabRow(selectedTabIndex = tab, containerColor = Apex.Panel2, contentColor = Apex.Accent) {
            Tab(tab == 0, { tab = 0 }) { Text(t("ТЕЛЕФОН ${phone.size}", "PHONE ${phone.size}"), fontFamily = FontFamily.Monospace, fontSize = 10.sp, modifier = Modifier.padding(9.dp)) }
            Tab(tab == 1, { tab = 1 }) { Text(t("СЕРВЕР ${server.size}", "SERVER ${server.size}"), fontFamily = FontFamily.Monospace, fontSize = 10.sp, modifier = Modifier.padding(9.dp)) }
            Tab(tab == 2, { tab = 2 }) { Text(t("ПЛАТА", "BOARD"), fontFamily = FontFamily.Monospace, fontSize = 10.sp, modifier = Modifier.padding(9.dp)) }
        }
        if (busy.isNotEmpty()) Text(busy, color = Apex.Accent, fontFamily = FontFamily.Monospace, fontSize = 10.sp, modifier = Modifier.padding(horizontal = 12.dp, vertical = 3.dp))

        when (tab) {
            2 -> BoardLogTab(vm, uk) { title, lines ->
                val id = LogStore.newId(); LogStore.append(ctx, id, title, lines); reloadPhone()
                busy = t("лог плати збережено у ТЕЛЕФОН ✓", "board log saved to PHONE ✓"); tab = 0
            }
            else -> {
                val items = if (tab == 0) phone else server
                LazyColumn(Modifier.fillMaxSize().padding(horizontal = 12.dp)) {
                    item {
                        if (tab == 0) RetentionRow(ctx, uk, serverUrl, scope)
                        Row(Modifier.fillMaxWidth().padding(vertical = 6.dp), verticalAlignment = Alignment.CenterVertically) {
                            Text(t("${items.size} сесій", "${items.size} sessions"), color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 10.sp, modifier = Modifier.weight(1f))
                            Text(t("ОЧИСТИТИ ВСЕ", "CLEAR ALL"), color = Apex.Bad, fontFamily = FontFamily.Monospace, fontSize = 10.sp, modifier = Modifier.clickable {
                                if (tab == 0) { LogStore.clearAll(ctx); reloadPhone() }
                                else scope.launch { withContext(Dispatchers.IO) { Updater.logClearAll(serverUrl) }; reloadServer() }
                            })
                        }
                        HorizontalDivider(color = Apex.Edge)
                    }
                    if (items.isEmpty()) item { Text(t("порожньо", "empty"), color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 11.sp, modifier = Modifier.padding(vertical = 10.dp)) }
                    items(items) { m ->
                        LogRow(m, phone = tab == 0, uk = uk,
                            onView = { scope.launch {
                                val lines = withContext(Dispatchers.IO) {
                                    if (tab == 0) LogStore.get(ctx, m.id)?.lines ?: emptyList() else srvSessionLines(serverUrl, m.id)
                                }; viewing = m.id to lines } },
                            onPin = {
                                if (tab == 0) { LogStore.setPinned(ctx, m.id, !m.pinned); reloadPhone() }
                                else scope.launch { withContext(Dispatchers.IO) { Updater.logSessionPin(serverUrl, m.id, !m.pinned) }; reloadServer() } },
                            onExport = { if (tab == 0) exportSession(ctx, m.id) },
                            onTransfer = {
                                scope.launch {
                                    busy = t("трансфер…", "transferring…")
                                    withContext(Dispatchers.IO) {
                                        if (tab == 0) {   // phone -> server
                                            val s = LogStore.get(ctx, m.id) ?: return@withContext
                                            val payload = buildJsonObject { put("id", s.id); put("title", s.title); put("lines", buildJsonArray { s.lines.forEach { add(it) } }) }
                                            Updater.logSessionIngest(serverUrl, payload.toString())
                                        } else {          // server -> phone
                                            val lines = srvSessionLines(serverUrl, m.id)
                                            LogStore.append(ctx, m.id, m.title.ifBlank { m.id }, lines)
                                        }
                                    }
                                    if (tab == 0) busy = t("→ СЕРВЕР ✓", "→ SERVER ✓") else { busy = t("→ ТЕЛЕФОН ✓", "→ PHONE ✓"); reloadPhone() }
                                } },
                            onDelete = {
                                if (tab == 0) { LogStore.delete(ctx, m.id); reloadPhone() }
                                else scope.launch { withContext(Dispatchers.IO) { Updater.logSessionDelete(serverUrl, m.id) }; reloadServer() } })
                        HorizontalDivider(color = Apex.Edge)
                    }
                }
            }
        }
    }
    viewing?.let { (id, lines) ->
        AlertDialog(
            onDismissRequest = { viewing = null }, containerColor = Apex.Panel,
            confirmButton = { TextButton({ viewing = null }) { Text(L("close")) } },
            title = { Text(id, fontFamily = FontFamily.Monospace, fontSize = 12.sp, color = Apex.Accent) },
            text = { Column(Modifier.fillMaxWidth().heightIn(max = 440.dp).verticalScroll(rememberScrollState())) {
                if (lines.isEmpty()) Text(t("порожньо", "empty"), color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 11.sp)
                lines.forEach { Text(it, color = Apex.Ink2, fontFamily = FontFamily.Monospace, fontSize = 10.sp, lineHeight = 13.sp) }
            } },
        )
    }
}

@Composable
private fun LogRow(m: LogStore.Meta, phone: Boolean, uk: Boolean,
                   onView: () -> Unit, onPin: () -> Unit, onExport: () -> Unit, onTransfer: () -> Unit, onDelete: () -> Unit) {
    fun t(u: String, e: String) = if (uk) u else e
    Row(Modifier.fillMaxWidth().padding(vertical = 4.dp), verticalAlignment = Alignment.CenterVertically) {
        Column(Modifier.weight(1f).clickable { onView() }) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(m.title.ifBlank { m.id }, color = Apex.Ink, fontFamily = FontFamily.Monospace, fontSize = 12.sp)
                if (m.compressed) { Spacer(Modifier.width(5.dp)); Text("gz", color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 8.sp, modifier = Modifier.border(1.dp, Apex.Edge, RoundedCornerShape(3.dp)).padding(horizontal = 3.dp)) }
            }
            Text(fmtTs(m.ts, !phone) + " · ${m.lines} " + t("рядків", "lines"), color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 9.sp)
        }
        TapIcon(if (m.pinned) "📌" else "📍", onClick = onPin, tint = if (m.pinned) Apex.Accent else Apex.Muted, fontSize = 13.sp)
        // трансфер: phone→server (⇧) або server→phone (⇩)
        TapIcon(if (phone) "⇧" else "⇩", onClick = onTransfer, tint = Apex.Accent, fontSize = 15.sp)
        if (phone) TapIcon("⬇", onClick = onExport, tint = Apex.Ink2, fontSize = 14.sp)
        TapIcon("✕", onClick = onDelete, tint = Apex.Bad, fontSize = 14.sp)
    }
}

/** Вкладка ПЛАТА: живий лог плати (vm.logLines) + «зберегти як сесію у телефон». */
@Composable
private fun BoardLogTab(vm: MainViewModel, uk: Boolean, onSave: (String, List<String>) -> Unit) {
    fun t(u: String, e: String) = if (uk) u else e
    Column(Modifier.fillMaxSize().padding(12.dp)) {
        Row(Modifier.fillMaxWidth().padding(bottom = 6.dp), verticalAlignment = Alignment.CenterVertically) {
            Text(t("живий лог плати · ${vm.logLines.size} рядків", "live board log · ${vm.logLines.size} lines"),
                color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 10.sp, modifier = Modifier.weight(1f))
            if (vm.logLines.isNotEmpty()) OutlinedButton({
                onSave(t("Лог плати ${java.text.SimpleDateFormat("MM-dd HH:mm", java.util.Locale.US).format(java.util.Date())}", "Board log ${java.text.SimpleDateFormat("MM-dd HH:mm", java.util.Locale.US).format(java.util.Date())}"), vm.logLines)
            }, contentPadding = PaddingValues(horizontal = 8.dp, vertical = 2.dp)) {
                Text(t("ЗБЕРЕГТИ → ТЕЛЕФОН", "SAVE → PHONE"), fontSize = 10.sp)
            }
        }
        if (vm.logLines.isEmpty()) Text(t("нема зв'язку з платою або лог порожній", "no board link or empty log"), color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 11.sp)
        Column(Modifier.fillMaxWidth().weight(1f).border(1.dp, Apex.Edge, RoundedCornerShape(6.dp)).padding(8.dp).verticalScroll(rememberScrollState())) {
            vm.logLines.takeLast(300).forEach { Text(it, color = logLineColor(it), fontFamily = FontFamily.Monospace, fontSize = 9.5.sp, lineHeight = 12.sp) }
        }
    }
}

private fun logLineColor(l: String) = when {
    l.contains("FAIL", true) || l.contains("error", true) || l.startsWith("✗") -> Apex.Bad
    l.contains("WARN", true) -> Apex.Warn
    else -> Apex.Ink2
}
