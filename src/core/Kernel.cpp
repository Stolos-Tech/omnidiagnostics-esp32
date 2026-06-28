#include "Kernel.h"
#include <Arduino.h>
#include "../apps/AppMenu.h"

void Kernel::begin() {
    _ui.begin();
    _input.begin();

    static AppMenu menu(*this);    // the main menu is created once
    _menu = &menu;
    switchTo(_menu);
}

void Kernel::registerApp(App* app) {
    if (_count < MAX_APPS) _apps[_count++] = app;
}

void Kernel::switchTo(App* app) {
    if (!app) return;
    if (_current) _current->onExit();
    _current = app;
    _ui.clear();
    _current->onEnter();
}

void Kernel::goHome() {
    switchTo(_menu);
}

void Kernel::tick() {
    Event e = _input.poll();
    uint32_t now = millis();

    if (_current) {
        if (e != Event::NONE) _current->onEvent(e);
        _current->onTick(now);
        if (_current->dirty) {
            _current->onDraw(_ui);
            _current->dirty = false;
        }
    }
    delay(5);
}
