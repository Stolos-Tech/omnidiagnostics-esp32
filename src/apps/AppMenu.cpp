#include "AppMenu.h"
#include "../core/Kernel.h"
#include "../core/UIManager.h"
#include "config.h"

void AppMenu::onEnter() {
    _list.count = _k.appCount();
    _list.clampSel();
    dirty = true;
}

void AppMenu::onEvent(Event e) {
    if (e == Event::NEXT)   { _list.next(); dirty = true; }
    else if (e == Event::SELECT) {
        if (_k.appCount() > 0) _k.switchTo(_k.apps()[_list.sel]);
    }
    // BACK is ignored in the main menu
}

void AppMenu::onDraw(UIManager& ui) {
    ui.header("OMNIDIAGNOSTICS");
    _list.draw(ui, [&](int i, char* b, size_t n) {
        snprintf(b, n, "%s", _k.apps()[i]->title());
    });
    ui.footer("NEXT:scroll  OK:select");
}
