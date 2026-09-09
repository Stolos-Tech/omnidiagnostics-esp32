package one.espos.app

/**
 * Діаграма Сміта — інструмент узгодження імпедансу антен (nRF24 2.44G / CC1101 868).
 * Self-contained: власний стан (R,X,Z0,f), чиста математика (SmithMath), Canvas-візуал у
 * стилі APEX. Порт із валідованого веб-прототипу; математика — рівняння Pozar (L-ланка).
 */
import androidx.compose.animation.core.*
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.clipPath
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.withStyle
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import one.espos.app.ui.*
import kotlinx.serialization.json.*
import java.util.Locale
import kotlin.math.*

/* ============================ PURE RF MATH ============================ */
data class Cplx(val re: Double, val im: Double)

object SmithMath {
    /** Γ = (z−1)/(z+1), z = (R+jX)/Z0. */
    fun gamma(R: Double, X: Double, Z0: Double): Cplx {
        val zr = R / Z0; val zi = X / Z0
        val dr = zr + 1; val di = zi; val nr = zr - 1; val ni = zi
        val den = dr * dr + di * di
        return Cplx((nr * dr + ni * di) / den, (ni * dr - nr * di) / den)
    }

    data class Metrics(
        val g: Cplx, val mag: Double, val angDeg: Double,
        val vswr: Double, val rlDb: Double, val prPct: Double, val mlDb: Double,
    )

    fun metrics(R: Double, X: Double, Z0: Double): Metrics {
        val g = gamma(R, X, Z0)
        val mag = hypot(g.re, g.im)
        val ang = Math.toDegrees(atan2(g.im, g.re))
        val vswr = if (mag >= 1) Double.POSITIVE_INFINITY else (1 + mag) / (1 - mag)
        val rl = if (mag <= 1e-9) Double.POSITIVE_INFINITY else -20 * log10(mag)
        val pr = mag * mag * 100
        val ml = if (mag >= 1) Double.POSITIVE_INFINITY else -10 * log10(1 - mag * mag)
        return Metrics(g, mag, ang, vswr, rl, pr, ml)
    }

    /** L-ланка до Z0 (Pozar): series-реактанс Xs + shunt-сусцептанс B. */
    data class LSol(val Xs: Double, val B: Double, val topo: String)

    fun lmatch(R: Double, X: Double, Z0: Double): List<LSol> {
        val out = mutableListOf<LSol>()
        if (R <= 0) return out
        if (R < Z0) {
            val rt = sqrt((Z0 - R) / R)
            for (s in listOf(1.0, -1.0)) out.add(LSol(s * R * rt - X, -s * rt / Z0, "series→shunt"))
        } else {
            for (s in listOf(1.0, -1.0)) {
                val disc = R * R + X * X - Z0 * R
                if (disc < 0) continue
                val B = (X + s * sqrt(R / Z0) * sqrt(disc)) / (R * R + X * X)
                out.add(LSol(1 / B + X * Z0 / R - Z0 / (B * R), B, "shunt→series"))
            }
        }
        return out.filter { it.Xs.isFinite() && it.B.isFinite() }
    }

    /** Найпрактичніша L-ланка (мінімум сумарної реактивності). */
    fun bestMatch(R: Double, X: Double, Z0: Double): LSol? =
        lmatch(R, X, Z0).minByOrNull { abs(it.Xs) + abs(1 / it.B) }

    data class Comp(val type: String, val value: Double, val unit: String)

    /** series-реактанс -> компонент (L нГн / C пФ). */
    fun seriesComp(Xs: Double, fMHz: Double): Comp {
        val w = 2 * PI * fMHz * 1e6
        return if (Xs >= 0) Comp("L", Xs / w * 1e9, "нГн") else Comp("C", -1 / (w * Xs) * 1e12, "пФ")
    }
    /** shunt-сусцептанс -> компонент (C пФ / L нГн). */
    fun shuntComp(B: Double, fMHz: Double): Comp {
        val w = 2 * PI * fMHz * 1e6
        return if (B >= 0) Comp("C", B / w * 1e12, "пФ") else Comp("L", -1 / (w * B) * 1e9, "нГн")
    }

    private fun gammaFromZ(zr: Double, zi: Double): Pair<Float, Float> {
        val dr = zr + 1; val di = zi; val nr = zr - 1; val ni = zi; val d = dr * dr + di * di
        return Pair(((nr * dr + ni * di) / d).toFloat(), ((ni * dr - nr * di) / d).toFloat())
    }

    /** Точки траєкторії узгодження у Γ-площині (load → центр). */
    fun trajectory(R: Double, X: Double, Z0: Double): List<Pair<Float, Float>> {
        val s = bestMatch(R, X, Z0) ?: return emptyList()
        val pts = mutableListOf<Pair<Float, Float>>(); val N = 40
        val order = s.topo == "series→shunt"
        for (i in 0..N) {
            val t = i.toDouble() / N; val zr: Double; val zi: Double
            if (order) { zr = R / Z0; zi = (X + t * s.Xs) / Z0 }
            else {
                val z0r = R / Z0; val z0i = X / Z0; val d = z0r * z0r + z0i * z0i
                var yr = z0r / d; var yi = -z0i / d; yi += t * s.B * Z0
                val dd = yr * yr + yi * yi; zr = yr / dd; zi = -yi / dd
            }
            pts.add(gammaFromZ(zr, zi))
        }
        for (i in 0..N) {
            val t = i.toDouble() / N; val zr: Double; val zi: Double
            if (order) {
                val z0r = R / Z0; val z0i = (X + s.Xs) / Z0; val d = z0r * z0r + z0i * z0i
                var yr = z0r / d; var yi = -z0i / d; yi += t * s.B * Z0
                val dd = yr * yr + yi * yi; zr = yr / dd; zi = -yi / dd
            } else {
                val z0r = R / Z0; val z0i = X / Z0; val d0 = z0r * z0r + z0i * z0i
                var yr = z0r / d0; var yi = -z0i / d0; yi += s.B * Z0
                val dd = yr * yr + yi * yi; zr = yr / dd; zi = -yi / dd + t * s.Xs / Z0
            }
            pts.add(gammaFromZ(zr, zi))
        }
        return pts
    }
}

