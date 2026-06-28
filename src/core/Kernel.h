#pragma once
#include <stdint.h>
#include "AppBase.h"
#include "UIManager.h"
#include "InputManager.h"

#define MAX_APPS 8

class AppMenu;   // forward declaration

// Kernel: owns the UIManager, InputManager and the app registry.
// Each tick() reads input, dispatches the event to the active app,
// runs its periodic logic and redraws only when needed (dirty flag).
class Kernel {
public:
    void begin();
    void tick();

    void registerApp(App* app);     // add a functional app (appears in the menu)
    void switchTo(App* app);        // enter an app
    void goHome();                  // return to the main menu

    App**   apps()     { return _apps;  }
    uint8_t appCount() { return _count; }
    UIManager& ui()    { return _ui;    }

private:
    UIManager    _ui;
    InputManager _input;
    App*    _apps[MAX_APPS];
    uint8_t _count   = 0;
    App*    _current = nullptr;
    AppMenu* _menu   = nullptr;
};
