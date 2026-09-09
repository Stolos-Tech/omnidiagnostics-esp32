package one.espos.app.ui

/**
 * IsoBoard — ізометричне «інженерне» креслення плати у стилі APEX-референсу:
 * PCB як iso-плита з товщиною, сіткою-трасами, монтажними отворами; компоненти —
 * тонкоконтурні (wireframe) iso-призми / циліндри / гребінки пінів, із ніжками у
 * центрального чіпа. Розташування наближене до РЕАЛЬНОЇ будови плат (T-Display / UNO).
 * Інтерактив: тап по модулю -> вибір (підсвітка+масштаб «наближення») + деталі під платою.
 * Плата віддає лише прапорці present/selectable; уся геометрія рахується тут.
 */
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.lerp
import androidx.compose.ui.graphics.nativeCanvas
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlin.math.cos
import kotlin.math.sin
import kotlin.math.abs
import kotlin.math.PI

/** Компонент на платі. nx,ny — центр на площині плати (0..1). du,dv — півширина сліду,
 *  h — висота (частка ширини плати). kind керує формою: mcu/chip/disp/usb/jack/cap/pot/
 *  header/xtal/btn/reg/conn/mod/ant. selectable=false — пасивна «начинка» плати (не тапається). */
data class IsoModule(
    val name: String, val nx: Float, val ny: Float, val present: Boolean,
    val sub: String = "", val detail: String = "", val kind: String = "mod",
    val du: Float = 0.05f, val dv: Float = 0.05f, val h: Float = 0.06f,
    val selectable: Boolean = true
)

private const val ISO = PI / 6.0                    // 30° ізометрія
private val COS = cos(ISO).toFloat()
private val SIN = sin(ISO).toFloat()

@Composable
fun IsoDeviceView(title: String, subtitle: String, modules: List<IsoModule>, aspect: Float = 1.6f, height: Int = 320) {
    val uk = LocalLang.current == Lang.UK
    var selected by remember(modules.size) { mutableStateOf(-1) }
    val sel by androidx.compose.animation.core.animateFloatAsState(if (selected >= 0) 1f else 0f, label = "sel")

    Column(Modifier.fillMaxWidth()) {
        Row(Modifier.fillMaxWidth().padding(bottom = 4.dp), verticalAlignment = Alignment.Bottom) {
            Text("[ $title ]", color = Apex.Accent, fontFamily = FontFamily.Monospace, fontWeight = FontWeight.SemiBold, fontSize = 12.sp, letterSpacing = 1.sp)
            Spacer(Modifier.weight(1f))
            Text(subtitle, color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 9.sp)
        }
        Box(Modifier.fillMaxWidth().height(height.dp)
            .pointerInput(modules, aspect) {
                detectTapGestures { off -> selected = hitTest(off, this.size.width.toFloat(), this.size.height.toFloat(), modules, aspect) }
            }) {
            Canvas(Modifier.fillMaxSize()) { drawIsoDevice(modules, selected, sel, aspect) }
        }
        if (selected in modules.indices) {
            val m = modules[selected]
            Column(Modifier.fillMaxWidth()
                .background(Apex.Panel2, RoundedCornerShape(8.dp))
                .border(1.dp, if (m.present) Apex.Accent else Apex.Edge, RoundedCornerShape(8.dp))
                .padding(12.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Box(Modifier.size(8.dp).background(if (m.present) Apex.Accent else Apex.Muted, RoundedCornerShape(2.dp)))
                    Spacer(Modifier.width(8.dp))
                    Text(m.name, color = Apex.Ink, fontFamily = FontFamily.Monospace, fontWeight = FontWeight.SemiBold, fontSize = 14.sp, modifier = Modifier.weight(1f))
                    StatusTag(if (m.present) "ACTIVE" else "ABSENT", if (m.present) 0 else 2)
                }
                if (m.sub.isNotBlank()) { Spacer(Modifier.height(4.dp)); Text(m.sub, color = Apex.Ink2, fontFamily = FontFamily.Monospace, fontSize = 11.sp) }
                if (m.detail.isNotBlank()) { Spacer(Modifier.height(4.dp)); Text(m.detail, color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 11.sp) }
            }
        } else {
            Text(if (uk) "▸ тапни компонент для деталей" else "▸ tap a component for details", color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 10.sp, modifier = Modifier.padding(top = 4.dp))
        }
    }
}

