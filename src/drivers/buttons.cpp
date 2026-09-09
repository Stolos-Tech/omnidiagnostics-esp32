#include <Arduino.h>
#include "buttons.h"
#include "button_edge.h"
#include "tap_hold.h"
#include "../kernel/input_queue.h"

// Рідні кнопки T-Display (лишаються активними ЗАВЖДИ — нуль-регресія + wake-жест):
//   GPIO35 (права)      -> BTN_S2  через edge-detection (миттєво на натиску)
//   GPIO0  (ліва, BOOT) -> tap = BTN_S5, довге утримання = "назад" у лаунчер
#define PIN_RIGHT 35
#define PIN_LEFT  0

static ButtonEdgeDetector right_edge;   // права: одна кнопка (індекс 0)
static TapHoldDetector    left_th;      // ліва: tap/hold
static uint32_t s_last_activity_ms = 0;

// Джойстик (KY-023 зі стартер-кіту UNO R3) висить НЕ на ESP32, а на Arduino UNO
// (A1=VRx, A2=VRy, D7=SW) — UNO має вільні ADC-канали, тож жоден пін ESP32 і апка
// EM Field (GPIO36) не жертвуються. UNO шле "JOY x y sw" по лінку; конвертація в
// навігацію (зони/авто-повтор) — у drivers/uno_link. Тут лишаються тільки рідні
// кнопки T-Display (GPIO35/GPIO0). Колишні свічі 13/17/26 більше не використовуються.

// Детекція довгого утримання ПРАВОЇ кнопки (для wake з пасивного режиму).
#define RIGHT_HOLD_MS 700
static uint32_t s_right_down_ms = 0;
static bool     s_right_lp_fired = false;   // щоб спрацювати раз за утримання
static bool     s_right_lp_flag  = false;   // one-shot, зчитує buttons_right_longpress()

void buttons_init() {
  pinMode(PIN_RIGHT, INPUT);            // input-only, зовнішній pullup на платі
  pinMode(PIN_LEFT, INPUT_PULLUP);
}

void buttons_poll(uint32_t now_ms) {
  // Права кнопка — гортання (edge на натиску)
  bool r = digitalRead(PIN_RIGHT) == HIGH;
  bool l = digitalRead(PIN_LEFT) == HIGH;
  if (!r || !l) s_last_activity_ms = now_ms;   // будь-яка кнопка зараз натиснута

  // Довге утримання правої (LOW = натиснута) -> одноразовий сигнал для wake.
  if (!r) {
    if (s_right_down_ms == 0) { s_right_down_ms = now_ms; s_right_lp_fired = false; }
    else if (!s_right_lp_fired && now_ms - s_right_down_ms >= RIGHT_HOLD_MS) {
      s_right_lp_fired = true; s_right_lp_flag = true;
    }
  } else {
    s_right_down_ms = 0;
  }

  if (right_edge.update(0, r, now_ms))
    input_queue().push_button(BTN_S2);

  // Ліва кнопка — tap = вибір/дія (S5), hold = універсальний "назад"
  switch (left_th.update(l, now_ms)) {
    case TapHoldDetector::TAP:  input_queue().push_button(BTN_S5); break;
    case TapHoldDetector::HOLD: input_queue().push_back(); break;
    default: break;
  }

  // Джойстик тепер на UNO (A1/A2/D7) -> навігація приходить через drivers/uno_link,
  // а не звідси. Тут лишаються тільки рідні кнопки T-Display вище.
}

uint32_t buttons_last_activity_ms() { return s_last_activity_ms; }

bool buttons_right_longpress() { bool f = s_right_lp_flag; s_right_lp_flag = false; return f; }
