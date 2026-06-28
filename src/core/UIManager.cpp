#include "UIManager.h"
#include <Arduino.h>
#include "config.h"

void UIManager::begin() {
    _tft.init();
    _tft.setRotation(1);                 // landscape 240x135
    _tft.fillScreen(COL_BG);
    _tft.setTextColor(COL_TEXT, COL_BG);
    pinMode(PIN_VBAT, INPUT);
}

void UIManager::clear() {
    _tft.fillScreen(COL_BG);
}

void UIManager::clearBody() {
    // header = rows 0..22, footer = bottom 16 px; clear everything in between
    _tft.fillRect(0, 23, width(), height() - 23 - 16, COL_BG);
}

void UIManager::header(const char* title) {
    _tft.fillRect(0, 0, width(), 22, COL_ACCENT);
    _tft.setTextColor(COL_BG, COL_ACCENT);
    _tft.setTextDatum(ML_DATUM);
    _tft.drawString(title, 6, 11, 2);
    _tft.setTextColor(COL_TEXT, COL_BG);
    _tft.setTextDatum(TL_DATUM);
}

void UIManager::headerRight(const char* s, uint16_t col) {
    _tft.setTextColor(col, COL_ACCENT);
    _tft.setTextDatum(MR_DATUM);
    _tft.drawString(s, width() - 6, 11, 1);
    _tft.setTextColor(COL_TEXT, COL_BG);
    _tft.setTextDatum(TL_DATUM);
}

float UIManager::batteryVolts() {
    // 1:2 divider on the board. analogReadMilliVolts() returns calibrated mV.
    uint32_t mv = analogReadMilliVolts(PIN_VBAT);
    return (mv * 2.0f) / 1000.0f;
}

int UIManager::batteryPct() {
    float v = batteryVolts();
    // Rough linear estimate for a Li-Po between 3.30 and 4.20 V
    int pct = (int)((v - 3.30f) / (4.20f - 3.30f) * 100.0f);
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;
    return pct;
}

void UIManager::footer(const char* hint) {
    int y = height() - 16;
    _tft.fillRect(0, y, width(), 16, COL_BG);
    _tft.setTextColor(COL_DIM, COL_BG);

    if (hint) {
        _tft.setTextDatum(ML_DATUM);
        _tft.drawString(hint, 6, y + 8, 1);
    }

    char b[8];
    snprintf(b, sizeof(b), "%d%%", batteryPct());
    _tft.setTextDatum(MR_DATUM);
    _tft.drawString(b, width() - 6, y + 8, 1);

    _tft.setTextDatum(TL_DATUM);
    _tft.setTextColor(COL_TEXT, COL_BG);
}
