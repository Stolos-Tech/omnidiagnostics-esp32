#include "native_api.h"
#include "berry_vm.h"
#include "berry.h"

// Кольори RGB565 (дублюють drivers/display.h, але тут без TFT-залежності,
// щоб файл компілювався на хості). Значення мають збігатися.
#define NA_C_TEXT 0xFFFF

static const NativeApiHooks* s_hooks = nullptr;

void native_api_set_hooks(const NativeApiHooks* hooks) { s_hooks = hooks; }

// display_clear() -> nil
static int l_display_clear(bvm* vm) {
  if (s_hooks && s_hooks->display_clear) s_hooks->display_clear();
  be_return_nil(vm);
}

// display_text(x, y, text [, color]) -> nil
static int l_display_text(bvm* vm) {
  int top = be_top(vm);
  if (top >= 3 && be_isint(vm, 1) && be_isint(vm, 2) && be_isstring(vm, 3)) {
    int x = (int)be_toint(vm, 1);
    int y = (int)be_toint(vm, 2);
    const char* s = be_tostring(vm, 3);
    uint16_t color = NA_C_TEXT;
    if (top >= 4 && be_isint(vm, 4)) color = (uint16_t)be_toint(vm, 4);
    if (s_hooks && s_hooks->display_text) s_hooks->display_text(x, y, s, color);
  }
  be_return_nil(vm);
}

// button_pressed() -> int (0..4) або -1
static int l_button_pressed(bvm* vm) {
  int id = -1;
  if (s_hooks && s_hooks->button_get) id = s_hooks->button_get();
  be_pushint(vm, id);
  be_return(vm);
}

// adc_read_battery() -> real (вольти)
static int l_adc_read_battery(bvm* vm) {
  float v = 0.0f;
  if (s_hooks && s_hooks->adc_read_battery) v = s_hooks->adc_read_battery();
  be_pushreal(vm, v);
  be_return(vm);
}

// millis() -> int (мс від старту)
static int l_millis(bvm* vm) {
  uint32_t t = 0;
  if (s_hooks && s_hooks->millis_ms) t = s_hooks->millis_ms();
  be_pushint(vm, (bint)t);
  be_return(vm);
}

// free_heap() -> int (байти)
static int l_free_heap(bvm* vm) {
  uint32_t h = 0;
  if (s_hooks && s_hooks->free_heap) h = s_hooks->free_heap();
  be_pushint(vm, (bint)h);
  be_return(vm);
}

// heap_total() -> int (байти)
static int l_heap_total(bvm* vm) {
  uint32_t h = 0;
  if (s_hooks && s_hooks->heap_total) h = s_hooks->heap_total();
  be_pushint(vm, (bint)h);
  be_return(vm);
}

// Спільний розбір 5 цілих аргументів (x, y, w, h, color) для прямокутників.
static bool read_rect(bvm* vm, int* x, int* y, int* w, int* h, uint16_t* color) {
  if (be_top(vm) < 5) return false;
  for (int i = 1; i <= 5; i++) if (!be_isint(vm, i)) return false;
  *x = (int)be_toint(vm, 1); *y = (int)be_toint(vm, 2);
  *w = (int)be_toint(vm, 3); *h = (int)be_toint(vm, 4);
  *color = (uint16_t)be_toint(vm, 5);
  return true;
}

// display_rect(x, y, w, h, color) -> nil
static int l_display_rect(bvm* vm) {
  int x, y, w, h; uint16_t c;
  if (read_rect(vm, &x, &y, &w, &h, &c) && s_hooks && s_hooks->display_rect)
    s_hooks->display_rect(x, y, w, h, c);
  be_return_nil(vm);
}

// display_fill_rect(x, y, w, h, color) -> nil
static int l_display_fill_rect(bvm* vm) {
  int x, y, w, h; uint16_t c;
  if (read_rect(vm, &x, &y, &w, &h, &c) && s_hooks && s_hooks->display_fill_rect)
    s_hooks->display_fill_rect(x, y, w, h, c);
  be_return_nil(vm);
}

// sys_temp() -> int (°C)
static int l_sys_temp(bvm* vm) {
  int t = 0;
  if (s_hooks && s_hooks->sys_temp) t = s_hooks->sys_temp();
  be_pushint(vm, (bint)t);
  be_return(vm);
}

// cpu_mhz() -> int
static int l_cpu_mhz(bvm* vm) {
  int m = 0;
  if (s_hooks && s_hooks->cpu_mhz) m = s_hooks->cpu_mhz();
  be_pushint(vm, (bint)m);
  be_return(vm);
}