// --- Ізо-математика: точка площини плати (u,v ∈ 0..1) -> екран. aspect=w/h реальних плат ---
private class IsoFrame(w: Float, h: Float, aspect: Float) {
    val cx = w * 0.5f
    val cy = h * 0.44f
    val unit = w * 0.31f
    private val k = kotlin.math.sqrt(aspect)
    val eU = Offset(COS * unit * k, SIN * unit * k)        // вісь ширини (довша для landscape)
    val eV = Offset(-COS * unit / k, SIN * unit / k)       // вісь глибини
    val corner0 = Offset(cx - (eU.x + eV.x) / 2f, cy - (eU.y + eV.y) / 2f)   // центр плати = (cx,cy)
    fun pt(u: Float, v: Float) = corner0 + eU * u + eV * v
}

private fun moduleScreen(f: IsoFrame, m: IsoModule): Offset = f.pt(m.nx, m.ny)

/** Виноска: індекс модуля + прямокутник підпису збоку + сторона (ліва/права). */
private class Callout(val idx: Int, val cy: Float, val x0: Float, val y0: Float, val x1: Float, val y1: Float, val left: Boolean)

/** Розкладка підписів у два бокові стовпці: селектні модулі діляться за nx (медіана),
 *  всередині стовпця — по ny згори вниз. Розміри рахуються без Paint (оцінка ширини),
 *  щоб draw і hitTest збігалися. */
private fun sideCallouts(w: Float, h: Float, modules: List<IsoModule>): List<Callout> {
    val res = ArrayList<Callout>()
    val ts = w * 0.030f; val ch = ts * 0.60f; val boxH = ts * 1.8f
    val top = h * 0.09f; val bot = h * 0.94f
    val sel = modules.indices.filter { modules[it].selectable }.sortedBy { modules[it].nx }
    val half = (sel.size + 1) / 2
    fun place(list: List<Int>, leftSide: Boolean) {
        val n = list.size; if (n == 0) return
        list.sortedBy { modules[it].ny }.forEachIndexed { i, idx ->
            val m = modules[idx]
            val cy = if (n == 1) (top + bot) / 2 else top + (bot - top) * i / (n - 1)
            val tw = m.name.length * ch + ts * 1.3f
            val x1 = if (leftSide) w * 0.205f else w * 0.795f + tw
            val x0 = x1 - tw
            res.add(Callout(idx, cy, x0, cy - boxH / 2, x1, cy + boxH / 2, leftSide))
        }
    }
    place(sel.take(half), true); place(sel.drop(half), false)
    return res
}

private fun hitTest(off: Offset, w: Float, h: Float, modules: List<IsoModule>, aspect: Float): Int {
    val f = IsoFrame(w, h, aspect)
    // 1) підписи-виноски (великі зручні цілі)
    for (c in sideCallouts(w, h, modules)) {
        if (off.x in c.x0..c.x1 && off.y in c.y0..c.y1) return c.idx
    }
    // 2) сам компонент на платі
    var best = -1; var bestD = Float.MAX_VALUE
    modules.forEachIndexed { i, m ->
        if (!m.selectable) return@forEachIndexed
        val p = moduleScreen(f, m)
        val d = (off - p).getDistanceSquared()
        if (d < bestD && d < (w * 0.10f) * (w * 0.10f)) { bestD = d; best = i }
    }
    return best
}

// --- примітиви ---

private fun DrawScope.quadFill(a: Offset, b: Offset, c: Offset, d: Offset, col: Color, stroke: Color, sw: Float) {
    val p = Path().apply { moveTo(a.x, a.y); lineTo(b.x, b.y); lineTo(c.x, c.y); lineTo(d.x, d.y); close() }
    drawPath(p, col)
    if (sw > 0f) drawPath(p, stroke, style = Stroke(sw))
}

