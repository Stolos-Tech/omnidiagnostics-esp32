#pragma once
#include "../core/AppBase.h"
#include "../core/Kernel.h"
#include "../core/UIManager.h"
#include "config.h"

// Placeholder apps for features that require external hardware not yet wired.
// They render an info screen and return on Back. Replace each one with a full
// implementation once the corresponding module is connected.

// Shared helper: header + two text lines + footer.
static inline void drawStub(UIManager& ui, const char* hdr,
                            const char* line1, const char* line2) {
    TFT_eSPI& tft = ui.tft();
    ui.header(hdr);
    ui.clearBody();
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(COL_TEXT, COL_BG);
    tft.drawString(line1, 8, 36, 2);
    tft.setTextColor(COL_DIM, COL_BG);
    tft.drawString(line2, 8, 60, 1);
    ui.footer("OK(hold): back");
}

// ---- APP_RF_SPECTRAL_ANALYZER (stub) ----
// Passive sub-GHz monitoring; needs an external CC1101 (868 MHz) on HSPI.
class AppRfAuditor : public App {
public:
    explicit AppRfAuditor(Kernel& k) : _k(k) {}
    const char* title() const override { return "RF Sub-1GHz"; }
    void onEvent(Event e) override { if (e == Event::BACK) _k.goHome(); }
    void onDraw(UIManager& ui) override {
        drawStub(ui, "RF SUB-1GHZ",
                 "CC1101 not detected",
                 "Need ext. CC1101 (868MHz). HSPI: 25/26/27/33, GDO0=32.");
    }
private:
    Kernel& _k;
};

// ---- APP_IR_PROTOCOL_DEBUGGER (stub) ----
// IR capture/replay; needs IRremoteESP8266 and an IR LED on the TX pin.
class AppIrAnalyzer : public App {
public:
    explicit AppIrAnalyzer(Kernel& k) : _k(k) {}
    const char* title() const override { return "IR Analyzer"; }
    void onEvent(Event e) override { if (e == Event::BACK) _k.goHome(); }
    void onDraw(UIManager& ui) override {
        drawStub(ui, "IR ANALYZER",
                 "Capture NEC/RC5/Sony",
                 "TODO: IRremoteESP8266. RX=13 ready, TX=17 needs IR-LED.");
    }
private:
    Kernel& _k;
};
