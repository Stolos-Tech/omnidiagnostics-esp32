#include <Arduino.h>
#include <esp_sleep.h>
#include "power.h"
#include "../drivers/display.h"

#define PIN_TFT_BL   4
#define PIN_WAKE_BTN 35              // права рідна кнопка
#define WAKE_GPIO    GPIO_NUM_35

void power_off() {
  // екран прощання
  TFT_eSprite& spr = display_sprite();
  spr.fillSprite(C_BG);
  spr.setTextDatum(MC_DATUM);
  spr.setTextColor(C_TEXT, C_BG);
  spr.drawString("POWER OFF", SCR_W/2, SCR_H/2 - 10, 4);
  spr.setTextColor(C_DIM, C_BG);
  spr.drawString("RIGHT button = power on", SCR_W/2, SCR_H/2 + 18, 2);
  display_push();
  delay(900);

  // панель у сон + підсвітка off
  display_tft().writecommand(0x28);  // DISPOFF
  display_tft().writecommand(0x10);  // SLPIN
  pinMode(PIN_TFT_BL, OUTPUT);
  digitalWrite(PIN_TFT_BL, LOW);

  // дочекатись відпускання обох кнопок, щоб не прокинутись одразу
  while (digitalRead(PIN_WAKE_BTN) == LOW || digitalRead(0) == LOW) delay(10);
  delay(80);

  esp_sleep_enable_ext0_wakeup(WAKE_GPIO, 0);
  esp_deep_sleep_start();             // сюди більше не повертаємось
}

void power_on_wake() {
  // якщо прокинулись натиском кнопки — дочекатись відпускання,
  // інакше перший же poll згенерує хибну подію гортання
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) {
    uint32_t t = millis();
    while (digitalRead(PIN_WAKE_BTN) == LOW && millis() - t < 3000) delay(10);
    delay(50);
  }
}
