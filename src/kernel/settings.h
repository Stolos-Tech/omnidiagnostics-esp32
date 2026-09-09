// Збереження стану/налаштувань ОС у NVS (Preferences). Device-only.
#pragma once

// Остання позиція меню — зберігається за ІМ'ЯМ застосунку (не за індексом): стійко
// до зміни складу/порядку меню (стале ім'я просто не знайдеться -> курсор 0), тож
// ручний MENU_LAYOUT_VERSION більше не потрібен.
const char* settings_cursor_name();          // ім'я застосунку під курсором ("" якщо нема)
void        settings_save_cursor_name(const char* name);  // пише лише при зміні

// Профіль: яскравість (0..255), таймаут сну (сек, 0=вимк), автозапуск (ім'я
// застосунку, ""=нема). Значення кешуються в RAM після settings_begin().
void settings_begin();              // завантажити профіль з NVS (виклик у setup)

int  settings_brightness();         // 0..255
void settings_set_brightness(int v);

int  settings_sleep_sec();          // 0=вимкнено
void settings_set_sleep_sec(int s);

const char* settings_autostart_name();          // ""=нема
void        settings_set_autostart_name(const char* name);  // nullptr/""=вимкнути

// Стелс: 1 = менше слідів у мережі (рандомний локально-адмін. MAC замість
// Espressif-OUI, нейтральний hostname, без mDNS-анонсу). 0 = звичайний режим.
int  settings_stealth();            // 0/1
void settings_set_stealth(int on);

// mDNS-анонс: стабільне ім'я <name>.local незалежно від DHCP-IP (не прив'язане до стелсу).
int  settings_mdns_enabled();            // 0/1
void settings_set_mdns_enabled(int on);
const char* settings_mdns_name();        // поточне ім'я (без ".local")
void settings_set_mdns_name(const char* n);

// Адреса Telegram-бота (host:port) для пуша звітів із плати.
const char* settings_bot_url();
void settings_set_bot_url(const char* u);

// Веб-UI (браузерний index.html): 1 = віддавати сторінку, 0 = лише REST/WS (керуєш із
// телефона/шлюзу, браузерний UI вимкнено -> менша поверхня атаки). REST/WS лишаються
// в обох режимах (їх юзає і телефон, і шлюз) — вимкнення НЕ звільняє WebServer, лише
// прибирає HTML-UI. Default 1.
int  settings_webui_enabled();      // 0/1
void settings_set_webui(int on);
