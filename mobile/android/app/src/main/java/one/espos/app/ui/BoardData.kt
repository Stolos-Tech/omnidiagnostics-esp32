package one.espos.app.ui

/** Статичні дані плати для System-info: пінаут, глосарій термінів, розгорнуті підказки.
 *  Джерело пінауту — апаратний аудит (esp32-os-hardware-modules). Глосарій/довідка —
 *  справжні EN+UK пари (обираються за LocalLang, як і решта UI). */

data class Pin(val gpio: String, val fn: String, val note: String = "")

/** Повний пінаут ESP32 T-Display (з shared HSPI nRF24/SD/CC1101 + strapping-мітками). */
val PINOUT = listOf(
    Pin("0", "BTN LEFT", "strapping · boot"),
    Pin("2", "SPI SCK", "nRF24+SD+CC1101 · strapping"),
    Pin("4", "TFT backlight", "strapping · PWM"),
    Pin("5", "TFT CS", "strapping"),
    Pin("12", "nRF24 CE", "strapping · 10k pulldown!"),
    Pin("13", "SW S2", ""),
    Pin("14", "ADC enable", ""),
    Pin("15", "SPI MOSI", "shared · strapping"),
    Pin("16", "TFT DC", ""),
    Pin("17", "SW S1", ""),
    Pin("18", "TFT SCLK", ""),
    Pin("19", "TFT MOSI", ""),
    Pin("21", "SPI MISO (SD)", "SD dynamic MISO"),
    Pin("22", "UNO link TX", "-> UNO D4"),
    Pin("23", "TFT RST", ""),
    Pin("25", "nRF24 CSN", ""),
    Pin("26", "SW S5", ""),
    Pin("27", "CC1101 CS", "(був BACK-свіч)"),
    Pin("32", "I2C SCL", ""),
    Pin("33", "SD CS", ""),
    Pin("34", "VBAT sense", "in-only · ADC"),
    Pin("35", "BTN RIGHT", "in-only"),
    Pin("36", "EM field", "in-only · ADC"),
    Pin("37", "UNO link RX", "in-only · <- UNO D5 (дільник)"),
    Pin("38", "SPI MISO (nRF/CC)", "in-only"),
    Pin("39", "CC1101 GDO0", "in-only · OOK RMT"),
)

/** Глосарій термінів — англійська. */
val GLOSSARY_EN = linkedMapOf(
    "RSSI" to "Received Signal Strength Indicator, in dBm. Closer to 0 = stronger (−45 great, −85 weak).",
    "Sub-GHz" to "Radio in 300–928 MHz (CC1101): remotes, sensors, meters, telemetry. Not the 2.4 GHz band.",
    "OOK / ASK" to "On-Off / Amplitude-Shift Keying — the simple modulation of cheap 433 MHz remotes.",
    "nRF24" to "2.4 GHz transceiver; here used as an air-occupancy scanner (2.4 GHz Analyzer).",
    "mDNS" to "Multicast DNS — reach the board as esp32os.local on the LAN without a DNS server.",
    "PWM" to "Pulse-Width Modulation — controls backlight / fan by duty cycle (% of on-time).",
    "Tailscale" to "WireGuard mesh-VPN: the phone reaches the home gateway from anywhere, encrypted.",
    "Gateway" to "Home service-bridge: phone -> gateway -> board; holds the PIN and does the crypto.",
    "Strapping pin" to "A GPIO that sets ESP32 boot mode — its level at power-on must not be forced.",
    "HSPI" to "The shared SPI bus. nRF24, SD and CC1101 share SCK/MOSI; MISO is switched per-device.",
)

/** Глосарій термінів — українська. */
val GLOSSARY_UK = linkedMapOf(
    "RSSI" to "Сила прийнятого сигналу, dBm. Ближче до 0 — сильніший (−45 чудово, −85 слабко).",
    "Sub-GHz" to "Радіо 300–928 MHz (CC1101): пульти, датчики, лічильники, телеметрія. Не діапазон 2.4 ГГц.",
    "OOK / ASK" to "Амплітудна маніпуляція (вкл/викл) — проста модуляція дешевих пультів 433 MHz.",
    "nRF24" to "Трансивер 2.4 ГГц; тут — сканер зайнятості ефіру (аналізатор 2.4 ГГц).",
    "mDNS" to "Multicast DNS — доступ до плати як esp32os.local у мережі без DNS-сервера.",
    "PWM" to "Широтно-імпульсна модуляція — керує підсвіткою / вентилятором шпаруватістю (% часу «увімк»).",
    "Tailscale" to "Mesh-VPN на WireGuard: телефон дістається домашнього шлюзу звідусіль, зашифровано.",
    "Gateway" to "Домашній сервіс-міст: телефон -> шлюз -> плата; тримає PIN і робить крипто.",
    "Strapping-пін" to "GPIO, що задає режим boot ESP32 — його рівень при старті не можна нав'язувати.",
    "HSPI" to "Спільна шина SPI. nRF24, SD і CC1101 ділять SCK/MOSI; MISO перемикається на пристрій.",
)

/** Розгорнуті підказки по інструментах — англійська (порт HELP із веб index.html). */
val HELP_EN = linkedMapOf(
    "Sub-GHz Analyzer" to "CC1101 RSSI sweep across the three sub-GHz windows (300/433/868 MHz). Passive, RX only. Finds active remotes, sensors, meters.",
    "Sub-GHz Capture" to "Listens on an ISM frequency and decodes OOK remotes (EV1527/PT2262). S5 listens, S1 cycles frequency. Rolling-code keys never repeat.",
    "Sub-GHz Watch" to "Counter-surveillance sweep flagging persistent transmitters (seen several passes) — 433/868 trackers, bugs hiding off 2.4 GHz.",
    "WiFi Analyzer" to "Nearby networks: SSID, channel, RSSI, encryption. Channel-load histogram + RSSI over time.",
    "Detect" to "Counter-surveillance: trackers (AirTag/Tile/SmartTag), cameras by OUI, card skimmers, WiFi attackers.",
    "EM Field" to "Coil + diode on GPIO36 reads the induced field (mV pp). Comparative EMI finder. S5 calibrates the zero.",
)

/** Розгорнуті підказки по інструментах — українська. */
val HELP_UK = linkedMapOf(
    "Sub-GHz Analyzer" to "Свіп RSSI CC1101 по трьох вікнах суб-ГГц (300/433/868 MHz). Пасивно, лише прийом. Знаходить активні пульти, датчики, лічильники.",
    "Sub-GHz Capture" to "Слухає ISM-частоту й декодує OOK-пульти (EV1527/PT2262). S5 — слухати, S1 — частота. Rolling-code-ключі не повторюються.",
    "Sub-GHz Watch" to "Контрсурвейланс-свіп: позначає стійких передавачів (кілька проходів) — трекери 433/868, жучки, що ховаються поза 2.4 ГГц.",
    "WiFi Analyzer" to "Сусідні мережі: SSID, канал, RSSI, шифрування. Гістограма завантаженості каналів + RSSI у часі.",
    "Detect" to "Контрсурвейланс: трекери (AirTag/Tile/SmartTag), камери за OUI, скімери карт, WiFi-атакери.",
    "EM Field" to "Котушка + діод на GPIO36 міряє наведене поле (mV pp). Порівняльний EMI-шукач. S5 — калібрувати нуль.",
)
