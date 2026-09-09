// Юніт-тести Berry-мосту на хості (pio test -e native) з РЕАЛЬНОЮ libberry.
// Апаратні хуки підмінені моками, що записують виклики зі скрипта.
#include <unity.h>
#include <string.h>
#include "berry_bridge/berry_vm.h"
#include "berry_bridge/native_api.h"

// --- Моки апаратних хуків ---
static int   mock_clear_calls;
static int   mock_text_calls;
static char  mock_last_text[64];
static int   mock_last_x, mock_last_y;
static uint16_t mock_last_color;
static float mock_battery_value;
static int   mock_button_value;
static uint32_t mock_millis_value;
static uint32_t mock_heap_value;
static uint32_t mock_heap_total_value;
static int mock_temp_value;
static int mock_cpu_value;
static int mock_fill_calls;
static int mock_last_rect[5];  // x,y,w,h,color

static void m_clear() { mock_clear_calls++; }
static void m_text(int x, int y, const char* s, uint16_t color) {
  mock_text_calls++;
  mock_last_x = x; mock_last_y = y; mock_last_color = color;
  strncpy(mock_last_text, s, sizeof(mock_last_text) - 1);
  mock_last_text[sizeof(mock_last_text) - 1] = '\0';
}
static float    m_battery() { return mock_battery_value; }
static int      m_button()  { return mock_button_value; }
static uint32_t m_millis()  { return mock_millis_value; }
static uint32_t m_heap()    { return mock_heap_value; }
static uint32_t m_heap_total() { return mock_heap_total_value; }
static void m_rect(int x, int y, int w, int h, uint16_t c) {
  mock_last_rect[0]=x; mock_last_rect[1]=y; mock_last_rect[2]=w; mock_last_rect[3]=h; mock_last_rect[4]=c;
}
static void m_fill_rect(int x, int y, int w, int h, uint16_t c) {
  mock_fill_calls++;
  mock_last_rect[0]=x; mock_last_rect[1]=y; mock_last_rect[2]=w; mock_last_rect[3]=h; mock_last_rect[4]=c;
}
static int m_temp() { return mock_temp_value; }
static int m_cpu()  { return mock_cpu_value; }

static int mock_gpio_pin, mock_gpio_val, mock_gpio_read_ret;
static void m_gpio_mode(int, int) {}
static void m_gpio_write(int pin, int val) { mock_gpio_pin = pin; mock_gpio_val = val; }
static int  m_gpio_read(int pin) { mock_gpio_pin = pin; return mock_gpio_read_ret; }
static int  m_i2c_probe(int) { return 1; }
static int  m_i2c_read8(int, int) { return 0x42; }
static void m_i2c_write8(int, int, int) {}

// Заглушки членів між i2c та мережевими пробами (MOCK_HOOKS — позиційний init).
static int  m_wifi_rssi() { return -42; }
static const char* m_wifi_ip() { return "10.0.0.9"; }

// Мокі мережевих проб (записують аргументи, повертають фіксовані значення).
static char mock_dns_host[64];
static const char* m_net_dns(const char* h) { strncpy(mock_dns_host, h, 63); mock_dns_host[63] = 0; return "1.2.3.4"; }
static char mock_ping_host[64];
static int  m_net_ping(const char* h) { strncpy(mock_ping_host, h, 63); mock_ping_host[63] = 0; return 7; }
static int  mock_tcp_port;
static int  m_net_tcp(const char* h, int p) { (void)h; mock_tcp_port = p; return 1; }
static char mock_http_url[96];
static int  m_net_http_get(const char* u) { strncpy(mock_http_url, u, 95); mock_http_url[95] = 0; return 200; }
static const char* m_net_http_body() { return "BODY-OK"; }
static int  m_net_arp_count() { return 2; }
static const char* m_net_arp_ip(int i)  { return i == 0 ? "192.168.0.1" : "192.168.0.2"; }
static const char* m_net_arp_mac(int i) { return i == 0 ? "AA:BB:CC:00:11:22" : "DE:AD:BE:EF:00:01"; }

static const NativeApiHooks MOCK_HOOKS = {
  m_clear, m_text, m_battery, m_button, m_millis, m_heap,
  m_heap_total, m_rect, m_fill_rect, m_temp, m_cpu,
  m_gpio_mode, m_gpio_write, m_gpio_read, m_i2c_probe, m_i2c_read8, m_i2c_write8,
  m_wifi_rssi, m_wifi_ip,
  m_net_dns, m_net_ping, m_net_tcp, m_net_http_get, m_net_http_body,
  m_net_arp_count, m_net_arp_ip, m_net_arp_mac
};

