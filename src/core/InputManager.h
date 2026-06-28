#pragma once
#include "../core/AppBase.h"

// Reads the two on-board buttons and turns them into logical events
// (non-blocking):
//   NEXT (short)  -> Event::NEXT
//   OK   (short)  -> Event::SELECT
//   OK   (long)   -> Event::BACK
// Debounce is time-based (millis), with no delay(), so the UI stays responsive.
class InputManager {
public:
    void  begin();
    Event poll();

private:
    bool     _okLast = false,  _okStable = false,  _okLong = false;
    bool     _nextLast = false, _nextStable = false;
    uint32_t _okT = 0, _nextT = 0, _okDownAt = 0;
};
