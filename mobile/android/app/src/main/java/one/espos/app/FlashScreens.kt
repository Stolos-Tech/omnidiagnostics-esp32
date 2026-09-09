package one.espos.app

import androidx.compose.foundation.background
import androidx.compose.foundation.border
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
import one.espos.app.ui.*

/**
 * FlashCard — прошивка ПЛАТИ через USB-OTG з телефона. Три кроки: SCAN USB →
 * (firmware з сервера) → ПРОШИТИ. Пише в app-партицію напряму (без dual-OTA).
 * serverUrl — база dashboard (звідки беремо firmware.bin + маніфест SHA-256).
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun FlashCard(fvm: FlashViewModel, serverUrl: String) {
    val uk = LocalLang.current == Lang.UK
    fun t(u: String, e: String) = if (uk) u else e
    LaunchedEffect(serverUrl) { if (serverUrl.isNotBlank()) fvm.refreshFirmware(serverUrl) }

    HudCard(t("ПРОШИВКА ПЛАТИ (USB)", "FLASH BOARD (USB)"),
        if (fvm.fwVersion.isNotBlank()) "fw v${fvm.fwVersion}" else "OTG") {

        Text(t("Пряма прошивка через OTG-кабель (телефон → USB-C плати). Пише напряму в " +
               "app-партицію — не потребує двох OTA-партицій, тож усі фічі лишаються.",
               "Direct flash over an OTG cable (phone → board USB-C). Writes the app " +
               "partition directly — no dual-OTA needed, so all features stay."),
            color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 10.sp)
        Spacer(Modifier.height(10.dp))

        // крок 1 — USB
        StepRow("1", t("USB-пристрій", "USB device"),
            if (fvm.usbReady) Apex.Accent else Apex.Muted)
        Row(Modifier.fillMaxWidth().padding(top = 4.dp), verticalAlignment = Alignment.CenterVertically) {
            OutlinedButton({ fvm.scanUsb() }, enabled = !fvm.busy) { Text(t("SCAN USB", "SCAN USB"), fontSize = 11.sp) }
            Spacer(Modifier.width(10.dp))
            Text(fvm.usbLabel.ifBlank { t("не під'єднано", "not attached") },
                color = if (fvm.usbReady) Apex.Accent else Apex.Ink2,
                fontFamily = FontFamily.Monospace, fontSize = 10.sp)
        }

        Spacer(Modifier.height(12.dp))
        // крок 2 — firmware з сервера
        StepRow("2", t("Прошивка з сервера", "Firmware from server"),
            if (fvm.fwSize > 0) Apex.Accent else Apex.Muted)
        if (fvm.fwSize > 0) {
            Text("v${fvm.fwVersion} · ${fvm.fwSize / 1024} КБ · 0x%X".format(fvm.fwOffset),
                color = Apex.Ink2, fontFamily = FontFamily.Monospace, fontSize = 10.sp, modifier = Modifier.padding(top = 2.dp))
            if (fvm.fwSha.isNotBlank())
                Text("sha256 ${fvm.fwSha.take(16)}…", color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 9.sp)
            if (fvm.fwNotes.isNotBlank())
                Text(fvm.fwNotes, color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 9.sp)
        } else {
            Row(verticalAlignment = Alignment.CenterVertically) {
                OutlinedButton({ fvm.refreshFirmware(serverUrl) }, enabled = !fvm.busy) { Text(t("ПЕРЕВІРИТИ", "CHECK"), fontSize = 11.sp) }
                Spacer(Modifier.width(10.dp))
                Text(fvm.stage.ifBlank { t("нема даних", "no data") }, color = Apex.Ink2, fontFamily = FontFamily.Monospace, fontSize = 10.sp)
            }
        }

        Spacer(Modifier.height(12.dp))
        // крок 3 — прошити: USB (пряме, будь-яка таблиця) або WiFi→SD (потрібен factory-updater)
        val usbReady = fvm.usbReady && fvm.fwSize > 0 && !fvm.busy
        Button({ fvm.flash(serverUrl) }, enabled = usbReady, modifier = Modifier.fillMaxWidth()) {
            Text(if (fvm.busy) t("ПРОШИВАЮ…", "FLASHING…") else t("ПРОШИТИ USB", "FLASH USB"), fontWeight = FontWeight.SemiBold)
        }
        Spacer(Modifier.height(6.dp))
        val wifiReady = fvm.boardHost.isNotBlank() && fvm.fwSize > 0 && !fvm.busy
        OutlinedButton({ fvm.wifiFlash(serverUrl) }, enabled = wifiReady, modifier = Modifier.fillMaxWidth()) {
            Text(t("ПО WiFi → SD ПЛАТИ", "OVER WiFi → BOARD SD"), fontSize = 12.sp)
        }
        Text(t("USB — пряме (будь-яка плата). WiFi→SD — без кабелю, але плата має бути на OTA-таблиці (factory-updater) + SD змонтована.",
               "USB — direct (any board). WiFi→SD — cableless, but the board needs the OTA table (factory-updater) + mounted SD."),
            color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 9.sp, modifier = Modifier.padding(top = 4.dp))

        // прогрес
        if (fvm.dlProgress in 0..100) {
            Spacer(Modifier.height(8.dp))
            Text(t("завантаження ${fvm.dlProgress}%", "download ${fvm.dlProgress}%"), color = Apex.Ink2, fontFamily = FontFamily.Monospace, fontSize = 10.sp)
            LinearProgressIndicator(progress = { fvm.dlProgress / 100f }, modifier = Modifier.fillMaxWidth(), color = Apex.Accent, trackColor = Apex.Edge)
        }
        if (fvm.flProgress in 0..100) {
            Spacer(Modifier.height(8.dp))
            Text(t("запис у флеш ${fvm.flProgress}%", "flash write ${fvm.flProgress}%"), color = Apex.Accent, fontFamily = FontFamily.Monospace, fontSize = 10.sp)
            LinearProgressIndicator(progress = { fvm.flProgress / 100f }, modifier = Modifier.fillMaxWidth(), color = Apex.Accent, trackColor = Apex.Edge)
        }

        if (fvm.busy) {
            Spacer(Modifier.height(8.dp))
            Row(Modifier.fillMaxWidth().background(Apex.AccentSoft, RoundedCornerShape(6.dp)).padding(8.dp), verticalAlignment = Alignment.CenterVertically) {
                Text("⚠", fontSize = 13.sp); Spacer(Modifier.width(6.dp))
                Text(t("НЕ ВІД'ЄДНУЙ кабель і не вимикай плату до кінця.",
                       "DO NOT unplug the cable or power off the board until done."),
                    color = Apex.Warn, fontFamily = FontFamily.Monospace, fontSize = 10.sp)
            }
        }
        if (fvm.done) Text(t("✓ Прошито. Плата стартує з новою прошивкою.",
                             "✓ Flashed. Board is booting the new firmware."),
            color = Apex.Accent, fontFamily = FontFamily.Monospace, fontSize = 11.sp, modifier = Modifier.padding(top = 8.dp))
        if (fvm.error.isNotBlank()) Text("✗ ${fvm.error}", color = Apex.Bad, fontFamily = FontFamily.Monospace, fontSize = 11.sp, modifier = Modifier.padding(top = 8.dp))

        // консоль флешу
        if (fvm.lines.isNotEmpty()) {
            Spacer(Modifier.height(8.dp))
            Column(Modifier.fillMaxWidth().heightIn(max = 180.dp).verticalScroll(rememberScrollState())
                .border(1.dp, Apex.Edge, RoundedCornerShape(6.dp)).padding(8.dp)) {
                fvm.lines.forEach { l ->
                    Text(l, color = if (l.startsWith("✗")) Apex.Bad else Apex.Ink2,
                        fontFamily = FontFamily.Monospace, fontSize = 9.5.sp, lineHeight = 12.sp)
                }
            }
        }
    }
}

@Composable
private fun StepRow(n: String, label: String, dot: androidx.compose.ui.graphics.Color) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Box(Modifier.size(16.dp).background(dot, RoundedCornerShape(4.dp)), contentAlignment = Alignment.Center) {
            Text(n, color = Apex.Bg, fontFamily = FontFamily.Monospace, fontSize = 10.sp, fontWeight = FontWeight.Bold)
        }
        Spacer(Modifier.width(8.dp))
        Text(label, color = Apex.Ink, fontFamily = FontFamily.Monospace, fontSize = 12.sp, fontWeight = FontWeight.SemiBold)
    }
}