static void reset_mocks() {
  mock_clear_calls = 0;
  mock_text_calls = 0;
  mock_last_text[0] = '\0';
  mock_last_x = mock_last_y = 0;
  mock_last_color = 0;
  mock_battery_value = 3.85f;
  mock_button_value = -1;
  mock_millis_value = 0;
  mock_heap_value = 0;
  mock_heap_total_value = 0;
  mock_temp_value = 0;
  mock_cpu_value = 0;
  mock_fill_calls = 0;
  for (int i = 0; i < 5; i++) mock_last_rect[i] = 0;
  mock_dns_host[0] = 0;
  mock_ping_host[0] = 0;
  mock_tcp_port = 0;
  mock_http_url[0] = 0;
}

void setUp() {
  reset_mocks();
  native_api_set_hooks(&MOCK_HOOKS);
}
void tearDown() {}

// VM ініціалізується (DoD: без падіння)
void test_vm_init() {
  TEST_ASSERT_TRUE(berry_vm_init());
  TEST_ASSERT_NOT_NULL(berry_vm_raw());
}

// Базове виконання арифметики
void test_run_simple() {
  TEST_ASSERT_TRUE(berry_vm_init());
  TEST_ASSERT_TRUE(berry_vm_run_string("t", "var a = 2 + 3"));
}

// Синтаксична помилка ловиться, повідомлення не порожнє, VM живе далі
void test_syntax_error_caught() {
  TEST_ASSERT_TRUE(berry_vm_init());
  TEST_ASSERT_FALSE(berry_vm_run_string("t", "def broken( ="));
  TEST_ASSERT_TRUE(strlen(berry_vm_last_error()) > 0);
  // після помилки VM усе ще виконує коректний код
  TEST_ASSERT_TRUE(berry_vm_run_string("t", "var ok = 1"));
}

// native display_clear() викликається зі скрипта
void test_native_display_clear() {
  TEST_ASSERT_TRUE(berry_vm_init());
  native_api_register();
  TEST_ASSERT_TRUE(berry_vm_run_string("t", "display_clear()"));
  TEST_ASSERT_EQUAL_INT(1, mock_clear_calls);
}

// native display_text(x,y,str) передає аргументи правильно
void test_native_display_text_args() {
  TEST_ASSERT_TRUE(berry_vm_init());
  native_api_register();
  TEST_ASSERT_TRUE(berry_vm_run_string("t", "display_text(10, 20, 'HELLO')"));
  TEST_ASSERT_EQUAL_INT(1, mock_text_calls);
  TEST_ASSERT_EQUAL_INT(10, mock_last_x);
  TEST_ASSERT_EQUAL_INT(20, mock_last_y);
  TEST_ASSERT_EQUAL_STRING("HELLO", mock_last_text);
}

// display_text із явним кольором (4-й аргумент)
void test_native_display_text_color() {
  TEST_ASSERT_TRUE(berry_vm_init());
  native_api_register();
  TEST_ASSERT_TRUE(berry_vm_run_string("t", "display_text(0, 0, 'X', 0xF800)"));
  TEST_ASSERT_EQUAL_HEX16(0xF800, mock_last_color);
}

// adc_read_battery() повертає значення від хука у скрипт
void test_native_adc_returns_value() {
  TEST_ASSERT_TRUE(berry_vm_init());
  native_api_register();
  mock_battery_value = 4.0f;  // точно представне: уникаємо усічення float (4.10f*100=409.99)
  // скрипт бере напругу і кладе в текст через display_text -> перевіряємо доставку
  TEST_ASSERT_TRUE(berry_vm_run_string("t",
    "var v = adc_read_battery()\n"
    "display_text(0, 0, str(int(v * 100)))"));
  TEST_ASSERT_EQUAL_STRING("400", mock_last_text);
}

// button_pressed() пробрасує id від хука в скрипт
void test_native_button_value() {
  TEST_ASSERT_TRUE(berry_vm_init());
  native_api_register();
  mock_button_value = 4;  // S5
  TEST_ASSERT_TRUE(berry_vm_run_string("t",
    "var b = button_pressed()\n"
    "display_text(0, 0, str(b))"));
  TEST_ASSERT_EQUAL_STRING("4", mock_last_text);
}

