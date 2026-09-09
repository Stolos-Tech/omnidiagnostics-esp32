package one.espos.app

/**
 * Server + Library screens (tasks 3,4,5). Строго відокремлене «серверне» середовище
 * від «плати». Двомовність через LocalLang (як решта додатка).
 */
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.background
import androidx.compose.foundation.border
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
import kotlinx.serialization.json.*
import one.espos.app.ui.*

private fun JsonObject.i(k: String) = this[k]?.jsonPrimitive?.intOrNull ?: 0
private fun JsonObject.s(k: String) = this[k]?.jsonPrimitive?.contentOrNull ?: ""
private fun JsonObject.f(k: String) = this[k]?.jsonPrimitive?.floatOrNull

/* ==================== SERVER (tasks 3,4,9) ==================== */
@Composable
fun ServerScreen(vm: AdminViewModel, fvm: FlashViewModel? = null, serverUrl: String = "") {
    val uk = LocalLang.current == Lang.UK
    fun t(u: String, e: String) = if (uk) u else e
    LaunchedEffect(Unit) { while (true) { vm.refreshServer(); kotlinx.coroutines.delay(6000) } }   // самополінг — незалежно від плати
    var askReboot by remember { mutableStateOf(false) }
    var wifiDlg by remember { mutableStateOf(false) }
    val ctx = LocalContext.current

    if (askReboot) AlertDialog(
        onDismissRequest = { askReboot = false }, containerColor = Apex.Panel,
        title = { Text(t("Перезавантажити СЕРВЕР?", "Reboot SERVER?"), fontFamily = FontFamily.Monospace, fontSize = 14.sp, color = Apex.Ink) },
        text = { Text(t("Усі сервіси зупиняться до перезавантаження.", "All services stop until it restarts."), color = Apex.Ink2, fontFamily = FontFamily.Monospace, fontSize = 12.sp) },
        confirmButton = { TextButton({ askReboot = false; vm.rebootServer("reboot") }) { Text("REBOOT", color = Apex.Bad) } },
        dismissButton = { TextButton({ askReboot = false }) { Text(t("Скасувати", "Cancel")) } },
    )

    if (wifiDlg) WifiExportDialog(vm, ctx, uk) { wifiDlg = false }

    LazyColumn(Modifier.padding(2.dp)) {
        item {
            HudCard(t("СЕРВЕР · ЗДОРОВ'Я", "SERVER · HEALTH"), if (vm.busy) "…" else "live") {
                val h = vm.health
                if (h == null) Text(t("нема зв'язку з сервером", "no server link"), color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 11.sp)
                else {
                    val temp = h.f("cpu_temp_c"); val uv = h.i("undervolt") == 1
                    LeaderRow("cpu temp", temp?.let { "${it.toInt()}°C" } ?: "—", null, if ((temp ?: 0f) > 80) 2 else if ((temp ?: 0f) > 65) 1 else 0)
                    LeaderRow("load", "${h.i("load_pct")}%", null, if (h.i("load_pct") > 85) 2 else 0); SegBar(h.i("load_pct"))
                    LeaderRow("ram", "${h.i("mem_used_pct")}%"); SegBar(h.i("mem_used_pct"))
                    LeaderRow("disk", "${h.i("disk_used_pct")}%"); SegBar(h.i("disk_used_pct"), 70, 90)
                    LeaderRow("undervolt", if (uv) t("⚠ ТАК", "⚠ YES") else t("ні", "no"), if (uv) "RISK" else "OK", if (uv) 2 else 0)
                    val sd = h["shutdowns"]?.jsonArray?.firstOrNull()?.jsonObject
                    if (sd != null) {
                        Spacer(Modifier.height(6.dp))
                        val clean = sd.i("clean") == 1
                        Text(t("останнє вимкнення: ", "last shutdown: ") + (if (clean) t("штатне", "clean") else t("РАПТОВЕ", "SUDDEN")) + " · " + sd.s("suspected"),
                            color = if (clean) Apex.Muted else Apex.Bad, fontFamily = FontFamily.Monospace, fontSize = 10.sp)
                        if (sd.s("evidence").isNotBlank()) Text(sd.s("evidence"), color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 9.sp)
                    }
                }
            }
        }
        item {
            HudCard(t("СЕРВІСИ", "SERVICES"), "${vm.services.size}") {
                if (vm.services.isEmpty()) Text(t("завантаження…", "loading…"), color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 11.sp)
                vm.services.forEach { (name, state) ->
                    Row(Modifier.fillMaxWidth().padding(vertical = 3.dp), verticalAlignment = Alignment.CenterVertically) {
                        val on = state == "active"
                        Box(Modifier.size(7.dp).background(if (on) Apex.Accent else Apex.Bad, RoundedCornerShape(2.dp)))
                        Spacer(Modifier.width(8.dp))
                        Text(name.removePrefix("espos-"), color = Apex.Ink, fontFamily = FontFamily.Monospace, fontSize = 12.sp, modifier = Modifier.weight(1f))
                        Text(state, color = if (on) Apex.Accent else Apex.Bad, fontFamily = FontFamily.Monospace, fontSize = 10.sp)
                        Spacer(Modifier.width(8.dp))
                        Text("↻", color = Apex.Accent, fontSize = 15.sp, modifier = Modifier.clickable { vm.svc(name, "restart") })
                        Spacer(Modifier.width(10.dp))
                        Text("■", color = Apex.Bad, fontSize = 13.sp, modifier = Modifier.clickable { vm.svc(name, "stop") })
                    }
                }
            }
        }
        if (vm.boards.isNotEmpty()) item {
            HudCard(t("ПЛАТИ (USB)", "BOARDS (USB)"), "${vm.boards.size}") {
                Row(Modifier.fillMaxWidth().padding(bottom = 6.dp)) {
                    OutlinedButton({ vm.discoverBoards() }, Modifier.weight(1f)) { Text(t("СКАН USB", "SCAN USB"), fontSize = 11.sp) }
                }
                vm.boards.forEach { b ->
                    Row(Modifier.fillMaxWidth().padding(vertical = 4.dp), verticalAlignment = Alignment.CenterVertically) {
                        Column(Modifier.weight(1f)) {
                            Text("#${b.i("id")} ${b.s("name")}", color = Apex.Ink, fontFamily = FontFamily.Monospace, fontSize = 12.sp)
                            Text("${b.s("transport")} · ${b.s("state")} ${b.s("serial_port")}", color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 9.sp)
                        }
                        StatusTag(b.s("state").uppercase().take(7), if (b.s("state") == "online") 0 else if (b.s("state") == "frozen") 2 else 1)
                        Spacer(Modifier.width(8.dp))
                        Text("♻", color = Apex.Warn, fontSize = 15.sp, modifier = Modifier.clickable { vm.rebootBoard(b.i("id")) })
                    }
                }
            }
        }
        item {
            HudCard(t("КЕРУВАННЯ СЕРВЕРОМ", "SERVER CONTROL"), "") {
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    OutlinedButton({ vm.refreshServer() }, Modifier.weight(1f)) { Text(t("ОНОВИТИ", "REFRESH"), fontSize = 11.sp) }
                    OutlinedButton({ askReboot = true }, Modifier.weight(1f)) { Text("REBOOT", fontSize = 11.sp, color = Apex.Bad) }
                    OutlinedButton({ vm.rebootServer("poweroff") }, Modifier.weight(1f)) { Text("OFF", fontSize = 11.sp, color = Apex.Bad) }
                }
                Spacer(Modifier.height(8.dp))
                OutlinedButton({ wifiDlg = true }, Modifier.fillMaxWidth()) {
                    Text(t("ЕКСПОРТ WiFi ТЕЛЕФОНА → СЕРВЕР", "EXPORT PHONE WiFi → SERVER"), fontSize = 11.sp)
                }
                if (vm.msg.isNotEmpty()) Text(vm.msg, color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 10.sp, modifier = Modifier.padding(top = 6.dp))
            }
        }
        if (fvm != null) item { FlashCard(fvm, serverUrl) }
    }
}

