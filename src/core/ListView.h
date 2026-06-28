#pragma once
#include <Arduino.h>
#include "UIManager.h"
#include "config.h"

// Reusable list widget: highlighted active row, scrolling viewport and
// a scrollbar. Row text is supplied by the getText(i, buf, size) callback.
class ListView {
public:
    int count = 0, sel = 0, top = 0;
    int visibleRows = 5, rowH = 18, topY = 26;

    void reset() { sel = 0; top = 0; }

    void clampSel() {
        if (count <= 0) { sel = 0; top = 0; return; }
        if (sel >= count) sel = count - 1;
        if (sel < 0) sel = 0;
        ensureVisible();
    }
    void next() { if (count <= 0) return; sel = (sel + 1) % count; ensureVisible(); }
    void prev() { if (count <= 0) return; sel = (sel - 1 + count) % count; ensureVisible(); }

    void ensureVisible() {
        if (count <= visibleRows) { top = 0; return; }
        if (sel < top) top = sel;
        else if (sel >= top + visibleRows) top = sel - visibleRows + 1;
        if (top > count - visibleRows) top = count - visibleRows;
        if (top < 0) top = 0;
    }

    template <typename F>
    void draw(UIManager& ui, F getText) {
        TFT_eSPI& t = ui.tft();
        ui.clearBody();
        int shown = (count < visibleRows) ? count : visibleRows;
        for (int r = 0; r < shown; r++) {
            int i = top + r;
            if (i >= count) break;
            int y = topY + r * rowH;
            bool s = (i == sel);
            if (s) t.fillRect(0, y - 1, ui.width(), rowH, COL_ACCENT);
            t.setTextColor(s ? COL_BG : COL_TEXT, s ? COL_ACCENT : COL_BG);
            t.setTextDatum(ML_DATUM);
            char buf[48]; buf[0] = 0;
            getText(i, buf, sizeof(buf));
            t.drawString(buf, 8, y + rowH / 2 - 1, 2);
        }
        t.setTextColor(COL_TEXT, COL_BG);
        t.setTextDatum(TL_DATUM);

        // scrollbar on the right edge
        if (count > visibleRows) {
            int trackH = visibleRows * rowH;
            int barH = trackH * visibleRows / count; if (barH < 8) barH = 8;
            int denom = count - visibleRows; if (denom < 1) denom = 1;
            int barY = topY + (trackH - barH) * top / denom;
            t.fillRect(ui.width() - 3, topY, 3, trackH, 0x2104);
            t.fillRect(ui.width() - 3, barY, 3, barH, COL_ACCENT);
        }
    }
};