// Повний демо-сценарій: def app_draw() + виклик через call_global
void test_app_draw_flow() {
  TEST_ASSERT_TRUE(berry_vm_init());
  native_api_register();
  const char* script =
    "def app_draw()\n"
    "  display_clear()\n"
    "  display_text(6, 4, 'DEMO')\n"
    "end\n";
  TEST_ASSERT_TRUE(berry_vm_run_string("demo", script));
  TEST_ASSERT_TRUE(berry_vm_call_global("app_draw"));
  TEST_ASSERT_EQUAL_INT(1, mock_clear_calls);
  TEST_ASSERT_EQUAL_INT(1, mock_text_calls);
  TEST_ASSERT_EQUAL_STRING("DEMO", mock_last_text);
}

// millis() пробрасує значення хука у скрипт
void test_native_millis() {
  TEST_ASSERT_TRUE(berry_vm_init());
  native_api_register();
  mock_millis_value = 12345;
  TEST_ASSERT_TRUE(berry_vm_run_string("t", "display_text(0, 0, str(millis()))"));
  TEST_ASSERT_EQUAL_STRING("12345", mock_last_text);
}

// free_heap() пробрасує значення хука у скрипт
void test_native_free_heap() {
  TEST_ASSERT_TRUE(berry_vm_init());
  native_api_register();
  mock_heap_value = 88372;
  TEST_ASSERT_TRUE(berry_vm_run_string("t", "display_text(0, 0, str(free_heap()))"));
  TEST_ASSERT_EQUAL_STRING("88372", mock_last_text);
}

// display_fill_rect передає 5 аргументів у хук
void test_native_fill_rect() {
  TEST_ASSERT_TRUE(berry_vm_init());
  native_api_register();
  TEST_ASSERT_TRUE(berry_vm_run_string("t", "display_fill_rect(10, 20, 100, 8, 0x05FF)"));
  TEST_ASSERT_EQUAL_INT(1, mock_fill_calls);
  TEST_ASSERT_EQUAL_INT(10, mock_last_rect[0]);
  TEST_ASSERT_EQUAL_INT(20, mock_last_rect[1]);
  TEST_ASSERT_EQUAL_INT(100, mock_last_rect[2]);
  TEST_ASSERT_EQUAL_INT(8, mock_last_rect[3]);
  TEST_ASSERT_EQUAL_HEX16(0x05FF, mock_last_rect[4]);
}

// sys_temp / cpu_mhz / heap_total пробрасують значення хуків
void test_native_sys_getters() {
  TEST_ASSERT_TRUE(berry_vm_init());
  native_api_register();
  mock_temp_value = 42;
  mock_cpu_value = 240;
  mock_heap_total_value = 300000;
  TEST_ASSERT_TRUE(berry_vm_run_string("t", "display_text(0,0, str(sys_temp()))"));
  TEST_ASSERT_EQUAL_STRING("42", mock_last_text);
  TEST_ASSERT_TRUE(berry_vm_run_string("t", "display_text(0,0, str(cpu_mhz()))"));
  TEST_ASSERT_EQUAL_STRING("240", mock_last_text);
  TEST_ASSERT_TRUE(berry_vm_run_string("t", "display_text(0,0, str(heap_total()))"));
  TEST_ASSERT_EQUAL_STRING("300000", mock_last_text);
}

// gpio_write/gpio_read зі скрипта доходять до хуків
void test_native_gpio() {
  TEST_ASSERT_TRUE(berry_vm_init());
  native_api_register();
  mock_gpio_read_ret = 1;
  TEST_ASSERT_TRUE(berry_vm_run_string("t",
    "gpio_mode(2, 1)\n gpio_write(2, 1)\n display_text(0,0, str(gpio_read(2)))"));
  TEST_ASSERT_EQUAL_INT(2, mock_gpio_pin);
  TEST_ASSERT_EQUAL_INT(1, mock_gpio_val);
  TEST_ASSERT_EQUAL_STRING("1", mock_last_text);
}

// i2c_probe/read8 зі скрипта
void test_native_i2c() {
  TEST_ASSERT_TRUE(berry_vm_init());
  native_api_register();
  TEST_ASSERT_TRUE(berry_vm_run_string("t", "display_text(0,0, str(i2c_read8(0x40, 0)))"));
  TEST_ASSERT_EQUAL_STRING("66", mock_last_text);   // 0x42
}

