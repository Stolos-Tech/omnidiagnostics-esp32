// WiFi-транспорт: SoftAP + WebServer(80) + WebSocketsServer(81).
// Тонкий шар — тільки підіймає/гасить радіо й сервери і передає байти рядків
// у/з remote_dispatch (парсинг/логіка живуть у protocol/session). Device-only.
//
// Взаємовиключність: перед стартом цього транспорту ядро зупиняє протилежний (BT).
#pragma once

// Піднімає SoftAP "OmniDiag-Setup", веб-сервер (роздає /web/index.html) і WebSocket.
// PIN уже має бути згенерований (session_set_mode(MODE_WIFI)) до виклику.
void transport_wifi_start();

// Гасить сервери й SoftAP.
void transport_wifi_stop();

// Неблокуючий: handleClient() + webSocket.loop(). Викликати щоцикл у loop().
void transport_wifi_loop();

bool transport_wifi_active();

// Розсилає JSON-стан усім автентифікованим клієнтам (дзеркалення екрана).
void transport_wifi_broadcast(const char* json);

// Поточний WPA2-пароль SoftAP (згенерований при transport_wifi_start, як PIN).
// "" якщо транспорт не активний.
const char* transport_wifi_ap_password();

// USB-міст: повне керування платою по OTG-кабелю (serial REQ/RES). Реюз REST-handler'ів.
// Повертає HTTP-подібний статус, пише JSON у out (cap — розмір). 501 = шлях не наш.
#include <stddef.h>
int wifi_serial_dispatch(const char* method, const char* path, const char* body, char* out, size_t cap);