// gpio_mode(pin, mode) / gpio_write(pin, val) / gpio_read(pin)
static int l_gpio_mode(bvm* vm) {
  if (be_top(vm) >= 2 && be_isint(vm, 1) && be_isint(vm, 2) && s_hooks && s_hooks->gpio_mode)
    s_hooks->gpio_mode((int)be_toint(vm, 1), (int)be_toint(vm, 2));
  be_return_nil(vm);
}
static int l_gpio_write(bvm* vm) {
  if (be_top(vm) >= 2 && be_isint(vm, 1) && be_isint(vm, 2) && s_hooks && s_hooks->gpio_write)
    s_hooks->gpio_write((int)be_toint(vm, 1), (int)be_toint(vm, 2));
  be_return_nil(vm);
}
static int l_gpio_read(bvm* vm) {
  int r = 0;
  if (be_top(vm) >= 1 && be_isint(vm, 1) && s_hooks && s_hooks->gpio_read)
    r = s_hooks->gpio_read((int)be_toint(vm, 1));
  be_pushint(vm, r); be_return(vm);
}
// i2c_probe(addr) / i2c_read8(addr, reg) / i2c_write8(addr, reg, val)
static int l_i2c_probe(bvm* vm) {
  int r = 0;
  if (be_top(vm) >= 1 && be_isint(vm, 1) && s_hooks && s_hooks->i2c_probe)
    r = s_hooks->i2c_probe((int)be_toint(vm, 1));
  be_pushbool(vm, r); be_return(vm);
}
static int l_i2c_read8(bvm* vm) {
  int r = -1;
  if (be_top(vm) >= 2 && be_isint(vm, 1) && be_isint(vm, 2) && s_hooks && s_hooks->i2c_read8)
    r = s_hooks->i2c_read8((int)be_toint(vm, 1), (int)be_toint(vm, 2));
  be_pushint(vm, r); be_return(vm);
}
static int l_i2c_write8(bvm* vm) {
  if (be_top(vm) >= 3 && be_isint(vm, 1) && be_isint(vm, 2) && be_isint(vm, 3) && s_hooks && s_hooks->i2c_write8)
    s_hooks->i2c_write8((int)be_toint(vm, 1), (int)be_toint(vm, 2), (int)be_toint(vm, 3));
  be_return_nil(vm);
}

// wifi_rssi() -> int, wifi_ip() -> string
static int l_wifi_rssi(bvm* vm) {
  int v = 0; if (s_hooks && s_hooks->wifi_rssi) v = s_hooks->wifi_rssi();
  be_pushint(vm, v); be_return(vm);
}
static int l_wifi_ip(bvm* vm) {
  const char* s = "";
  if (s_hooks && s_hooks->wifi_ip) { const char* r = s_hooks->wifi_ip(); if (r) s = r; }
  be_pushstring(vm, s); be_return(vm);
}

// --- мережеві проби ---
// dns_resolve(host) -> string ("" fail)
static int l_dns_resolve(bvm* vm) {
  const char* out = "";
  if (be_top(vm) >= 1 && be_isstring(vm, 1) && s_hooks && s_hooks->net_dns) {
    const char* r = s_hooks->net_dns(be_tostring(vm, 1)); if (r) out = r;
  }
  be_pushstring(vm, out); be_return(vm);
}
// ping(host) -> int мс (-1 fail)
static int l_ping(bvm* vm) {
  int ms = -1;
  if (be_top(vm) >= 1 && be_isstring(vm, 1) && s_hooks && s_hooks->net_ping)
    ms = s_hooks->net_ping(be_tostring(vm, 1));
  be_pushint(vm, ms); be_return(vm);
}
// tcp_probe(host, port) -> bool
static int l_tcp_probe(bvm* vm) {
  int ok = 0;
  if (be_top(vm) >= 2 && be_isstring(vm, 1) && be_isint(vm, 2) && s_hooks && s_hooks->net_tcp)
    ok = s_hooks->net_tcp(be_tostring(vm, 1), (int)be_toint(vm, 2));
  be_pushbool(vm, ok); be_return(vm);
}
// http_get(url) -> int статус (-1 fail); тіло -> http_body()
static int l_http_get(bvm* vm) {
  int code = -1;
  if (be_top(vm) >= 1 && be_isstring(vm, 1) && s_hooks && s_hooks->net_http_get)
    code = s_hooks->net_http_get(be_tostring(vm, 1));
  be_pushint(vm, code); be_return(vm);
}
// http_body() -> string
static int l_http_body(bvm* vm) {
  const char* s = "";
  if (s_hooks && s_hooks->net_http_body) { const char* r = s_hooks->net_http_body(); if (r) s = r; }
  be_pushstring(vm, s); be_return(vm);
}
// arp_count() -> int
static int l_arp_count(bvm* vm) {
  int n = 0; if (s_hooks && s_hooks->net_arp_count) n = s_hooks->net_arp_count();
  be_pushint(vm, n); be_return(vm);
}
// arp_ip(i) -> string
static int l_arp_ip(bvm* vm) {
  const char* s = "";
  if (be_top(vm) >= 1 && be_isint(vm, 1) && s_hooks && s_hooks->net_arp_ip) {
    const char* r = s_hooks->net_arp_ip((int)be_toint(vm, 1)); if (r) s = r;
  }
  be_pushstring(vm, s); be_return(vm);
}
// arp_mac(i) -> string
static int l_arp_mac(bvm* vm) {
  const char* s = "";
  if (be_top(vm) >= 1 && be_isint(vm, 1) && s_hooks && s_hooks->net_arp_mac) {
    const char* r = s_hooks->net_arp_mac((int)be_toint(vm, 1)); if (r) s = r;
  }
  be_pushstring(vm, s); be_return(vm);
}

