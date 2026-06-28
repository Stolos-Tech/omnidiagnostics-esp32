#include "AppSysDashboard.h"
#include "../core/Kernel.h"
#include "../core/UIManager.h"
#include "config.h"
#include <Arduino.h>

// Internal ESP32 temperature sensor (uncalibrated, approximate).
extern "C" uint8_t temprature_sens_read();
static float chipTempC() {
    return (temprature_sens_read() - 32) / 1.8f;   // the built-in sensor returns deg F
}

void AppSysDashboard::onEnter() {
    _lastRefresh = 0;
    dirty = true;
}

void AppSysDashboard::onEvent(Event e) {
    if (e == Event::BACK) _k.goHome();
}

void AppSysDashboard::onTick(uint32_t now) {
    if (now - _lastRefresh >= 1000) {     // refresh once per second
        _lastRefresh = now;
        dirty = true;
    }
}

void AppSysDashboard::onDraw(UIManager& ui) {
    TFT_eSPI& tft = ui.tft();
    ui.header("SYSTEM DASHBOARD");
    ui.clearBody();
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(COL_TEXT, COL_BG);

    char buf[40];
    int y = 28;

    snprintf(buf, sizeof(buf), "Battery : %.2f V  (%d%%)",
             ui.batteryVolts(), ui.batteryPct());
    tft.drawString(buf, 8, y, 2); y += 20;

    snprintf(buf, sizeof(buf), "Free RAM: %u KB", ESP.getFreeHeap() / 1024);
    tft.drawString(buf, 8, y, 2); y += 20;

    snprintf(buf, sizeof(buf), "Chip T  : %.1f C", chipTempC());
    tft.drawString(buf, 8, y, 2); y += 20;

    uint32_t s = millis() / 1000;
    snprintf(buf, sizeof(buf), "Uptime  : %02u:%02u:%02u",
             s / 3600, (s % 3600) / 60, s % 60);
    tft.drawString(buf, 8, y, 2);

    ui.footer("OK(hold): back");
}
