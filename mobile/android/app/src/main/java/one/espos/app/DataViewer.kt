package one.espos.app

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.serialization.json.*
import one.espos.app.ui.*

/**
 * DataViewer — єдиний переглядач УСІХ зібраних даних інструментів у повному обсязі (без лімітів),
 * з описами полів і сортуванням. Працює офлайн (рендерить кеш VM), тож дані можна «ходити» й без плати.
 * Розділи згортаються; порожні підказують, який інструмент запустити.
 */
@Composable
fun DataViewerScreen(vm: MainViewModel, onBack: () -> Unit) {
    val uk = LocalLang.current == Lang.UK
    fun t(u: String, e: String) = if (uk) u else e
    Column(Modifier.fillMaxSize()) {
        Row(Modifier.fillMaxWidth().clickable { onBack() }.padding(vertical = 12.dp, horizontal = 12.dp),
            verticalAlignment = Alignment.CenterVertically) {
            Text(t("‹ НАЗАД", "‹ BACK"), color = Apex.Accent, fontFamily = FontFamily.Monospace, fontSize = 13.sp, fontWeight = FontWeight.SemiBold)
            Spacer(Modifier.weight(1f))
            Text(t("ПЕРЕГЛЯДАЧ ДАНИХ", "DATA VIEWER"), color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 11.sp, letterSpacing = 1.5.sp)
        }
        Box(Modifier.fillMaxWidth().height(1.dp).background(Apex.Edge))
        LazyColumn(Modifier.fillMaxSize().padding(horizontal = 12.dp, vertical = 10.dp)) {
            item {
                // Зведення: скільки чого зібрано (одразу видно повний обсяг)
                Row(Modifier.fillMaxWidth().padding(bottom = 8.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    CountTile("WiFi", vm.airAps.size, Modifier.weight(1f))
                    CountTile("LAN", vm.netHosts.size, Modifier.weight(1f))
                    CountTile("2.4G", vm.nrfCounts.count { it > 0 }, Modifier.weight(1f))
                    CountTile(t("ЛОГ", "LOG"), vm.logLines.size, Modifier.weight(1f))
                }
                Button({ vm.sendDataToBot() }, Modifier.fillMaxWidth(), enabled = !vm.botBusy) {
                    Text(if (vm.botBusy) "…" else t("НАДІСЛАТИ ВСЕ В БОТА", "SEND ALL TO BOT"), fontSize = 12.sp, fontFamily = FontFamily.Monospace)
                }
                if (vm.botMsg.isNotBlank()) Text(vm.botMsg, color = Apex.Accent, fontFamily = FontFamily.Monospace, fontSize = 10.sp, modifier = Modifier.padding(top = 3.dp, bottom = 6.dp))
                else Spacer(Modifier.height(10.dp))
            }
            item { WifiSection(vm, uk); Spacer(Modifier.height(10.dp)) }
            item { LanSection(vm, uk); Spacer(Modifier.height(10.dp)) }
            item { Rf24Section(vm, uk); Spacer(Modifier.height(10.dp)) }
            item { SubGhzSection(vm, uk); Spacer(Modifier.height(10.dp)) }
            item { LogSectionCard(vm, uk); Spacer(Modifier.height(24.dp)) }
        }
    }
}

@Composable
private fun CountTile(label: String, n: Int, modifier: Modifier = Modifier) {
    Column(modifier.background(Apex.Panel2, RoundedCornerShape(8.dp)).border(1.dp, Apex.Edge, RoundedCornerShape(8.dp))
        .padding(vertical = 8.dp), horizontalAlignment = Alignment.CenterHorizontally) {
        Text("$n", color = if (n > 0) Apex.Accent else Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 18.sp, fontWeight = FontWeight.Bold)
        Text(label, color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 8.5.sp, letterSpacing = 0.5.sp)
    }
}

@Composable
private fun Section(title: String, sub: String, empty: Boolean, emptyHint: String, legend: String, content: @Composable () -> Unit) {
    var open by remember { mutableStateOf(true) }
    HudCard(title, sub) {
        Row(Modifier.fillMaxWidth().clickable { open = !open }.padding(bottom = 4.dp), verticalAlignment = Alignment.CenterVertically) {
            Text(if (open) "▾" else "▸", color = Apex.Accent, fontFamily = FontFamily.Monospace, fontSize = 12.sp)
            Spacer(Modifier.width(6.dp))
            Text(legend, color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 8.5.sp, lineHeight = 12.sp, modifier = Modifier.weight(1f))
        }
        if (open) {
            if (empty) Text(emptyHint, color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 10.sp, modifier = Modifier.padding(top = 4.dp))
            else content()
        }
    }
}

@Composable
private fun WifiSection(vm: MainViewModel, uk: Boolean) {
    var byRssi by remember { mutableStateOf(true) }
    val aps = if (byRssi) vm.airAps.sortedByDescending { it[2].toIntOrNull() ?: -120 }
              else vm.airAps.sortedBy { it[0].lowercase() }
    Section(
        if (uk) "WiFi-МЕРЕЖІ" else "WiFi NETWORKS",
        "${vm.airAps.size} · twins ${vm.airTwins} · open ${vm.airOpen} · wep ${vm.airWeak}",
        vm.airAps.isEmpty(),
        if (uk) "нема даних — запусти AIR SCAN" else "no data — run AIR SCAN",
        if (uk) "RSSI: −30 відмінно…−90 на межі · enc: OPEN/WEP небезпечно, WPA2/3 норма · TWIN = клон SSID (evil-twin)"
        else "RSSI: −30 great…−90 edge · enc: OPEN/WEP risky, WPA2/3 ok · TWIN = SSID clone (evil-twin)",
    ) {
        Row(Modifier.fillMaxWidth().padding(vertical = 4.dp)) {
            SortChip(if (uk) "RSSI" else "RSSI", byRssi) { byRssi = true }
            Spacer(Modifier.width(6.dp))
            SortChip(if (uk) "SSID" else "SSID", !byRssi) { byRssi = false }
        }
        aps.forEach { ap ->
            val twin = ap[5] == "twin"; val risk = ap.getOrElse(6) { "" }
            Row(Modifier.fillMaxWidth().padding(vertical = 3.dp), verticalAlignment = Alignment.CenterVertically) {
                Column(Modifier.weight(1f)) {
                    Text(ap[0], color = if (twin) Apex.Bad else Apex.Ink, fontFamily = FontFamily.Monospace, fontSize = 12.sp)
                    Text("${ap[4]} · ch${ap[3]} · ${ap[1]}", color = if (risk == "open") Apex.Bad else if (risk == "weak") Apex.Warn else Apex.Muted,
                        fontFamily = FontFamily.Monospace, fontSize = 8.5.sp)
                }
                Text("${ap[2]}dBm", color = Apex.Ink2, fontFamily = FontFamily.Monospace, fontSize = 11.sp)
                if (twin) { Spacer(Modifier.width(6.dp)); StatusTag("TWIN", 2) }
                else if (risk == "open") { Spacer(Modifier.width(6.dp)); StatusTag("OPEN", 2) }
                else if (risk == "weak") { Spacer(Modifier.width(6.dp)); StatusTag("WEP", 1) }
            }
        }
    }
}

@Composable
private fun LanSection(vm: MainViewModel, uk: Boolean) {
    Section(
        if (uk) "LAN-ХОСТИ" else "LAN HOSTS",
        "${vm.netHosts.size}",
        vm.netHosts.isEmpty(),
        if (uk) "нема даних — запусти NET SCAN" else "no data — run NET SCAN",
        if (uk) "IP · виробник (OUI) · MAC · GW=шлюз · [TGT] = встановити таргетом"
        else "IP · vendor (OUI) · MAC · GW=gateway · [TGT] = set as target",
    ) {
        vm.netHosts.forEach { h ->
            val isTarget = vm.targetMac.equals(h[1], true) && h[1].isNotBlank()
            Row(Modifier.fillMaxWidth().padding(vertical = 3.dp), verticalAlignment = Alignment.CenterVertically) {
                Text(h[0], color = if (h[3] == "gw") Apex.Warn else Apex.Ink, fontFamily = FontFamily.Monospace, fontSize = 12.sp, modifier = Modifier.width(118.dp))
                Column(Modifier.weight(1f)) {
                    Text(h[2].ifBlank { "?" }, color = Apex.Ink2, fontFamily = FontFamily.Monospace, fontSize = 10.sp)
                    Text(h[1], color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 8.5.sp)
                }
                if (h[3] == "gw") StatusTag("GW", 1)
                if (isTarget) { Spacer(Modifier.width(4.dp)); StatusTag("TARGET", 0) }
                Text("[TGT]", color = Apex.Accent, fontFamily = FontFamily.Monospace, fontSize = 11.sp, modifier = Modifier.padding(start = 6.dp)
                    .clickable { vm.setTarget(h[0], h[1], h[2].ifBlank { "host" }) })
            }
        }
        // Остання глибока розвідка (крос-використання даних між інструментами)
        vm.deviceIntel?.let { di ->
            Spacer(Modifier.height(6.dp))
            Text(if (uk) "── ОСТАННЯ РОЗВІДКА ──" else "── LAST INTEL ──", color = Apex.Accent, fontFamily = FontFamily.Monospace, fontSize = 9.sp)
            fun s(k: String) = di[k]?.let { runCatching { it.toString().trim('"') }.getOrNull() } ?: ""
            val ip = s("ip"); val cat = s("category"); val mac = s("mac")
            if (ip.isNotBlank()) Text("$ip  ${if (mac.isNotBlank()) "· $mac" else ""}", color = Apex.Ink, fontFamily = FontFamily.Monospace, fontSize = 11.sp)
            if (cat.isNotBlank()) Text(cat, color = Apex.Ink2, fontFamily = FontFamily.Monospace, fontSize = 10.sp)
            di["port_labels"]?.let { pl ->
                runCatching {
                    val o = pl.jsonObject
                    val ports = o.entries.joinToString(" · ") { "${it.key}:${it.value.toString().trim('"')}" }
                    if (ports.isNotBlank()) Text((if (uk) "порти: " else "ports: ") + ports, color = Apex.Warn, fontFamily = FontFamily.Monospace, fontSize = 9.5.sp)
                }
            }
        }
    }
}

@Composable
private fun Rf24Section(vm: MainViewModel, uk: Boolean) {
    val busy = vm.nrfCounts.mapIndexed { i, c -> i to c }.filter { it.second > 0 }.sortedByDescending { it.second }
    Section(
        if (uk) "2.4GHz ЕФІР · nRF24" else "2.4GHz AIR · nRF24",
        "${busy.size} " + if (uk) "активних каналів" else "busy ch",
        busy.isEmpty(),
        if (uk) "нема даних — запусти 2.4G ANALYZER" else "no data — run 2.4G ANALYZER",
        if (uk) "Канал 0..125 = 2400..2525 МГц (крок 1 МГц). Число = скільки разів канал був зайнятий (RPD)."
        else "Channel 0..125 = 2400..2525 MHz (1 MHz step). Count = times channel was busy (RPD).",
    ) {
        busy.take(40).forEach { (ch, cnt) ->
            Row(Modifier.fillMaxWidth().padding(vertical = 1.dp), verticalAlignment = Alignment.CenterVertically) {
                Text("ch$ch", color = Apex.Ink2, fontFamily = FontFamily.Monospace, fontSize = 10.sp, modifier = Modifier.width(52.dp))
                Text("${2400 + ch}MHz", color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 9.sp, modifier = Modifier.width(72.dp))
                Box(Modifier.weight(1f).height(9.dp).background(Apex.Panel2, RoundedCornerShape(2.dp))) {
                    val frac = (cnt.coerceAtMost(255)) / 255f
                    Box(Modifier.fillMaxWidth(frac).height(9.dp).background(Apex.Accent, RoundedCornerShape(2.dp)))
                }
                Text("$cnt", color = Apex.Ink2, fontFamily = FontFamily.Monospace, fontSize = 9.sp, modifier = Modifier.padding(start = 6.dp).width(30.dp))
            }
        }
    }
}

@Composable
private fun SubGhzSection(vm: MainViewModel, uk: Boolean) {
    val bins = vm.subghzDb
    val peaks = bins.mapIndexed { i, d -> i to d }.sortedByDescending { it.second }.take(20)
    Section(
        if (uk) "SUB-GHz · CC1101" else "SUB-GHz · CC1101",
        "${bins.count { it > -100 }} " + if (uk) "бінів" else "bins",
        bins.isEmpty(),
        if (uk) "нема даних — запусти SUB-GHz ANALYZER (потребує CC1101)" else "no data — run SUB-GHz ANALYZER (needs CC1101)",
        if (uk) "dBm ближче до 0 = сильніше. Вікна: 300–348 / 387–464 / 779–928 МГц (авто/ворота/домофони)."
        else "dBm closer to 0 = stronger. Windows: 300–348 / 387–464 / 779–928 MHz.",
    ) {
        peaks.forEach { (i, db) ->
            val mhz = vm.subghzMhz.getOrElse(i) { 0 }
            Row(Modifier.fillMaxWidth().padding(vertical = 1.dp)) {
                Text(if (mhz > 0) "${mhz}MHz" else "bin$i", color = Apex.Ink2, fontFamily = FontFamily.Monospace, fontSize = 10.sp, modifier = Modifier.width(90.dp))
                Text("${db}dBm", color = if (db > -60) Apex.Bad else if (db > -80) Apex.Warn else Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 10.sp)
            }
        }
    }
}

@Composable
private fun LogSectionCard(vm: MainViewModel, uk: Boolean) {
    Section(
        if (uk) "ЛОГ ПЛАТИ" else "BOARD LOG",
        "${vm.logLines.size}",
        vm.logLines.isEmpty(),
        if (uk) "порожньо" else "empty",
        if (uk) "Повний журнал подій плати (усі рядки, без обрізання)." else "Full board event log (all lines, no truncation).",
    ) {
        Column(Modifier.heightIn(max = 320.dp).verticalScroll(rememberScrollState())) {
            vm.logLines.forEach { line ->
                Text(line, color = logColor(line), fontFamily = FontFamily.Monospace, fontSize = 10.sp, lineHeight = 13.sp)
            }
        }
    }
}

@Composable
private fun SortChip(label: String, on: Boolean, onClick: () -> Unit) {
    Text(label, color = if (on) Apex.Accent else Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 10.sp, fontWeight = if (on) FontWeight.SemiBold else FontWeight.Normal,
        modifier = Modifier.clickable { onClick() }.border(1.dp, if (on) Apex.Accent else Apex.Edge, RoundedCornerShape(4.dp)).padding(horizontal = 8.dp, vertical = 2.dp))
}
