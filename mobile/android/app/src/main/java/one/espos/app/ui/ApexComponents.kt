package one.espos.app.ui

/** Багаторазові APEX-HUD компоненти + графіки (Canvas). Уся важка візуалізація тут —
 *  плата віддає числа, телефон малює. */
import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.setValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.lerp
import androidx.compose.ui.graphics.nativeCanvas
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

/** HUD-картка: моно uppercase заголовок + hairline + вміст. */
@Composable
fun HudCard(title: String, subtitle: String = "", content: @Composable ColumnScope.() -> Unit) {
    Column(
        Modifier.fillMaxWidth().padding(bottom = 12.dp)
            .background(Apex.Panel, RoundedCornerShape(12.dp))
            .border(1.dp, Apex.Edge, RoundedCornerShape(12.dp))
            .padding(14.dp)
    ) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.Bottom) {
            Text("[ $title ]", color = Apex.Accent, fontFamily = FontFamily.Monospace,
                fontWeight = FontWeight.SemiBold, fontSize = 12.sp, letterSpacing = 1.5.sp)
            Spacer(Modifier.weight(1f))
            if (subtitle.isNotEmpty()) Text(subtitle, color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 10.sp)
        }
        Spacer(Modifier.height(6.dp))
        Box(Modifier.fillMaxWidth().height(1.dp).background(Apex.Edge))
        Spacer(Modifier.height(10.dp))
        content()
    }
}

