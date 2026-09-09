// Профіль конфігурації модулів: які модулі на яких платах «висять». Дозволяє АДАПТУВАТИ
// прошивку під конкретний стенд БЕЗ перекомпіляції — плата читає профіль (NVS, а на першому
// старті імпортує з SD /config/profile.json якщо є), а /api/modules та ініціалізація драйверів
// поважають його: вимкнений модуль НЕ пробується (швидше, без хибних зависань на голій платі).
// Редагується з додатка через POST /config/profile.
#pragma once

void        profile_begin();                 // завантажити з NVS (fallback: SD /config/profile.json)
bool        profile_get(const char* key);    // "nrf24","cc1101","sd","bt","uno","joy","rc522","dht","pot","reed"
int         profile_get_int(const char* key, int def);  // esp.<key> як число (напр. power-enable GPIO модуля); def якщо нема
void        profile_set_json(const char* json);  // застосувати+зберегти (NVS) новий профіль
const char* profile_json();                  // поточний профіль як JSON-рядок
bool        profile_loaded();                // чи є явний профіль (інакше — усе дозволено = автодетект)
