// Тонка обгортка дисплея: володіє TFT_eSPI і повноекранним спрайтом 240x135.
// Увесь рендер іде в спрайт (flicker-free), потім display_push() на панель.
#pragma once
#include <TFT_eSPI.h>
#include <stdint.h>

#define SCR_W 240
#define SCR_H 135

// Спільна палітра ОС — RUNTIME-змінні (не #define), щоб зміна теми одразу
// перефарбовувала ВСІ апки без правок (усі малюють через ці ж імена). Значення
// за замовчуванням = тема 0 ("Mint", поточний APEX-вигляд). RGB565; 8bpp-спрайт
// авто-конвертує в палітру.
extern uint16_t C_BG;
extern uint16_t C_PANEL;
extern uint16_t C_TEXT;
extern uint16_t C_DIM;
extern uint16_t C_ACCENT;
extern uint16_t C_GOOD;
extern uint16_t C_WARN;
extern uint16_t C_BAD;
extern uint16_t C_GRID;

// --- Кольорові теми (перевага над моно-Flipper: у нас повнокольоровий TFT) ---
int         theme_count();
const char* theme_name(int i);
int         theme_current();
void        theme_apply(int i);   // застосувати палітру теми i (лише RAM-змінні)
void        theme_set(int i);     // застосувати + зберегти вибір у NVS
void        theme_begin();        // завантажити збережену тему з NVS і застосувати

void display_init();
TFT_eSprite& display_sprite();  // спрайт для малювання
TFT_eSPI& display_tft();        // прямий доступ (сон панелі, підсвітка)
void display_push();            // спрайт -> екран
void display_set_brightness(int v);  // 0..255 (PWM підсвітки GPIO4)

// Стандартна панель-заголовок апки: смуга C_PANEL заввишки 16px + назва зліва
// (C_ACCENT, font2). Єдине джерело — раніше кожна апка мала власну копію topBar().
void display_top_bar(const char* title);
