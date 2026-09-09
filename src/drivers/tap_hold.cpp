#include "tap_hold.h"

TapHoldDetector::TapHoldDetector()
  : raw_(true), stable_(true), last_change_(0), press_start_(0), hold_fired_(false) {}

TapHoldDetector::Event TapHoldDetector::update(bool level, uint32_t now_ms) {
  // антибрязкіт: зміна сирого рівня перезапускає вікно
  if (level != raw_) { raw_ = level; last_change_ = now_ms; }
  if (now_ms - last_change_ < DEBOUNCE_MS) return NONE;  // ще не стійко

  if (raw_ != stable_) {                 // стійкий перехід
    bool was = stable_;
    stable_ = raw_;
    if (was && !raw_) {                  // HIGH->LOW: початок натиску
      press_start_ = now_ms;
      hold_fired_ = false;
    } else if (!was && raw_) {           // LOW->HIGH: відпускання
      if (!hold_fired_) return TAP;      // короткий тап (довгий уже віддав HOLD)
    }
    return NONE;
  }

  // стійко без переходу: під час утримання перевіряємо поріг HOLD
  if (!stable_ && !hold_fired_ && now_ms - press_start_ >= HOLD_MS) {
    hold_fired_ = true;
    return HOLD;
  }
  return NONE;
}
