#include <Arduino.h>
#include <Preferences.h>
#include "hw_safe.h"

bool     g_hw_safe    = true;   // default safe: доки NVS не завантажено, нічого не жене
uint32_t g_hw_blocked = 0;      // маска пінів, драйв яких заблокував КЗ-детектор

struct DrivePin { uint8_t pin; uint8_t level; };
// піни, які активний режим драйвить в idle: усі CS/CSN deselect (HIGH), nRF24 CE idle low
static const DrivePin DRIVE[] = { {25, HIGH}, {33, HIGH}, {27, HIGH}, {12, LOW} };
static const int NDRIVE = 4;

// Чи безпечно драйвити pin у level.
//  1) pull-toggle (без драйву): якщо пін слухає внутрішній pull -> плаває (непід'єднаний або
//     high-Z вхід модуля) -> безпечно, без ризикованого драйву.
//  2) якщо утримується зовнішнім рівнем -> коротка драйв-верифікація (10µs): резистивний pull
//     (напр. 10k на SD-модулі) драйвер ПЕРЕБИВАЄ -> readback == level -> безпечно; жорсткий КЗ на
//     протилежну рейку перебити не можна -> readback != level -> НЕ драйвити.
static bool safe_to_drive(uint8_t pin, uint8_t level) {
    pinMode(pin, INPUT_PULLUP);   delayMicroseconds(50); int up = digitalRead(pin);
    pinMode(pin, INPUT_PULLDOWN); delayMicroseconds(50); int dn = digitalRead(pin);
    if (up != dn) { pinMode(pin, INPUT); return true; }        // плаває -> safe
    pinMode(pin, OUTPUT); digitalWrite(pin, level); delayMicroseconds(10);
    int rb = digitalRead(pin);
    pinMode(pin, INPUT);                                        // одразу відпустити
    return rb == level;                                        // перебили pull -> safe; КЗ -> ні
}

static void all_highz() {
    pinMode(2, INPUT); pinMode(15, INPUT);        // SPI SCK / MOSI
    pinMode(25, INPUT); pinMode(33, INPUT);       // nRF24 CSN / SD CS
    pinMode(27, INPUT); pinMode(12, INPUT);       // CC1101 CS / nRF24 CE
}

void hw_safe_apply_pins() {
    if (g_hw_safe) { all_highz(); return; }

    // Активація: спершу КЗ-перевірка. SCK/MOSI (2,15) драйвляться SPI обома рівнями -> перевіряємо
    // обидва напрямки (КЗ на будь-яку рейку небезпечний).
    g_hw_blocked = 0;
    if (!safe_to_drive(2, HIGH)  || !safe_to_drive(2, LOW))  g_hw_blocked |= (1u << 4);
    if (!safe_to_drive(15, HIGH) || !safe_to_drive(15, LOW)) g_hw_blocked |= (1u << 4);
    for (int i = 0; i < NDRIVE; i++)
        if (!safe_to_drive(DRIVE[i].pin, DRIVE[i].level)) g_hw_blocked |= (1u << i);

    if (g_hw_blocked) { g_hw_safe = true; all_highz(); return; }   // КЗ -> лишитись safe

    // Чисто -> драйвити idle-safe рівні
    all_highz();
    for (int i = 0; i < NDRIVE; i++) {
        pinMode(DRIVE[i].pin, OUTPUT);
        digitalWrite(DRIVE[i].pin, DRIVE[i].level);
    }
}

void hw_safe_begin() {
    Preferences p;
    if (p.begin("hwsafe", true)) { g_hw_safe = p.getBool("safe", true); p.end(); }
}

void hw_safe_set(bool safe) {
    g_hw_safe = safe;
    Preferences p;
    if (p.begin("hwsafe", false)) { p.putBool("safe", safe); p.end(); }
    hw_safe_apply_pins();   // при активації тут же зробить КЗ-перевірку й може відкотитись у safe
}