/* ---- WiFi export dialog: Android блокує авто-читання -> ручний ввід + best-effort SSID ---- */
@Composable
private fun WifiExportDialog(vm: AdminViewModel, ctx: android.content.Context, uk: Boolean, onClose: () -> Unit) {
    fun t(u: String, e: String) = if (uk) u else e
    var entries by remember { mutableStateOf(PhoneWifi.collect(ctx).map { Triple(it.ssid, it.psk, it.enc) }) }
    var ssid by remember { mutableStateOf("") }
    var pw by remember { mutableStateOf("") }
    AlertDialog(
        onDismissRequest = onClose, containerColor = Apex.Panel,
        title = { Text(t("Експорт WiFi на сервер", "Export WiFi to server"), fontFamily = FontFamily.Monospace, fontSize = 14.sp, color = Apex.Accent) },
        confirmButton = {
            TextButton(onClick = { if (entries.isNotEmpty()) { vm.exportPhoneWifi(entries); onClose() } }, enabled = entries.isNotEmpty()) {
                Text(t("ЕКСПОРТ (${entries.size})", "EXPORT (${entries.size})"), color = if (entries.isNotEmpty()) Apex.Accent else Apex.Muted)
            }
        },
        dismissButton = { TextButton(onClose) { Text(t("Закрити", "Close")) } },
        text = {
            Column(Modifier.heightIn(max = 420.dp).verticalScroll(rememberScrollState())) {
                Text(t("Android не дає читати збережені паролі без root. Додай мережі вручну (SSID+пароль), або експортуй ті, що вдалось знайти.",
                       "Android blocks reading saved passwords without root. Add networks manually (SSID+password), or export any auto-detected."),
                    color = Apex.Ink2, fontFamily = FontFamily.Monospace, fontSize = 11.sp)
                Spacer(Modifier.height(10.dp))
                OutlinedTextField(ssid, { ssid = it }, singleLine = true, label = { Text("SSID") }, modifier = Modifier.fillMaxWidth())
                Spacer(Modifier.height(6.dp))
                OutlinedTextField(pw, { pw = it }, singleLine = true, label = { Text(t("пароль", "password")) }, modifier = Modifier.fillMaxWidth())
                Spacer(Modifier.height(6.dp))
                OutlinedButton({
                    if (ssid.isNotBlank()) {
                        entries = entries + Triple(ssid.trim(), pw, if (pw.isBlank()) "OPEN" else "WPA2")
                        ssid = ""; pw = ""
                    }
                }, Modifier.fillMaxWidth()) { Text(t("+ ДОДАТИ", "+ ADD"), fontSize = 11.sp) }
                if (entries.isNotEmpty()) {
                    Spacer(Modifier.height(10.dp))
                    Text(t("У СПИСКУ (${entries.size}):", "IN LIST (${entries.size}):"), color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 10.sp)
                    entries.forEach { (s, p, _) ->
                        Text("• $s ${if (p.isBlank()) "(open/?)" else "•••"}", color = Apex.Ink2, fontFamily = FontFamily.Monospace, fontSize = 11.sp)
                    }
                }
            }
        },
    )
}