// --- UNO R3 копроцесор: живі датчики зі скрипта (uno_pot/reed/ir/temp/hum) ---
static int l_uno_pot(bvm* vm)  { int v=0; if (s_hooks && s_hooks->uno_pot)  v=s_hooks->uno_pot();  be_pushint(vm, v); be_return(vm); }
static int l_uno_reed(bvm* vm) { int v=0; if (s_hooks && s_hooks->uno_reed) v=s_hooks->uno_reed(); be_pushbool(vm, v); be_return(vm); }
static int l_uno_ir(bvm* vm)   { int v=0; if (s_hooks && s_hooks->uno_ir)   v=s_hooks->uno_ir();   be_pushint(vm, v); be_return(vm); }
static int l_uno_temp(bvm* vm) { int v=0; if (s_hooks && s_hooks->uno_temp) v=s_hooks->uno_temp(); be_pushint(vm, v); be_return(vm); }
static int l_uno_hum(bvm* vm)  { int v=0; if (s_hooks && s_hooks->uno_hum)  v=s_hooks->uno_hum();  be_pushint(vm, v); be_return(vm); }

void native_api_register() {
  berry_vm_regfunc("uno_pot",    l_uno_pot);
  berry_vm_regfunc("uno_reed",   l_uno_reed);
  berry_vm_regfunc("uno_ir",     l_uno_ir);
  berry_vm_regfunc("uno_temp",   l_uno_temp);
  berry_vm_regfunc("uno_hum",    l_uno_hum);
  berry_vm_regfunc("wifi_rssi",  l_wifi_rssi);
  berry_vm_regfunc("wifi_ip",    l_wifi_ip);
  berry_vm_regfunc("dns_resolve", l_dns_resolve);
  berry_vm_regfunc("ping",        l_ping);
  berry_vm_regfunc("tcp_probe",   l_tcp_probe);
  berry_vm_regfunc("http_get",    l_http_get);
  berry_vm_regfunc("http_body",   l_http_body);
  berry_vm_regfunc("arp_count",   l_arp_count);
  berry_vm_regfunc("arp_ip",      l_arp_ip);
  berry_vm_regfunc("arp_mac",     l_arp_mac);
  berry_vm_regfunc("gpio_mode",   l_gpio_mode);
  berry_vm_regfunc("gpio_write",  l_gpio_write);
  berry_vm_regfunc("gpio_read",   l_gpio_read);
  berry_vm_regfunc("i2c_probe",   l_i2c_probe);
  berry_vm_regfunc("i2c_read8",   l_i2c_read8);
  berry_vm_regfunc("i2c_write8",  l_i2c_write8);
  berry_vm_regfunc("display_clear",     l_display_clear);
  berry_vm_regfunc("display_text",      l_display_text);
  berry_vm_regfunc("display_rect",      l_display_rect);
  berry_vm_regfunc("display_fill_rect", l_display_fill_rect);
  berry_vm_regfunc("button_pressed",    l_button_pressed);
  berry_vm_regfunc("adc_read_battery",  l_adc_read_battery);
  berry_vm_regfunc("millis",            l_millis);
  berry_vm_regfunc("free_heap",         l_free_heap);
  berry_vm_regfunc("heap_total",        l_heap_total);
  berry_vm_regfunc("sys_temp",          l_sys_temp);
  berry_vm_regfunc("cpu_mhz",           l_cpu_mhz);
}
