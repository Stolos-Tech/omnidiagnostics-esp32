#include "power_state.h"

PowerState power_classify(int mv, float dvdt_mv_min) {
  if (mv >= 4400) return PWR_USB_NO_BATT;      // аномально високо -> USB без елемента
  if (dvdt_mv_min >= 8.0f) return PWR_CHARGING; // напруга помітно росте -> заряджається
  if (mv >= 4250) return PWR_FULL;              // високо і стабільно -> повна / USB
  return PWR_ON_BATTERY;                         // інакше — розряд від батареї
}

int power_runtime_estimate_min(int mv, float dvdt_mv_min) {
  if (power_classify(mv, dvdt_mv_min) != PWR_ON_BATTERY) return -1;
  if (dvdt_mv_min >= -0.5f) return -1;          // не розряджається вимірно
  float mins = (float)(mv - 3300) / (-dvdt_mv_min);
  if (mins < 0) mins = 0;
  if (mins > 6000) mins = 6000;                 // обмеження 100 год
  return (int)mins;
}

const char* power_state_str(PowerState s) {
  switch (s) {
    case PWR_CHARGING:    return "Charging";
    case PWR_FULL:        return "Full / USB";
    case PWR_USB_NO_BATT: return "USB (no batt)";
    default:              return "On battery";
  }
}
