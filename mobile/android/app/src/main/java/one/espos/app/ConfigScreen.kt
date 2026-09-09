package one.espos.app

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import one.espos.app.ui.*

/**
 * ConfigCard — «КОНФІГ»: які модулі на якій платі. Чекбокси → зберегти профіль на плату
 * (/config/profile, NVS/SD) → прошивка адаптується БЕЗ перекомпіляції (вимкнені модулі не
 * пробуються), а візуал плати підсвічує лише ввімкнені. Плюс розпіновка обраних модулів.
 */
private val PINOUT = mapOf(
    "nrf24"  to listOf("SCK G2", "MOSI G15", "MISO G38", "CSN G25", "CE G12*", "VCC 3V3"),
    "cc1101" to listOf("SCK G2", "MOSI G15", "MISO G38", "CSN G27", "GDO0 G39", "VCC 3V3"),
    "sd"     to listOf("SCK G2", "MOSI G15", "MISO G21", "CS G33", "VCC 5V"),
    "bt"     to listOf("вбудований (без пінів)"),
)
private val PIN_NOTE = mapOf("nrf24" to "* G12 = strapping → 10k на GND")

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ConfigCard(vm: MainViewModel) {
    val uk = LocalLang.current == Lang.UK
    fun t(u: String, e: String) = if (uk) u else e
    LaunchedEffect(vm.connected) { if (vm.connected) vm.loadProfile() }
    HudCard(t("КОНФІГ · ЖИВЛЕННЯ МОДУЛІВ", "MODULES · POWER"), if (vm.profLoaded) t("профіль активний", "profile active") else t("автодетект", "autodetect")) {
        Text(t("Познач активні модулі. Вимкнений — не пробується І знімається драйв з його пінів (економія/менеджмент живлення). Спільні SCK/MOSI лишаються, поки активний хоч один SPI-модуль (SD↔nRF не гасять одне одного). Якщо у профілі задати <модуль>_pwr GPIO (до MOSFET) — реальне відключення рейки.",
               "Mark active modules. Disabled = not probed AND its pins are released (power management). Shared SCK/MOSI stay while any SPI module is on (SD↔nRF don't kill each other). Set <module>_pwr GPIO (to a MOSFET) in the profile for a real rail cut."),
            color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 9.sp)
        Spacer(Modifier.height(8.dp))
        Text("ESP32", color = Apex.Accent, fontFamily = FontFamily.Monospace, fontSize = 11.sp, fontWeight = FontWeight.SemiBold)
        vm.profEspKeys.forEach { k -> ModuleToggle(k.uppercase(), vm.profEsp[k] ?: true, PINOUT[k], PIN_NOTE[k]) { vm.toggleProf("esp", k) } }
        Spacer(Modifier.height(6.dp))
        Text(t("UNO R3", "UNO R3"), color = Apex.Accent, fontFamily = FontFamily.Monospace, fontSize = 11.sp, fontWeight = FontWeight.SemiBold)
        vm.profUnoKeys.forEach { k ->
            val label = if (k == "enabled") t("UNO під'єднано", "UNO attached") else k.uppercase()
            ModuleToggle(label, vm.profUno[k] ?: true, null, null) { vm.toggleProf("uno", k) }
        }
        Spacer(Modifier.height(8.dp))
        var pwrOpen by remember { mutableStateOf(vm.profPwr.any { it.value.isNotBlank() }) }
        Row(Modifier.fillMaxWidth().clickable { pwrOpen = !pwrOpen }, verticalAlignment = Alignment.CenterVertically) {
            Text((if (pwrOpen) "▾ " else "▸ ") + t("АПАРАТНЕ ВІДКЛЮЧЕННЯ РЕЙКИ (MOSFET)", "HARDWARE RAIL CUT (MOSFET)"),
                color = Apex.Accent, fontFamily = FontFamily.Monospace, fontSize = 10.sp, fontWeight = FontWeight.SemiBold)
        }
        if (pwrOpen) {
            Text(t("Опційно: GPIO, заведений на затвор MOSFET/load-switch у розриві VCC модуля (high-side, active-high). Порожньо = логічне вимкнення (high-Z). Вільні безпечні: 26, 32, 17.",
                   "Optional: GPIO wired to a MOSFET/load-switch gate on the module's VCC (high-side, active-high). Empty = logical off (high-Z). Free safe pins: 26, 32, 17."),
                color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 8.5.sp, modifier = Modifier.padding(vertical = 3.dp))
            vm.profPwrKeys.forEach { k ->
                Row(Modifier.fillMaxWidth().padding(vertical = 2.dp), verticalAlignment = Alignment.CenterVertically) {
                    Text(k.uppercase() + " pwr", color = Apex.Ink2, fontFamily = FontFamily.Monospace, fontSize = 11.sp, modifier = Modifier.width(96.dp))
                    OutlinedTextField(
                        value = vm.profPwr[k] ?: "", onValueChange = { vm.setPwrGpio(k, it) },
                        placeholder = { Text("GPIO", fontSize = 11.sp) }, singleLine = true, enabled = vm.connected,
                        textStyle = androidx.compose.ui.text.TextStyle(fontFamily = FontFamily.Monospace, fontSize = 12.sp),
                        modifier = Modifier.width(110.dp),
                    )
                }
            }
        }
        Spacer(Modifier.height(8.dp))
        Button({ vm.saveProfile() }, Modifier.fillMaxWidth(), enabled = vm.connected) {
            Text(t("ЗБЕРЕГТИ НА ПЛАТУ", "SAVE TO BOARD"), fontWeight = FontWeight.SemiBold)
        }
        if (vm.profMsg.isNotEmpty()) Text(vm.profMsg, color = Apex.Accent, fontFamily = FontFamily.Monospace, fontSize = 10.sp, modifier = Modifier.padding(top = 4.dp))
    }
}

