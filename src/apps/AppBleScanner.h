#pragma once
#include "../core/AppBase.h"
#include "../core/ListView.h"

class Kernel;

// APP_BLE_BEACON_SCANNER - passive scan + connect to a chosen device.
// Three states: SCAN -> CONNECTING -> CONNECTED. Connections are only made
// to a device the user explicitly selects (own-device diagnostics);
// the app performs read-only service discovery, no writes.
class AppBleScanner : public App {
public:
    explicit AppBleScanner(Kernel& k) : _k(k) {}

    const char* title() const override { return "BLE Auditor"; }
    void onEnter() override;
    void onExit()  override;
    void onEvent(Event e) override;
    void onTick(uint32_t nowMs) override;
    void onDraw(UIManager& ui) override;

private:
    enum View { SCAN, CONNECTING, CONNECTED };
    Kernel&  _k;
    ListView _list;
    uint32_t _lastDraw = 0;
    View     _view = SCAN;
    int      _target = -1;
    int      _connPhase = 0;     // 0 = show "Connecting", 1 = perform connect
    bool     _connOk = false;
    int      _svcCount = 0;

    void startScan();
    void stopConn();
    void doConnect();
};