/* ============================ FORMAT HELPERS ============================ */
private fun f1(v: Double) = String.format(Locale.US, "%.1f", v)
private fun f2(v: Double) = String.format(Locale.US, "%.2f", v)
private fun f3(v: Double) = String.format(Locale.US, "%.3f", v)
private fun fc(v: Double) = when { v >= 100 -> String.format(Locale.US, "%.0f", v); v >= 10 -> f1(v); else -> f2(v) }
private fun inf(v: Double, dp: Int = 2) = if (v.isFinite()) String.format(Locale.US, "%.${dp}f", v) else "∞"
private fun cx(re: Double, im: Double, dp: Int = 3) =
    String.format(Locale.US, "%.${dp}f", re) + (if (im < 0) " − j" else " + j") + String.format(Locale.US, "%.${dp}f", abs(im))
/** Локалізація одиниць номіналів (SmithMath повертає кириличні): нГн→nH, пФ→pF, мСм→mS. */
private fun lu(u: String, uk: Boolean) = if (uk) u else when (u) { "нГн" -> "nH"; "пФ" -> "pF"; "мСм" -> "mS"; else -> u }

private val RCurve = Color(0xFF2BD4C6)  // R = const
private val XCurve = Color(0xFFC07BFF)  // X = const

private data class Zone(val word: String, val level: Int)
private fun zoneOf(vswr: Double, uk: Boolean): Zone = when {
    vswr < 1.5 -> Zone(if (uk) "ЗБІГ" else "MATCH", 0); vswr < 2 -> Zone(if (uk) "ДОБРЕ" else "GOOD", 0)
    vswr < 3 -> Zone(if (uk) "ПРИЙНЯТНО" else "OK", 1); else -> Zone(if (uk) "РОЗСОГЛАС." else "MISMATCH", 2)
}

