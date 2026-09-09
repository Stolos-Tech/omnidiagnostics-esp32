// Класифікація стану живлення за напругою VBAT і трендом — чиста логіка,
// тестується на хості. VBAT читається ПІСЛЯ зарядного IC (T-Display), тож
// при USB там ~4.2-4.3В; реальний стан елемента видно лише без USB.
#pragma once

enum PowerState {
  PWR_ON_BATTERY = 0,  // живлення від акумулятора (розряд)
  PWR_CHARGING,        // заряджається (напруга росте / USB підключено, ще не повна)
  PWR_FULL,            // повна / USB, тримає максимум
  PWR_USB_NO_BATT      // USB без акумулятора (аномально висока напруга)
};

// mv — напруга VBAT у мілівольтах; dvdt_mv_min — тренд у мВ/хв (EMA).
PowerState power_classify(int mv, float dvdt_mv_min);

// Оцінка часу роботи від батареї у хвилинах (до ~3300мВ). -1, якщо не на батареї
// або розряд надто повільний/невимірний.
int power_runtime_estimate_min(int mv, float dvdt_mv_min);

// Короткий ASCII-опис стану (для екрана плати).
const char* power_state_str(PowerState s);
