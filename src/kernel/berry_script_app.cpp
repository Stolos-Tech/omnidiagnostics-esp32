#include <Arduino.h>
#include "berry_script_app.h"
#include "../drivers/display.h"
#include "../berry_bridge/berry_vm.h"
#include "../berry_bridge/native_api.h"

void BerryScriptApp::configure(const char* menu_name, const char* path) {
  snprintf(name_, sizeof(name_), "%s", menu_name);
  snprintf(path_, sizeof(path_), "%s", path);
}

void BerryScriptApp::init() {
  wants_exit_ = false;
  loaded_ok_ = false;
  err_[0] = '\0';

  native_api_install_hardware();
  native_api_register();

  String src;
  if (!fs_read_file(path_, src)) {
    snprintf(err_, sizeof(err_), "cannot read %s", path_);
    Serial.printf("[SCRIPT] %s\n", err_);
    return;
  }
  if (!berry_vm_run_string(path_, src.c_str())) {
    snprintf(err_, sizeof(err_), "%s", berry_vm_last_error());
    Serial.printf("[SCRIPT] load error: %s\n", err_);
    return;
  }
  loaded_ok_ = true;
}

void BerryScriptApp::draw() {
  TFT_eSprite& spr = display_sprite();
  if (!loaded_ok_) {
    spr.fillSprite(C_BG);
    spr.setTextDatum(TL_DATUM);
    spr.setTextColor(C_BAD, C_BG);
    spr.drawString("Script error:", 8, 20, 2);
    spr.setTextColor(C_DIM, C_BG);
    spr.drawString(err_, 8, 44, 1);
    spr.setTextColor(C_ACCENT, C_BG);
    spr.drawString("LEFT = exit", 8, 116, 2);
    return;
  }
  if (!berry_vm_call_global("app_draw")) {
    // помилка виконання під час малювання — показуємо, не даємо впасти системі
    snprintf(err_, sizeof(err_), "%s", berry_vm_last_error());
    spr.fillSprite(C_BG);
    spr.setTextDatum(TL_DATUM);
    spr.setTextColor(C_BAD, C_BG);
    spr.drawString("Runtime error:", 8, 20, 2);
    spr.setTextColor(C_DIM, C_BG);
    spr.drawString(err_, 8, 44, 1);
    spr.setTextColor(C_ACCENT, C_BG);
    spr.drawString("LEFT = exit", 8, 116, 2);
  }
}

void BerryScriptApp::button(ButtonId id) {
  if (loaded_ok_) {
    char call[32];
    snprintf(call, sizeof(call), "app_button(%d)", (int)id);
    berry_vm_run_string("btn", call);  // помилка тут не критична
  }
  if (id == BTN_S5) wants_exit_ = true;  // вихід у лаунчер обробляє ядро
}

void BerryScriptApp::text(const char* field, const char* value) {
  // Проброс тексту (напр. WiFi-пароль з телефона) у скрипт: app_text(field, value).
  // Якщо скрипт не має app_text — тихо ігнорується.
  if (loaded_ok_) berry_vm_call_global_ss("app_text", field, value);
}
