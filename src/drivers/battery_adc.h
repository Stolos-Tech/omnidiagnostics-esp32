// Драйвер вимірювання напруги батареї: GPIO34 через дільник 100k/100k,
// ADC_EN=GPIO14. Калібрування через esp_adc_cal.
#pragma once
#include <stdint.h>

void battery_adc_init();

// Усереднене вимірювання (64 семпли). Повертає напругу батареї у вольтах.
// stddev  — розкид (шум) у вольтах, rawOut — середній сирий ADC,
// mvOut   — середня напруга на піні до дільника, мВ. Параметри опційні.
float battery_read(float* stddev = nullptr, uint32_t* rawOut = nullptr, float* mvOut = nullptr);

// Кешоване читання: якщо остання проба свіжіша за max_age_ms — повертає її БЕЗ
// нового 24мс-блокуючого заміру. Для періодичних/інформаційних НЕкритичних шляхів
// (статус-мірор, brownout-guard, REST /api), які інакше кожен робив би окремий
// замір; спільний кеш схлопує їх в один. battery_read() лишається БЕЗ кешу —
// діагностика (SAG/Rint під навантаженням) і живий монітор потребують свіжих проб.
float battery_read_cached(uint32_t max_age_ms, float* stddev = nullptr,
                          uint32_t* rawOut = nullptr, float* mvOut = nullptr);
