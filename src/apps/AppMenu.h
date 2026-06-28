#pragma once
#include "../core/AppBase.h"
#include "../core/ListView.h"

class Kernel;

// Main menu: lists the registered apps using ListView (highlight + scrollbar).
class AppMenu : public App {
public:
    explicit AppMenu(Kernel& k) : _k(k) {}

    const char* title() const override { return "OmniDiagnostics"; }
    void onEnter() override;
    void onEvent(Event e) override;
    void onDraw(UIManager& ui) override;

private:
    Kernel&  _k;
    ListView _list;
};
