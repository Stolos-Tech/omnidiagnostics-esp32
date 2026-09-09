// Координатор віддаленого режиму (device-only). Єдина точка, що гарантує
// взаємовиключність транспортів: активація одного завжди зупиняє протилежний.
// Керує session (режим/PIN) і фізичними транспортами (WiFi/BT) узгоджено.
#pragma once
#include "session.h"

// Активує режим: зупиняє протилежний транспорт (якщо був), генерує новий PIN
// (session_set_mode) і піднімає потрібний транспорт. MODE_OFF гасить усе.
// Повертає false, якщо активацію відхилено (напруга живлення критично
// низька для радіо — див. drivers/power_guard_hw) — режим лишається MODE_OFF.
bool remote_activate(RemoteMode mode);

// Поточний активний режим транспорту.
RemoteMode remote_active_mode();

// Обслуговування активного транспорту — щоцикл у loop(), неблокуюче.
void remote_loop();

// Дзеркалення стану активним транспортом (кому саме — вирішує координатор).
void remote_broadcast(const char* json);

// USB-міст активний: дзеркалити екран у кеш (s_last_state) навіть без WiFi-режиму,
// щоб керування по USB-кабелю бачило екран із будь-якого стану плати.
void remote_set_usb_active(bool active);
bool remote_usb_active();

// Готує сесію до USB-логіну статичним PIN (піднімає session-режим+персистентний PIN
// без радіо, якщо був OFF). Виклик перед session_authenticate(client_pin) по USB.
void remote_prepare_usb_auth();

// Відновлює мережу після апки, що захопила радіо в promiscuous-режим
// (channel monitor / wifi sniffer / deauth alert): якщо Remote:WiFi був
// активний (was_wifi=true) — перезапускає транспорт (SoftAP+web повертаються
// самі) і підхоплює STA авто-конектом. was_wifi=false -> no-op. Єдине джерело
// цієї логіки — апки лише викликають її у своєму on_exit().
void remote_restore_after_promiscuous(bool was_wifi);