/**
 * ApCard — «ТОЧКА ДОСТУПУ»: назва SoftAP плати (редагована) + вимикач точки. Керується по
 * кабелю (serial) або WiFi: /api/ap (стан), /api/ap/config (нова назва), /api/ap/toggle.
 * Вимкнення = плата йде в STA-only (точка зникає) — не рвати з'єднання, коли керуєш по USB.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ApCard(vm: MainViewModel) {
    val uk = LocalLang.current == Lang.UK
    fun t(u: String, e: String) = if (uk) u else e
    LaunchedEffect(vm.connected) { if (vm.connected) vm.loadAp() }
    var field by remember(vm.apSsid) { mutableStateOf(vm.apSsid) }
    val subtitle = if (vm.apLoaded) (if (vm.apUp) t("активна · ${vm.apClients} клієнт.", "up · ${vm.apClients} client(s)") else t("вимкнена", "down")) else t("—", "—")
    HudCard(t("ТОЧКА ДОСТУПУ", "ACCESS POINT"), subtitle) {
        Text(t("Назва WiFi-точки плати та її вимикач. Зміна назви — перепідключіться до нової SSID.",
               "Board's WiFi AP name and on/off. After renaming, reconnect to the new SSID."),
            color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 9.sp)
        Spacer(Modifier.height(8.dp))
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Text(t("Точка увімкнена", "AP enabled"), color = Apex.Ink, fontFamily = FontFamily.Monospace, fontSize = 12.sp, modifier = Modifier.weight(1f))
            Switch(checked = vm.apEnabled, onCheckedChange = { vm.toggleAp(it) }, enabled = vm.connected)
        }
        Spacer(Modifier.height(6.dp))
        OutlinedTextField(
            value = field, onValueChange = { if (it.length <= 32) field = it },
            label = { Text(t("SSID точки", "AP SSID")) }, singleLine = true, enabled = vm.connected,
            textStyle = androidx.compose.ui.text.TextStyle(fontFamily = FontFamily.Monospace, fontSize = 13.sp),
            modifier = Modifier.fillMaxWidth(),
        )
        Spacer(Modifier.height(6.dp))
        Button({ vm.saveApSsid(field) }, Modifier.fillMaxWidth(),
            enabled = vm.connected && field.trim().isNotEmpty() && field != vm.apSsid) {
            Text(t("ЗБЕРЕГТИ НАЗВУ", "SAVE NAME"), fontWeight = FontWeight.SemiBold)
        }
        if (vm.apMsg.isNotEmpty()) Text(vm.apMsg, color = Apex.Accent, fontFamily = FontFamily.Monospace, fontSize = 10.sp, modifier = Modifier.padding(top = 4.dp))
    }
}

/**
 * HostingCard — «ХОСТИНГ (low-RAM)»: майстер-вимикач WiFi+веб-сервера НА СЕСІЮ. Вимкнення
 * гасить STA+SoftAP+WebServer+WebSocket і звільняє ~45 КБ heap (/sys/passive); ребут вмикає
 * назад. Має сенс по USB — по WiFi вимкнення обірве власне з'єднання (тому попереджаємо).
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun HostingCard(vm: MainViewModel) {
    val uk = LocalLang.current == Lang.UK
    fun t(u: String, e: String) = if (uk) u else e
    val overWifi = !vm.useUsb
    HudCard(t("ХОСТИНГ · LOW-RAM", "HOSTING · LOW-RAM"), if (vm.hostingOn) t("активний", "on") else t("вимкнено · +~45КБ", "off · +~45KB")) {
        Text(t("Вимкнути WiFi-точку та веб-сервер плати, щоб звільнити ~45 КБ heap. На сесію — ребут вмикає назад.",
               "Turn off the board's WiFi + web server to free ~45 KB heap. Session-only — reboot re-enables."),
            color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 9.sp)
        Spacer(Modifier.height(8.dp))
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Text(t("Хостинг увімкнено", "Hosting on"), color = Apex.Ink, fontFamily = FontFamily.Monospace, fontSize = 12.sp, modifier = Modifier.weight(1f))
            Switch(checked = vm.hostingOn, onCheckedChange = { vm.toggleHosting(it) }, enabled = vm.connected)
        }
        if (overWifi) Text(t("⚠ Ви на WiFi: вимкнення обірве це з'єднання. Назад — по USB або ребут.",
                             "⚠ You're on WiFi: turning off drops this connection. Back via USB or reboot."),
            color = Apex.Warn, fontFamily = FontFamily.Monospace, fontSize = 9.sp, modifier = Modifier.padding(top = 4.dp))
        if (vm.hostingMsg.isNotEmpty()) Text(vm.hostingMsg, color = Apex.Accent, fontFamily = FontFamily.Monospace, fontSize = 10.sp, modifier = Modifier.padding(top = 4.dp))
    }
}

@Composable
private fun ModuleToggle(label: String, on: Boolean, pins: List<String>?, note: String?, onToggle: () -> Unit) {
    Column(Modifier.fillMaxWidth().padding(vertical = 3.dp)) {
        Row(Modifier.fillMaxWidth().clickable { onToggle() }, verticalAlignment = Alignment.CenterVertically) {
            Box(Modifier.size(18.dp).border(1.5.dp, if (on) Apex.Accent else Apex.Edge, RoundedCornerShape(4.dp))
                .background(if (on) Apex.AccentSoft else Apex.Panel2, RoundedCornerShape(4.dp)), contentAlignment = Alignment.Center) {
                if (on) Text("✓", color = Apex.Accent, fontFamily = FontFamily.Monospace, fontSize = 12.sp, fontWeight = FontWeight.Bold)
            }
            Spacer(Modifier.width(8.dp))
            Text(label, color = if (on) Apex.Ink else Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 12.sp)
        }
        // розпіновка обраного модуля (лише коли ввімкнено)
        if (on && pins != null) {
            Text(pins.joinToString("  ·  "), color = Apex.Ink2, fontFamily = FontFamily.Monospace, fontSize = 8.5.sp,
                modifier = Modifier.padding(start = 26.dp, top = 1.dp))
            if (note != null) Text(note, color = Apex.Warn, fontFamily = FontFamily.Monospace, fontSize = 8.sp, modifier = Modifier.padding(start = 26.dp))
        }
    }
}
