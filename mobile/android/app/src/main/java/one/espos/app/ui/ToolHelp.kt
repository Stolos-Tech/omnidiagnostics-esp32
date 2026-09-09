package one.espos.app.ui

/**
 * ToolHelp — повна контекстна довідка по КОЖНОМУ інструменту (EN+UK), з поясненням
 * що робить / як користуватись / як читати вивід + значення полів (params).
 * Порт і розширення веб-HELP (data/web/index.html) для мобільного додатка.
 *
 * Ключі збігаються зі slug-ами інструментів. Пошук: toolHelp(key, lang).
 */

data class ToolHelp(
    val title: String,
    val what: String,                                   // що робить
    val use: String,                                    // як користуватись
    val output: String,                                 // як читати вивід
    val params: List<Pair<String, String>> = emptyList(), // поле/значення -> пояснення
    val related: String = ""                            // де ще застосувати дані / зв'язані інструменти
)

data class HelpPair(val en: ToolHelp, val uk: ToolHelp)

/** Довідка інструмента за ключем і мовою (null, якщо ключа нема). */
fun toolHelp(key: String, lang: Lang): ToolHelp? =
    TOOL_HELP[key]?.let { if (lang == Lang.UK) it.uk else it.en }

/** «Де ще застосувати дані» — крос-посилання між інструментами (те, чого бракувало). */
private val RELATED_UK: Map<String, String> = mapOf(
    "subghz_analyzer" to "Знайшов пік → «Sub-GHz Capture» щоб декодувати цю частоту. Постійна активність на 433/868 → «Attacker Detect». Захоплений код → «Sub-GHz Watch» для повторів.",
    "subghz_capture" to "Декодований код брелока → порівняй у «Sub-GHz Watch»; невідома частота — спершу «Sub-GHz Analyzer».",
    "rf24_analyzer" to "Зайняті канали 2.4G корелюй з «WiFi Analyzer» (Wi-Fi ch1/6/11 ≈ nRF 2412/2437/2462). Сплеск на кількох каналах → можливий BLE-spam/джемер.",
    "wifi_analyzer" to "Обрав ціль-AP → BSSID/канал у «Channel Monitor» і «WiFi Sniffer». OPEN/WEP/twin → познач у ТАРГЕТ і перевір клієнтів «ARP Scan».",
    "channel_monitor" to "Багато deauth на каналі → «Deauth Alert». Клієнти AP → «WiFi Sniffer».",
    "wifi_sniffer" to "MAC клієнтів → «ARP Scan»/«Net Info» щоб дізнатись виробника й сервіси. Зникнення клієнта після сплеску → «Deauth Alert».",
    "deauth_alert" to "Джерело атаки (BSSID) → «WiFi Analyzer» знайти клон-AP; познач як ТАРГЕТ.",
    "net_scan" to "Тап по хосту → глибока розвідка (порти/виробник/hostname/UPnP). Підозрілий → ТАРГЕТ, далі «Net Info»/«HTTP Sec». Новий MAC → бот-алерт уже містить категорію.",
    "arp_scan" to "MAC → виробник (OUI) у розвідці хоста; невідомий пристрій → ТАРГЕТ і «Net Info».",
    "net_info" to "Відкриті порти → «HTTP Get»/«HTTP Sec»/«TLS Cert» для веб-сервісів; 445/139 → SMB-хост; 554 → камера («Camera Finder»).",
    "battery" to "Різкий провал напруги під навантаженням → «System» (споживання/кулер). Тренд розряду — в «Insights» на дашборді.",
    "system" to "Перегрів/високе споживання → звір з активними радіо у «Modules»; кулер керується автоматично за темп.",
)
private val RELATED_EN: Map<String, String> = mapOf(
    "subghz_analyzer" to "Found a peak → use Sub-GHz Capture to decode that frequency. Constant 433/868 activity → Attacker Detect.",
    "rf24_analyzer" to "Correlate busy 2.4G channels with WiFi Analyzer. Bursts across channels → possible BLE-spam/jammer.",
    "wifi_analyzer" to "Pick a target AP → feed BSSID/channel into Channel Monitor & WiFi Sniffer. OPEN/WEP/twin → set as TARGET, check clients via ARP Scan.",
    "net_scan" to "Tap a host → deep intel (ports/vendor/hostname/UPnP). Suspicious → TARGET, then Net Info / HTTP Sec.",
    "net_info" to "Open ports → HTTP Get/Sec/TLS Cert for web services; 445/139 → SMB host; 554 → camera.",
)

/** Крос-посилання «де ще застосувати» для інструмента (порожньо, якщо нема). */
fun toolRelated(key: String, lang: Lang): String =
    (if (lang == Lang.UK) RELATED_UK else RELATED_EN)[key] ?: ""

/** Усі ключі довідки в порядку доменів (для екрана «всі інструменти»). */
val HELP_ORDER = listOf(
    "subghz_analyzer", "subghz_capture", "subghz_watch",
    "wifi_analyzer", "channel_monitor", "wifi_sniffer", "deauth_alert", "rf24_analyzer",
    "net_scan", "arp_scan", "net_info", "mdns", "ssdp", "dns_lookup", "traceroute", "link_qual",
    "http_get", "http_post", "http_sec", "tls_cert", "tcp_term", "captive", "wol",
    "bt_scan", "ble_scan",
    "tracker_detect", "camera_finder", "card_skimmer", "attacker_detect",
    "rfid", "em_field", "battery", "self_test", "system", "remote", "scripts",
)