/** iso-призма з прямокутного сліду [nx±du, ny±dv] заданої висоти h (частка unit). */
private fun DrawScope.isoPrism(f: IsoFrame, nx: Float, ny: Float, du: Float, dv: Float, h: Float,
                               top: Color, right: Color, front: Color, stroke: Color, sw: Float) {
    val b00 = f.pt(nx - du, ny - dv); val b10 = f.pt(nx + du, ny - dv)
    val b11 = f.pt(nx + du, ny + dv); val b01 = f.pt(nx - du, ny + dv)
    val up = Offset(0f, -h * f.unit)
    val t00 = b00 + up; val t10 = b10 + up; val t11 = b11 + up; val t01 = b01 + up
    quadFill(b10, b11, t11, t10, right, stroke, sw)    // права грань (+u)
    quadFill(b01, b11, t11, t01, front, stroke, sw)    // передня грань (+v)
    quadFill(t00, t10, t11, t01, top, stroke, sw)      // верх
}

/** iso-циліндр (конденсатор / барильце живлення / підстроєчник). */
private fun DrawScope.isoCyl(f: IsoFrame, nx: Float, ny: Float, r: Float, h: Float,
                             top: Color, side: Color, stroke: Color, sw: Float) {
    val c = f.pt(nx, ny); val up = Offset(0f, -h * f.unit); val N = 22
    fun ring(cc: Offset) = (0..N).map { i ->
        val t = 2.0 * PI * i / N
        cc + f.eU * (r * cos(t)).toFloat() + f.eV * (r * sin(t)).toFloat()
    }
    val bot = ring(c); val topR = ring(c + up)
    // передні (нижні) сегменти бічної поверхні — заливка; задні — лише тонкий контур
    for (i in 0 until N) {
        val front = (bot[i].y + bot[i + 1].y) * 0.5f > c.y
        if (front) quadFill(bot[i], bot[i + 1], topR[i + 1], topR[i], side, side, 0f)
    }
    // силует (крайні вертикалі)
    var li = 0; var ri = 0
    for (i in 0..N) { if (bot[i].x < bot[li].x) li = i; if (bot[i].x > bot[ri].x) ri = i }
    drawLine(stroke, bot[li], topR[li], sw); drawLine(stroke, bot[ri], topR[ri], sw)
    // кільця
    val pb = Path().apply { bot.forEachIndexed { i, p -> if (i == 0) moveTo(p.x, p.y) else lineTo(p.x, p.y) } }
    drawPath(pb, stroke, style = Stroke(sw * 0.8f))
    val ptp = Path().apply { topR.forEachIndexed { i, p -> if (i == 0) moveTo(p.x, p.y) else lineTo(p.x, p.y) } }
    drawPath(ptp, top); drawPath(ptp, stroke, style = Stroke(sw))
}

/** гребінка пінів: низька основа + сітка cols×rows коротких вертикальних ніжок. */
private fun DrawScope.isoHeader(f: IsoFrame, nx: Float, ny: Float, du: Float, dv: Float,
                                cols: Int, rows: Int, base: Color, stroke: Color, sw: Float) {
    isoPrism(f, nx, ny, du, dv, 0.02f, base, lerp(base, Color.Black, 0.3f), lerp(base, Color.Black, 0.5f), stroke, sw * 0.8f)
    val up = Offset(0f, -0.02f * f.unit); val pin = Offset(0f, -0.05f * f.unit)
    for (ci in 0 until cols) for (ri in 0 until rows) {
        val u = if (cols == 1) nx else nx - du + 2 * du * ci / (cols - 1)
        val v = if (rows == 1) ny else ny - dv + 2 * dv * ri / (rows - 1)
        val b = f.pt(u, v) + up
        drawLine(stroke, b, b + pin, sw)
    }
}

/** ніжки чіпа: короткі виводи вздовж чотирьох ребер сліду. */
private fun DrawScope.chipLeads(f: IsoFrame, nx: Float, ny: Float, du: Float, dv: Float, n: Int, stroke: Color, sw: Float) {
    val outU = Offset(f.eU.x, f.eU.y) * 0.10f; val outV = Offset(f.eV.x, f.eV.y) * 0.10f
    for (i in 0 until n) {
        val fu = if (n == 1) 0.5f else i.toFloat() / (n - 1)
        val u = nx - du + 2 * du * fu; val v = ny - dv + 2 * dv * fu
        // виводи на ±v ребрах (довгі сторони)
        run { val p = f.pt(u, ny + dv); drawLine(stroke, p, p + outV, sw) }
        run { val p = f.pt(u, ny - dv); drawLine(stroke, p, p - outV, sw) }
        // виводи на ±u ребрах (короткі сторони)
        run { val p = f.pt(nx + du, v); drawLine(stroke, p, p + outU, sw) }
        run { val p = f.pt(nx - du, v); drawLine(stroke, p, p - outU, sw) }
    }
}