/* ============================ MAIN CARD ============================ */
@OptIn(ExperimentalLayoutApi::class)
@Composable
fun SmithCard() {
    val uk = LocalLang.current == Lang.UK
    fun t(u: String, e: String) = if (uk) u else e

    var R by remember { mutableStateOf(35f) }
    var X by remember { mutableStateOf(-20f) }
    var Z0 by remember { mutableStateOf(50f) }
    var fMHz by remember { mutableStateOf(868f) }
    var showPath by remember { mutableStateOf(false) }
    var sweepOn by remember { mutableStateOf(false) }
    var showMath by remember { mutableStateOf(true) }
    var showTheory by remember { mutableStateOf(false) }

    val m = SmithMath.metrics(R.toDouble(), X.toDouble(), Z0.toDouble())
    val zone = zoneOf(m.vswr, uk)
    val traj = remember(R, X, Z0) { SmithMath.trajectory(R.toDouble(), X.toDouble(), Z0.toDouble()) }
    val prog by animateFloatAsState(if (showPath) 1f else 0f, tween(900), label = "match")
    // sweep-анімація працює ЛИШЕ коли увімкнено (інакше не ганяємо рекомпозицію картки)
    val phaseAnim = remember { Animatable(0f) }
    LaunchedEffect(sweepOn) {
        if (sweepOn) phaseAnim.animateTo((2 * PI).toFloat(), infiniteRepeatable(tween(4200, easing = LinearEasing)))
        else phaseAnim.snapTo(0f)
    }

    fun setFromPoint(x: Float, y: Float, w: Float, h: Float) {
        val cxp = w / 2; val cyp = h / 2; val rad = w * 0.44f
        val re = (x - cxp) / rad; val im = -(y - cyp) / rad
        if (hypot(re, im) > 0.985f) return
        val dr = 1 - re; val di = -im; val nr = 1 + re; val ni = im; val d = dr * dr + di * di
        val zr = (nr * dr + ni * di) / d; val zi = (ni * dr - nr * di) / d
        R = (zr * Z0).coerceIn(1f, 250f); X = (zi * Z0).coerceIn(-200f, 200f)
        showPath = false
    }

    // ---- CHART ----
    HudCard(t("ДІАГРАМА СМІТА", "SMITH CHART"),
        "Z₀ ${Z0.toInt()}Ω · ${fMHz.toInt()}MHz") {
        Row(Modifier.fillMaxWidth().padding(bottom = 8.dp), verticalAlignment = Alignment.CenterVertically) {
            StatusTag("${zone.word} · ${inf(m.vswr)}", zone.level)
            Spacer(Modifier.weight(1f))
            SmallBtn(if (showPath) "◈" else "◈ ${t("УЗГ", "MATCH")}", true) { sweepOn = false; showPath = !showPath }
            Spacer(Modifier.width(6.dp))
            SmallBtn("∿", sweepOn) { showPath = false; sweepOn = !sweepOn }
        }
        Canvas(Modifier.fillMaxWidth().aspectRatio(1f)
            .pointerInput(Z0) { detectTapGestures { setFromPoint(it.x, it.y, size.width.toFloat(), size.height.toFloat()) } }
            .pointerInput(Z0) { detectDragGestures { c, _ -> setFromPoint(c.position.x, c.position.y, size.width.toFloat(), size.height.toFloat()) } }
        ) {
            // читаємо фазу ВСЕРЕДИНІ draw -> перемальовується лише Canvas, не вся картка
            val sweepG = if (sweepOn) SmithMath.gamma(R.toDouble(),
                X.toDouble() + sin(phaseAnim.value.toDouble()) * max(15.0, abs(X.toDouble()) * 0.6), Z0.toDouble()) else null
            drawSmith(this, m.g, if (showPath) traj else emptyList(), prog, sweepG)
        }
        Row(Modifier.fillMaxWidth().padding(top = 6.dp), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            Legend(RCurve, "R"); Legend(XCurve, "X"); Legend(Apex.Accent, t("наван.", "load")); Legend(Apex.Warn, t("узгодж.", "match"))
        }
        Text(t("тапни/тягни по діаграмі, щоб задати R та X", "tap/drag the chart to set R and X"),
            color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 9.sp, modifier = Modifier.padding(top = 4.dp))
    }

    // ---- CONTROLS ----
    HudCard(t("ПАРАМЕТРИ СИСТЕМИ", "SYSTEM PARAMS")) {
        FlowRow(Modifier.fillMaxWidth().padding(bottom = 8.dp),
            horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            Preset("CC1101 868", fMHz == 868f && R == 35f) { fMHz = 868f; R = 35f; X = -20f; showPath = false }
            Preset("nRF 2.44G", fMHz == 2440f && R == 45f) { fMHz = 2440f; R = 45f; X = 18f; showPath = false }
            Preset("WiFi 2.44G", fMHz == 2440f && R == 70f) { fMHz = 2440f; R = 70f; X = -30f; showPath = false }
            Preset("433", fMHz == 433f) { fMHz = 433f; R = 28f; X = 40f; showPath = false }
        }
        SliderRow(t("Опір R", "Resistance R"), "${R.toInt()} Ω", R, 1f, 250f) { R = it; showPath = false }
        SliderRow(t("Реактанс X", "Reactance X"), "${if (X < 0) "−" else ""}${abs(X.toInt())} Ω", X, -200f, 200f) { X = it; showPath = false }
        Row(Modifier.fillMaxWidth().padding(top = 4.dp), horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            NumField("Z₀ Ω", Z0, Modifier.weight(1f)) { Z0 = it.coerceIn(1f, 200f); showPath = false }
            NumField(t("Частота МГц", "Freq MHz"), fMHz, Modifier.weight(1f)) { fMHz = it.coerceAtLeast(1f); showPath = false }
        }
    }

    // ---- READOUTS ----
    HudCard(t("ВІДБИТТЯ", "REFLECTION")) {
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(9.dp)) {
            StatTile("VSWR", inf(m.vswr), "", zone.level, Modifier.weight(1f))
            StatTile("RL", inf(m.rlDb, 1), "dB", 0, Modifier.weight(1f))
            StatTile(t("Відбито", "Refl."), f1(m.prPct), "%", zone.level, Modifier.weight(1f))
        }
        Spacer(Modifier.height(9.dp))
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(9.dp)) {
            StatTile(t("У антену", "To ant."), f1(100 - m.prPct), "%", 0, Modifier.weight(1f))
            StatTile(t("Втрата дал.", "Range loss"), inf(m.mlDb), "dB", if (m.mlDb > 1) 1 else 0, Modifier.weight(1f))
            StatTile("|Γ|∠θ", "${f2(m.mag)}∠${m.angDeg.toInt()}°", "", 0, Modifier.weight(1f))
        }
    }

    // ---- INTERPRETATION (dynamic reading) ----
    HudCard(t("ТРАКТУВАННЯ · ЩО ЦЕ НА ПРАКТИЦІ", "INTERPRETATION · IN PRACTICE")) {
        val lines = remember(R, X, Z0, uk) { buildInterp(m, uk) }
        lines.forEachIndexed { i, (ic, txt) ->
            Row(Modifier.fillMaxWidth().padding(vertical = 4.dp), verticalAlignment = Alignment.Top) {
                Text(ic, color = Apex.Accent, fontFamily = FontFamily.Monospace, fontSize = 13.sp, modifier = Modifier.width(18.dp))
                Text(txt, color = Apex.Ink2, fontFamily = FontFamily.Monospace, fontSize = 12.5.sp, lineHeight = 17.sp)
            }
        }
    }

    // ---- MATCHING + ADVICE ----
    val best = SmithMath.bestMatch(R.toDouble(), X.toDouble(), Z0.toDouble())
    HudCard(t("УЗГОДЖЕННЯ · L-ЛАНКА → 50 Ω", "MATCHING · L → 50 Ω")) {
        if (best == null) {
            Text(t("навантаження вже ≈ Z₀ — узгодження не потрібне", "load already ≈ Z₀ — no matching needed"),
                color = Apex.Accent, fontFamily = FontFamily.Monospace, fontSize = 12.sp)
        } else {
            val ser = SmithMath.seriesComp(best.Xs, fMHz.toDouble())
            val sh = SmithMath.shuntComp(best.B, fMHz.toDouble())
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                NetNode(t("антена", "ant"), "$R${if (X < 0) "−j" else "+j"}${abs(X.toInt())}", "Ω", Modifier.weight(1f))
                NetNode(t("послід.", "series"), "${ser.type} ${fc(ser.value)}", lu(ser.unit, uk), Modifier.weight(1f))
                NetNode(t("парал.", "shunt"), "${sh.type} ${fc(sh.value)}", lu(sh.unit, uk), Modifier.weight(1f))
                NetNode(t("радіо", "radio"), "50", "Ω", Modifier.weight(1f))
            }
            Spacer(Modifier.height(10.dp))
            Advice(t("ЗАЛІЗО", "HW"), t(
                "Додай ${ser.type} ${fc(ser.value)} ${lu(ser.unit, uk)} послідовно + ${sh.type} ${fc(sh.value)} ${lu(sh.unit, uk)} паралельно між антеною і радіо. SMD 0402, якнайближче до виводу антени.",
                "Add ${ser.type} ${fc(ser.value)} ${lu(ser.unit, uk)} in series + ${sh.type} ${fc(sh.value)} ${lu(sh.unit, uk)} in shunt between antenna and radio. SMD 0402, as close to the antenna feed as possible."))
            Advice(t("СТРУМ·ТЕПЛО", "CURRENT"), t(
                "Зараз назад відбивається ${m.prPct.toInt()}% потужності. Узгодження підніме випромінену потужність і прибере зайвий нагрів вихідного каскаду.",
                "Right now ${m.prPct.toInt()}% of power reflects back. Matching raises radiated power and removes the extra heat in the output stage."))
            Advice(t("СОФТ", "SW"), t(
                "До фізичного узгодження — тримай нижчу TX-потужність (менше відбитого струму) і обери канал із кращим RSSI.",
                "Until physical matching — keep lower TX power (less reflected current) and pick a channel with better RSSI."))
        }
    }

    // ---- LIVE MATH ----
    HudCard(t("МАТЕМАТИКА НАЖИВО", "LIVE MATH"),
        "z = ${cx(R.toDouble() / Z0, X.toDouble() / Z0, 2)}") {
        FoldHeader(showMath) { showMath = !showMath }
        if (showMath) {
            Spacer(Modifier.height(4.dp))
            // мемоізуємо рядки — не перебудовувати AnnotatedString щокадру драгу
            val mathRows = remember(R, X, Z0, fMHz, uk) {
                buildMathRows(R.toDouble(), X.toDouble(), Z0.toDouble(), fMHz.toDouble(), m, uk)
            }
            for (row in mathRows) MathRow(row)
        }
    }

    // ---- THEORY ----
    HudCard(t("ДОВІДНИК · ТЕОРІЯ ТА ПРАКТИКА", "MANUAL · THEORY & PRACTICE")) {
        FoldHeader(showTheory) { showTheory = !showTheory }
        if (showTheory) {
            Spacer(Modifier.height(6.dp))
            Th(t("1 · Що таке діаграма Сміта", "1 · What the Smith chart is"), t(
                "Це номограма, що відображає будь-який імпеданс у точку всередині кола. Замість того щоб малювати опір від 0 до ∞, вона показує коефіцієнт відбиття Γ — комплексне число, що завжди вміщається в одиничне коло. Центр = ідеальне узгодження (навантаження = Z₀, нічого не відбивається). Край кола = повне відбиття (вся потужність вертається назад).",
                "It's a nomogram that maps any impedance to a point inside a circle. Instead of drawing resistance from 0 to ∞, it shows the reflection coefficient Γ — a complex number that always fits inside the unit circle. Center = perfect match (load = Z₀, nothing reflects). Circle edge = full reflection (all power comes back)."))
            Th(t("2 · Осі та криві", "2 · Axes and curves"), t(
                "Горизонтальна вісь — чистий опір (реактанс = 0): ліворуч 0 Ω (коротке), праворуч ∞ (розрив), центр = Z₀. Кола R=const (teal) — точки з однаковим активним опором. Дуги X=const (violet) — однаковий реактанс: верхня половина індуктивна (+jX), нижня ємнісна (−jX). Точка навантаження лежить на перетині свого кола R і дуги X — тягни її, і R та X оновляться.",
                "The horizontal axis is pure resistance (reactance = 0): 0 Ω at left (short), ∞ at right (open), Z₀ at center. R=const circles (teal) — points with equal resistance. X=const arcs (violet) — equal reactance: upper half is inductive (+jX), lower is capacitive (−jX). The load point sits where its R-circle and X-arc cross — drag it and R, X update."))
            Th(t("3 · Що означає вивід", "3 · What the readout means"), t(
                "Γ (|Γ|∠θ) — частка амплітуди, що відбивається, і її фаза; 0 = ідеал. VSWR — коефіцієнт стоячої хвилі: 1.0 ідеал, ціль для радіо <2, >3 погано. Return loss (dB) — на скільки дБ відбита хвиля слабша за падаючу; більше = краще (10 dB ок, 20 dB відмінно). Відбита потужність (%) = |Γ|² — скільки TX-потужності вертається у підсилювач. Втрата дальності (mismatch loss, dB) = −10·log₁₀(1−|Γ|²) — на стільки падає реально випромінена потужність.",
                "Γ (|Γ|∠θ) — the fraction of amplitude that reflects, and its phase; 0 = ideal. VSWR — standing-wave ratio: 1.0 ideal, radio target <2, >3 bad. Return loss (dB) — how many dB weaker the reflected wave is; more = better (10 dB ok, 20 dB excellent). Reflected power (%) = |Γ|² — how much TX power returns to the PA. Range loss (mismatch loss, dB) = −10·log₁₀(1−|Γ|²) — how much the actually radiated power drops."))
            Th(t("4 · Узгодження (matching)", "4 · Matching"), t(
                "Мета — перенести точку навантаження в центр двома реактивними елементами (L/C), не витрачаючи потужність (реактивні елементи не гріються). Послідовний елемент рухає точку вздовж кола R=const (додає ±jX). Паралельний — вздовж кола провідності G=const (додає ±jB). Тому мінімальна «L-ланка» — це два кроки: одна дуга R + одна дуга G, які виводять у центр. Кнопка ◈ УЗГОДИТИ малює цю траєкторію.",
                "The goal is to move the load point to the center with two reactive elements (L/C) without spending power (reactive elements don't heat up). A series element moves the point along a constant-R circle (adds ±jX). A shunt element — along a constant-G conductance circle (adds ±jB). So the minimal L-network is two steps: one R-arc + one G-arc that reach the center. The ◈ MATCH button draws this path."))
            Th(t("5 · Практика для наших радіо", "5 · Practice for our radios"), t(
                "CC1101 (868 МГц) і nRF24 (2.44 ГГц) розраховані на 50 Ω. Реальна антена/котушка/доріжка рідко дає рівно 50 Ω — звідси VSWR>1. Наслідки поганого узгодження: менша дальність (частина потужності не випромінюється), зайвий струм і нагрів вихідного каскаду, у крайніх випадках — деградація підсилювача. Практична ціль: VSWR<2 (return loss>10 dB) → втрата дальності <0.5 dB, непомітно.",
                "CC1101 (868 MHz) and nRF24 (2.44 GHz) are designed for 50 Ω. A real antenna/coil/trace rarely gives exactly 50 Ω — hence VSWR>1. Consequences of a poor match: less range (some power isn't radiated), extra current and heat in the output stage, and in extreme cases PA degradation. Practical target: VSWR<2 (return loss>10 dB) → range loss <0.5 dB, unnoticeable."))
            Th(t("6 · Як міряти свій імпеданс", "6 · How to measure your impedance"), t(
                "Щоб ввести реальні числа замість прикладу, імпеданс антени треба виміряти: VNA (векторний аналізатор, напр. NanoVNA — дешевий) на робочій частоті дасть R і X напряму. Без VNA — орієнтуйся на даташит антени/модуля або підбирай L/C ітеративно за RSSI/дальністю.",
                "To enter real numbers instead of the example, the antenna impedance must be measured: a VNA (vector network analyzer, e.g. NanoVNA — cheap) at the working frequency gives R and X directly. Without a VNA — use the antenna/module datasheet or tune L/C iteratively by RSSI/range."))
            Th(t("7 · Компоненти узгодження", "7 · Matching components"), t(
                "Індуктор L: реактанс X = 2πf·L (додатний), номінал у нГн. Конденсатор C: реактанс X = −1/(2πf·C) (від'ємний), номінал у пФ. На 868 МГц / 2.4 ГГц бери SMD 0402 і став якнайближче до виводу антени — довгі доріжки самі стають реактансом і псують розрахунок.",
                "Inductor L: reactance X = 2πf·L (positive), value in nH. Capacitor C: reactance X = −1/(2πf·C) (negative), value in pF. At 868 MHz / 2.4 GHz use SMD 0402 placed as close to the antenna feed as possible — long traces become reactance themselves and spoil the calculation."))
        }
    }
}

