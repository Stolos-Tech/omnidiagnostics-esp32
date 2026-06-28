#pragma once
#include <stdint.h>

class UIManager;   // forward declaration - avoids pulling TFT into every header

// Logical input events. The source (buttons / encoder / pot) is abstracted
// away in InputManager, so apps never depend on a specific input device.
enum class Event : uint8_t { NONE, NEXT, SELECT, BACK };

// Base class for a micro-OS app.
// Each module (WiFi, BLE, RF, IR, Dashboard) derives from this and
// implements only the lifecycle hooks it needs.
class App {
public:
    virtual ~App() {}

    virtual const char* title() const = 0;       // label shown in the menu

    virtual void onEnter() { dirty = true; }      // entering the app
    virtual void onExit()  {}                      // leaving (free resources here!)
    virtual void onEvent(Event e) {}               // input event
    virtual void onTick(uint32_t nowMs) {}         // periodic logic
    virtual void onDraw(UIManager& ui) {}          // render (only while dirty)

    bool dirty = true;                             // needs a redraw

protected:
    void redraw() { dirty = true; }
};