private fun DrawScope.drawIsoDevice(modules: List<IsoModule>, selected: Int, sel: Float, aspect: Float) {
    val f = IsoFrame(size.width, size.height, aspect)
    // ---- PCB-плита ----
    val c00 = f.pt(0f, 0f); val c10 = f.pt(1f, 0f); val c11 = f.pt(1f, 1f); val c01 = f.pt(0f, 1f)
    val thpx = size.height * 0.045f; val down = Offset(0f, thpx)
    val pcb = Color(0xFF0A1014)
    quadFill(c01, c11, c11 + down, c01 + down, Color(0xFF060B0E), Color(0xFF1B2429), 1.2f)  // передній торець
    quadFill(c10, c11, c11 + down, c10 + down, Color(0xFF04080A), Color(0xFF141B1F), 1.2f)  // правий торець
    quadFill(c00, c10, c11, c01, pcb, Apex.Accent.copy(alpha = 0.55f), 1.6f)                // верх PCB
    // сітка-траси
    for (i in 1..5) { val t = i / 6f
        drawLine(Color(0x12FFFFFF), f.pt(t, 0f), f.pt(t, 1f), 1f)
        drawLine(Color(0x12FFFFFF), f.pt(0f, t), f.pt(1f, t), 1f)
    }
    // декоративні L-траси (мідь)
    val trace = Apex.Accent.copy(alpha = 0.16f)
    drawLine(trace, f.pt(0.5f, 0.5f), f.pt(0.5f, 0.1f), 1.2f); drawLine(trace, f.pt(0.5f, 0.1f), f.pt(0.85f, 0.1f), 1.2f)
    drawLine(trace, f.pt(0.5f, 0.5f), f.pt(0.15f, 0.5f), 1.2f); drawLine(trace, f.pt(0.15f, 0.5f), f.pt(0.15f, 0.9f), 1.2f)
    drawLine(trace, f.pt(0.5f, 0.5f), f.pt(0.75f, 0.85f), 1.2f)
    // монтажні отвори по кутах
    for (h in listOf(0.06f to 0.06f, 0.94f to 0.06f, 0.06f to 0.94f, 0.94f to 0.94f)) {
        val cc = f.pt(h.first, h.second); val N = 14
        val p = Path().apply { for (i in 0..N) { val t = 2.0 * PI * i / N; val q = cc + f.eU * (0.022f * cos(t)).toFloat() + f.eV * (0.022f * sin(t)).toFloat(); if (i == 0) moveTo(q.x, q.y) else lineTo(q.x, q.y) } }
        drawPath(p, Color(0xFF05090B)); drawPath(p, Color(0xFF2C383D), style = Stroke(1.1f))
    }

    // ---- компоненти (back-to-front) ----
    val order = modules.indices.sortedBy { moduleScreen(f, modules[it]).y }
    for (idx in order) {
        val m = modules[idx]
        val on = m.present; val isSel = idx == selected
        val grow = if (isSel) 1f + 0.28f * sel else 1f
        val du = m.du * grow; val dv = m.dv * grow; val hh = m.h * grow
        // стилі
        val stroke = when { isSel -> Apex.Ink; !m.selectable -> Color(0xFF5D6E73); on -> Apex.Accent; else -> Apex.Edge }
        val sw = if (isSel) 1.7f else 1.15f
        val topBase = Color(0xFF12191D)
        val top = when { isSel -> lerp(topBase, Apex.Accent, 0.28f); on && m.selectable -> lerp(topBase, Apex.Accent, 0.12f); else -> topBase }
        val right = lerp(top, Color.Black, 0.32f); val front = lerp(top, Color.Black, 0.52f)
        // траса живлення для активних периферій
        if (m.selectable) {
            val col = (if (on) Apex.Accent else Apex.Edge).copy(alpha = 0.45f)
            drawLine(col, f.pt(m.nx, m.ny), f.pt(0.5f, 0.5f), 1f)
        }
        when (m.kind) {
            "cap", "jack", "pot" -> isoCyl(f, m.nx, m.ny, maxOf(du, dv), hh, top, right, stroke, sw)
            "header" -> {
                val cols = maxOf(2, (du / 0.028f).toInt()); val rows = maxOf(1, (dv / 0.028f).toInt())
                isoHeader(f, m.nx, m.ny, du, dv, cols, rows, Color(0xFF0E141A), stroke, sw)
            }
            "disp" -> {
                isoPrism(f, m.nx, m.ny, du, dv, hh, top, right, front, stroke, sw)
                // екран (заглиблена панель)
                val up = Offset(0f, -hh * f.unit); val id = du * 0.86f; val iv = dv * 0.86f
                val s00 = f.pt(m.nx - id, m.ny - iv) + up; val s10 = f.pt(m.nx + id, m.ny - iv) + up
                val s11 = f.pt(m.nx + id, m.ny + iv) + up; val s01 = f.pt(m.nx - id, m.ny + iv) + up
                quadFill(s00, s10, s11, s01, if (on) Color(0xFF04231C) else Color(0xFF0A1014), Apex.Accent.copy(alpha = if (on) 0.5f else 0.2f), 1f)
                if (on) for (i in 1..3) { val t = i / 4f; drawLine(Apex.Accent.copy(alpha = 0.18f), s00 + (s01 - s00) * t, s10 + (s11 - s10) * t, 1f) }
            }
            "mcu" -> {
                isoPrism(f, m.nx, m.ny, du, dv, hh, top, right, front, stroke, sw)
                chipLeads(f, m.nx, m.ny, du, dv, maxOf(5, (dv / 0.03f).toInt()), if (on) stroke else Apex.Edge, sw * 0.7f)
                // мітка «пін 1»
                val d = f.pt(m.nx - du * 0.72f, m.ny - dv * 0.72f) + Offset(0f, -hh * f.unit)
                drawCircle(stroke, 1.8f, d)
            }
            else -> isoPrism(f, m.nx, m.ny, du, dv, hh, top, right, front, stroke, sw)
        }
    }

    // ---- бокові виноски (підписи навколо плати з лініями до компонентів) ----
    val ts = size.width * 0.030f
    for (c in sideCallouts(size.width, size.height, modules)) {
        val m = modules[c.idx]; val on = m.present; val isSel = c.idx == selected
        val grow = if (isSel) 1.28f else 1f
        val anchor = f.pt(m.nx, m.ny) + Offset(0f, -m.h * f.unit * grow)
        val lc = if (isSel) Apex.Ink else if (on) Apex.Accent else Apex.Edge
        val innerX = if (c.left) c.x1 else c.x0
        // лінія-виноска: компонент -> внутрішній край підпису (з невеликим горизонтальним хвостом)
        drawLine(lc.copy(alpha = 0.75f), anchor, Offset(innerX, c.cy), if (isSel) 1.7f else 1f)
        drawCircle(lc, 2.4f, anchor)
        // рамка підпису
        val bg = if (isSel) Apex.AccentSoft else Color(0xE6070C0F)
        drawRoundRect(bg, Offset(c.x0, c.y0), Size(c.x1 - c.x0, c.y1 - c.y0), CornerRadius(4f, 4f))
        drawRoundRect(lc, Offset(c.x0, c.y0), Size(c.x1 - c.x0, c.y1 - c.y0), CornerRadius(4f, 4f), style = Stroke(if (isSel) 1.6f else 1f))
        // індикатор стану + текст
        val dotX = c.x0 + ts * 0.7f
        drawCircle(if (on) Apex.Accent else Apex.Muted, ts * 0.24f, Offset(dotX, c.cy))
        val paint = android.graphics.Paint().apply {
            color = (if (isSel) Apex.Ink else if (on) Apex.Ink2 else Apex.Muted).toArgb()
            textSize = ts; isAntiAlias = true; textAlign = android.graphics.Paint.Align.LEFT
            typeface = android.graphics.Typeface.create(android.graphics.Typeface.MONOSPACE, if (on) android.graphics.Typeface.BOLD else android.graphics.Typeface.NORMAL)
        }
        drawContext.canvas.nativeCanvas.drawText(m.name, dotX + ts * 0.5f, c.cy + ts * 0.35f, paint)
    }
}