/* ============================ RF CALIBRATOR (live) ============================ */
@Composable
fun RfCalibCard(vm: MainViewModel) {
    val uk = LocalLang.current == Lang.UK
    fun t(u: String, e: String) = if (uk) u else e
    LaunchedEffect(vm.connected) { if (vm.connected) vm.loadRf() }
    val rf = vm.rf
    fun jd(o: JsonObject?, k: String) = o?.get(k)?.jsonPrimitive?.doubleOrNull ?: 0.0
    fun ji(o: JsonObject?, k: String) = o?.get(k)?.jsonPrimitive?.intOrNull ?: 0
    fun jb(o: JsonObject?, k: String) = o?.get(k)?.jsonPrimitive?.booleanOrNull ?: false

    HudCard(t("КАЛІБРАТОР · WiFi TX", "CALIBRATOR · WiFi TX"),
        if (rf != null && jb(rf, "connected")) t("аналіз наживо", "live") else t("немає лінка", "no link")) {
        // ---- live module analysis ----
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(9.dp)) {
            StatTile("RSSI", "${ji(rf, "rssi")}", "dBm", if (ji(rf, "rssi") < -75) 1 else 0, Modifier.weight(1f))
            StatTile("TX", f1(jd(rf, "tx_dbm")), "dBm", 0, Modifier.weight(1f))
            StatTile(t("Канал", "Ch"), "${ji(rf, "channel")}", "", 0, Modifier.weight(1f))
            StatTile("T°", f1(jd(rf, "temp")), "°C", if (jd(rf, "temp") > 55) 1 else 0, Modifier.weight(1f))
        }
        Spacer(Modifier.height(10.dp))
        Text(t("Знаходить найнижчу TX-потужність, що тримає лінк → менше струму й нагріву. Змінює ЛИШЕ WiFi-потужність у межах enum драйвера (перевищити рейтинг і спалити — фізично неможливо), таймінги не чіпає, і авто-відкочує, якщо лінк просів.",
               "Finds the lowest TX power that keeps the link → less current and heat. Changes ONLY WiFi power within the driver enum (exceeding the chip rating / burning it is physically impossible), touches no timings, and auto-reverts if the link drops."),
            color = Apex.Ink2, fontFamily = FontFamily.Monospace, fontSize = 11.5.sp)
        Spacer(Modifier.height(6.dp))
        Text(t("ЕКОНОМ — найнижча TX (менше струму/нагріву). БУСТ — максимум (дальність/стабільність, але більший струм і нагрів).",
               "SAVE — lowest TX (less current/heat). BOOST — max (range/stability, but more current and heat)."),
            color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 10.sp)
        Spacer(Modifier.height(8.dp))
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Button({ vm.calibrate() }, Modifier.weight(1f), enabled = vm.connected && !vm.calibBusy) {
                Text(if (vm.calibBusy && vm.calibMode == "optimize") "…" else t("◈ ЕКОНОМ", "◈ SAVE"),
                    fontWeight = FontWeight.SemiBold, fontSize = 12.sp)
            }
            OutlinedButton({ vm.boost() }, Modifier.weight(1f), enabled = vm.connected && !vm.calibBusy) {
                Text(if (vm.calibBusy && vm.calibMode == "boost") "…" else t("⤒ БУСТ", "⤒ BOOST"),
                    fontWeight = FontWeight.SemiBold, fontSize = 12.sp)
            }
        }
        if (vm.calibMsg.isNotEmpty()) Text(vm.calibMsg, color = Apex.Accent, fontFamily = FontFamily.Monospace,
            fontSize = 11.sp, modifier = Modifier.padding(top = 5.dp))

        // ---- result: old vs new + plain explanation ----
        vm.calibResult?.let { r ->
            val reverted = jb(r, "reverted"); val old = jd(r, "old_dbm"); val neu = jd(r, "new_dbm")
            val rssiA = ji(r, "rssi_after")
            Spacer(Modifier.height(12.dp))
            Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                NetNode(t("було", "was"), f1(old), "dBm", Modifier.weight(1f))
                Text("→", color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 16.sp)
                NetNode(t("стало", "now"), f1(neu), "dBm", Modifier.weight(1f))
            }
            Spacer(Modifier.height(8.dp))
            val boost = vm.calibMode == "boost"
            val expl = when {
                reverted -> t("Відкат: зниження TX розірвало б лінк — залишив старе ${f1(old)} dBm. Нічого не зіпсовано.",
                              "Reverted: lowering TX would drop the link — kept ${f1(old)} dBm. Nothing broken.")
                boost && neu > old -> t("БУСТ: TX ${f1(old)}→${f1(neu)} dBm — максимум для дальності та стабільності. Ціна: більший струм і нагрів; стеж за T°.",
                                        "BOOST: TX ${f1(old)}→${f1(neu)} dBm — max for range and stability. Cost: more current and heat; watch T°.")
                boost -> t("Уже на максимумі (${f1(old)} dBm) — вище безпечно неможливо.", "Already at max (${f1(old)} dBm) — no safe headroom above.")
                neu < old -> t("TX ${f1(old)}→${f1(neu)} dBm: менша потужність → менший струм і нагрів; лінк збережено (RSSI ${rssiA} dBm).",
                               "TX ${f1(old)}→${f1(neu)} dBm: less power → less current and heat; link kept (RSSI ${rssiA} dBm).")
                neu > old -> t("TX ${f1(old)}→${f1(neu)} dBm: підняв потужність — лінк був слабкий, тепер більше запасу надійності.",
                               "TX ${f1(old)}→${f1(neu)} dBm: raised power — link was weak, now more reliability margin.")
                else -> t("Уже оптимально (${f1(old)} dBm) — змін не потрібно.", "Already optimal (${f1(old)} dBm) — no change needed.")
            }
            Text(expl, color = if (reverted || boost) Apex.Warn else Apex.Accent, fontFamily = FontFamily.Monospace, fontSize = 12.sp)
        }
    }
}

