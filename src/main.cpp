// =====================================================================
//  OmniDiagnostics-ESP32  ::  main
//  Modular micro-OS built around a finite-state machine.
//  Board: LilyGO T-Display V1.1 (ESP32 + ST7789 135x240).
// =====================================================================
#include <Arduino.h>
#include "core/Kernel.h"
#include "apps/AppWifiAudit.h"
#include "apps/AppBleScanner.h"
#include "apps/AppNetwork.h"
#include "apps/AppSysDashboard.h"
#include "apps/AppStubs.h"

Kernel kernel;

// Apps (registration order = order in the menu)
AppWifiAudit    appWifi(kernel);   // real scan
AppBleScanner   appBle (kernel);   // real scan (NimBLE)
AppNetwork      appNet (kernel);   // WiFi station connect
AppRfAuditor    appRf  (kernel);   // stub (requires CC1101)
AppIrAnalyzer   appIr  (kernel);   // stub (requires IR LED)
AppSysDashboard appSys (kernel);   // real device metrics

void setup() {
    Serial.begin(115200);
    kernel.begin();

    kernel.registerApp(&appWifi);
    kernel.registerApp(&appBle);
    kernel.registerApp(&appNet);
    kernel.registerApp(&appRf);
    kernel.registerApp(&appIr);
    kernel.registerApp(&appSys);

    kernel.goHome();    // redraw the menu now that the app list is populated
}

void loop() {
    kernel.tick();
}