/* ==================== LIBRARY (task 5) ==================== */
@Composable
fun LibraryScreen(vm: AdminViewModel) {
    val uk = LocalLang.current == Lang.UK
    fun t(u: String, e: String) = if (uk) u else e
    LaunchedEffect(Unit) { vm.refreshLibrary() }
    var tab by remember { mutableStateOf(0) }

    vm.openProfile?.let { prof ->
        AlertDialog(
            onDismissRequest = { vm.closeProfile() }, containerColor = Apex.Panel,
            title = { Text(vm.profileTitle, fontFamily = FontFamily.Monospace, fontSize = 13.sp, color = Apex.Accent) },
            confirmButton = {
                val dev = prof["device"]?.jsonObject
                if (dev != null) TextButton({ vm.makeTarget("device", dev.i("id"), dev.s("category").ifBlank { dev.s("mac") }); vm.closeProfile() }) {
                    Text(t("ЗРОБИТИ ТАРГЕТОМ", "SET TARGET"), color = Apex.Accent)
                }
            },
            dismissButton = { TextButton({ vm.closeProfile() }) { Text(t("Закрити", "Close")) } },
            text = { ProfileBody(prof, uk, vm.deviceIntel, vm.intelBusy) { vm.scanDeviceNow() } },
        )
    }

    LaunchedEffect(tab) { if (tab == 2) vm.refreshNetGuard() }
    Column {
        TabRow(selectedTabIndex = tab, containerColor = Apex.Panel2, contentColor = Apex.Accent) {
            Tab(tab == 0, { tab = 0 }) { Text(t("ПРИСТРОЇ ${vm.devices.size}", "DEVICES ${vm.devices.size}"), fontFamily = FontFamily.Monospace, fontSize = 10.sp, modifier = Modifier.padding(8.dp)) }
            Tab(tab == 1, { tab = 1 }) { Text(t("МЕРЕЖІ ${vm.networks.size}", "NETWORKS ${vm.networks.size}"), fontFamily = FontFamily.Monospace, fontSize = 10.sp, modifier = Modifier.padding(8.dp)) }
            Tab(tab == 2, { tab = 2 }) { Text(t("ДОПУСК ${vm.admPending.size}", "ADMIT ${vm.admPending.size}"), fontFamily = FontFamily.Monospace, fontSize = 10.sp, color = if (vm.admPending.isNotEmpty()) Apex.Warn else Apex.Accent, modifier = Modifier.padding(8.dp)) }
        }
        if (tab == 2) NetGuardTab(vm, uk)
        else if (tab == 0) LazyColumn {
            if (vm.devices.isEmpty()) item { Text(t("порожньо — сервер ще не сканував", "empty — server hasn't scanned yet"), color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 11.sp, modifier = Modifier.padding(14.dp)) }
            items(vm.devices.size) { idx ->
                val d = vm.devices[idx]
                val age = System.currentTimeMillis() / 1000 - (d["last_seen"]?.jsonPrimitive?.longOrNull ?: 0L)
                val online = age in 0..899
                val seen = when { d["last_seen"] == null -> ""
                    online -> ""
                    age < 3600 -> t(" · ${age / 60}хв тому", " · ${age / 60}m ago")
                    age < 86400 -> t(" · ${age / 3600}год тому", " · ${age / 3600}h ago")
                    else -> t(" · ${age / 86400}дн тому", " · ${age / 86400}d ago") }
                Row(Modifier.fillMaxWidth().clickable { vm.openDevice(d.i("id"), d.s("name").ifBlank { d.s("category").ifBlank { d.s("mac") } }) }
                    .padding(horizontal = 12.dp, vertical = 8.dp), verticalAlignment = Alignment.CenterVertically) {
                    Text("●", color = if (online) Apex.Accent else Apex.Edge, fontSize = 10.sp, modifier = Modifier.width(16.dp))
                    Column(Modifier.weight(1f)) {
                        Text(d.s("name").ifBlank { d.s("category").ifBlank { d.s("vendor").ifBlank { "?" } } },
                            color = if (d.s("trust") == "blocked") Apex.Bad else if (online) Apex.Ink else Apex.Ink2, fontFamily = FontFamily.Monospace, fontSize = 13.sp)
                        Text("${d.s("ip")} · ${d.s("mac")}${if (d.s("hostname").isNotBlank()) " · " + d.s("hostname") else ""}$seen",
                            color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 9.sp)
                    }
                    if (d.i("is_gateway") == 1) StatusTag("GW", 1)
                    Text("›", color = Apex.Accent, fontFamily = FontFamily.Monospace, fontSize = 16.sp, modifier = Modifier.padding(start = 6.dp))
                }
            }
        } else LazyColumn {
            if (vm.networks.isEmpty()) item { Text(t("порожньо", "empty"), color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 11.sp, modifier = Modifier.padding(14.dp)) }
            items(vm.networks.size) { idx ->
                val nw = vm.networks[idx]
                Row(Modifier.fillMaxWidth().clickable { vm.openNetwork(nw.i("id"), nw.s("ssid").ifBlank { "(hidden)" }) }
                    .padding(horizontal = 12.dp, vertical = 8.dp), verticalAlignment = Alignment.CenterVertically) {
                    Column(Modifier.weight(1f)) {
                        Text(nw.s("ssid").ifBlank { "(hidden)" }, color = if (nw.i("is_twin") == 1) Apex.Bad else Apex.Ink, fontFamily = FontFamily.Monospace, fontSize = 13.sp)
                        Text("${nw.s("encryption")} · ch${nw.i("channel")} · ${nw.s("bssid")}", color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 9.sp)
                    }
                    if (nw.i("is_saved") == 1) StatusTag("SAVED", 0)
                    if (nw.i("is_twin") == 1) StatusTag("TWIN", 2)
                }
            }
        }
    }
}

