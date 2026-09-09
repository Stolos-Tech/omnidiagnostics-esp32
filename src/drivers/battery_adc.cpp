#include <Arduino.h>
#include <esp_adc_cal.h>
#include "battery_adc.h"

#define PIN_ADC_BAT   34
#define PIN_ADC_EN    14
#define ADC_SAMPLES   64

// Підправ за мультиметром: real/shown * 2.0
static float DIVIDER_RATIO = 2.0f;

static esp_adc_cal_characteristics_t adc_chars;

void battery_adc_init() {
  pinMode(PIN_ADC_EN, OUTPUT);
  digitalWrite(PIN_ADC_EN, HIGH);
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_ADC_BAT, ADC_11db);
  esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_12, ADC_WIDTH_BIT_12, 1100, &adc_chars);
}

float battery_read(float* stddev, uint32_t* rawOut, float* mvOut) {
  digitalWrite(PIN_ADC_EN, HIGH);
  delay(5);
  double sum = 0, sum2 = 0, sumRaw = 0, sumMv = 0;
  for (int i = 0; i < ADC_SAMPLES; i++) {
    uint32_t raw = analogRead(PIN_ADC_BAT);
    uint32_t mv  = esp_adc_cal_raw_to_voltage(raw, &adc_chars);
    float v = (mv / 1000.0f) * DIVIDER_RATIO;
    sum += v; sum2 += (double)v * v; sumRaw += raw; sumMv += mv;
    delayMicroseconds(300);
  }
  float mean = sum / ADC_SAMPLES;
  if (stddev) { float var = sum2/ADC_SAMPLES - (double)mean*mean; *stddev = var>0 ? sqrtf(var) : 0; }
  if (rawOut) *rawOut = (uint32_t)(sumRaw / ADC_SAMPLES);
  if (mvOut)  *mvOut  = sumMv / ADC_SAMPLES;
  return mean;
}

float battery_read_cached(uint32_t max_age_ms, float* stddev, uint32_t* rawOut, float* mvOut) {
  static uint32_t last_ms = 0;
  static bool have = false;
  static float c_mean = 0, c_sd = 0, c_mv = 0;
  static uint32_t c_raw = 0;
  uint32_t now = millis();
  if (!have || now - last_ms >= max_age_ms) {
    c_mean = battery_read(&c_sd, &c_raw, &c_mv);
    last_ms = now; have = true;
  }
  if (stddev) *stddev = c_sd;
  if (rawOut) *rawOut = c_raw;
  if (mvOut)  *mvOut  = c_mv;
  return c_mean;
}
