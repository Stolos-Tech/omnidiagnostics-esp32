// Протокол лінка з боку UNO — дзеркало src/drivers/uno_proto.h (ESP32-проєкт).
// Формат рядків має ЗБІГАТИСЯ з тим файлом. Тут — протилежна половина:
// UNO БУДУЄ телеметрію (S/ENV/RFID) і ПАРСИТЬ команди хаба (SET/SCAN).
// Актуаторні команди (SERVO/STEP/RELAY) прибрано разом з моторами.
//
// UNO -> ESP32:  "S <pot> <reed> <ir_hex>"      ~10 Гц
//                "ENV <temp_x10> <hum_x10>"     ~0.5 Гц (DHT11)
//                "RFID <uid_hex>"               при новому тезі RC522
// ESP32 -> UNO:  "SCAN"  "SET <slot> <args>"    (модульний хаб, нижче)
#pragma once
#include <stdint.h>
#include <stddef.h>

int link_build_sensors(int pot, bool reed, uint32_t ir, char* out, size_t out_size);
int link_build_env(int temp_x10, int hum_x10, char* out, size_t out_size);
int link_build_rfid(uint32_t uid, char* out, size_t out_size);
// Джойстик "JOY <x> <y> <sw>" (x,y 0..1023 з ADC UNO, sw 0/1). ESP конвертує в навігацію.
int link_build_joy(int x, int y, int sw, char* out, size_t out_size);
// Метрики самої UNO "STAT <free_ram> <uptime_s>" — вільна SRAM (байт) + аптайм (с).
int link_build_stat(int free_ram, unsigned long uptime_s, char* out, size_t out_size);

// RFID write/clone: ESP32 -> UNO "WRITE <block> <32hex>" (16 байт у блок Mifare
// Classic), UNO -> ESP32 "WRES <block> <0|1>" (результат). UNO авторизує сектор
// відомими ключами (той самий словник, що аудит) і пише MIFARE_Write.
bool link_parse_write(const char* line, int* out_block, uint8_t* out_data16);
int  link_build_wres(int block, int ok, char* out, size_t out_size);

// --- Модульний хаб (Фаза 1): самоописовий шар поверх фіксованого протоколу ---
// UNO -> ESP32:  "CAP <slot> <type> <name> <present>"  — оголошення можливості
//                (present: 1=фізично є/детектовано, 0=нема)
//                "EVT <slot> <value>"                   — узагальнене показання
// ESP32 -> UNO:  "SET <slot> <args...>"                 — узагальнена дія
//                "SCAN"                                 — переслати всі CAP заново
int link_build_cap(int slot, const char* type, const char* name, int present, char* out, size_t out_size);
int link_build_evt(int slot, const char* value, char* out, size_t out_size);

// Парсить "SET <slot> <args>". args — усе після номера слота (без \n), може бути порожнім.
bool link_parse_set(const char* line, int* out_slot, char* out_args, size_t args_size);
// true, якщо рядок — команда "SCAN".
bool link_is_scan(const char* line);