// dns_resolve(host) пробрасує host у хук і повертає IP-рядок
void test_net_dns_resolve() {
  TEST_ASSERT_TRUE(berry_vm_init());
  native_api_register();
  TEST_ASSERT_TRUE(berry_vm_run_string("t", "display_text(0,0, dns_resolve('example.com'))"));
  TEST_ASSERT_EQUAL_STRING("1.2.3.4", mock_last_text);
  TEST_ASSERT_EQUAL_STRING("example.com", mock_dns_host);
}

// ping(host) -> int мс від хука
void test_net_ping() {
  TEST_ASSERT_TRUE(berry_vm_init());
  native_api_register();
  TEST_ASSERT_TRUE(berry_vm_run_string("t", "display_text(0,0, str(ping('host.local')))"));
  TEST_ASSERT_EQUAL_STRING("7", mock_last_text);
  TEST_ASSERT_EQUAL_STRING("host.local", mock_ping_host);
}

// tcp_probe(host, port) -> bool, порт доходить до хука
void test_net_tcp_probe() {
  TEST_ASSERT_TRUE(berry_vm_init());
  native_api_register();
  TEST_ASSERT_TRUE(berry_vm_run_string("t", "display_text(0,0, str(tcp_probe('h', 8080)))"));
  TEST_ASSERT_EQUAL_STRING("true", mock_last_text);
  TEST_ASSERT_EQUAL_INT(8080, mock_tcp_port);
}

// http_get(url) -> статус; url доходить до хука; http_body() -> тіло
void test_net_http() {
  TEST_ASSERT_TRUE(berry_vm_init());
  native_api_register();
  TEST_ASSERT_TRUE(berry_vm_run_string("t", "display_text(0,0, str(http_get('http://x/y')))"));
  TEST_ASSERT_EQUAL_STRING("200", mock_last_text);
  TEST_ASSERT_EQUAL_STRING("http://x/y", mock_http_url);
  TEST_ASSERT_TRUE(berry_vm_run_string("t", "display_text(0,0, http_body())"));
  TEST_ASSERT_EQUAL_STRING("BODY-OK", mock_last_text);
}

// arp_count()/arp_ip(i)/arp_mac(i) зі скрипта
void test_net_arp() {
  TEST_ASSERT_TRUE(berry_vm_init());
  native_api_register();
  TEST_ASSERT_TRUE(berry_vm_run_string("t", "display_text(0,0, str(arp_count()))"));
  TEST_ASSERT_EQUAL_STRING("2", mock_last_text);
  TEST_ASSERT_TRUE(berry_vm_run_string("t", "display_text(0,0, arp_ip(1))"));
  TEST_ASSERT_EQUAL_STRING("192.168.0.2", mock_last_text);
  TEST_ASSERT_TRUE(berry_vm_run_string("t", "display_text(0,0, arp_mac(0))"));
  TEST_ASSERT_EQUAL_STRING("AA:BB:CC:00:11:22", mock_last_text);
}

// call_global на неіснуючу функцію — false, помилка непорожня
void test_call_missing_global() {
  TEST_ASSERT_TRUE(berry_vm_init());
  TEST_ASSERT_FALSE(berry_vm_call_global("no_such_func"));
  TEST_ASSERT_TRUE(strlen(berry_vm_last_error()) > 0);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_vm_init);
  RUN_TEST(test_run_simple);
  RUN_TEST(test_syntax_error_caught);
  RUN_TEST(test_native_display_clear);
  RUN_TEST(test_native_display_text_args);
  RUN_TEST(test_native_display_text_color);
  RUN_TEST(test_native_adc_returns_value);
  RUN_TEST(test_native_button_value);
  RUN_TEST(test_native_millis);
  RUN_TEST(test_native_free_heap);
  RUN_TEST(test_native_fill_rect);
  RUN_TEST(test_native_sys_getters);
  RUN_TEST(test_native_gpio);
  RUN_TEST(test_native_i2c);
  RUN_TEST(test_net_dns_resolve);
  RUN_TEST(test_net_ping);
  RUN_TEST(test_net_tcp_probe);
  RUN_TEST(test_net_http);
  RUN_TEST(test_net_arp);
  RUN_TEST(test_app_draw_flow);
  RUN_TEST(test_call_missing_global);
  return UNITY_END();
}