val TOOL_HELP: Map<String, HelpPair> = mapOf(

    "subghz_analyzer" to HelpPair(
        ToolHelp("Sub-GHz Analyzer",
            "CC1101 RSSI sweep across the three sub-GHz windows (300–348 / 387–464 / 779–928 MHz). Passive, RX only.",
            "Watch the histogram; peak-hold marks the strongest bin. Reset peak on the board (S5).",
            "Bar height = signal strength. Green weak, amber medium, red strong; vertical lines split the three windows.",
            listOf("dBm" to "Сила сигналу; ближче до 0 = сильніший (−45 сильно, −100 шум).",
                "300–348 MHz" to "Вікно ISM: авто-брелоки, датчики тиску в шинах.",
                "433 MHz" to "Найпоширеніші пульти воріт/розеток (EV1527/PT2262).",
                "868 MHz" to "EU ISM: домофони, лічильники, LoRa-телеметрія.")),
        ToolHelp("Sub-GHz Analyzer",
            "Свіп RSSI CC1101 по трьох вікнах суб-ГГц (300–348 / 387–464 / 779–928 MHz). Пасивно, лише прийом.",
            "Дивись гістограму; peak-hold позначає найсильніший стовпчик. Скид піку на платі (S5).",
            "Висота стовпця = сила сигналу. Зелений слабко, бурштин середньо, червоний сильно; вертикалі ділять вікна.",
            listOf("dBm" to "Сила сигналу; ближче до 0 = сильніший (−45 сильно, −100 шум).",
                "300–348 MHz" to "ISM: авто-брелоки, датчики тиску шин.",
                "433 MHz" to "Пульти воріт/розеток (EV1527/PT2262).",
                "868 MHz" to "EU ISM: домофони, лічильники, LoRa."))
    ),

    "subghz_capture" to HelpPair(
        ToolHelp("Sub-GHz Capture",
            "Listens on one ISM frequency and decodes OOK/ASK remotes of the EV1527/PT2262 family.",
            "S1 cycles frequency (433.92 / 868.35 / 315 / 915), S5 listens ~2.5 s for a button press.",
            "Shows protocol, code (hex), bit count, Te timing. Rolling-code keys capture but never repeat.",
            listOf("Code (hex)" to "Захоплене кодове слово; фіксований код = відтворюваний.",
                "Bits" to "Довжина коду (зазвичай 24 біти для EV1527).",
                "Te (µs)" to "Базовий такт кодування; ідентифікує родину протоколу.",
                "Rolling" to "Код-стрибок (авто-ключі) — НЕ повторюється, лише індикація.")),
        ToolHelp("Sub-GHz Capture",
            "Слухає одну ISM-частоту й декодує OOK/ASK-пульти родини EV1527/PT2262.",
            "S1 — частота (433.92 / 868.35 / 315 / 915), S5 — слухати ~2.5 с натиск кнопки.",
            "Показує протокол, код (hex), к-ть біт, тайминг Te. Rolling-code ловиться, але не відтворюється.",
            listOf("Код (hex)" to "Захоплене слово; фіксований код = відтворюваний.",
                "Біти" to "Довжина коду (типово 24 біти для EV1527).",
                "Te (мкс)" to "Базовий такт; ідентифікує родину протоколу.",
                "Rolling" to "Код-стрибок (авто-ключі) — не повторюється, лише індикація."))
    ),

    "subghz_watch" to HelpPair(
        ToolHelp("Sub-GHz Watch",
            "Counter-surveillance: repeatedly sweeps sub-GHz and flags PERSISTENT transmitters (several passes in a row).",
            "Leave it running; S5 resets. A source seen 3+ passes appears in the list.",
            "Each row: frequency, RSSI, and how many passes it persisted. Empty = clear air.",
            listOf("Passes" to "Скільки проходів поспіль джерело активне (3+ = стійке).",
                "RSSI" to "Сила стійкого передавача.",
                "Empty" to "Тихий ефір — прихованих 433/868 передавачів не видно.")),
        ToolHelp("Sub-GHz Watch",
            "Контрсурвейланс: свіпить суб-ГГц і позначає СТІЙКІ передавачі (кілька проходів поспіль).",
            "Лиши працювати; S5 — скид. Джерело, бачене 3+ проходів, з'являється в списку.",
            "Рядок: частота, RSSI, скільки проходів протримався. Порожньо = чистий ефір.",
            listOf("Проходи" to "Скільки проходів поспіль джерело активне (3+ = стійке).",
                "RSSI" to "Сила стійкого передавача.",
                "Порожньо" to "Тихо — прихованих 433/868 передавачів не видно."))
    ),

    "wifi_analyzer" to HelpPair(
        ToolHelp("WiFi Analyzer",
            "Top networks by RSSI, per-channel load 1–13, and RSSI over time.",
            "Pages scroll Top/Channels/Graph; rescan on the board (S5).",
            "Network list with SSID, channel, encryption; histogram shows crowded channels.",
            listOf("RSSI" to "−30 відмінно, −67 добре, −80 слабко, −90 на межі.",
                "Channel" to "1/6/11 не перекриваються (2.4 ГГц) — обирай найменш зайнятий.",
                "Enc" to "OPEN небезпечно, WEP зламано, WPA2/WPA3 норма.")),
        ToolHelp("WiFi Analyzer",
            "Топ мереж за RSSI, завантаженість каналів 1–13, RSSI у часі.",
            "Сторінки: Топ/Канали/Графік; ре-скан на платі (S5).",
            "Список мереж: SSID, канал, шифрування; гістограма — зайняті канали.",
            listOf("RSSI" to "−30 відмінно, −67 добре, −80 слабко, −90 на межі.",
                "Канал" to "1/6/11 не перекриваються (2.4 ГГц) — бери найменш зайнятий.",
                "Шифр" to "OPEN небезпечно, WEP зламано, WPA2/WPA3 норма."))
    ),

    "channel_monitor" to HelpPair(
        ToolHelp("Channel Monitor",
            "Counts 802.11 frames per WiFi channel (passive hop 1–13).",
            "Watch the histogram on the board; here — the current hop channel.",
            "Taller bar = busier channel right now.",
            listOf("Frames" to "К-ть кадрів за вікно на каналі — вимір реального трафіку.",
                "Hop" to "Канал, який слухаємо цю мить (циклічно 1→13).")),
        ToolHelp("Channel Monitor",
            "Рахує 802.11-кадри по каналах WiFi (пасивний хоп 1–13).",
            "Дивись гістограму на платі; тут — поточний канал хопу.",
            "Вищий стовпець = зайнятіший канал саме зараз.",
            listOf("Кадри" to "К-ть кадрів за вікно на каналі — реальний трафік.",
                "Хоп" to "Канал, який слухаємо цю мить (циклічно 1→13)."))
    ),

    "wifi_sniffer" to HelpPair(
        ToolHelp("WiFi Sniffer",
            "Promiscuous inventory of APs + clients; counts deauth/disassoc frames. Listening only.",
            "Pages scroll Summary/APs/Clients; S5 clears. Hops channels 1–13.",
            "AP/client lists with MAC + RSSI; deauth counter spikes signal an attack.",
            listOf("AP" to "Точка доступу (BSSID + канал + RSSI).",
                "Client" to "Пристрій, прив'язаний до AP (MAC рандомізований у сучасних).",
                "Deauth" to "Кадри розриву; сплеск = деаутентифікаційна атака поруч.")),
        ToolHelp("WiFi Sniffer",
            "Promiscuous-інвентар AP + клієнтів; рахує deauth/disassoc. Лише слухає.",
            "Сторінки: Підсумок/AP/Клієнти; S5 — очистити. Хопить канали 1–13.",
            "Списки AP/клієнтів з MAC + RSSI; сплеск deauth = атака.",
            listOf("AP" to "Точка доступу (BSSID + канал + RSSI).",
                "Клієнт" to "Пристрій, прив'язаний до AP (MAC часто рандомний).",
                "Deauth" to "Кадри розриву; сплеск = деаут-атака поруч."))
    ),

    "deauth_alert" to HelpPair(
        ToolHelp("Deauth Alert",
            "Passively catches deauth/disassoc frames and alerts on a spike. Detector, not attacker.",
            "Leave running; S5 clears counters.",
            "Counters + top source BSSIDs; a sudden rise means someone is kicking clients off.",
            listOf("Count" to "К-ть deauth за вікно; норма ~0, атака — десятки/сотні.",
                "Source" to "BSSID, що шле розриви (часто підроблений).")),
        ToolHelp("Deauth Alert",
            "Пасивно ловить deauth/disassoc і б'є тривогу на сплеску. Детектор, не атакер.",
            "Лиши працювати; S5 — скид лічильників.",
            "Лічильники + топ-джерела BSSID; різкий ріст = хтось вибиває клієнтів.",
            listOf("Count" to "К-ть deauth за вікно; норма ~0, атака — десятки+.",
                "Джерело" to "BSSID, що шле розриви (часто підроблений)."))
    ),

    "rf24_analyzer" to HelpPair(
        ToolHelp("2.4G Analyzer",
            "nRF24 air-occupancy scan across 126 channels (2400–2525 MHz) — WiFi, BT, drones, wireless mice.",
            "Watch the spectrum; passive RPD detection.",
            "Per-channel occupancy count; busy 2.4 GHz bands light up.",
            listOf("Channel" to "0..125 = 2400..2525 МГц (1 МГц крок).",
                "Count" to "Скільки разів канал був зайнятий за прохід.")),
        ToolHelp("2.4G Analyzer",
            "nRF24-скан зайнятості ефіру по 126 каналах (2400–2525 МГц) — WiFi, BT, дрони, миші.",
            "Дивись спектр; пасивна RPD-детекція.",
            "Зайнятість по каналах; активні 2.4 ГГц смуги підсвічуються.",
            listOf("Канал" to "0..125 = 2400..2525 МГц (крок 1 МГц).",
                "Count" to "Скільки разів канал зайнятий за прохід."))
    ),

    "net_scan" to HelpPair(
        ToolHelp("Net Scan",
            "Sweeps the subnet, finds live hosts and their open TCP ports.",
            "Wait (or S5 to stop). Tap a host for details; fields set a range or ports.",
            "Live IPs; details show RTT + open ports.",
            listOf("RTT" to "Час відгуку хоста, мс; менше = ближче/швидше.",
                "80/443" to "Веб-сервер (HTTP/HTTPS).",
                "22" to "SSH; 23 — telnet (небезпечно), 1883 — MQTT, 554 — камера RTSP.")),
        ToolHelp("Net Scan",
            "Сканує підмережу, знаходить живі хости й їхні відкриті TCP-порти.",
            "Чекай (або S5 — стоп). Тап по хосту — деталі; поля задають діапазон/порти.",
            "Живі IP; деталі показують RTT + відкриті порти.",
            listOf("RTT" to "Час відгуку хоста, мс; менше = ближче/швидше.",
                "80/443" to "Веб-сервер (HTTP/HTTPS).",
                "22" to "SSH; 23 — telnet (небезпечно), 1883 — MQTT, 554 — камера RTSP."))
    ),

    "arp_scan" to HelpPair(
        ToolHelp("ARP Scan",
            "ARP across the subnet — finds hosts hidden from TCP/ICMP (sees more than Net Scan).",
            "Tap a host for MAC / vendor / type.",
            "IP + vendor; gateway marked, randomized phone MACs shown too.",
            listOf("MAC" to "Апаратна адреса; перші 3 байти = виробник (OUI).",
                "Vendor" to "Виробник за OUI (Apple/Samsung/TP-Link…).",
                "Random" to "Локально-адміністрований MAC — телефон із приватним MAC.")),
        ToolHelp("ARP Scan",
            "ARP по підмережі — знаходить хости, приховані від TCP/ICMP (бачить більше за Net Scan).",
            "Тап по хосту — MAC / виробник / тип.",
            "IP + виробник; шлюз позначено, рандомні MAC телефонів теж видно.",
            listOf("MAC" to "Апаратна адреса; перші 3 байти = виробник (OUI).",
                "Виробник" to "За OUI (Apple/Samsung/TP-Link…).",
                "Random" to "Локально-адмін. MAC — телефон із приватним MAC."))
    ),

    "net_info" to HelpPair(
        ToolHelp("Net Info",
            "Connection parameters + ping reachability check.",
            "Select to ping the gateway and 8.8.8.8.",
            "IP/GW/DNS/RSSI; GW-ping = router latency, Internet = online, FAIL = no reply.",
            listOf("GW ping" to "Затримка до роутера, мс; висока = проблема Wi-Fi.",
                "Internet" to "Пінг 8.8.8.8; є відповідь = онлайн.",
                "DNS" to "Сервер імен; якщо порожній — резолв не працюватиме.")),
        ToolHelp("Net Info",
            "Параметри з'єднання + перевірка досяжності пінгом.",
            "Обери, щоб пінганути шлюз і 8.8.8.8.",
            "IP/GW/DNS/RSSI; GW-ping = затримка роутера, Internet = онлайн, FAIL = нема відповіді.",
            listOf("GW ping" to "Затримка до роутера, мс; висока = проблема Wi-Fi.",
                "Internet" to "Пінг 8.8.8.8; є відповідь = онлайн.",
                "DNS" to "Сервер імен; порожній — резолв не працюватиме."))
    ),

    "mdns" to HelpPair(
        ToolHelp("mDNS",
            "Finds devices advertising services (routers, Chromecast, printers, NAS).",
            "Wait; tap a service for details.",
            "\"name :port\"; details show service type, IP, port — tells you WHAT a device is.",
            listOf("_tcp type" to "Тип сервісу (_http, _airplay, _ipp printer…).",
                "Port" to "TCP-порт сервісу для підключення.")),
        ToolHelp("mDNS",
            "Знаходить пристрої, що анонсують сервіси (роутери, Chromecast, принтери, NAS).",
            "Чекай; тап по сервісу — деталі.",
            "«ім'я :порт»; деталі — тип сервісу, IP, порт — каже, ЩО це за пристрій.",
            listOf("_tcp тип" to "Тип сервісу (_http, _airplay, _ipp принтер…).",
                "Порт" to "TCP-порт сервісу для підключення."))
    ),

    "ssdp" to HelpPair(
        ToolHelp("SSDP / UPnP",
            "M-SEARCH finds UPnP devices often invisible to mDNS (smart TVs, media, routers).",
            "Tap a device; in details, select opens its LOCATION URL in HTTP GET.",
            "SERVER + IP; details show device type + description URL.",
            listOf("ST" to "Search Target — клас пристрою UPnP.",
                "LOCATION" to "URL XML-опису пристрою (відкрий у HTTP GET).")),
        ToolHelp("SSDP / UPnP",
            "M-SEARCH знаходить UPnP-пристрої, часто невидимі для mDNS (ТВ, медіа, роутери).",
            "Тап по пристрою; у деталях вибір відкриває LOCATION у HTTP GET.",
            "SERVER + IP; деталі — тип пристрою + URL опису.",
            listOf("ST" to "Search Target — клас UPnP-пристрою.",
                "LOCATION" to "URL XML-опису (відкрий у HTTP GET)."))
    ),

    "dns_lookup" to HelpPair(
        ToolHelp("DNS Lookup",
            "Resolves a domain name to an IP address.",
            "Enter a host, then Resolve; select repeats.",
            "\"IP: …\" resolved; \"Not found\" = no such name or DNS down.",
            listOf("A record" to "IPv4-адреса домену.",
                "Not found" to "Домену нема або DNS недоступний.")),
        ToolHelp("DNS Lookup",
            "Резолвить доменне ім'я в IP-адресу.",
            "Введи хост, потім Resolve; вибір повторює.",
            "«IP: …» — знайдено; «Not found» = нема імені або DNS не працює.",
            listOf("A-запис" to "IPv4-адреса домену.",
                "Not found" to "Домену нема або DNS недоступний."))
    ),

    "traceroute" to HelpPair(
        ToolHelp("Traceroute",
            "Incrementing-TTL ICMP: hops to the target + each hop's RTT. Default 8.8.8.8.",
            "S5 stops/restarts on the board.",
            "Each row = one hop (IP + ms); a '*' hop = no reply / filtered.",
            listOf("Hop" to "Маршрутизатор на шляху (за зростанням TTL).",
                "RTT" to "Затримка до цього хопу, мс.",
                "*" to "Хоп не відповів (фаєрвол/ICMP заблоковано).")),
        ToolHelp("Traceroute",
            "ICMP зі зростанням TTL: хопи до цілі + RTT кожного. Типово 8.8.8.8.",
            "S5 — стоп/рестарт на платі.",
            "Рядок = хоп (IP + мс); хоп '*' = нема відповіді / фільтр.",
            listOf("Хоп" to "Маршрутизатор на шляху (за зростанням TTL).",
                "RTT" to "Затримка до цього хопу, мс.",
                "*" to "Хоп не відповів (фаєрвол/ICMP заблоковано)."))
    ),

    "link_qual" to HelpPair(
        ToolHelp("Link Qual",
            "Periodic ping: RTT min/avg/max, jitter, packet loss + graph.",
            "Board: S2 cycles target, S5 resets.",
            "Numeric stats here; graph on the board.",
            listOf("Jitter" to "Розкид затримки; високий = нестабільний зв'язок (погано для дзвінків).",
                "Loss %" to "Втрачені пакети; >1% — проблема.",
                "avg RTT" to "Середня затримка, мс.")),
        ToolHelp("Link Qual",
            "Періодичний пінг: RTT мін/сер/макс, джитер, втрати + графік.",
            "Плата: S2 — ціль, S5 — скид.",
            "Числа тут; графік на платі.",
            listOf("Джитер" to "Розкид затримки; високий = нестабільно (дзвінки страждають).",
                "Втрати %" to "Втрачені пакети; >1% — проблема.",
                "сер RTT" to "Середня затримка, мс."))
    ),

    "http_get" to HelpPair(
        ToolHelp("HTTP GET",
            "GET request to a device web API on the network.",
            "Enter a URL, then GET; select repeats.",
            "\"Status 200\" green = ok; 4xx/5xx red = error. Below — response start.",
            listOf("200" to "OK; 301/302 — редирект.",
                "401/403" to "Потрібна авторизація / доступ заборонено.",
                "404" to "Нема сторінки; 5xx — помилка сервера.")),
        ToolHelp("HTTP GET",
            "GET-запит до веб-API пристрою в мережі.",
            "Введи URL, потім GET; вибір повторює.",
            "«Status 200» зелений = ок; 4xx/5xx червоний = помилка. Нижче — початок відповіді.",
            listOf("200" to "OK; 301/302 — редирект.",
                "401/403" to "Потрібна авторизація / доступ заборонено.",
                "404" to "Нема сторінки; 5xx — помилка сервера."))
    ),

    "http_post" to HelpPair(
        ToolHelp("HTTP POST",
            "POSTs data to a web API (e.g. change a device state).",
            "Set URL, then body (JSON), then POST.",
            "\"Status 2xx\" accepted; below — response.",
            listOf("Body" to "Корисне навантаження (зазвичай JSON).",
                "2xx" to "Прийнято; 400 — некоректне тіло.")),
        ToolHelp("HTTP POST",
            "POST-ить дані у веб-API (напр., змінити стан пристрою).",
            "Задай URL, потім тіло (JSON), потім POST.",
            "«Status 2xx» прийнято; нижче — відповідь.",
            listOf("Body" to "Навантаження (зазвичай JSON).",
                "2xx" to "Прийнято; 400 — некоректне тіло."))
    ),

    "http_sec" to HelpPair(
        ToolHelp("HTTP Sec",
            "Checks HSTS/CSP/X-Frame/X-Content/Referrer/Permissions headers + a score.",
            "Board S5 repeats.",
            "Score X/6 + which headers are present.",
            listOf("HSTS" to "Форс HTTPS; захист від downgrade.",
                "CSP" to "Content-Security-Policy — захист від XSS.",
                "X-Frame" to "Захист від clickjacking (framing).",
                "Score" to "X/6 наявних захисних заголовків.")),
        ToolHelp("HTTP Sec",
            "Перевіряє заголовки HSTS/CSP/X-Frame/X-Content/Referrer/Permissions + оцінку.",
            "Плата S5 — повтор.",
            "Оцінка X/6 + які заголовки присутні.",
            listOf("HSTS" to "Форс HTTPS; захист від downgrade.",
                "CSP" to "Content-Security-Policy — захист від XSS.",
                "X-Frame" to "Захист від clickjacking.",
                "Score" to "X/6 наявних захисних заголовків."))
    ),

    "tls_cert" to HelpPair(
        ToolHelp("TLS Cert",
            "TLS handshake on :443 — subject, issuer, validity. No trust-chain check.",
            "Board S5 repeats.",
            "CN / issuer / expiry date.",
            listOf("CN" to "Common Name — для якого домену виданий.",
                "Issuer" to "Хто видав (Let's Encrypt, DigiCert…).",
                "Expires" to "Дата закінчення; прострочений = небезпечно.")),
        ToolHelp("TLS Cert",
            "TLS-хендшейк на :443 — subject, issuer, термін. Без перевірки ланцюга довіри.",
            "Плата S5 — повтор.",
            "CN / видавець / дата закінчення.",
            listOf("CN" to "Common Name — для якого домену виданий.",
                "Issuer" to "Хто видав (Let's Encrypt, DigiCert…).",
                "Expires" to "Дата закінчення; прострочений = небезпечно."))
    ),

    "tcp_term" to HelpPair(
        ToolHelp("TCP Terminal",
            "Raw TCP client to host:port (like telnet).",
            "Enter target, Connect; type lines, Send; select reconnects.",
            "Board streams received bytes; sent lines marked \"> \".",
            listOf("host:port" to "Ціль підключення (напр. 192.168.1.1:80).",
                "> line" to "Твій надісланий рядок.")),
        ToolHelp("TCP Terminal",
            "Сирий TCP-клієнт до host:port (як telnet).",
            "Введи ціль, Connect; пиши рядки, Send; вибір — реконект.",
            "Плата стрімить прийняті байти; надіслані рядки з «> ».",
            listOf("host:port" to "Ціль (напр. 192.168.1.1:80).",
                "> рядок" to "Твій надісланий рядок."))
    ),

    "captive" to HelpPair(
        ToolHelp("Captive",
            "204-probe: detects hotel/airport portal interception; can best-effort auto-accept.",
            "Select = auto-accept (captive) or re-check; S2 exits.",
            "Verdict open / captive / no-net; shows portal URL + result.",
            listOf("open" to "Прямий інтернет, порталу нема.",
                "captive" to "Перехоплення — треба пройти портал.",
                "no-net" to "Мережа без інтернету.")),
        ToolHelp("Captive",
            "204-проба: виявляє портал готелю/аеропорту; може спробувати авто-прийняти.",
            "Вибір = авто-прийняти (captive) або пере-перевірка; S2 — вихід.",
            "Вердикт open / captive / no-net; показує URL порталу + результат.",
            listOf("open" to "Прямий інтернет, порталу нема.",
                "captive" to "Перехоплення — треба пройти портал.",
                "no-net" to "Мережа без інтернету."))
    ),

    "wol" to HelpPair(
        ToolHelp("Wake on LAN",
            "Magic packet to power on a device by MAC (needs WoL support in the target).",
            "Enter a MAC, Wake; select repeats. Board: S1 cycles saved presets.",
            "\"Magic packet sent\" = went out (waking depends on the target NIC).",
            listOf("MAC" to "Апаратна адреса цілі (AA:BB:CC:DD:EE:FF).",
                "Sent" to "Пакет пішов; реальне пробудження — лише якщо ціль підтримує WoL.",
                "WoL" to "Ethernet — надійно; WiFi/телефони — майже ніколи.")),
        ToolHelp("Wake on LAN",
            "Magic-пакет для вмикання пристрою за MAC (ціль має підтримувати WoL).",
            "Введи MAC, Wake; вибір повторює. Плата: S1 гортає збережені пресети.",
            "«Magic packet sent» = пішов (пробудження залежить від мережевої карти цілі).",
            listOf("MAC" to "Апаратна адреса цілі (AA:BB:CC:DD:EE:FF).",
                "Sent" to "Пакет пішов; реальне пробудження лише якщо ціль підтримує WoL.",
                "WoL" to "Ethernet — надійно; WiFi/телефони — майже ніколи."))
    ),

    "bt_scan" to HelpPair(
        ToolHelp("BT Scan",
            "Inventory of nearby Classic Bluetooth devices.",
            "Select rescans.",
            "Name / address / RSSI.",
            listOf("RSSI" to "Сила BT-сигналу; ближче до 0 = ближче пристрій.",
                "Address" to "BT MAC пристрою.")),
        ToolHelp("BT Scan",
            "Інвентар класичних Bluetooth-пристроїв поруч.",
            "Вибір — ре-скан.",
            "Ім'я / адреса / RSSI.",
            listOf("RSSI" to "Сила BT-сигналу; ближче до 0 = ближче пристрій.",
                "Адреса" to "BT MAC пристрою."))
    ),

    "ble_scan" to HelpPair(
        ToolHelp("BLE Scan",
            "Nearby BLE devices; select one for its GATT services.",
            "Tap a device.",
            "GATT service details on the board screen.",
            listOf("GATT" to "Профіль сервісів BLE-пристрою.",
                "RSSI" to "Сила BLE-сигналу.",
                "Addr type" to "Public або Random (приватність).")),
        ToolHelp("BLE Scan",
            "BLE-пристрої поруч; вибери один — його GATT-сервіси.",
            "Тап по пристрою.",
            "Деталі GATT-сервісів на екрані плати.",
            listOf("GATT" to "Профіль сервісів BLE-пристрою.",
                "RSSI" to "Сила BLE-сигналу.",
                "Тип адреси" to "Public або Random (приватність)."))
    ),

    "tracker_detect" to HelpPair(
        ToolHelp("Tracker Detect",
            "Finds nearby item trackers (AirTag, Tile, SmartTag) by their BLE advertising.",
            "Walk around; persistent trackers following you are flagged.",
            "Tracker type + RSSI; rising RSSI over time = it's moving with you.",
            listOf("AirTag" to "Apple Find My tracker (BLE).",
                "RSSI trend" to "Росте разом із тобою = ймовірно підкинутий.",
                "Persistent" to "Бачений багато разів у різних місцях.")),
        ToolHelp("Tracker Detect",
            "Знаходить трекери поруч (AirTag, Tile, SmartTag) за їхнім BLE-анонсом.",
            "Походи навколо; стійкі трекери, що йдуть за тобою, позначаються.",
            "Тип трекера + RSSI; ріст RSSI у часі = він рухається з тобою.",
            listOf("AirTag" to "Apple Find My трекер (BLE).",
                "RSSI-тренд" to "Росте разом із тобою = ймовірно підкинутий.",
                "Стійкий" to "Бачений багато разів у різних місцях."))
    ),

    "camera_finder" to HelpPair(
        ToolHelp("Camera Finder",
            "Flags likely IP cameras on the network by OUI vendor + camera ports (RTSP 554, ONVIF).",
            "Run on the LAN; suspected cameras are listed.",
            "IP + vendor + why-flagged (port/OUI).",
            listOf("RTSP 554" to "Порт відеопотоку камери.",
                "OUI" to "Виробник за MAC (Hikvision, Dahua…).",
                "ONVIF" to "Стандарт керування IP-камерами.")),
        ToolHelp("Camera Finder",
            "Позначає ймовірні IP-камери в мережі за OUI-виробником + портами (RTSP 554, ONVIF).",
            "Запусти в LAN; підозрілі камери в списку.",
            "IP + виробник + причина (порт/OUI).",
            listOf("RTSP 554" to "Порт відеопотоку камери.",
                "OUI" to "Виробник за MAC (Hikvision, Dahua…).",
                "ONVIF" to "Стандарт керування IP-камерами."))
    ),

    "card_skimmer" to HelpPair(
        ToolHelp("Card Skimmer",
            "Detects Bluetooth skimmer modules (HC-05/HC-06) commonly hidden in gas pumps / ATMs.",
            "Scan near the terminal; known skimmer names/MACs are flagged.",
            "Device name + MAC; default HC-05/06 names are a red flag.",
            listOf("HC-05/06" to "Дешеві BT-модулі — типові у скімерах.",
                "Default name" to "Незмінена заводська назва = підозріло.")),
        ToolHelp("Card Skimmer",
            "Виявляє Bluetooth-скімери (HC-05/HC-06), сховані в колонках АЗС / банкоматах.",
            "Скануй біля терміналу; відомі імена/MAC скімерів позначаються.",
            "Ім'я пристрою + MAC; дефолтні HC-05/06 — червоний прапорець.",
            listOf("HC-05/06" to "Дешеві BT-модулі — типові у скімерах.",
                "Дефолт-ім'я" to "Незмінена заводська назва = підозріло."))
    ),

    "attacker_detect" to HelpPair(
        ToolHelp("Attacker Detect",
            "Watches for hostile WiFi activity: deauth floods, evil-twin APs, beacon spam.",
            "Leave running near the network you protect.",
            "Alert type + source; distinguishes attack from normal noise.",
            listOf("Evil twin" to "Клон твоєї мережі з тим же SSID.",
                "Beacon spam" to "Флуд фейкових точок (Marauder/атака).",
                "Deauth flood" to "Масовий розрив клієнтів.")),
        ToolHelp("Attacker Detect",
            "Стежить за ворожою WiFi-активністю: deauth-флуд, evil-twin, beacon-спам.",
            "Лиши працювати біля мережі, яку захищаєш.",
            "Тип тривоги + джерело; відрізняє атаку від звичайного шуму.",
            listOf("Evil twin" to "Клон твоєї мережі з тим же SSID.",
                "Beacon spam" to "Флуд фейкових точок (Marauder/атака).",
                "Deauth-флуд" to "Масовий розрив клієнтів."))
    ),

    "rfid" to HelpPair(
        ToolHelp("RFID (RC522)",
            "13.56 MHz tag read + Mifare Classic security audit (default-key dictionary) via the UNO's RC522.",
            "Tap a card on the reader; audit runs the known-key dictionary across sectors.",
            "UID + type; audit verdict SECURED / PARTIAL / WIDE-OPEN + cracked/total sectors.",
            listOf("UID" to "Унікальний ID картки (hex).",
                "Classic 1K/4K" to "Тип Mifare; 1K = 16 секторів.",
                "WIDE-OPEN" to "Усі сектори на дефолтних ключах — клонується.",
                "SECURED" to "Дефолтні ключі не підійшли.")),
        ToolHelp("RFID (RC522)",
            "Читання 13.56 МГц-тегів + аудит безпеки Mifare Classic (словник дефолт-ключів) через RC522 на UNO.",
            "Піднеси картку до рідера; аудит проганяє відомі ключі по секторах.",
            "UID + тип; вердикт SECURED / PARTIAL / WIDE-OPEN + зламано/всього секторів.",
            listOf("UID" to "Унікальний ID картки (hex).",
                "Classic 1K/4K" to "Тип Mifare; 1K = 16 секторів.",
                "WIDE-OPEN" to "Усі сектори на дефолтних ключах — клонується.",
                "SECURED" to "Дефолтні ключі не підійшли."))
    ),

    "em_field" to HelpPair(
        ToolHelp("EM Field",
            "Coil + diode on GPIO36 reads induced EM field (mV pp per 50 Hz cycle). Comparative EMI finder, not calibrated.",
            "Hold coil near a source. Calibrate: hold AWAY then S5 captures the noise floor as zero. S2 exits.",
            "Big number = field above noise; peak = max; zero = baseline. Auto-scales.",
            listOf("mV pp" to "Розмах наведеної напруги; більше = сильніше поле.",
                "Zero" to "Збережений фон (калібрування S5).",
                "Peak" to "Максимум за сесію.")),
        ToolHelp("EM Field",
            "Котушка + діод на GPIO36 міряє наведене ЕМ-поле (mV pp за період 50 Гц). Порівняльний EMI-шукач, не калібрований.",
            "Тримай котушку біля джерела. Калібрування: тримай ОСТОРОНЬ, потім S5 запам'ятає фон як нуль. S2 — вихід.",
            "Велике число = поле над шумом; peak = макс; zero = база. Авто-шкала.",
            listOf("mV pp" to "Розмах наведеної напруги; більше = сильніше поле.",
                "Zero" to "Збережений фон (калібрування S5).",
                "Peak" to "Максимум за сесію."))
    ),

    "battery" to HelpPair(
        ToolHelp("Battery Diag",
            "Li-ion monitor + test: voltage, internal resistance (sag under load), health.",
            "Next scrolls pages; select resets stats or runs the DIAG test.",
            "Voltage / % / trend; after test — Rint, Health, verdict.",
            listOf("Voltage" to "4.2 повна, 3.7 номінал, 3.3 майже пуста.",
                "Rint" to "Внутрішній опір за просадкою; росте = старіння.",
                "Health" to "Оцінка стану за просадкою під навантаженням.")),
        ToolHelp("Battery Diag",
            "Монітор + тест Li-ion: напруга, внутрішній опір (просадка під навантаженням), здоров'я.",
            "Далі гортає сторінки; вибір скидає статистику або запускає DIAG-тест.",
            "Напруга / % / тренд; після тесту — Rint, Health, вердикт.",
            listOf("Напруга" to "4.2 повна, 3.7 номінал, 3.3 майже пуста.",
                "Rint" to "Внутрішній опір за просадкою; росте = старіння.",
                "Health" to "Оцінка стану за просадкою під навантаженням."))
    ),

    "self_test" to HelpPair(
        ToolHelp("Self Test",
            "Runs all non-radio network modules, saves a report each to /reports (bot syncs to Telegram).",
            "S5 start. At end: S2 scrolls results, S5 repeats, back exits.",
            "Progress N/12; then each module's result page by page.",
            listOf("N/12" to "Прогрес прогону модулів.",
                "Report" to "Кожен результат зберігається в Архів (і в бота).")),
        ToolHelp("Self Test",
            "Проганяє всі не-радіо мережеві модулі, зберігає звіт кожного в /reports (бот синкає в Telegram).",
            "S5 — старт. Наприкінці: S2 гортає результати, S5 повтор, back — вихід.",
            "Прогрес N/12; далі результат кожного модуля посторінково.",
            listOf("N/12" to "Прогрес прогону модулів.",
                "Звіт" to "Кожен результат — в Архів (і в бота)."))
    ),

    "system" to HelpPair(
        ToolHelp("System",
            "ESP32 hardware & runtime: chip/cores/clock/flash/ID and uptime/heap/reset/temperature.",
            "Next scrolls CHIP <-> RUNTIME.",
            "CHIP: model/cores/clock/flash/ID. RUNTIME: uptime/heap/reset reason/temp.",
            listOf("Heap" to "Вільна RAM; мало = ризик збою (напр. TLS хоче ~45 КБ).",
                "Temp" to "Внутрішній сенсор (некалібр., ±кілька °C).",
                "Reset" to "Причина останнього ресету (Panic/Brownout/WDT…).")),
        ToolHelp("System",
            "Залізо й рантайм ESP32: чип/ядра/частота/флеш/ID та uptime/heap/ресет/температура.",
            "Далі гортає CHIP <-> RUNTIME.",
            "CHIP: модель/ядра/частота/флеш/ID. RUNTIME: uptime/heap/причина ресету/темп.",
            listOf("Heap" to "Вільна RAM; мало = ризик збою (TLS хоче ~45 КБ).",
                "Temp" to "Внутрішній сенсор (некалібр., ±кілька °C).",
                "Reset" to "Причина останнього ресету (Panic/Brownout/WDT…)."))
    ),

    "remote" to HelpPair(
        ToolHelp("Remote",
            "Shows the PIN and keeps the control channel active (WiFi AP + STA).",
            "Next = keep on and exit; Select = confirm before turning off.",
            "6-digit PIN for login; \"authorized\" = a client is connected.",
            listOf("PIN" to "6 цифр для входу з телефона/додатка.",
                "AP" to "Точка плати (192.168.4.1) для прямого підключення.",
                "authorized" to "Клієнт залогінений і керує.")),
        ToolHelp("Remote",
            "Показує PIN і тримає канал керування активним (WiFi AP + STA).",
            "Далі = лишити ввімк. і вийти; Вибір = підтвердити вимкнення.",
            "6-значний PIN для входу; «authorized» = клієнт під'єднаний.",
            listOf("PIN" to "6 цифр для входу з телефона/додатка.",
                "AP" to "Точка плати (192.168.4.1) для прямого підключення.",
                "authorized" to "Клієнт залогінений і керує."))
    ),

    "scripts" to HelpPair(
        ToolHelp("Scripts (Berry)",
            "Runs .be Berry scripts on the board (native calls: display_*, wifi_*, ping, free_heap…). Output captured from print().",
            "Tap a script to run it; the print() output returns in a dialog. View source to inspect.",
            "{ok:true} + captured output, or an error line with the Berry message.",
            listOf(".be" to "Файл Berry-скрипта в LittleFS плати.",
                "print()" to "Вивід скрипта, що повертається в додаток.",
                "error" to "Повідомлення Berry-VM при збої.")),
        ToolHelp("Скрипти (Berry)",
            "Запускає .be Berry-скрипти на платі (нативи: display_*, wifi_*, ping, free_heap…). Вивід — із print().",
            "Тап по скрипту — запуск; вивід print() повертається у діалог. «Джерело» — переглянути код.",
            "{ok:true} + захоплений вивід, або рядок помилки з повідомленням Berry.",
            listOf(".be" to "Файл Berry-скрипта у LittleFS плати.",
                "print()" to "Вивід скрипта, що повертається в додаток.",
                "error" to "Повідомлення Berry-VM при збої."))
    ),
)
