#pragma once
#include "../core/AppBase.h"

class Kernel;

// Connects to a WiFi network (station mode). Credentials live in config.h
// (WIFI_SSID / WIFI_PASS). Shows status, IP and RSSI.
// OK = reconnect, OK held = back.
class AppNetwork : public App {
public:
    explicit AppNetwork(Kernel& k) : _k(k) {}

    const char* title() const override { return "Network (WiFi)"; }
    void onEnter() override;
    void onExit()  override;
    void onEvent(Event e) override;
    void onTick(uint32_t nowMs) override;
    void onDraw(UIManager& ui) override;

private:
    Kernel&  _k;
    uint32_t _t0 = 0, _lastDraw = 0;
    void connect();
};
