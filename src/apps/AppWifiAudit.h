#pragma once
#include "../core/AppBase.h"
#include "../core/ListView.h"

class Kernel;

// APP_WIFI_ENVIRONMENT_AUDIT - passive 2.4 GHz survey (receive only).
// Two levels: a list of networks -> a detail screen
// (BSSID, channel, RSSI, encryption).
class AppWifiAudit : public App {
public:
    explicit AppWifiAudit(Kernel& k) : _k(k) {}

    const char* title() const override { return "802.11 Auditor"; }
    void onEnter() override;
    void onExit()  override;
    void onEvent(Event e) override;
    void onTick(uint32_t nowMs) override;
    void onDraw(UIManager& ui) override;

private:
    enum View { LIST, DETAIL };
    Kernel&  _k;
    ListView _list;
    int      _n = -1;          // -1 = scan in progress
    View     _view = LIST;
    int      _detail = 0;      // network index in DETAIL view

    void drawDetail(UIManager& ui);
};
