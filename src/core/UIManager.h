#pragma once
#include <TFT_eSPI.h>

// Display manager: initialisation, theme, shared widgets (header/footer)
// and battery/power helper readings.
class UIManager {
public:
    void begin();
    void clear();
    void clearBody();          // clear the work area (between header and footer)

    TFT_eSPI& tft() { return _tft; }
    int width()  { return _tft.width();  }
    int height() { return _tft.height(); }

    void header(const char* title);                // amber title bar at the top
    void headerRight(const char* s, uint16_t col); // right-side status in the header
    void footer(const char* hint = nullptr);       // hint + battery % at the bottom

    float batteryVolts();                          // Li-Po voltage, V
    int   batteryPct();                            // rough 0..100 % estimate

private:
    TFT_eSPI _tft;
};
