// Ідентифікація пристрою в мережі за MAC (OUI-вендор) + евристика типу за
// вендором і відкритими портами. Чиста логіка, тестується на хості.
//
// OUI-таблиця — курований підмножина найпоширеніших вендорів (повна IEEE-база
// ~35k записів у флеш не влазить). Для невідомого OUI повертаємо "".
#pragma once
#include <stdint.h>
#include <stddef.h>

// Категорія вендора (підказка для визначення типу): маршрутизатор, телефон,
// комп'ютер, IoT/MCU, принтер, камера, динамік/каст, невідомо.
enum DevCat : char {
  DC_ROUTER = 'R', DC_PHONE = 'P', DC_COMPUTER = 'C', DC_IOT = 'I',
  DC_PRINTER = 'M', DC_CAMERA = 'V', DC_CAST = 'S', DC_UNKNOWN = 'U'
};

// Вендор за 3-байтним OUI (старші 3 байти MAC). "" якщо не в таблиці.
const char* net_oui_vendor(const uint8_t oui[3]);

// Категорія вендора за OUI (DC_UNKNOWN якщо не знайдено).
char net_oui_category(const uint8_t oui[3]);

// Здогад типу пристрою за: чи це шлюз, категорією вендора (з net_oui_category),
// чи MAC локально-адміністрований (біт 0x02 першого октета — ознака рандомізації
// MAC, майже завжди телефон/планшет), і списком відкритих портів. Повертає
// короткий людський рядок ("Router", "PC (Windows)", ...). Ніколи не nullptr.
const char* net_device_type(bool is_gateway, char vendor_cat, bool mac_local,
                            const uint16_t* open_ports, int port_count);

// true, якщо MAC локально-адміністрований (біт 0x02 у першому октеті).
bool net_mac_is_local(const uint8_t mac[6]);
