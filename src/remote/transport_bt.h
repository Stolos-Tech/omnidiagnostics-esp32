// Bluetooth Classic SPP-транспорт: BluetoothSerial з ім'ям "OmniDiag".
// Той самий протокол і той самий крок PIN-автентифікації, що й WiFi — сирі
// JSON-рядки йдуть у remote_handle_incoming. Device-only.
//
// Взаємовиключність: перед стартом цього транспорту ядро зупиняє WiFi.
#pragma once

// Піднімає BluetoothSerial (SPP) з ім'ям пристрою "OmniDiag".
// PIN уже має бути згенерований (session_set_mode(MODE_BT)) до виклику.
void transport_bt_start();

// Гасить BluetoothSerial.
void transport_bt_stop();

// Неблокуючий: читає доступні байти, збирає рядки, диспетчеризує. Щоцикл у loop().
void transport_bt_loop();

bool transport_bt_active();

// Надсилає JSON-стан підключеному клієнту (дзеркалення екрана), якщо є з'єднання.
void transport_bt_broadcast(const char* json);
