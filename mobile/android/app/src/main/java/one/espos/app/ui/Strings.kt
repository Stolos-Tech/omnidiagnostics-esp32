package one.espos.app.ui

/**
 * Локалізація EN/UK через CompositionLocal (легко перемикати в додатку, без ресурс-
 * кваліфікаторів). Використання: L("connect"). Додати ключ -> вписати в обидві мапи.
 * Технічні токени (RSSI, dBm, mV, CC1101, nRF24, PWM, GPIO) НЕ перекладаємо — це
 * універсальні позначення; перекладаємо лише людиночитний текст.
 */
import androidx.compose.runtime.Composable
import androidx.compose.runtime.compositionLocalOf

enum class Lang { EN, UK }

val LocalLang = compositionLocalOf { Lang.EN }

private val EN = mapOf(
    // розділи drawer-меню (ключі = Sec.name.lowercase())
    "server" to "SERVER",
    "boards" to "BOARDS", "tests" to "TESTS", "calib" to "CALIBRATION",
    "modules" to "MODULES & PINS", "data" to "DATA / LOGS", "scripts" to "SCRIPTS",
    "settings" to "SETTINGS",
    // під'єднання
    "connect" to "CONNECT", "find" to "FIND", "host" to "Host", "pin" to "PIN",
    "connect_hint" to "mDNS or IP",
    "direct" to "DIRECT", "gateway" to "GATEWAY", "gw_url" to "Gateway host:port",
    "token" to "Token", "gw_hint" to "via Tailscale",
    // device-екран
    "battery" to "BATTERY", "rssi" to "RSSI", "screen" to "SCREEN",
    "live" to "LIVE", "poll" to "poll",
    // apps
    "no_scripts" to "no scripts",
    // theme / storage
    "theme" to "THEME", "board_palette" to "board palette",
    "storage" to "STORAGE", "sd_card" to "SD card",
    "mounted" to "mounted", "no_card" to "no card",
    // system-info картка
    "system" to "SYSTEM", "temp_power" to "temp · power",
    "cooling" to "cooling", "fan_pwm" to "fan pwm", "draw" to "draw",
    // довідкові картки
    "pinout" to "PINOUT", "glossary" to "GLOSSARY", "terms" to "terms",
    "help" to "HELP", "modules_hud" to "MODULES",
    // спектри
    "sub_ghz_spectrum" to "SUB-GHZ SPECTRUM", "no_cc1101" to "no CC1101",
    "spectrum_24" to "2.4 GHZ SPECTRUM", "no_nrf24" to "no nRF24",
    "sta_rssi" to "STA RSSI", "mv_over_time" to "mV over time", "dbm_over_time" to "dBm over time",
    // спільні
    "no_data" to "no data", "collecting" to "collecting…",
    "items" to "items", "close" to "CLOSE",
    "ok" to "OK", "fail" to "FAIL", "warn" to "WARN", "crit" to "CRIT",
)
private val UK = mapOf(
    "server" to "СЕРВЕР",
    "boards" to "ПЛАТИ", "tests" to "ТЕСТИ", "calib" to "КАЛІБРУВАННЯ",
    "modules" to "МОДУЛІ ТА ПІНИ", "data" to "ДАНІ / ЛОГИ", "scripts" to "СКРИПТИ",
    "settings" to "НАЛАШТУВАННЯ",
    "connect" to "З'ЄДНАТИ", "find" to "ПОШУК", "host" to "Хост", "pin" to "PIN",
    "connect_hint" to "mDNS або IP",
    "direct" to "ПРЯМО", "gateway" to "ШЛЮЗ", "gw_url" to "Хост:порт шлюзу",
    "token" to "Токен", "gw_hint" to "через Tailscale",
    "battery" to "БАТАРЕЯ", "rssi" to "RSSI", "screen" to "ЕКРАН",
    "live" to "НАЖИВО", "poll" to "опит",
    "no_scripts" to "немає скриптів",
    "theme" to "ТЕМА", "board_palette" to "палітра плати",
    "storage" to "СХОВИЩЕ", "sd_card" to "SD-карта",
    "mounted" to "змонтовано", "no_card" to "немає карти",
    "system" to "СИСТЕМА", "temp_power" to "темп · живлення",
    "cooling" to "охолодж.", "fan_pwm" to "вентилятор", "draw" to "спожив.",
    "pinout" to "РОЗПІНОВКА", "glossary" to "ГЛОСАРІЙ", "terms" to "терміни",
    "help" to "ДОВІДКА", "modules_hud" to "МОДУЛІ",
    "sub_ghz_spectrum" to "СУБ-ГГЦ СПЕКТР", "no_cc1101" to "немає CC1101",
    "spectrum_24" to "2.4 ГГЦ СПЕКТР", "no_nrf24" to "немає nRF24",
    "sta_rssi" to "STA RSSI", "mv_over_time" to "mV у часі", "dbm_over_time" to "dBm у часі",
    "no_data" to "немає даних", "collecting" to "збираю…",
    "items" to "записів", "close" to "ЗАКРИТИ",
    "ok" to "OK", "fail" to "ЗБІЙ", "warn" to "УВАГА", "crit" to "КРИТ",
)

@Composable
fun L(key: String): String {
    val m = if (LocalLang.current == Lang.UK) UK else EN
    return m[key] ?: EN[key] ?: key
}
