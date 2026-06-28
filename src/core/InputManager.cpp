#include "InputManager.h"
#include <Arduino.h>
#include "config.h"

void InputManager::begin() {
    pinMode(PIN_BTN_OK,   INPUT_PULLUP);   // internal pull-up
    pinMode(PIN_BTN_NEXT, INPUT);          // GPIO35 is input-only -> board pull-up
}

Event InputManager::poll() {
    uint32_t now = millis();
    Event ev = Event::NONE;

    bool okRaw   = (digitalRead(PIN_BTN_OK)   == LOW);
    bool nextRaw = (digitalRead(PIN_BTN_NEXT) == LOW);

    // ---- NEXT: fire on press edge, time-based debounce ----
    if (nextRaw != _nextLast) { _nextT = now; _nextLast = nextRaw; }
    if ((now - _nextT) > DEBOUNCE_MS && nextRaw != _nextStable) {
        _nextStable = nextRaw;
        if (_nextStable) ev = Event::NEXT;
    }

    // ---- OK: short press = SELECT, long press = BACK ----
    if (okRaw != _okLast) { _okT = now; _okLast = okRaw; }
    if ((now - _okT) > DEBOUNCE_MS && okRaw != _okStable) {
        _okStable = okRaw;
        if (_okStable) { _okDownAt = now; _okLong = false; }   // pressed
        else if (!_okLong) ev = Event::SELECT;                 // released before threshold
    }
    if (_okStable && !_okLong && (now - _okDownAt >= LONGPRESS_MS)) {
        ev = Event::BACK;                                      // held
        _okLong = true;
    }

    return ev;
}
