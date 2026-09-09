#include "berry_vm.h"
#include "berry.h"
#include <string.h>
#include <stdio.h>

static bvm* s_vm = nullptr;
static char s_error[256] = "";

bool berry_vm_init() {
  if (s_vm) return true;
  s_vm = be_vm_new();
  s_error[0] = '\0';
  return s_vm != nullptr;
}

void berry_vm_regfunc(const char* name, int (*f)(bvm*)) {
  if (!s_vm) return;
  be_regfunc(s_vm, name, (bntvfunc)f);
}

bool berry_vm_run_string(const char* name, const char* code) {
  s_error[0] = '\0';
  if (!s_vm) { snprintf(s_error, sizeof(s_error), "VM not initialized"); return false; }

  int res = be_loadbuffer(s_vm, name, code, strlen(code));
  if (res == 0) res = be_pcall(s_vm, 0);

  if (res != 0) {
    // На вершині стека — рядок помилки (синтаксис) або виняток + трейс
    const char* msg = be_tostring(s_vm, -1);
    snprintf(s_error, sizeof(s_error), "%s", msg ? msg : "unknown error");
    be_pop(s_vm, be_top(s_vm));  // очистити стек після помилки
    return false;
  }
  be_pop(s_vm, be_top(s_vm));
  return true;
}

bool berry_vm_call_global(const char* name) {
  s_error[0] = '\0';
  if (!s_vm) { snprintf(s_error, sizeof(s_error), "VM not initialized"); return false; }

  if (!be_getglobal(s_vm, name) || !be_isfunction(s_vm, -1)) {
    be_pop(s_vm, be_top(s_vm));
    snprintf(s_error, sizeof(s_error), "no function '%s'", name);
    return false;
  }
  int res = be_pcall(s_vm, 0);
  if (res != 0) {
    const char* msg = be_tostring(s_vm, -1);
    snprintf(s_error, sizeof(s_error), "%s", msg ? msg : "unknown error");
    be_pop(s_vm, be_top(s_vm));
    return false;
  }
  be_pop(s_vm, be_top(s_vm));
  return true;
}

bool berry_vm_call_global_ss(const char* name, const char* a, const char* b) {
  s_error[0] = '\0';
  if (!s_vm) { snprintf(s_error, sizeof(s_error), "VM not initialized"); return false; }

  if (!be_getglobal(s_vm, name) || !be_isfunction(s_vm, -1)) {
    be_pop(s_vm, be_top(s_vm));
    return false;  // нема функції — не помилка (скрипт може не обробляти текст)
  }
  be_pushstring(s_vm, a ? a : "");
  be_pushstring(s_vm, b ? b : "");
  int res = be_pcall(s_vm, 2);  // 2 аргументи
  if (res != 0) {
    const char* msg = be_tostring(s_vm, -1);
    snprintf(s_error, sizeof(s_error), "%s", msg ? msg : "unknown error");
    be_pop(s_vm, be_top(s_vm));
    return false;
  }
  be_pop(s_vm, be_top(s_vm));
  return true;
}

// Захоплення виводу print() визначене в порту Berry (lib/berry/src/be_port.c).
extern "C" {
  void   be_capture_begin(char* buf, unsigned int cap);
  unsigned int be_capture_end(void);
}

bool berry_vm_run_capture(const char* name, const char* code, char* out, unsigned int out_cap) {
  if (out && out_cap) out[0] = '\0';
  be_capture_begin(out, out_cap);
  bool ok = berry_vm_run_string(name, code);
  be_capture_end();
  return ok;
}

const char* berry_vm_last_error() { return s_error; }

bvm* berry_vm_raw() { return s_vm; }
