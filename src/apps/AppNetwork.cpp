#include "AppNetwork.h"
#include "../core/Kernel.h"
#include "../core/UIManager.h"
#include "config.h"
#include <WiFi.h>

void AppNetwork::connect() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    if (strlen(WIFI_SSID) > 0) WiFi.begin(WIFI_SSID, WIFI_PASS);
    _t0 = millis();
}

void AppNetwork::onEnter() { connect(); dirty = true; }

void AppNetwork::onExit() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
}

void AppNetwork::onEvent(Event e) {
    if (e == Event::BACK)        { _k.goHome(); return; }
    else if (e == Event::SELECT) { connect(); dirty = true; }   // reconnect
}

void AppNetwork::onTick(uint32_t now) {
    if (now - _lastDraw >= 500) { _lastDraw = now; dirty = true; }
}

void AppNetwork::onDraw(UIManager& ui) {
    TFT_eSPI& tft = ui.tft();
    ui.header("NETWORK / WIFI");
    ui.clearBody();
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(COL_TEXT, COL_BG);

    char buf[40];
    int y = 28;

    if (strlen(WIFI_SSID) == 0) {
        tft.setTextColor(COL_WARN, COL_BG);
        tft.drawString("No SSID set", 8, y, 2); y += 22;
        tft.setTextColor(COL_DIM, COL_BG);
        tft.drawString("edit WIFI_SSID/PASS in config.h", 8, y, 1);
        ui.footer("OK(hold): back");
        return;
    }

    snprintf(buf, sizeof(buf), "SSID: %.20s", WIFI_SSID);
    tft.drawString(buf, 8, y, 2); y += 22;

    wl_status_t st = WiFi.status();
    if (st == WL_CONNECTED) {
        tft.setTextColor(COL_OK, COL_BG);
        tft.drawString("Connected", 8, y, 2); y += 22;
        tft.setTextColor(COL_TEXT, COL_BG);
        snprintf(buf, sizeof(buf), "IP : %s", WiFi.localIP().toString().c_str());
        tft.drawString(buf, 8, y, 2); y += 20;
        snprintf(buf, sizeof(buf), "RSSI: %d dBm", WiFi.RSSI());
        tft.drawString(buf, 8, y, 2);
        ui.footer("OK:reconnect  hold:back");
    } else {
        uint32_t s = (millis() - _t0) / 1000;
        tft.setTextColor(COL_ACCENT, COL_BG);
        snprintf(buf, sizeof(buf), "Connecting... %us", s);
        tft.drawString(buf, 8, y, 2); y += 22;
        tft.setTextColor(COL_DIM, COL_BG);
        tft.drawString("OK to retry", 8, y, 1);
        ui.footer("OK:retry  hold:back");
    }
}