/* ============================ LIVE MATH ROWS ============================ */
private data class MRow(val step: String, val eq: AnnotatedString, val why: String)

private fun eq(vararg seg: Pair<String, Int>) = buildAnnotatedString {
    for ((txt, s) in seg) when (s) {
        1 -> withStyle(SpanStyle(color = Apex.Accent, fontWeight = FontWeight.Bold)) { append(txt) }
        2 -> withStyle(SpanStyle(color = Apex.Muted)) { append(txt) }
        else -> withStyle(SpanStyle(color = Apex.Ink)) { append(txt) }
    }
}

private fun buildMathRows(R: Double, X: Double, Z0: Double, f: Double, m: SmithMath.Metrics, uk: Boolean): List<MRow> {
    fun t(u: String, e: String) = if (uk) u else e
    val zr = R / Z0; val zi = X / Z0; val g = m.g; val mag = m.mag; val ang = m.angDeg
    val rows = mutableListOf<MRow>()
    val sign = if (zi >= 0) "+" else "−"

    // 1 — коментар читає нормалізовані частини (як smith.html)
    val rWord = if (zr > 1.05) t("вище за Z₀ (${R.toInt()} > ${Z0.toInt()} Ω)", "above Z₀ (${R.toInt()} > ${Z0.toInt()} Ω)")
                else if (zr < 0.95) t("нижче за Z₀ (${R.toInt()} < ${Z0.toInt()} Ω)", "below Z₀ (${R.toInt()} < ${Z0.toInt()} Ω)")
                else t("≈ Z₀", "≈ Z₀")
    val xWord = if (zi > 0.02) t("індуктивний, +j${abs(X.toInt())} Ω", "inductive, +j${abs(X.toInt())} Ω")
                else if (zi < -0.02) t("ємнісний, −j${abs(X.toInt())} Ω", "capacitive, −j${abs(X.toInt())} Ω")
                else t("відсутній (чисто активне)", "none (purely resistive)")
    val concl = if (abs(zr - 1) < 0.05 && abs(zi) < 0.05) t("Практично 1 → точка в центрі.", "≈ 1 → the point is at the center.")
                else t("Обидві частини не 1/0 → точка зміщена від центру.", "Neither part is 1/0 → the point is offset from center.")
    rows.add(MRow("1 · " + t("Нормалізація імпедансу", "Impedance normalization"),
        eq("z = (R+jX)/Z₀ = " to 0, cx(zr, zi) to 1),
        t("Активна частина ${f2(zr)} — опір $rWord. Реактивна $sign${f2(abs(zi))} — $xWord. $concl",
          "Real part ${f2(zr)} — resistance $rWord. Imag $sign${f2(abs(zi))} — $xWord. $concl")))

    // 2 — коментар читає |Γ| і фазу
    val amp = mag * 100; val pw = mag * mag * 100
    val gWord = if (mag < 0.1) t("зовсім мало", "very little") else if (mag < 0.33) t("помірно", "moderate") else if (mag < 0.6) t("багато", "a lot") else t("майже все", "almost all")
    val half = if (g.im > 0.01) t("верхню половину діаграми", "the upper half of the chart") else if (g.im < -0.01) t("нижню половину", "the lower half") else t("вісь", "the axis")
    rows.add(MRow("2 · " + t("Коефіцієнт відбиття Γ", "Reflection coefficient Γ"),
        eq("Γ = (z−1)/(z+1) = " to 0, cx(g.re, g.im) to 1, " = ${f3(mag)}∠${ang.toInt()}°" to 1),
        t("|Γ| = ${f2(mag)} → назад вертається ${amp.toInt()}% амплітуди (${pw.toInt()}% потужності) — відбиття $gWord. Фаза ${ang.toInt()}° кладе точку в $half.",
          "|Γ| = ${f2(mag)} → ${amp.toInt()}% of amplitude (${pw.toInt()}% of power) reflects back — $gWord. Phase ${ang.toInt()}° puts the point in $half.")))

    // 3 — вердикт для ЦЬОГО vswr
    val vv = m.vswr
    val vWord = if (vv < 1.5) t("відмінно, майже ідеал", "excellent, near ideal")
                else if (vv < 2) t("у нормі для радіо (ціль <2 виконана)", "within radio target (goal <2 met)")
                else if (vv < 3) t("прийнятно, але вже варто узгодити", "acceptable, but matching is advisable")
                else t("погано — узгодження обовʼязкове", "poor — matching required")
    rows.add(MRow("3 · " + t("VSWR (стояча хвиля)", "VSWR (standing wave)"),
        eq("VSWR = (1+|Γ|)/(1−|Γ|) = (1+${f3(mag)})/(1−${f3(mag)}) = " to 0, inf(vv) to 1),
        "${inf(vv)} → $vWord."))

    // 4 — RL + відбита %
    val rlWord = if (m.rlDb > 20) t("відмінне узгодження", "excellent match") else if (m.rlDb > 14) t("добре", "good")
                 else if (m.rlDb > 10) t("на межі норми", "borderline") else t("замало — багато відбивається", "too low — much is reflected")
    rows.add(MRow("4 · " + t("Return loss і відбита потужність", "Return loss and reflected power"),
        eq("RL = −20·log₁₀|Γ| = " to 0, "${inf(m.rlDb, 1)} dB" to 1, "  ·  ${t("Pвідб", "Prefl")} = |Γ|² = " to 0, "${f1(m.prPct)} %" to 1),
        t("RL ${inf(m.rlDb, 1)} dB → $rlWord: у підсилювач марно вертається ${m.prPct.toInt()}% потужності замість антени.",
          "RL ${inf(m.rlDb, 1)} dB → $rlWord: ${m.prPct.toInt()}% of power is wasted back into the PA instead of the antenna.")))

    // 5 — mismatch loss у практичних термінах
    val notRad = (1 - 10.0.pow(-m.mlDb / 10)) * 100
    val mWord = if (m.mlDb < 0.5) t("практично непомітно на дальності", "practically unnoticeable for range")
                else if (m.mlDb < 1) t("невелика, але вже помітна втрата", "a small but noticeable loss")
                else if (m.mlDb < 3) t("помітно ріже дальність", "noticeably cuts range") else t("сильно ріже дальність", "heavily cuts range")
    rows.add(MRow("5 · " + t("Втрата на розсогласуванні", "Mismatch loss"),
        eq("ML = −10·log₁₀(1 − |Γ|²) = −10·log₁₀(${f3(1 - mag * mag)}) = " to 0, "${inf(m.mlDb)} dB" to 1),
        t("Недовипромінюється ${if (notRad.isFinite()) notRad.toInt() else 0}% потужності → $mWord.",
          "Under-radiates ${if (notRad.isFinite()) notRad.toInt() else 0}% of power → $mWord.")))

    // 6 — пояснює ЧОМУ саме цей компонент/номінал
    val s = SmithMath.bestMatch(R, X, Z0)
    if (s != null) {
        val ser = SmithMath.seriesComp(s.Xs, f); val sh = SmithMath.shuntComp(s.B, f)
        val serrx = if (s.Xs >= 0) "L = X / 2πf" else "C = −1 / 2πfX"
        val serWhy = if (s.Xs >= 0) t("потрібно ПІДНЯТИ реактанс на +${f1(s.Xs)} Ω → тому індуктор ${fc(ser.value)} нГн",
                                      "need to RAISE reactance by +${f1(s.Xs)} Ω → hence an inductor ${fc(ser.value)} nH")
                     else t("потрібно ОПУСТИТИ реактанс на ${f1(s.Xs)} Ω → тому конденсатор ${fc(ser.value)} пФ",
                            "need to LOWER reactance by ${f1(s.Xs)} Ω → hence a capacitor ${fc(ser.value)} pF")
        rows.add(MRow("6 · L-" + t("ланка", "network") + " → 50 Ω (${s.topo})",
            eq("${t("Xпосл", "Xser")} = ${f1(s.Xs)} Ω → $serrx = " to 0, "${ser.type} ${fc(ser.value)} ${lu(ser.unit, uk)}" to 1),
            t("Послідовно (${f.toInt()} МГц): $serWhy. Рухає точку вздовж кола R.",
              "In series (${f.toInt()} MHz): $serWhy. Moves the point along the constant-R circle.")))
        val shWhy = if (s.B >= 0) t("додаємо ємнісну провідність +${f2(s.B * 1000)} мСм → конденсатор ${fc(sh.value)} пФ паралельно",
                                    "add capacitive susceptance +${f2(s.B * 1000)} mS → capacitor ${fc(sh.value)} pF in shunt")
                    else t("додаємо індуктивну провідність ${f2(s.B * 1000)} мСм → котушка ${fc(sh.value)} нГн паралельно",
                           "add inductive susceptance ${f2(s.B * 1000)} mS → coil ${fc(sh.value)} nH in shunt")
        rows.add(MRow("    " + t("паралельний елемент", "shunt element"),
            eq("${t("Bпар", "Bsh")} = ${f2(s.B * 1000)} ${lu("мСм", uk)} → " to 0, "${sh.type} ${fc(sh.value)} ${lu(sh.unit, uk)}" to 1),
            t("Паралельно: $shWhy. Доводить точку в центр — VSWR стане ≈ 1.",
              "In shunt: $shWhy. Brings the point to the center — VSWR becomes ≈ 1.")))
    } else {
        rows.add(MRow("6 · " + t("Узгодження", "Matching"),
            eq(t("z ≈ 1 — навантаження вже узгоджене", "z ≈ 1 — the load is already matched") to 1),
            t("Точка вже в центрі (VSWR ${inf(vv)}) — L-ланка не потрібна.",
              "The point is already at the center (VSWR ${inf(vv)}) — no L-network needed.")))
    }
    return rows
}

