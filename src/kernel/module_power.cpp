#include "module_power.h"
#include <Arduino.h>
#include "board_profile.h"
#include "hw_safe.h"
#include "../drivers/sd_store.h"

// Піни модулів (дзеркало драйверів). СПІЛЬНІ: SCK=2, MOSI=15 (nRF+SD+CC). MISO 38/21 та GDO 39 —
// входи (ESP не жене), керування не потребують.
#define P_SCK     2
#define P_MOSI    15
#define P_CSN_NRF 25
#define P_CE_NRF  12
#define P_CS_SD   33
#define P_CS_CC   27

// Опційна апаратна power-enable лінія модуля (wired до MOSFET/load-switch). active-high.
static void pwr_line(const char* key, bool on) {
  int g = profile_get_int(key, -1);
  if (g < 0) return;                      // нема апаратного switch -> лише логічне вимкнення
  pinMode(g, OUTPUT); digitalWrite(g, on ? HIGH : LOW);
}

void module_power_apply() {
  if (g_hw_safe) return;                  // глобальний SAFE вже high-Z'нув усі піни модулів
  bool nrf = profile_get("nrf24");
  bool sd  = profile_get("sd");
  bool cc  = profile_get("cc1101");
  bool anyBus = nrf || sd || cc;

  // Апаратні power-enable лінії (якщо задані у профілі) — РЕАЛЬНЕ відключення рейки модуля
  pwr_line("nrf24_pwr",  nrf);
  pwr_line("sd_pwr",     sd);
  pwr_line("cc1101_pwr", cc);

  // Виділені піни: ввімкнено -> idle-драйв; вимкнено -> high-Z (не тримає рейку/шину)
  if (nrf) { pinMode(P_CSN_NRF, OUTPUT); digitalWrite(P_CSN_NRF, HIGH);
             pinMode(P_CE_NRF, OUTPUT);  digitalWrite(P_CE_NRF, LOW); }
  else     { pinMode(P_CSN_NRF, INPUT);  pinMode(P_CE_NRF, INPUT); }

  if (cc)  { pinMode(P_CS_CC, OUTPUT); digitalWrite(P_CS_CC, HIGH); }
  else     { pinMode(P_CS_CC, INPUT); }

  if (sd)  { pinMode(P_CS_SD, OUTPUT); digitalWrite(P_CS_SD, HIGH); }
  else     { if (sd_mounted()) sd_unmount(); pinMode(P_CS_SD, INPUT); }

  // СПІЛЬНА SPI-шина: high-Z ЛИШЕ коли вимкнено ВСІ SPI-модулі. Поки активний хоч один —
  // SCK/MOSI лишаються драйвовані (вимкнення одного модуля НЕ глушить спільні піни іншого).
  if (anyBus) { pinMode(P_SCK, OUTPUT); digitalWrite(P_SCK, LOW);
                pinMode(P_MOSI, OUTPUT); digitalWrite(P_MOSI, HIGH); }
  else        { pinMode(P_SCK, INPUT);  pinMode(P_MOSI, INPUT); }
}
