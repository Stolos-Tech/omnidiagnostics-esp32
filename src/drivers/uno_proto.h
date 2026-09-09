// Протокол лінка ESP32 <-> Arduino UNO (текстові рядки по UART) — чиста логіка,
// тестується на хості. Дзеркальна копія живе в arduino/uno_r3/src/link_proto.h —
// при зміні формату рядка правити ОБИДВА файли.
//
// UNO -> ESP32 (телеметрія, кожен тип — окремий рядок):
//   "S <pot> <reed> <ir_hex>"      — базові датчики, ~10 Гц
//   "ENV <temp_x10> <hum_x10>"     — DHT11, фіксована крапка *10, ~1 Гц
//   "RFID <uid_hex>"               — новий зчитаний тег RC522 (4-байтний UID)
//
// ESP32 -> UNO (модульний хаб): "SCAN", "SET <slot> <args>".
// Актуаторні команди (SERVO/STEP/RELAY) прибрано разом з моторами.
#pragma once
#include <stdint.h>
#include <stddef.h>

struct UnoSensors {
  int      pot = 0;      // 0..1023
  bool     reed = false; // reed-датчик: замкнуто (магніт поруч) = true
  uint32_t ir = 0;       // код IR цього циклу (0 = нема нового)
};

struct UnoEnv {
  int temp_x10 = 0;  // температура *10 (°C), напр. 235 = 23.5°C
  int hum_x10 = 0;   // вологість *10 (%)
};

// Парсить рядок стану базових датчиків. false — не той формат.
bool uno_parse_sensors(const char* line, UnoSensors* out);

// Парсить рядок DHT11 (ENV). false — не той формат.
bool uno_parse_env(const char* line, UnoEnv* out);

// Парсить рядок джойстика "JOY <x> <y> <sw>" (x,y 0..1023, sw 0/1). Джойстик висить
// на UNO (A1/A2/D7); ESP конвертує осі в навігацію меню. false — не той формат.
bool uno_parse_joy(const char* line, int* out_x, int* out_y, int* out_sw);

// Парсить метрики UNO "STAT <free_ram> <uptime_s>". false — не той формат.
bool uno_parse_stat(const char* line, int* out_free_ram, uint32_t* out_uptime_s);

// Парсить рядок нового RFID-тега (RFID <uid_hex>). false — не той формат/нема uid.
bool uno_parse_rfid(const char* line, uint32_t* out_uid);

// --- Модульний хаб (Фаза 1): самоописовий шар ---------------------------------
// Дзеркало link_proto (UNO). UNO оголошує можливості рядком "CAP <slot> <type>
// <name> <present>"; узагальнене показання — "EVT <slot> <value>". ESP керує
// через "SET <slot> <args>" і просить перелік через "SCAN".
struct UnoModule {
  int  slot = -1;
  char type[10] = {0};    // вільний рядок типу: "analog","switch","env","rfid","radio","i2c"
  char name[14] = {0};    // людська назва
  bool present = false;   // фізично є/детектовано
  char value[20] = {0};   // останній EVT (Фаза 1: переважно порожній — телеметрія йде старим S/ENV)
};

// Парсить CAP. present опційний (нема -> 1). false — не той формат.
bool uno_parse_cap(const char* line, int* out_slot, char* out_type, size_t type_sz,
                   char* out_name, size_t name_sz, int* out_present);
// Парсить EVT (value = решта рядка після слота). false — не той формат.
bool uno_parse_evt(const char* line, int* out_slot, char* out_value, size_t value_sz);

// Будує "SET <slot> <args>" (порожні args -> без хвостового пробілу).
int uno_build_set(int slot, const char* args, char* out, size_t out_size);
// Будує "SCAN".
int uno_build_scan(char* out, size_t out_size);

// --- Red-team RFID-аудит (дзеркало RFA-рядків з link_proto UNO) ----------------
// UNO -> ESP на тап карти:
//   RFA uid <hex> sak <hex> <type>
//   RFA sec <n> <A|B> <key6hex>
//   RFA end <cracked>/<total> <verdict>
struct RfAudit {
  char uid[21] = {0};
  char type[18] = {0};
  int  cracked = 0;
  int  total = 0;
  char verdict[14] = {0};   // SECURED/PARTIAL/WIDE-OPEN/NOT-CLASSIC
  uint32_t seq = 0;         // росте на кожен ЗАВЕРШЕНИЙ аудит
  bool valid = false;
};

bool uno_is_rfa(const char* line);   // будь-який "RFA " рядок (для накопичення тіла звіту)
bool uno_parse_rfa_uid(const char* line, char* uid, size_t uidsz, char* type, size_t typesz);
bool uno_parse_rfa_end(const char* line, int* cracked, int* total, char* verdict, size_t vsz);

// --- RFID write/clone (ESP-бік) ---
// Будує "WRITE <block> <32hex>" (16 байт даних у блок Mifare Classic).
int  uno_build_write(int block, const uint8_t* data16, char* out, size_t out_size);
// Парсить "WRES <block> <0|1>" — результат запису блоку.
bool uno_parse_wres(const char* line, int* out_block, int* out_ok);