/* NetGuard: активний допуск (approve/deny) + часова стрічка під'єднань. */
@Composable
private fun NetGuardTab(vm: AdminViewModel, uk: Boolean) {
    fun t(u: String, e: String) = if (uk) u else e
    fun fmt(ts: Int) = java.text.SimpleDateFormat("MM-dd HH:mm", java.util.Locale.US).format(java.util.Date(ts.toLong() * 1000))
    LazyColumn(Modifier.padding(2.dp)) {
        item {
            HudCard(t("АКТИВНИЙ ДОПУСК", "ADMISSION CONTROL"), "${vm.admPending.size} " + t("очікують", "pending")) {
                Text(t("Пароль WiFi недостатньо: кожен НОВИЙ пристрій чекає твого підтвердження.",
                       "WiFi password alone isn't enough: every NEW device waits for your approval."),
                    color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 9.sp)
                Spacer(Modifier.height(6.dp))
                OutlinedButton({ vm.refreshNetGuard() }, Modifier.fillMaxWidth()) { Text(t("ОНОВИТИ", "REFRESH"), fontSize = 11.sp) }
                if (vm.admPending.isEmpty()) Text(t("· немає нових запитів", "· no pending requests"), color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 11.sp, modifier = Modifier.padding(top = 6.dp))
                vm.admPending.forEach { d ->
                    Column(Modifier.fillMaxWidth().padding(vertical = 6.dp).background(Apex.Panel2, RoundedCornerShape(8.dp)).border(1.dp, Apex.Warn, RoundedCornerShape(8.dp)).padding(8.dp)) {
                        Text(d.s("mac"), color = Apex.Warn, fontFamily = FontFamily.Monospace, fontSize = 12.sp, fontWeight = FontWeight.SemiBold)
                        if (d.s("name").isNotBlank()) Text(d.s("name"), color = Apex.Ink2, fontFamily = FontFamily.Monospace, fontSize = 9.sp)
                        Row(Modifier.fillMaxWidth().padding(top = 6.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                            Button({ vm.admit(d.s("mac"), "approved") }, Modifier.weight(1f)) { Text(t("ДОЗВОЛИТИ", "ALLOW"), fontSize = 11.sp) }
                            OutlinedButton({ vm.admit(d.s("mac"), "denied") }, Modifier.weight(1f)) { Text(t("ВІДХИЛИТИ", "DENY"), fontSize = 11.sp, color = Apex.Bad) }
                        }
                    }
                }
            }
        }
        if (vm.admDenied.isNotEmpty()) item {
            HudCard(t("ВІДХИЛЕНІ", "DENIED"), "${vm.admDenied.size}") {
                vm.admDenied.forEach { d ->
                    Row(Modifier.fillMaxWidth().padding(vertical = 4.dp), verticalAlignment = Alignment.CenterVertically) {
                        Text(d.s("mac"), color = Apex.Bad, fontFamily = FontFamily.Monospace, fontSize = 11.sp, modifier = Modifier.weight(1f))
                        Text(t("дозволити", "allow"), color = Apex.Accent, fontFamily = FontFamily.Monospace, fontSize = 10.sp, modifier = Modifier.clickable { vm.admit(d.s("mac"), "approved") })
                    }
                }
            }
        }
        item {
            HudCard(t("ДОЗВОЛЕНІ", "APPROVED"), "${vm.admApproved.size}") {
                if (vm.admApproved.isEmpty()) Text("—", color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 11.sp)
                vm.admApproved.take(40).forEach { d ->
                    Row(Modifier.fillMaxWidth().padding(vertical = 3.dp), verticalAlignment = Alignment.CenterVertically) {
                        Box(Modifier.size(6.dp).background(Apex.Accent, RoundedCornerShape(2.dp)))
                        Spacer(Modifier.width(8.dp))
                        Text(d.s("mac") + (if (d.s("name").isNotBlank()) " · " + d.s("name") else ""), color = Apex.Ink2, fontFamily = FontFamily.Monospace, fontSize = 10.sp, modifier = Modifier.weight(1f))
                        Text("✕", color = Apex.Bad, fontFamily = FontFamily.Monospace, fontSize = 12.sp, modifier = Modifier.clickable { vm.admit(d.s("mac"), "denied") })
                    }
                }
            }
        }
        item {
            HudCard(t("СТРІЧКА ПІД'ЄДНАНЬ", "CONNECTION TIMELINE"), "${vm.netEvents.size}") {
                if (vm.netEvents.isEmpty()) Text(t("порожньо — колектор ще не писав подій", "empty — collector hasn't logged events"), color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 10.sp)
                vm.netEvents.forEach { e ->   // без ліміту — уся зібрана стрічка (сервер віддає newest-first)
                    val join = e.s("event") == "join"
                    Row(Modifier.fillMaxWidth().padding(vertical = 3.dp), verticalAlignment = Alignment.CenterVertically) {
                        Text(if (join) "▲" else "▼", color = if (join) Apex.Accent else Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 11.sp)
                        Spacer(Modifier.width(6.dp))
                        Text(fmt(e.i("ts")), color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 9.sp)
                        Spacer(Modifier.width(8.dp))
                        Text(e.s("name").ifBlank { e.s("mac") }, color = if (join) Apex.Ink else Apex.Ink2, fontFamily = FontFamily.Monospace, fontSize = 10.sp, modifier = Modifier.weight(1f))
                        Text(e.s("ip"), color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 9.sp)
                    }
                }
            }
        }
    }
}

@Composable
private fun ProfileBody(prof: JsonObject, uk: Boolean, intel: JsonObject? = null,
                        intelBusy: Boolean = false, onScan: () -> Unit = {}) {
    fun t(u: String, e: String) = if (uk) u else e
    Column(Modifier.heightIn(max = 400.dp).verticalScroll(rememberScrollState())) {
        val dev = prof["device"]?.jsonObject
        val net = prof["network"]?.jsonObject
        val head = dev ?: net
        head?.forEach { (k, v) ->
            if (k == "intel_json") return@forEach
            val vs = (v as? JsonPrimitive)?.contentOrNull ?: ""
            if (vs.isNotBlank() && vs != "null") Row(Modifier.fillMaxWidth().padding(vertical = 2.dp)) {
                Text(k, color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 11.sp, modifier = Modifier.width(110.dp))
                Text(vs, color = Apex.Ink, fontFamily = FontFamily.Monospace, fontSize = 11.sp, modifier = Modifier.weight(1f))
            }
        }
        // ── Devintel-розвідка (лише для пристроїв): порти/ОС/NetBIOS/причини + скан ──
        if (dev != null) {
            Spacer(Modifier.height(8.dp))
            Text(t("РОЗВІДКА (порти/ОС)", "INTEL (ports/OS)"), color = Apex.Accent, fontFamily = FontFamily.Monospace, fontSize = 11.sp, fontWeight = FontWeight.SemiBold)
            val dj = intel?.get("intel") as? JsonObject
            val cat = (dj?.get("category") as? JsonPrimitive)?.contentOrNull?.ifBlank { null }
                ?: (intel?.get("category") as? JsonPrimitive)?.contentOrNull
            val pl = dj?.get("port_labels") as? JsonObject
            val portsArr = dj?.get("ports") as? JsonArray
            val portsStr = when {
                pl != null && pl.isNotEmpty() -> pl.entries.joinToString("  ·  ") { "${it.key} ${(it.value as? JsonPrimitive)?.contentOrNull ?: ""}" }
                !portsArr.isNullOrEmpty() -> portsArr.joinToString(", ") { (it as? JsonPrimitive)?.contentOrNull ?: "" }
                else -> null
            }
            val nb = (dj?.get("netbios") as? JsonPrimitive)?.contentOrNull?.ifBlank { null }
            val hn = (dj?.get("hostname") as? JsonPrimitive)?.contentOrNull?.ifBlank { null }
            val rnd = (dj?.get("random_mac") as? JsonPrimitive)?.booleanOrNull ?: false
            val reasons = dj?.get("reasons") as? JsonArray
            val intelAt = (intel?.get("intel_at") as? JsonPrimitive)?.longOrNull ?: 0L
            @Composable fun kv(k: String, v: String?) { if (!v.isNullOrBlank()) Row(Modifier.fillMaxWidth().padding(vertical = 1.dp)) {
                Text(k, color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 10.sp, modifier = Modifier.width(96.dp))
                Text(v, color = Apex.Ink, fontFamily = FontFamily.Monospace, fontSize = 10.sp, modifier = Modifier.weight(1f)) } }
            kv(t("категорія", "category"), cat)
            kv(t("порти", "ports"), portsStr ?: t("нема відкритих", "none open"))
            kv("NetBIOS", nb); kv("hostname", hn)
            if (rnd) kv(t("MAC", "MAC"), t("рандомний (приватний)", "random (private)"))
            if (!reasons.isNullOrEmpty()) reasons.take(6).forEach {
                Text("• " + ((it as? JsonPrimitive)?.contentOrNull ?: ""), color = Apex.Ink2, fontFamily = FontFamily.Monospace, fontSize = 9.sp) }
            val fresh = if (intelAt <= 0L) t("ще не сканувалось", "not scanned yet") else {
                val age = (System.currentTimeMillis() / 1000 - intelAt)
                when { age < 3600 -> t("${age / 60} хв тому", "${age / 60} min ago")
                       age < 86400 -> t("${age / 3600} год тому", "${age / 3600} h ago")
                       else -> t("${age / 86400} дн тому", "${age / 86400} d ago") }
            }
            Text(t("розвідано: ", "scanned: ") + fresh, color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 9.sp, modifier = Modifier.padding(top = 2.dp))
            Spacer(Modifier.height(6.dp))
            OutlinedButton({ onScan() }, Modifier.fillMaxWidth(), enabled = !intelBusy) {
                Text(if (intelBusy) t("СКАНУЮ…", "SCANNING…") else t("СКАН ЗАРАЗ", "SCAN NOW"), fontSize = 11.sp)
            }
        }
        val hist = prof["history"]?.jsonArray
        if (!hist.isNullOrEmpty()) {
            Spacer(Modifier.height(6.dp))
            Text((if (uk) "ІСТОРІЯ" else "HISTORY") + " (${hist.size})", color = Apex.Accent, fontFamily = FontFamily.Monospace, fontSize = 10.sp)
            hist.take(20).forEach {
                val h = it.jsonObject
                Text("• ${h.s("event")}${if (h.s("detail").isNotBlank()) " " + h.s("detail") else ""}${if (h.s("ip").isNotBlank()) " " + h.s("ip") else ""}",
                    color = Apex.Ink2, fontFamily = FontFamily.Monospace, fontSize = 9.sp)
            }
        }
    }
}
