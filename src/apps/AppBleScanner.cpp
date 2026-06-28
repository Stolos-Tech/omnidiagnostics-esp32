#include "AppBleScanner.h"
#include "../core/Kernel.h"
#include "../core/UIManager.h"
#include "config.h"
#include <NimBLEDevice.h>
#include <string.h>

struct BleEntry { char mac[18]; char name[18]; int rssi; char type[6]; uint8_t addrType; };
static const int    BLE_MAX = 32;
static BleEntry     g_dev[BLE_MAX];
static volatile int g_count = 0;
static NimBLEClient* g_client = nullptr;

// Classify an advertisement: Eddystone (0xFEAA) / Apple iBeacon / plain BLE.
static const char* classify(NimBLEAdvertisedDevice* d) {
    if (d->isAdvertisingService(NimBLEUUID((uint16_t)0xFEAA))) return "EDDY";
    if (d->haveManufacturerData()) {
        std::string m = d->getManufacturerData();
        if (m.size() >= 4 &&
            (uint8_t)m[0] == 0x4C && (uint8_t)m[1] == 0x00 &&
            (uint8_t)m[2] == 0x02 && (uint8_t)m[3] == 0x15) return "iBCN";
    }
    return "BLE";
}

// Scan callback (runs in the NimBLE task). Deduplicates by MAC.
class ScanCB : public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* d) override {
        std::string mac = d->getAddress().toString();
        for (int i = 0; i < g_count; i++)
            if (strncmp(g_dev[i].mac, mac.c_str(), 17) == 0) { g_dev[i].rssi = d->getRSSI(); return; }
        if (g_count >= BLE_MAX) return;
        BleEntry& e = g_dev[g_count];
        strncpy(e.mac, mac.c_str(), 17); e.mac[17] = 0;
        if (d->haveName()) { strncpy(e.name, d->getName().c_str(), 17); e.name[17] = 0; }
        else e.name[0] = 0;
        e.rssi = d->getRSSI();
        e.addrType = d->getAddress().getType();
        strncpy(e.type, classify(d), 5); e.type[5] = 0;
        g_count++;
    }
};
static ScanCB g_cb;

void AppBleScanner::startScan() {
    g_count = 0;
    _list.reset();
    NimBLEScan* s = NimBLEDevice::getScan();
    s->setAdvertisedDeviceCallbacks(&g_cb, false);
    s->setActiveScan(false);              // passive - listen only
    s->setInterval(100);
    s->setWindow(99);
    s->start(0, nullptr, false);
}

void AppBleScanner::onEnter() {
    NimBLEDevice::init("");
    _view = SCAN;
    startScan();
    dirty = true;
}

void AppBleScanner::stopConn() {
    if (g_client) {
        if (g_client->isConnected()) g_client->disconnect();
        NimBLEDevice::deleteClient(g_client);
        g_client = nullptr;
    }
}

void AppBleScanner::onExit() {
    stopConn();
    NimBLEDevice::getScan()->stop();
    NimBLEDevice::deinit(true);
}

void AppBleScanner::doConnect() {
    NimBLEDevice::getScan()->stop();
    BleEntry& e = g_dev[_target];
    g_client = NimBLEDevice::createClient();
    _connOk = g_client->connect(NimBLEAddress(std::string(e.mac), e.addrType));
    if (_connOk) {
        std::vector<NimBLERemoteService*>* svcs = g_client->getServices(true);
        _svcCount = svcs ? (int)svcs->size() : 0;
    } else {
        stopConn();
    }
    _view = CONNECTED;     // show the result (success or failure)
}

void AppBleScanner::onEvent(Event e) {
    if (_view == SCAN) {
        if (e == Event::BACK)        { _k.goHome(); return; }
        if (e == Event::NEXT)        { _list.next(); dirty = true; }
        else if (e == Event::SELECT) {
            if (g_count > 0) { _target = _list.sel; _view = CONNECTING; _connPhase = 0; dirty = true; }
        }
    } else if (_view == CONNECTING) {
        if (e == Event::BACK) { _view = SCAN; startScan(); dirty = true; }
    } else { // CONNECTED
        if (e == Event::BACK) { stopConn(); _view = SCAN; startScan(); dirty = true; }
    }
}

void AppBleScanner::onTick(uint32_t now) {
    if (_view == CONNECTING) {
        if (_connPhase == 0) { _connPhase = 1; dirty = true; return; }  // let "Connecting" draw first
        if (_connPhase == 1) { doConnect(); _connPhase = 2; dirty = true; }
        return;
    }
    if (now - _lastDraw >= 600) {
        _lastDraw = now;
        if (_view == SCAN) { _list.count = g_count; _list.clampSel(); }
        dirty = true;
    }
}

void AppBleScanner::onDraw(UIManager& ui) {
    TFT_eSPI& tft = ui.tft();
    tft.setTextDatum(TL_DATUM);

    if (_view == CONNECTING) {
        ui.header("BLE CONNECT");
        ui.headerRight("BT", COL_ACCENT);
        ui.clearBody();
        tft.setTextColor(COL_TEXT, COL_BG);
        tft.drawString("Connecting...", 8, 44, 2);
        tft.setTextColor(COL_DIM, COL_BG);
        tft.drawString(g_dev[_target].mac, 8, 66, 1);
        ui.footer("OK(hold): cancel");
        return;
    }

    if (_view == CONNECTED) {
        ui.header("BLE DEVICE");
        ui.headerRight("BT", _connOk ? COL_OK : COL_WARN);
        ui.clearBody();
        BleEntry& e = g_dev[_target];
        int y = 28;
        tft.setTextColor(COL_ACCENT, COL_BG);
        tft.drawString(e.name[0] ? e.name : e.mac, 8, y, 2); y += 22;
        if (_connOk) {
            tft.setTextColor(COL_OK, COL_BG);
            tft.drawString("Connected", 8, y, 2); y += 22;
            tft.setTextColor(COL_TEXT, COL_BG);
            char b[36];
            snprintf(b, sizeof(b), "Services: %d", _svcCount);
            tft.drawString(b, 8, y, 2); y += 20;
            snprintf(b, sizeof(b), "RSSI: %d dBm  %s", e.rssi, e.type);
            tft.drawString(b, 8, y, 1);
        } else {
            tft.setTextColor(COL_WARN, COL_BG);
            tft.drawString("Connect failed", 8, y, 2);
        }
        ui.footer("OK(hold): back");
        return;
    }

    // ---- SCAN ----
    ui.header("BLE AUDITOR");
    ui.headerRight("BT", COL_OK);
    if (g_count == 0) {
        ui.clearBody();
        tft.setTextColor(COL_TEXT, COL_BG);
        tft.drawString("Passive scan...", 8, 44, 2);
        ui.footer("OK(hold): back");
        return;
    }
    _list.draw(ui, [&](int i, char* b, size_t n) {
        const char* nm = g_dev[i].name[0] ? g_dev[i].name : g_dev[i].mac;
        snprintf(b, n, "%.15s", nm);
    });
    int i = _list.sel;
    char f[36];
    snprintf(f, sizeof(f), "%ddBm %s OK:connect", g_dev[i].rssi, g_dev[i].type);
    ui.footer(f);
}