/* ---- динамічне трактування: рядки читаються з поточних величин ---- */
private fun buildInterp(m: SmithMath.Metrics, uk: Boolean): List<Pair<String, String>> {
    fun t(u: String, e: String) = if (uk) u else e
    val out = mutableListOf<Pair<String, String>>()
    val v = m.vswr
    out.add(when {
        v < 1.5 -> "✓" to t("Точка практично в центрі — чудове узгодження. Майже вся потужність іде в антену.",
                            "Point practically at center — excellent match. Almost all power reaches the antenna.")
        v < 2 -> "✓" to t("Точка близько до центру — робоче узгодження (VSWR ${inf(v)}). Для радіо цього досить.",
                          "Point near center — working match (VSWR ${inf(v)}). Good enough for radio.")
        v < 3 -> "!" to t("Помітне зміщення від центру (VSWR ${inf(v)}) — варто узгодити L-ланкою.",
                          "Noticeable offset (VSWR ${inf(v)}) — better to match with an L-network.")
        else -> "✕" to t("Точка далеко від центру (VSWR ${inf(v)}) — сильне розсогласування, узгодження обовʼязкове.",
                         "Point far from center (VSWR ${inf(v)}) — strong mismatch, matching required.")
    })
    out.add("↩" to t("${m.prPct.toInt()}% TX-потужності відбивається назад у підсилювач замість випромінення.",
                     "${m.prPct.toInt()}% of TX power reflects back into the PA instead of radiating."))
    out.add("⤓" to t("Реально випромінена потужність нижча на ${inf(m.mlDb)} dB — це і є втрата дальності.",
                     "Actually radiated power is ${inf(m.mlDb)} dB lower — that is the range loss."))
    val side = when {
        m.g.im > 0.01 -> t("у верхній половині → навантаження індуктивне (+jX): компенсуй ємністю.",
                           "in the upper half → load is inductive (+jX): compensate with capacitance.")
        m.g.im < -0.01 -> t("у нижній половині → навантаження ємнісне (−jX): компенсуй індуктивністю.",
                            "in the lower half → load is capacitive (−jX): compensate with inductance.")
        else -> t("на осі → навантаження чисто активне.", "on the axis → load is purely resistive.")
    }
    out.add("◈" to t("Точка $side", "Point $side"))
    return out
}

