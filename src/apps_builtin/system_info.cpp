#include <Arduino.h>
#include <esp_system.h>
#include "system_info.h"
#include "../drivers/display.h"
#include "../kernel/format_util.h"
#include "../remote/protocol.h"

static const char* reset_reason_str() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return "PowerOn";
    case ESP_RST_EXT:       return "External";
    case ESP_RST_SW:        return "Software";
    case ESP_RST_PANIC:     return "Panic";
    case ESP_RST_INT_WDT:   return "IntWDT";
    case ESP_RST_TASK_WDT:  return "TaskWDT";
    case ESP_RST_WDT:       return "WDT";
    case ESP_RST_DEEPSLEEP: return "DeepSleep";
    case ESP_RST_BROWNOUT:  return "Brownout";
    default:                return "Unknown";
  }
}

void SystemInfoApp::init() {
  wants_exit_ = false;
  page_ = PAGE_CHIP;
}

const char* SystemInfoApp::page_title() const {
  return page_ == PAGE_CHIP ? "SYSTEM > CHIP" : "SYSTEM > RUNTIME";
}

int SystemInfoApp::build_lines(char lines[][LINE_LEN]) const {
  int n = 0;
  char tmp[24];
  if (page_ == PAGE_CHIP) {
    snprintf(lines[n++], LINE_LEN, "Chip: %s r%d", ESP.getChipModel(), (int)ESP.getChipRevision());
    snprintf(lines[n++], LINE_LEN, "Cores: %d @ %d MHz", (int)ESP.getChipCores(), (int)ESP.getCpuFreqMHz());
    format_bytes(ESP.getFlashChipSize(), tmp, sizeof(tmp));
    snprintf(lines[n++], LINE_LEN, "Flash: %s", tmp);
    snprintf(lines[n++], LINE_LEN, "ID: %04X%08X",
             (uint16_t)(ESP.getEfuseMac() >> 32), (uint32_t)ESP.getEfuseMac());
    format_bytes(ESP.getSketchSize(), tmp, sizeof(tmp));
    snprintf(lines[n++], LINE_LEN, "Sketch: %s", tmp);
  } else {
    char up[24];
    format_uptime(millis() / 1000, up, sizeof(up));
    snprintf(lines[n++], LINE_LEN, "Uptime: %s", up);
    format_bytes(ESP.getFreeHeap(), tmp, sizeof(tmp));
    char tot[24]; format_bytes(ESP.getHeapSize(), tot, sizeof(tot));
    snprintf(lines[n++], LINE_LEN, "Heap: %s / %s", tmp, tot);
    format_bytes(ESP.getMinFreeHeap(), tmp, sizeof(tmp));
    snprintf(lines[n++], LINE_LEN, "Min heap: %s", tmp);
    format_bytes(ESP.getFreeSketchSpace(), tmp, sizeof(tmp));
    snprintf(lines[n++], LINE_LEN, "Free app: %s", tmp);
    snprintf(lines[n++], LINE_LEN, "Reset: %s", reset_reason_str());
    snprintf(lines[n++], LINE_LEN, "Temp: %d C", (int)temperatureRead());
  }
  return n;
}

void SystemInfoApp::draw() {
  TFT_eSprite& spr = display_sprite();
  spr.fillSprite(C_BG);

  // Заголовок + індикатор сторінки
  spr.fillRect(0, 0, SCR_W, 16, C_PANEL);
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(C_ACCENT, C_PANEL);
  spr.drawString(page_title(), 6, 3, 2);
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(C_DIM, C_PANEL);
  char pg[8]; snprintf(pg, sizeof(pg), "%d/%d", page_ + 1, PAGE_COUNT);
  spr.drawString(pg, SCR_W - 6, 3, 2);

  char lines[MAX_LINES][LINE_LEN];
  int n = build_lines(lines);
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(C_TEXT, C_BG);
  int y = 22, dy = 18;
  for (int i = 0; i < n; i++) { spr.drawString(lines[i], 8, y, 2); y += dy; }

  spr.setTextColor(C_DIM, C_BG);
  spr.drawString("S2=page  S5=exit", 8, SCR_H - 12, 1);
}

void SystemInfoApp::button(ButtonId id) {
  if (id == BTN_S2)      page_ = (page_ + 1) % PAGE_COUNT;
  else if (id == BTN_S1) page_ = (page_ + PAGE_COUNT - 1) % PAGE_COUNT;
  else if (id == BTN_S5) wants_exit_ = true;
}

std::string SystemInfoApp::remote_state() {
  char lines[MAX_LINES][LINE_LEN];
  int n = build_lines(lines);
  const char* items[MAX_LINES];
  for (int i = 0; i < n; i++) items[i] = lines[i];
  return protocol_build_menu(page_ == PAGE_CHIP ? "sys_chip" : "sys_runtime", items, n, -1);
}
