#include "AppWifiAudit.h"
#include "../core/Kernel.h"
#include "../core/UIManager.h"
#include "config.h"
#include <WiFi.h>

static const char* encShort(wifi_auth_mode_t m) {
    switch (m) {
        case WIFI_AUTH_OPEN:          return "OPEN";
        case WIFI_AUTH_WEP:           return "WEP";
        case WIFI_AUTH_WPA_PSK:       return "WPA";
        case WIFI_AUTH_WPA2_PSK:      return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK:  return "WPA2";
        case WIFI_AUTH_WPA3_PSK:      return "WPA3";
        case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA3";
        case WIFI_AUTH_WPA2_ENTERPRISE: return "ENT";
        default:                      return "?";
    }
}

// 0..4 bars from the RSSI value
static int rssiBars(int rssi) {
    if (rssi >= -55) return 4;
    if (rssi >= -65) return 3;
    if (rssi >= -75) return 2;
    if (rssi >= -85) return 1;
    return 0;
}

static void drawBars(TFT_eSPI& t, int x, int y, int bars) {
    for (int i = 0; i < 4; i++) {
        int h = 4 + i * 4;
        uint16_t c = (i < bars) ? COL_OK : 0x2104;
        t.fillRect(x + i * 7, y - h, 5, h, c);
    }
}

void AppWifiAudit::onEnter() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    WiFi.scanDelete();
    WiFi.scanNetworks(/*async=*/true, /*show_hidden=*/true);
    _n = -1;
    _view = LIST;
    _list.reset();
    dirty = true;
}

void AppWifiAudit::onExit() {
    WiFi.scanDelete();
    WiFi.mode(WIFI_OFF);
}

void AppWifiAudit::onEvent(Event e) {
    if (_view == LIST) {
        if (e == Event::BACK)        { _k.goHome(); return; }
        if (e == Event::NEXT)        { _list.next(); dirty = true; }
        else if (e == Event::SELECT) {
            if (_list.sel == 0) { onEnter(); }                 // "Rescan" row
            else if (_n > 0)    { _detail = _list.sel - 1; _view = DETAIL; dirty = true; }
        }
    } else { // DETAIL
        if (e == Event::BACK)        { _view = LIST; dirty = true; }
        else if (e == Event::SELECT) { _view = LIST; dirty = true; }
        else if (e == Event::NEXT)   { if (_n > 0) _detail = (_detail + 1) % _n; dirty = true; }
    }
}

void AppWifiAudit::onTick(uint32_t) {
    if (_n < 0) {
        int r = WiFi.scanComplete();
        if (r >= 0) { _n = r; _list.count = _n + 1; _list.clampSel(); dirty = true; }
    }
}

void AppWifiAudit::onDraw(UIManager& ui) {
    TFT_eSPI& tft = ui.tft();

    if (_n < 0) {
        ui.header("802.11 AUDITOR");
        ui.clearBody();
        tft.setTextDatum(TL_DATUM);
        tft.setTextColor(COL_TEXT, COL_BG);
        tft.drawString("Scanning 2.4 GHz...", 8, 44, 2);
        ui.footer("OK(hold): back");
        return;
    }

    if (_view == DETAIL) { drawDetail(ui); return; }

    // ---- LIST ----
    ui.header("802.11 AUDITOR");
    ui.headerRight("WiFi", COL_OK);
    _list.draw(ui, [&](int i, char* b, size_t n) {
        if (i == 0) { snprintf(b, n, "%s", "\xAB Rescan"); return; }   // << Rescan
        String s = WiFi.SSID(i - 1);
        if (s.length() == 0) s = "<hidden>";
        if (s.length() > 15)  s = s.substring(0, 15);
        snprintf(b, n, "%s", s.c_str());
    });

    if (_list.sel == 0) {
        ui.footer("OK: rescan");
    } else {
        int i = _list.sel - 1;
        char f[34];
        snprintf(f, sizeof(f), "%ddBm %s  OK:info",
                 WiFi.RSSI(i), encShort(WiFi.encryptionType(i)));
        ui.footer(f);
    }
}

void AppWifiAudit::drawDetail(UIManager& ui) {
    TFT_eSPI& tft = ui.tft();
    int i = _detail;
    ui.header("NETWORK INFO");
    ui.headerRight("WiFi", COL_OK);
    ui.clearBody();
    tft.setTextDatum(TL_DATUM);

    char buf[48];
    int y = 27;

    String s = WiFi.SSID(i);
    if (s.length() == 0) s = "<hidden>";
    if (s.length() > 20) s = s.substring(0, 20);
    tft.setTextColor(COL_ACCENT, COL_BG);
    tft.drawString(s.c_str(), 8, y, 2); y += 20;

    tft.setTextColor(COL_TEXT, COL_BG);
    snprintf(buf, sizeof(buf), "BSSID %s", WiFi.BSSIDstr(i).c_str());
    tft.drawString(buf, 8, y, 1); y += 14;

    int rssi = WiFi.RSSI(i);
    snprintf(buf, sizeof(buf), "RSSI  %d dBm", rssi);
    tft.drawString(buf, 8, y, 1);
    drawBars(tft, 150, y + 12, rssiBars(rssi));
    y += 16;

    snprintf(buf, sizeof(buf), "Enc   %s", encShort(WiFi.encryptionType(i)));
    tft.drawString(buf, 8, y, 1); y += 14;

    snprintf(buf, sizeof(buf), "Ch %d   Band 2.4G   %s",
             WiFi.channel(i), (WiFi.SSID(i).length() == 0) ? "hidden" : "");
    tft.drawString(buf, 8, y, 1);

    char f[30];
    snprintf(f, sizeof(f), "%d/%d  NEXT  OK:list", i + 1, _n);
    ui.footer(f);
}
