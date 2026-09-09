package one.espos.app.ui

/**
 * APEX Aerospace Mission-Control дизайн-система (той самий візуал, що у веб index.html):
 * dark HUD, моно-типографіка скрізь, мінт-акцент #00FFAB, статуси green/amber/red,
 * leader-рядки "LABEL ···· VALUE [TAG]", табличні цифри. Уся важка графіка — у додатку
 * (плата лише віддає дані), тож рендеримо спектри/графіки/метри багатше за 240×135 TFT.
 */
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Typography
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

// --- APEX-палітра (точні значення з веб-теми) ---
object Apex {
    val Bg      = Color(0xFF0B0D0F)
    val Panel   = Color(0xFF12171A)
    val Panel2  = Color(0xFF0F1417)
    val Edge    = Color(0xFF232C30)
    val Ink     = Color(0xFFE7EEF0)
    val Ink2    = Color(0xFFB7C4C6)
    val Muted   = Color(0xFF7D8B8C)
    val Accent  = Color(0xFF00FFAB)   // GO / мінт-сигнатура
    val Warn    = Color(0xFFFDD036)
    val Bad     = Color(0xFFFF5A57)
    val Grid    = Color(0xFF141A1D)
    val AccentSoft = Color(0x2200FFAB)
}

// --- Дизайн-токени: 8dp-сітка відступів (Material/HIG-стандарт) ---
// Уся геометрія — з цих кроків, щоб інтерфейс був ритмічним, а не ad-hoc (2/6/9/14…).
object Space {
    val xs = 4.dp; val sm = 8.dp; val md = 12.dp; val lg = 16.dp; val xl = 24.dp; val xxl = 32.dp
}
// Мінімальний тач-таргет інтерактивних елементів (Material 48 / HIG 44). Іконки-гліфи
// (✕ › 📌 ⬇ ↻) без цього мали крихітну зону натиску — тепер ≥ MinTouch.
val MinTouch = 44.dp

// Типографічна шкала (моно-HUD): називні розміри замість «магічних sp» по коду.
object TypeScale {
    val display = 26.sp; val title = 15.sp; val subtitle = 13.sp
    val body = 12.sp; val label = 10.sp; val micro = 9.sp
}

// Моно всюди — HUD-відчуття. (Для повної точності можна підвантажити IBM Plex Mono у res/font.)
private val Mono = FontFamily.Monospace

val ApexType = Typography(
    titleLarge = TextStyle(fontFamily = Mono, fontWeight = FontWeight.SemiBold, fontSize = 20.sp, letterSpacing = 0.5.sp),
    titleMedium = TextStyle(fontFamily = Mono, fontWeight = FontWeight.SemiBold, fontSize = 15.sp, letterSpacing = 1.sp),
    bodyLarge = TextStyle(fontFamily = Mono, fontSize = 14.sp),
    bodyMedium = TextStyle(fontFamily = Mono, fontSize = 13.sp),
    labelLarge = TextStyle(fontFamily = Mono, fontWeight = FontWeight.Medium, fontSize = 12.sp, letterSpacing = 1.5.sp),
    labelSmall = TextStyle(fontFamily = Mono, fontSize = 10.sp, letterSpacing = 1.sp),
)

private val ApexScheme = darkColorScheme(
    primary = Apex.Accent,
    onPrimary = Apex.Bg,
    background = Apex.Bg,
    onBackground = Apex.Ink,
    surface = Apex.Panel,
    onSurface = Apex.Ink,
    surfaceVariant = Apex.Panel2,
    onSurfaceVariant = Apex.Ink2,
    outline = Apex.Edge,
    error = Apex.Bad,
    secondary = Apex.Warn,
)

@Composable
fun ApexTheme(content: @Composable () -> Unit) {
    // Свідомо dark-only (mission-control комітиться в один візуальний світ).
    @Suppress("UNUSED_EXPRESSION") isSystemInDarkTheme()
    MaterialTheme(colorScheme = ApexScheme, typography = ApexType, content = content)
}

// Семантичний колір статусу (GO/WARN/BAD) — окремо від акценту.
fun statusColor(level: Int): Color = when (level) {
    2 -> Apex.Bad
    1 -> Apex.Warn
    else -> Apex.Accent
}