/* ============================ SMALL COMPONENTS ============================ */
@Composable private fun Legend(c: Color, label: String) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Box(Modifier.size(14.dp, 3.dp).background(c, RoundedCornerShape(2.dp)))
        Spacer(Modifier.width(5.dp))
        Text(label, color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 10.sp)
    }
}

@Composable private fun SmallBtn(label: String, active: Boolean, onClick: () -> Unit) {
    val c = if (active) Apex.Accent else Apex.Edge
    Text(label, color = if (active) Apex.Accent else Apex.Ink2, fontFamily = FontFamily.Monospace, fontSize = 12.sp,
        modifier = Modifier.border(1.dp, c, RoundedCornerShape(6.dp))
            .background(if (active) Apex.AccentSoft else Apex.Panel2, RoundedCornerShape(6.dp))
            .clickable { onClick() }.padding(horizontal = 10.dp, vertical = 6.dp))
}

@Composable private fun Preset(label: String, active: Boolean, onClick: () -> Unit) {
    Text(label, color = if (active) Apex.Accent else Apex.Ink2, fontFamily = FontFamily.Monospace, fontSize = 11.sp,
        modifier = Modifier.border(1.dp, if (active) Apex.Accent else Apex.Edge, RoundedCornerShape(6.dp))
            .background(if (active) Apex.AccentSoft else Apex.Panel2, RoundedCornerShape(6.dp))
            .clickable { onClick() }.padding(horizontal = 9.dp, vertical = 6.dp))
}

