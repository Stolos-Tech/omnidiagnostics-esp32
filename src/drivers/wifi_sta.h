// Драйвер WiFi-станції (STA): асинхронне сканування, підключення до мережі,
// збереження облікових даних у NVS. Незалежний від remote-транспорту, але вміє
// співіснувати з активним SoftAP (AP+STA), не рвучи його.
//
// УВАГА (апаратне обмеження ESP32): одне радіо -> SoftAP і STA мусять бути на
// одному каналі. Підключення STA до домашньої мережі може на мить перевести
// SoftAP на її канал (клієнт-телефон коротко перепідключиться). Прийнятно для хобі.
#pragma once
#include <stdint.h>

enum WifiStaState : uint8_t {
  WSTA_IDLE = 0,
  WSTA_CONNECTING,
  WSTA_CONNECTED,
  WSTA_FAILED
};

// Ініціалізація (виклик у setup). Вантажить збережені креденшали, але НЕ
// під'єднується автоматично (щоб не вмикати радіо без потреби).
void wifi_sta_begin();

// Асинхронне сканування. Вмикає STA (додає до AP, якщо той активний).
void wifi_sta_scan_start();

// Стан сканування: -2 помилка, -1 у процесі, >=0 кількість знайдених мереж.
int wifi_sta_scan_state();

// Результати сканування (валідні коли scan_state >= 0).
const char* wifi_sta_ssid(int i);   // "" якщо i поза межами
int         wifi_sta_rssi(int i);
bool        wifi_sta_is_open(int i); // мережа без пароля
const char* wifi_sta_enc_str(int i); // тип захисту: "Open"/"WEP"/"WPA"/"WPA2"/"WPA3"/... ("" поза межами)
int         wifi_sta_channel(int i);       // WiFi-канал (1..13), 0 якщо поза межами
const char* wifi_sta_bssid_str(int i);     // "AA:BB:CC:DD:EE:FF", "" якщо поза межами

// Почати підключення до мережі (неблокуюче). Порожній pw -> відкрита мережа.
void wifi_sta_connect(const char* ssid, const char* pw);

// Поточний стан підключення. Викликати щоцикл для оновлення станів.
WifiStaState wifi_sta_status();
void         wifi_sta_loop();

// Повністю зупиняє STA (відключення + вимкнення WiFi-радіо). Викликається
// координатором перед стартом BT, щоб гарантувати взаємовиключність радіо.
void wifi_sta_stop();

// Повертає STA після wifi_sta_stop(): вмикає радіо й реконектиться до останньої мережі
// (неблокуюче). Для on_exit() модулів, що глушать WiFi (BLE-скан/скімер/трекер).
void wifi_sta_resume();

const char* wifi_sta_ip();          // IP коли WSTA_CONNECTED, інакше ""
const char* wifi_sta_current_ssid();// SSID останнього connect

// Зберегти/забути креденшали в NVS (останнє підключення — окремо від списку нижче).
void wifi_sta_save();
void wifi_sta_save_network(const char* ssid, const char* pw);  // + до списку відомих
void wifi_sta_forget();
bool wifi_sta_has_saved();

// ---- Список ВІДОМИХ мереж (кілька, на відміну від "останньої" вище) ----
#define WIFI_STA_MAX_SAVED 5

int  wifi_sta_saved_count();
const char* wifi_sta_saved_ssid(int i);   // "" якщо поза межами
void wifi_sta_forget_network(int i);
bool wifi_sta_saved_auto(int i);          // чи авто-підключатись до цієї збереженої мережі
void wifi_sta_set_saved_auto(int i, bool on);
bool wifi_sta_is_known(const char* ssid); // true — пароль уже збережений, питати не треба

// Якщо ssid є серед відомих — одразу стартує підключення зі збереженим паролем
// (caller НЕ питає пароль) і повертає true. Інакше нічого не робить, повертає false.
bool wifi_sta_try_connect_saved(const char* ssid);

// Викликати РАЗ у setup() (після wifi_sta_begin): якщо є відомі мережі — синхронний
// скан і спроба підключитись до першої видимої відомої, без телефона/PIN.
void wifi_sta_autoconnect();
