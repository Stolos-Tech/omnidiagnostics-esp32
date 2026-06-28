#pragma once
#include "../core/AppBase.h"

class Kernel;

// APP_HARDWARE_MONITOR - state of the device itself:
// Li-Po voltage, charge %, free heap, die temperature, uptime.
class AppSysDashboard : public App {
public:
    explicit AppSysDashboard(Kernel& k) : _k(k) {}

    const char* title() const override { return "System Dashboard"; }
    void onEnter() override;
    void onEvent(Event e) override;
    void onTick(uint32_t nowMs) override;
    void onDraw(UIManager& ui) override;

private:
    Kernel& _k;
    uint32_t _lastRefresh = 0;
};
