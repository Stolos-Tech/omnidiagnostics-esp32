#include "display.h"
#include <Preferences.h>

static TFT_eSPI tft = TFT_eSPI();
static TFT_eSprite spr = TFT_eSprite(&tft);

// --- Палітра (runtime) — дефолт = тема 0 "Mint" (поточний APEX-вигляд) ---
uint16_t C_BG     = 0x0000;
uint16_t C_PANEL  = 0x1082;
uint16_t C_TEXT   = 0xFFFF;
uint16_t C_DIM    = 0x8410;
uint16_t C_ACCENT = 0x05FF;
uint16_t C_GOOD   = 0x07E0;
uint16_t C_WARN   = 0xFD20;
uint16_t C_BAD    = 0xF800;
uint16_t C_GRID   = 0x2124;

// --- Таблиця тем (RGB565). bg/panel/text/dim/accent/good/warn/bad/grid ---
struct Theme {
  const char* name;
  uint16_t bg, panel, text, dim, accent, good, warn, bad, grid;
};
static const Theme THEMES[] = {
  // 0: Mint — поточний вигляд (нуль-регресія)
  { "Mint",    0x0000, 0x1082, 0xFFFF, 0x8410, 0x05FF, 0x07E0, 0xFD20, 0xF800, 0x2124 },
  // 1: Flipper — оранжевий акцент на чорному (омаж моно-помаранчу Flipper, але в кольорі)
  { "Flipper", 0x0000, 0x2100, 0xFFFF, 0x8410, 0xFC20, 0x07E0, 0xFFE0, 0xF800, 0x2965 },
  // 2: Matrix — зелений фосфор
  { "Matrix",  0x0000, 0x0140, 0x07E0, 0x0480, 0x07E0, 0x07E0, 0xFFE0, 0xF800, 0x0180 },
  // 3: Amber — бурштиновий термінал
  { "Amber",   0x0000, 0x2100, 0xFEA0, 0x9440, 0xFD20, 0x07E0, 0xFFE0, 0xF800, 0x3180 },
  // 4: Ice — крижаний блакить
  { "Ice",     0x0008, 0x10B2, 0xFFFF, 0x8C9F, 0x05FF, 0x07FF, 0xFD20, 0xFA1F, 0x2135 },
};
static const int THEME_N = (int)(sizeof(THEMES) / sizeof(THEMES[0]));
static int s_theme = 0;

int         theme_count()      { return THEME_N; }
const char* theme_name(int i)  { return (i >= 0 && i < THEME_N) ? THEMES[i].name : "?"; }
int         theme_current()    { return s_theme; }

void theme_apply(int i) {
  if (i < 0 || i >= THEME_N) return;
  const Theme& t = THEMES[i];
  C_BG = t.bg; C_PANEL = t.panel; C_TEXT = t.text; C_DIM = t.dim;
  C_ACCENT = t.accent; C_GOOD = t.good; C_WARN = t.warn; C_BAD = t.bad; C_GRID = t.grid;
  s_theme = i;
}

void theme_set(int i) {
  if (i < 0 || i >= THEME_N) return;
  theme_apply(i);
  Preferences p;
  if (p.begin("os", false)) { p.putInt("theme", i); p.end(); }
}

void theme_begin() {
  Preferences p;
  int i = 0;
  if (p.begin("os", true)) { i = p.getInt("theme", 0); p.end(); }
  theme_apply(i);
}

void display_init() {
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(C_BG);
  // 8bpp спрайт: 240x135x1 = ~32КБ heap замість ~64КБ (16bpp). Звільнені ~32КБ
  // критичні для надійної віддачі веб-сторінки (lwip інакше голодує по pbuf при
  // ~14КБ вільного heap -> TCP write EAGAIN, сторінка обривається). Кольори C_XXX
  // (RGB565) TFT_eSPI авто-конвертує в 8-бітну палітру-за-замовч. (RGB332) — правок
  // по апках НЕ треба; ціна — легкий колор-бендинг на градієнтах (UI переважно плаский).
  spr.setColorDepth(8);
  spr.createSprite(SCR_W, SCR_H);
}

TFT_eSprite& display_sprite() { return spr; }
TFT_eSPI& display_tft() { return tft; }

void display_push() { spr.pushSprite(0, 0); }

void display_top_bar(const char* title) {
  spr.fillRect(0, 0, SCR_W, 16, C_PANEL);
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(C_ACCENT, C_PANEL);
  spr.drawString(title, 6, 3, 2);
}

#define BL_PIN     4
#define BL_CHANNEL 7
static bool s_bl_ready = false;

void display_set_brightness(int v) {
  if (v < 0) v = 0; if (v > 255) v = 255;
  if (!s_bl_ready) {                     // одноразова ініціалізація PWM підсвітки
    ledcSetup(BL_CHANNEL, 5000, 8);
    ledcAttachPin(BL_PIN, BL_CHANNEL);
    s_bl_ready = true;
  }
  ledcWrite(BL_CHANNEL, v);
}