/** Сигнатурний рядок APEX: LABEL ········ VALUE [TAG]. */
@Composable
fun LeaderRow(label: String, value: String, tag: String? = null, level: Int = 0) {
    Row(Modifier.fillMaxWidth().padding(vertical = 4.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(label.uppercase(), color = Apex.Ink2, fontFamily = FontFamily.Monospace, fontSize = 12.sp)
        Text(" " + "·".repeat(40), color = Apex.Edge, fontFamily = FontFamily.Monospace, fontSize = 12.sp,
            maxLines = 1, overflow = TextOverflow.Clip, modifier = Modifier.weight(1f))
        Text(value, color = Apex.Ink, fontFamily = FontFamily.Monospace, fontSize = 13.sp, fontWeight = FontWeight.Medium)
        if (tag != null) {
            Spacer(Modifier.width(6.dp))
            StatusTag(tag, level)
        }
    }
}

/** Іконка-дія з ПОВНОЦІННИМ тач-таргетом (≥44dp) + ripple. Гліфи ✕ › 📌 ⬇ ↻ тощо
 *  раніше мали крихітну зону натиску (лише розмір тексту) — стандарт Material/HIG. */
@Composable
fun TapIcon(glyph: String, onClick: () -> Unit,
            tint: Color = Apex.Ink2, fontSize: androidx.compose.ui.unit.TextUnit = 16.sp) {
    Box(Modifier.sizeIn(minWidth = MinTouch, minHeight = MinTouch)
        .clickable(onClick = onClick), contentAlignment = Alignment.Center) {
        Text(glyph, color = tint, fontFamily = FontFamily.Monospace, fontSize = fontSize)
    }
}

/** Bracket-тег статусу [ GO ] / [ WARN ] / [ FAIL ]. */
@Composable
fun StatusTag(text: String, level: Int) {
    val c = statusColor(level)
    Text("[ ${text.uppercase()} ]", color = c, fontFamily = FontFamily.Monospace, fontSize = 10.sp,
        modifier = Modifier.border(1.dp, c, RoundedCornerShape(4.dp)).padding(horizontal = 5.dp, vertical = 1.dp))
}

/** Великий hero-метрик (табличні цифри). */
@Composable
fun StatTile(label: String, value: String, unit: String = "", level: Int = 0, modifier: Modifier = Modifier) {
    Column(modifier.background(Apex.Panel2, RoundedCornerShape(10.dp))
        .border(1.dp, Apex.Edge, RoundedCornerShape(10.dp)).padding(12.dp)) {
        Text(label.uppercase(), color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 10.sp, letterSpacing = 1.sp)
        Row(verticalAlignment = Alignment.Bottom) {
            Text(value, color = statusColor(level), fontFamily = FontFamily.Monospace, fontWeight = FontWeight.Bold, fontSize = 26.sp)
            if (unit.isNotEmpty()) Text(" $unit", color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 12.sp)
        }
    }
}

/** Сегментний бар-метр (18 сегментів), warn/bad пороги у %. */
@Composable
fun SegBar(pct: Int, warnAt: Int = 60, badAt: Int = 85) {
    val segs = 18
    val on = (pct.coerceIn(0, 100) * segs) / 100
    Row(Modifier.fillMaxWidth().height(14.dp), horizontalArrangement = Arrangement.spacedBy(2.dp)) {
        for (i in 0 until segs) {
            val p = (i + 1) * 100 / segs
            val col = when {
                i >= on -> Apex.Edge
                p >= badAt -> Apex.Bad
                p >= warnAt -> Apex.Warn
                else -> Apex.Accent
            }
            Box(Modifier.weight(1f).fillMaxHeight().background(col, RoundedCornerShape(1.dp)))
        }
    }
}

/** Спектр-бар-графік (dBm per точка). Уся ширина екрана — куди багатше за 240px TFT. */
@Composable
fun SpectrumChart(db: List<Int>, floor: Int = -120, top: Int = -30, height: Int = 130) {
    Canvas(Modifier.fillMaxWidth().height(height.dp)) {
        if (db.isEmpty()) return@Canvas
        val n = db.size
        val bw = size.width / n
        val range = (top - floor).toFloat().coerceAtLeast(1f)
        for (i in 0 until n) {
            val v = db[i]
            if (v <= floor) continue
            val h = ((v - floor) / range) * size.height
            val x = i * bw
            val col = when {
                v > -60 -> Apex.Bad
                v > -85 -> Apex.Accent
                else -> Color(0xFF07C08A)
            }
            drawRect(col, topLeft = Offset(x, size.height - h), size = androidx.compose.ui.geometry.Size(bw.coerceAtLeast(1f), h))
        }
    }
}

/** Колір waterfall за нормалізованою амплітудою t (0..1): темний→зелений→бурштин→червоний. */
private fun waterfallColor(t: Float): Color = when {
    t < 0.02f -> Apex.Grid
    t < 0.4f  -> lerp(Color(0xFF06251A), Apex.Accent, t / 0.4f)
    t < 0.7f  -> lerp(Apex.Accent, Apex.Warn, (t - 0.4f) / 0.3f)
    else      -> lerp(Apex.Warn, Apex.Bad, ((t - 0.7f) / 0.3f).coerceIn(0f, 1f))
}

/** RF-waterfall: історія спектрів як теплокарта (новіші зверху, час↓, частота→, колір=амплітуда). */
@Composable
fun WaterfallChart(history: List<List<Int>>, floor: Int, top: Int, height: Int = 150) {
    Canvas(Modifier.fillMaxWidth().height(height.dp)) {
        if (history.isEmpty()) return@Canvas
        val rows = history.size
        val rowH = size.height / rows
        val range = (top - floor).toFloat().coerceAtLeast(1f)
        // newest зверху -> малюємо з кінця списку
        history.asReversed().forEachIndexed { r, row ->
            if (row.isEmpty()) return@forEachIndexed
            val cw = size.width / row.size
            val y = r * rowH
            row.forEachIndexed { c, v ->
                val tt = ((v - floor) / range).coerceIn(0f, 1f)
                drawRect(waterfallColor(tt), topLeft = Offset(c * cw, y),
                    size = androidx.compose.ui.geometry.Size(cw + 0.6f, rowH + 0.6f))
            }
        }
    }
}

/** Лінійний графік історії (напр. батарея mV / RSSI dBm), фіксована шкала. */
@Composable
fun LineChart(points: List<Int>, lo: Int, hi: Int, height: Int = 90, color: Color = Apex.Accent) {
    Canvas(Modifier.fillMaxWidth().height(height.dp)) {
        if (points.size < 2) return@Canvas
        val range = (hi - lo).toFloat().coerceAtLeast(1f)
        val dx = size.width / (points.size - 1)
        // сітка
        for (g in 0..3) {
            val y = size.height * g / 3
            drawLine(Apex.Grid, Offset(0f, y), Offset(size.width, y), 1f)
        }
        var prev: Offset? = null
        points.forEachIndexed { i, p ->
            val v = p.coerceIn(lo, hi)
            val x = i * dx
            val y = size.height * (hi - v) / range
            val cur = Offset(x, y)
            prev?.let { drawLine(color, it, cur, 2.5f, cap = StrokeCap.Round) }
            prev = cur
        }
    }
}

/** Радіальний гейдж (напр. температура/споживання): дуга 240° + велике число в центрі. */
@Composable
fun RadialGauge(value: Float, min: Float, max: Float, unit: String, level: Int = 0, size: Int = 130) {
    val col = statusColor(level)
    Box(Modifier.fillMaxWidth().height(size.dp), contentAlignment = Alignment.Center) {
        Canvas(Modifier.fillMaxSize()) {
            val stroke = 12f
            val r = (kotlin.math.min(this.size.width, this.size.height) - stroke) / 2f
            val cx = this.size.width / 2f; val cy = this.size.height / 2f
            val start = 150f; val sweep = 240f
            // фон-дуга
            drawArc(Apex.Edge, start, sweep, false,
                topLeft = Offset(cx - r, cy - r), size = androidx.compose.ui.geometry.Size(r * 2, r * 2),
                style = androidx.compose.ui.graphics.drawscope.Stroke(stroke, cap = StrokeCap.Round))
            // значення
            val frac = ((value - min) / (max - min)).coerceIn(0f, 1f)
            drawArc(col, start, sweep * frac, false,
                topLeft = Offset(cx - r, cy - r), size = androidx.compose.ui.geometry.Size(r * 2, r * 2),
                style = androidx.compose.ui.graphics.drawscope.Stroke(stroke, cap = StrokeCap.Round))
        }
        Column(horizontalAlignment = Alignment.CenterHorizontally) {
            Text("%.0f".format(value), color = col, fontFamily = FontFamily.Monospace, fontWeight = FontWeight.Bold, fontSize = 30.sp)
            Text(unit, color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 11.sp)
        }
    }
}

/** Wireframe-блок модуля (APEX line-art): рамка з кутовими тік-мітками, назва в центрі,
 *  підписані піни ліворуч/праворуч із конекторними точками на краю. Плата віддає лише
 *  назви — усе креслення рахує телефон. */
@Composable
fun WireframeBlock(title: String, subtitle: String = "",
                   pinsLeft: List<String> = emptyList(), pinsRight: List<String> = emptyList()) {
    val rows = maxOf(pinsLeft.size, pinsRight.size, 1)
    val h = (34 + rows * 18).dp
    Box(Modifier.fillMaxWidth().height(h).padding(vertical = 4.dp)) {
        Canvas(Modifier.fillMaxSize()) {
            val w = size.width; val ht = size.height; val tick = 9f
            // рамка
            drawRoundRect(Apex.Edge, size = androidx.compose.ui.geometry.Size(w, ht),
                cornerRadius = androidx.compose.ui.geometry.CornerRadius(6f, 6f),
                style = androidx.compose.ui.graphics.drawscope.Stroke(1.5f))
            // кутові тік-мітки (акцент)
            val c = Apex.Accent
            for ((cx, cy, sx, sy) in listOf(
                arrayOf(0f, 0f, 1f, 1f), arrayOf(w, 0f, -1f, 1f),
                arrayOf(0f, ht, 1f, -1f), arrayOf(w, ht, -1f, -1f))) {
                drawLine(c, Offset(cx, cy), Offset(cx + tick * sx, cy), 1.5f)
                drawLine(c, Offset(cx, cy), Offset(cx, cy + tick * sy), 1.5f)
            }
        }
        // назва + підзаголовок по центру
        Column(Modifier.align(Alignment.Center), horizontalAlignment = Alignment.CenterHorizontally) {
            Text(title, color = Apex.Ink, fontFamily = FontFamily.Monospace, fontWeight = FontWeight.SemiBold,
                fontSize = 13.sp, letterSpacing = 1.sp)
            if (subtitle.isNotEmpty())
                Text(subtitle, color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 9.sp)
        }
        // піни ліворуч (акцент-крапка = конектор на краю)
        Column(Modifier.align(Alignment.CenterStart).padding(start = 6.dp)) {
            pinsLeft.forEach { Text("• $it", color = Apex.Ink2, fontFamily = FontFamily.Monospace, fontSize = 10.sp,
                modifier = Modifier.height(18.dp)) }
        }
        // піни праворуч
        Column(Modifier.align(Alignment.CenterEnd).padding(end = 6.dp), horizontalAlignment = Alignment.End) {
            pinsRight.forEach { Text("$it •", color = Apex.Ink2, fontFamily = FontFamily.Monospace, fontSize = 10.sp,
                modifier = Modifier.height(18.dp)) }
        }
    }
}

/** Шина-рейка: горизонтальна акцент-лінія з підписаними відводами (напр. spільний HSPI).
 *  Показує, що кілька пристроїв висять на одній лінії. */
@Composable
fun BusRail(rail: String, taps: List<String>) {
    Column(Modifier.fillMaxWidth().padding(vertical = 4.dp)) {
        Text(rail, color = Apex.Accent, fontFamily = FontFamily.Monospace, fontSize = 10.sp, letterSpacing = 1.sp)
        Box(Modifier.fillMaxWidth().height(28.dp)) {
            Canvas(Modifier.fillMaxSize()) {
                val w = size.width; val y = 6f
                drawLine(Apex.Accent, Offset(0f, y), Offset(w, y), 2f)           // рейка
                val n = taps.size.coerceAtLeast(1)
                for (i in 0 until taps.size) {
                    val x = w * (i + 0.5f) / n
                    drawLine(Apex.Edge, Offset(x, y), Offset(x, size.height), 1.5f)  // відвід
                    drawCircle(Apex.Accent, 3f, Offset(x, y))
                }
            }
            Row(Modifier.fillMaxWidth().align(Alignment.BottomCenter),
                horizontalArrangement = Arrangement.SpaceAround) {
                taps.forEach { Text(it, color = Apex.Ink2, fontFamily = FontFamily.Monospace, fontSize = 9.sp) }
            }
        }
    }
}

/** Секція довідки: uppercase-лейбл + текст. */
@Composable
private fun HelpBlock(label: String, text: String) {
    if (text.isBlank()) return
    Column(Modifier.padding(bottom = 8.dp)) {
        Text(label.uppercase(), color = Apex.Accent, fontFamily = FontFamily.Monospace, fontSize = 10.sp, letterSpacing = 1.sp)
        Text(text, color = Apex.Ink2, fontFamily = FontFamily.Monospace, fontSize = 12.sp)
    }
}

/** Контекстна довідка інструмента (що/як/вивід + значення полів), локалізована. */
@Composable
fun ToolHelpCard(key: String) {
    val lang = LocalLang.current
    val h = toolHelp(key, lang) ?: return
    val uk = lang == Lang.UK
    HudCard(if (uk) "ДОВІДКА" else "HELP", h.title) {
        HelpBlock(if (uk) "Що робить" else "What", h.what)
        HelpBlock(if (uk) "Як" else "Use", h.use)
        HelpBlock(if (uk) "Вивід" else "Output", h.output)
        if (h.params.isNotEmpty()) {
            Text(if (uk) "ЗНАЧЕННЯ" else "VALUES", color = Apex.Muted, fontFamily = FontFamily.Monospace,
                fontSize = 10.sp, letterSpacing = 1.sp, modifier = Modifier.padding(bottom = 2.dp))
            h.params.forEach { (k, v) ->
                Column(Modifier.padding(vertical = 3.dp)) {
                    Text(k, color = Apex.Accent, fontFamily = FontFamily.Monospace, fontSize = 11.sp, fontWeight = FontWeight.SemiBold)
                    Text(v, color = Apex.Ink2, fontFamily = FontFamily.Monospace, fontSize = 11.sp)
                }
            }
        }
        val rel = toolRelated(key, lang)
        if (rel.isNotBlank()) HelpBlock(if (uk) "Де ще застосувати" else "Where next", rel)
    }
}

/** Вбудована згортана довідка для тіла картки інструмента (щоб не бігати на окремий екран). */
@Composable
fun ToolHelpInline(key: String) {
    val uk = LocalLang.current == Lang.UK
    var open by remember { mutableStateOf(false) }
    Column(Modifier.fillMaxWidth().padding(top = 8.dp)) {
        Text((if (open) "▾ " else "▸ ") + (if (uk) "довідка — що це значить і де застосувати" else "help — what it means & where to use"),
            color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 10.sp,
            modifier = Modifier.clickable { open = !open })
        if (open) { Spacer(Modifier.height(6.dp)); ToolHelpCard(key) }
    }
}

private fun mkPaint(argb: Int, sizePx: Float, bold: Boolean): android.graphics.Paint =
    android.graphics.Paint().apply {
        color = argb; textSize = sizePx; isAntiAlias = true
        textAlign = android.graphics.Paint.Align.CENTER
        typeface = android.graphics.Typeface.create(android.graphics.Typeface.MONOSPACE,
            if (bold) android.graphics.Typeface.BOLD else android.graphics.Typeface.NORMAL)
    }

/** DeviceSchematic — Canvas-схема ДВОХ плат (ESP32 + UNO), кожна з власними модулями-вузлами
 *  й доріжками (device-upgrade стиль). Виявлені модулі світяться зеленим, відсутні — тьмяні.
 *  Плати спарені вертикально й зʼєднані link-трасою. */
@Composable
fun DeviceSchematic(esp: Map<String, Boolean>, espStat: String,
                    unoOn: Boolean, unoMods: List<Pair<String, Boolean>>, unoStat: String, height: Int = 440) {
    val cInk = Apex.Ink.toArgb(); val cAccent = Apex.Accent.toArgb(); val cMuted = Apex.Muted.toArgb()
    Canvas(Modifier.fillMaxWidth().height(height.dp)) {
        val w = size.width; val h = size.height; val d = density
        val nc = drawContext.canvas.nativeCanvas
        val bw = w * 0.38f; val bx = (w - bw) / 2f
        val nodeW = w * 0.20f; val nodeH = h * 0.075f

        fun node(x: Float, y: Float, name: String, on: Boolean, edgeX: Float) {
            if (on) drawRoundRect(Apex.AccentSoft, Offset(x, y), androidx.compose.ui.geometry.Size(nodeW, nodeH),
                androidx.compose.ui.geometry.CornerRadius(6f, 6f))
            drawRoundRect(if (on) Apex.Accent else Apex.Edge, Offset(x, y), androidx.compose.ui.geometry.Size(nodeW, nodeH),
                androidx.compose.ui.geometry.CornerRadius(6f, 6f), style = androidx.compose.ui.graphics.drawscope.Stroke(1.3f))
            nc.drawText(name, x + nodeW / 2, y + nodeH / 2 + 4f * d, mkPaint(if (on) cInk else cMuted, 10.5f * d, on))
            val ny = y + nodeH / 2; val tx = if (edgeX < x) x else x + nodeW
            drawLine(if (on) Apex.Accent else Apex.Edge, Offset(tx, ny), Offset(edgeX, ny), 1.2f)
            drawCircle(if (on) Apex.Accent else Apex.Muted, 2.4f, Offset(edgeX, ny))
        }
        fun board(by: Float, bh: Float, name: String, stat: String, mods: List<Pair<String, Boolean>>, dim: Boolean) {
            val tickCol = if (dim) Apex.Muted else Apex.Accent
            drawRoundRect(if (dim) Apex.Grid else Apex.Edge, Offset(bx, by), androidx.compose.ui.geometry.Size(bw, bh),
                androidx.compose.ui.geometry.CornerRadius(8f, 8f), style = androidx.compose.ui.graphics.drawscope.Stroke(1.6f))
            val tk = 12f
            for ((cx, cy, sx, sy) in listOf(arrayOf(bx, by, 1f, 1f), arrayOf(bx + bw, by, -1f, 1f),
                    arrayOf(bx, by + bh, 1f, -1f), arrayOf(bx + bw, by + bh, -1f, -1f))) {
                drawLine(tickCol, Offset(cx, cy), Offset(cx + tk * sx, cy), 1.6f)
                drawLine(tickCol, Offset(cx, cy), Offset(cx, cy + tk * sy), 1.6f)
            }
            nc.drawText(name, bx + bw / 2, by + 16f * d, mkPaint(if (dim) cMuted else cInk, 12f * d, true))
            nc.drawText(stat, bx + bw / 2, by + bh / 2 + 6f * d, mkPaint(if (dim) cMuted else cAccent, 15f * d, true))
            val rows = ((mods.size + 1) / 2).coerceAtLeast(1)
            mods.forEachIndexed { i, (mn, on) ->
                val rowIdx = i / 2
                val y = by + rowIdx * (bh / rows) + (bh / rows - nodeH) / 2
                if (i % 2 == 0) node(6f, y, mn, on, bx)
                else node(w - nodeW - 6f, y, mn, on, bx + bw)
            }
        }
        val espBy = h * 0.05f; val espBh = h * 0.30f
        val unoBy = h * 0.60f; val unoBh = h * 0.30f
        // link-траса між платами
        drawLine(if (unoOn) Apex.Accent else Apex.Edge, Offset(bx + bw / 2, espBy + espBh), Offset(bx + bw / 2, unoBy), 1.4f)
        drawCircle(if (unoOn) Apex.Accent else Apex.Muted, 2.6f, Offset(bx + bw / 2, espBy + espBh))
        drawCircle(if (unoOn) Apex.Accent else Apex.Muted, 2.6f, Offset(bx + bw / 2, unoBy))
        nc.drawText("UNO LINK", bx + bw / 2 + 44f * d, (espBy + espBh + unoBy) / 2 + 4f * d, mkPaint(cMuted, 8.5f * d, false))
        val espMods = listOf("nRF24" to (esp["nrf24"] == true), "WiFi" to (esp["wifi_sta"] == true),
            "CC1101" to (esp["cc1101"] == true), "AP" to (esp["soft_ap"] == true),
            "SD" to (esp["sd"] == true), "BT" to (esp["bt"] == true))
        board(espBy, espBh, "ESP32 T-Display", espStat, espMods, dim = false)
        board(unoBy, unoBh, "Arduino UNO R3", if (unoOn) unoStat else "offline", unoMods, dim = !unoOn)
    }
}

/** Wireframe-панель плати (device-upgrade вигляд): рамка з кутовими тік-мітками, заголовок+статус,
 *  усередині — вміст (stat-рядки, чіпи модулів). Стилістика APEX-референсу. */
@Composable
fun WireframePanel(label: String, tag: String? = null, tagLevel: Int = 0, content: @Composable ColumnScope.() -> Unit) {
    Box(Modifier.fillMaxWidth().padding(vertical = 4.dp)) {
        Column(Modifier.fillMaxWidth()
            .background(Apex.Panel2, RoundedCornerShape(8.dp))
            .border(1.dp, Apex.Edge, RoundedCornerShape(8.dp))
            .padding(13.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(label, color = Apex.Ink, fontFamily = FontFamily.Monospace, fontWeight = FontWeight.SemiBold, fontSize = 12.sp, letterSpacing = 0.5.sp, modifier = Modifier.weight(1f))
                if (tag != null) StatusTag(tag, tagLevel)
            }
            Spacer(Modifier.height(8.dp))
            content()
        }
        Canvas(Modifier.matchParentSize()) {
            val t = 11f; val c = Apex.Accent; val w = size.width; val h = size.height
            for ((cx, cy, sx, sy) in listOf(
                arrayOf(0f, 0f, 1f, 1f), arrayOf(w, 0f, -1f, 1f),
                arrayOf(0f, h, 1f, -1f), arrayOf(w, h, -1f, -1f))) {
                drawLine(c, Offset(cx, cy), Offset(cx + t * sx, cy), 1.6f)
                drawLine(c, Offset(cx, cy), Offset(cx, cy + t * sy), 1.6f)
            }
        }
    }
}

/** Елемент готовності підсистеми: назва, значення, рівень (0 GO / 1 WARN / 2 HOLD/FAIL). */
data class ReadyItem(val label: String, val value: String, val level: Int)

/** Панель готовності (як SYSTEM READINESS у APEX): матриця-щільність GO зліва + список статусів справа. */
@Composable
fun ReadinessPanel(items: List<ReadyItem>) {
    val n = items.size.coerceAtLeast(1)
    val go = items.count { it.level == 0 }
    val warn = items.count { it.level == 1 }
    val cleared = go * 100 / n
    Column {
        Row(Modifier.fillMaxWidth().padding(bottom = 8.dp)) {
            Text("SYSTEM READINESS", color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 10.sp, letterSpacing = 1.5.sp)
            Spacer(Modifier.weight(1f))
            Text("$cleared% CLEARED", color = if (cleared >= 80) Apex.Accent else Apex.Warn, fontFamily = FontFamily.Monospace, fontSize = 10.sp, fontWeight = FontWeight.SemiBold)
        }
        Row(Modifier.fillMaxWidth()) {
            // Матриця-щільність (7×6): зелені = частка GO, бурштин = WARN, решта тьмяні.
            val cols = 7; val rows = 6; val cells = cols * rows
            val goC = cells * go / n; val wC = cells * warn / n
            Column(Modifier.weight(1f)) {
                for (r in 0 until rows) {
                    Row {
                        for (c in 0 until cols) {
                            val i = r * cols + c
                            // псевдо-скатер, щоб виглядало «органічно», а не суцільним блоком
                            val rank = (i * 37 + 11) % cells
                            val col = when { rank < goC -> Apex.Accent; rank < goC + wC -> Apex.Warn; else -> Apex.Edge }
                            Box(Modifier.padding(2.dp).size(11.dp).background(col, RoundedCornerShape(2.dp)))
                        }
                    }
                }
            }
            Spacer(Modifier.width(14.dp))
            Column(Modifier.weight(1.25f)) {
                items.forEach {
                    val tag = when (it.level) { 0 -> "GO"; 1 -> "WARN"; else -> "HOLD" }
                    LeaderRow(it.label, it.value, tag, it.level)
                }
            }
        }
    }
}

/** Чіп модуля для автодетекту: зелений+обведення якщо виявлено, тьмяний якщо ні. */
@Composable
fun ModuleChip(name: String, on: Boolean, sub: String? = null) {
    Row(Modifier.border(1.dp, if (on) Apex.Accent else Apex.Edge, RoundedCornerShape(6.dp))
        .background(if (on) Apex.AccentSoft else Color.Transparent, RoundedCornerShape(6.dp))
        .padding(horizontal = 8.dp, vertical = 4.dp), verticalAlignment = Alignment.CenterVertically) {
        Box(Modifier.size(6.dp).background(if (on) Apex.Accent else Apex.Muted, CircleShape))
        Spacer(Modifier.width(6.dp))
        Text(name, color = if (on) Apex.Ink else Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 11.sp)
        if (sub != null) { Spacer(Modifier.width(5.dp)); Text(sub, color = Apex.Muted, fontFamily = FontFamily.Monospace, fontSize = 9.sp) }
    }
}

/** Банер «плата зайнята/перепід'єднання»: текст + рухомий скан-сегмент (APEX-стиль).
 *  Показуємо, коли опитування падає (плата в довгому аналізі) — замість мовчазного зависання. */
@Composable
fun BusyBanner(text: String) {
    val tr = rememberInfiniteTransition(label = "busy")
    val x by tr.animateFloat(0f, 1f, infiniteRepeatable(tween(1100, easing = LinearEasing)), label = "scan")
    Column(Modifier.fillMaxWidth().background(Apex.Panel2).padding(horizontal = 14.dp, vertical = 7.dp)) {
        Text(text, color = Apex.Warn, fontFamily = FontFamily.Monospace, fontSize = 11.sp, letterSpacing = 1.sp)
        Spacer(Modifier.height(5.dp))
        Box(Modifier.fillMaxWidth().height(3.dp).background(Apex.Edge, RoundedCornerShape(2.dp))) {
            Canvas(Modifier.fillMaxSize()) {
                val w = size.width; val seg = w * 0.30f
                val start = x * (w + seg) - seg
                val a = start.coerceIn(0f, w); val b = (start + seg).coerceIn(0f, w)
                if (b > a) drawRect(Apex.Accent, topLeft = Offset(a, 0f),
                    size = androidx.compose.ui.geometry.Size(b - a, size.height))
            }
        }
    }
}

/** Coded top-bar subline: // LINK <state> · RSSI NdBm · NmV · <PWR>. */
@Composable
fun CodedBar(text: String, ok: Boolean = true) {
    Text(text, color = if (ok) Apex.Accent else Apex.Bad, fontFamily = FontFamily.Monospace, fontSize = 11.sp,
        modifier = Modifier.fillMaxWidth().padding(vertical = 4.dp))
}