@Composable private fun SliderRow(label: String, value: String, v: Float, from: Float, to: Float, onChange: (Float) -> Unit) {
    Column(Modifier.padding(vertical = 3.dp)) {
        Row(Modifier.fillMaxWidth()) {
            Text(label, color = Apex.Ink2, fontFamily = FontFamily.Monospace, fontSize = 12.sp, modifier = Modifier.weight(1f))
            Text(value, color = Apex.Ink, fontFamily = FontFamily.Monospace, fontSize = 12.sp, fontWeight = FontWeight.Bold)
        }
        Slider(value = v, onValueChange = onChange, valueRange = from..to,
            colors = SliderDefaults.colors(thumbColor = Apex.Accent, activeTrackColor = Apex.Accent, inactiveTrackColor = Apex.Edge))
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable private fun NumField(label: String, v: Float, modifier: Modifier = Modifier, onChange: (Float) -> Unit) {
    OutlinedTextField(
        value = if (v == v.toInt().toFloat()) v.toInt().toString() else v.toString(),
        onValueChange = { it.toFloatOrNull()?.let(onChange) },
        label = { Text(label, fontSize = 11.sp) }, singleLine = true, modifier = modifier,
        textStyle = androidx.compose.ui.text.TextStyle(fontFamily = FontFamily.Monospace, fontSize = 13.sp),
    )
}

@Composable private fun NetNode(cap: String, value: String, unit: String, modifier: Modifier = Modifier) {
    Column(modifier.background(Apex.Panel2, RoundedCornerShape(8.dp))
        .border(1.dp, Apex.Edge, RoundedCornerShape(8.dp)).padding(vertical = 7.dp, horizontal = 5.dp),
        horizontalAlignment = Alignment.CenterHorizontally) {
        Text(cap, color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 9.sp)
        Text(value, color = Apex.Accent, fontFamily = FontFamily.Monospace, fontSize = 13.sp, fontWeight = FontWeight.Bold)
        Text(unit, color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 9.sp)
    }
}

@Composable private fun Advice(level: String, text: String) {
    Column(Modifier.fillMaxWidth().padding(vertical = 5.dp)) {
        Text(level, color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 10.sp, letterSpacing = 1.sp)
        Text(text, color = Apex.Ink2, fontFamily = FontFamily.Monospace, fontSize = 12.5.sp, modifier = Modifier.padding(top = 2.dp))
    }
}

@Composable private fun MathRow(r: MRow) {
    Column(Modifier.fillMaxWidth().padding(vertical = 7.dp)) {
        Text(r.step, color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 10.sp, letterSpacing = 1.sp)
        Text(r.eq, fontFamily = FontFamily.Monospace, fontSize = 13.sp, modifier = Modifier.padding(top = 4.dp))
        Text(r.why, color = Apex.Ink2, fontFamily = FontFamily.Monospace, fontSize = 11.5.sp, modifier = Modifier.padding(top = 4.dp))
        Spacer(Modifier.height(6.dp))
        Box(Modifier.fillMaxWidth().height(1.dp).background(Apex.Edge))
    }
}

@Composable private fun FoldHeader(open: Boolean, onToggle: () -> Unit) {
    Text(if (open) "▾ ${if (LocalLang.current == Lang.UK) "згорнути" else "collapse"}" else "▸ ${if (LocalLang.current == Lang.UK) "розгорнути" else "expand"}",
        color = Apex.Accent, fontFamily = FontFamily.Monospace, fontSize = 11.sp,
        modifier = Modifier.clickable { onToggle() }.padding(vertical = 2.dp))
}

@Composable private fun Th(head: String, body: String) {
    Column(Modifier.fillMaxWidth().padding(bottom = 8.dp)) {
        Text(head, color = Apex.Accent, fontFamily = FontFamily.Monospace, fontSize = 12.sp, fontWeight = FontWeight.SemiBold)
        Text(body, color = Apex.Ink2, fontFamily = FontFamily.Monospace, fontSize = 12.5.sp, modifier = Modifier.padding(top = 3.dp))
    }
}

/* ============================ CANVAS DRAW ============================ */
private fun drawSmith(ds: DrawScope, load: Cplx, traj: List<Pair<Float, Float>>, prog: Float, sweep: Cplx?) {
    with(ds) {
        val w = size.width; val cxp = w / 2f; val cyp = size.height / 2f; val rad = w * 0.44f
        fun xy(re: Float, im: Float) = Offset(cxp + re * rad, cyp - im * rad)

        // VSWR zone rings (fill)
        val rings = listOf(1.5 to Color(0x0F00FFAB), 2.0 to Color(0x0C7DD8B0), 3.0 to Color(0x0DFDD036), 99.0 to Color(0x0BFF5A57))
        for ((v, c) in rings) { val mm = (if (v >= 99) 1.0 else (v - 1) / (v + 1)).toFloat(); drawCircle(c, mm * rad, Offset(cxp, cyp)) }

        val clip = Path().apply { addOval(Rect(cxp - rad, cyp - rad, cxp + rad, cyp + rad)) }
        clipPath(clip) {
            for (r in listOf(0f, .2f, .5f, 1f, 2f, 5f)) {
                val c = r / (r + 1); val rr = 1 / (r + 1)
                drawCircle(RCurve.copy(alpha = .28f), rr * rad, Offset(cxp + c * rad, cyp), style = Stroke(1f))
            }
            for (x in listOf(.2f, .5f, 1f, 2f, 5f)) for (s in listOf(1f, -1f)) {
                drawCircle(XCurve.copy(alpha = .26f), (1 / x) * rad, Offset(cxp + rad, cyp - s * (1 / x) * rad), style = Stroke(1f))
            }
            drawLine(Color(0x389FB4AE), Offset(cxp - rad, cyp), Offset(cxp + rad, cyp), strokeWidth = 1f)
        }
        drawCircle(Apex.Accent.copy(alpha = .5f), rad, Offset(cxp, cyp), style = Stroke(1.4f))
        drawCircle(Apex.Accent.copy(alpha = .5f), 2.4f, Offset(cxp, cyp))

        if (traj.isNotEmpty() && prog > 0f) {
            val n = (traj.size * prog).toInt().coerceAtLeast(2)
            val p = Path()
            for (i in 0 until n) { val (re, im) = traj[i]; val o = xy(re, im); if (i == 0) p.moveTo(o.x, o.y) else p.lineTo(o.x, o.y) }
            drawPath(p, Apex.Warn, style = Stroke(2f))
        }
        if (sweep != null) { val o = xy(sweep.re.toFloat(), sweep.im.toFloat()); drawCircle(Apex.Warn.copy(alpha = .9f), 4f, o) }

        val o = xy(load.re.toFloat(), load.im.toFloat())
        drawCircle(Apex.Accent.copy(alpha = .35f), 11f, o)
        drawCircle(Apex.Accent, 5.5f, o)
    }
}
