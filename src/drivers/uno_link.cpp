#include <Arduino.h>
#include <string.h>
#include "uno_link.h"
#include "../kernel/input_queue.h"
#include "tap_hold.h"

static UnoSensors s_sensors;
static UnoEnv     s_env;
static int        s_unoFreeRam = -1;   // вільна SRAM UNO (байт); -1 = ще не приходило
static uint32_t   s_unoUptime  = 0;    // аптайм UNO (с)
static uint32_t   s_last_rfid = 0;
static uint32_t   s_rfid_seq = 0;
static char       s_buf[64];   // 40 було замало для "RFA dat <block> <32hex>" (~43 симв.) -> обрізало hex
static int        s_len = 0;
static uint32_t   s_lastRxMs = 0;   // час останнього прийнятого рядка (детект «UNO онлайн»)

static UnoModule  s_modules[UNO_MAX_MODULES];
static int        s_moduleCount = 0;

static RfAudit    s_audit;
static char       s_auditBody[2400];   // вміщує повний дамп 1K (uid+16 sec+48 dat+end)
static int        s_auditBodyLen = 0;
static bool       s_auditing = false;

static int        s_wrOk = 0;          // лічильники результатів запису (clone)
static int        s_wrFail = 0;

// --- Джойстик (на UNO) -> навігація меню -------------------------------------
// UNO шле "JOY x y sw" (0..1023, центр ~512). Осі -> кнопки черги вводу (обидві осі
// з авто-повтором при утриманні нахилу):
//   Y вгору=S1(попередній)/вниз=S2(наступний)   — гортання рядків;
//   X вправо=S2(наступна сторінка)/вліво=S1(попередня) — гортання сторінок;
//   SW тап=S5(вибір), утримання=назад.
// Якщо напрямок фізично інвертований — поміняти S1<->S2 у відповідній осі нижче.
#define JOY_LO 400                 // < -> вгору/вліво
#define JOY_HI 620                 // > -> вниз/вправо (мертва зона 400..620)
#define JOY_REPEAT_MS 260
static int      s_jyZone = 0, s_jxZone = 0;
static uint32_t s_jyNext = 0, s_jxNext = 0;
static TapHoldDetector s_jswTh;    // кнопка джойстика: tap=S5, hold=back

// Спільна зонна логіка з авто-повтором для однієї осі: prevBtn при нахилі в −, nextBtn у +.
static void joy_axis(int v, int& zone, uint32_t& next, uint32_t now, ButtonId prevBtn, ButtonId nextBtn) {
  int z = (v < JOY_LO) ? -1 : (v > JOY_HI) ? 1 : 0;
  if (z != zone) {
    zone = z;
    if (z != 0) { input_queue().push_button(z < 0 ? prevBtn : nextBtn); next = now + 500; }
  } else if (z != 0 && now >= next) {
    input_queue().push_button(z < 0 ? prevBtn : nextBtn); next = now + JOY_REPEAT_MS;
  }
}

static void joy_nav(int x, int y, int sw) {
  uint32_t now = millis();
  joy_axis(y, s_jyZone, s_jyNext, now, BTN_S1, BTN_S2);   // Y — рядки: вгору=S1 / вниз=S2
  joy_axis(x, s_jxZone, s_jxNext, now, BTN_S1, BTN_S2);   // X — сторінки: вліво=S1 / вправо=S2
  switch (s_jswTh.update(sw == 0, now)) {                 // sw==0 -> level HIGH (відпущено)
    case TapHoldDetector::TAP:  input_queue().push_button(BTN_S5); break;
    case TapHoldDetector::HOLD: input_queue().push_back(); break;
    default: break;
  }
}

static void auditAppend(const char* line) {
  int n = (int)strlen(line);
  if (s_auditBodyLen + n + 2 >= (int)sizeof(s_auditBody)) return;   // повний буфер -> ігнор
  memcpy(s_auditBody + s_auditBodyLen, line, n);
  s_auditBodyLen += n;
  s_auditBody[s_auditBodyLen++] = '\n';
  s_auditBody[s_auditBodyLen] = 0;
}

void uno_link_init() {
  Serial1.setRxBufferSize(1024);   // RFID-дамп шле довгий бурст RFA-рядків -> запас RX
  Serial1.begin(9600, SERIAL_8N1, /*rx*/37, /*tx*/22);
}

// Знайти модуль за слотом або створити новий (Фаза 1 без BYE — модулі лише додаються/оновлюються).
static UnoModule* module_upsert(int slot) {
  for (int i = 0; i < s_moduleCount; i++) if (s_modules[i].slot == slot) return &s_modules[i];
  if (s_moduleCount < UNO_MAX_MODULES) { s_modules[s_moduleCount].slot = slot; return &s_modules[s_moduleCount++]; }
  return nullptr;
}

static void handle_line(const char* line) {
  UnoSensors sn;
  UnoEnv en;
  uint32_t uid;
  int slot, present;
  int wblock, wok;
  char type[10], name[14], val[20];
  int jx, jy, jsw, fr; uint32_t up;
  if (uno_parse_sensors(line, &sn)) s_sensors = sn;
  else if (uno_parse_joy(line, &jx, &jy, &jsw)) joy_nav(jx, jy, jsw);
  else if (uno_parse_stat(line, &fr, &up)) { s_unoFreeRam = fr; s_unoUptime = up; }
  else if (uno_parse_env(line, &en)) s_env = en;
  else if (uno_parse_rfid(line, &uid)) { s_last_rfid = uid; s_rfid_seq++; }
  else if (uno_parse_wres(line, &wblock, &wok)) { if (wok) s_wrOk++; else s_wrFail++; }
  else if (uno_parse_cap(line, &slot, type, sizeof(type), name, sizeof(name), &present)) {
    UnoModule* m = module_upsert(slot);
    if (m) {
      strncpy(m->type, type, sizeof(m->type) - 1); m->type[sizeof(m->type) - 1] = 0;
      strncpy(m->name, name, sizeof(m->name) - 1); m->name[sizeof(m->name) - 1] = 0;
      m->present = present != 0;
    }
  } else if (uno_parse_evt(line, &slot, val, sizeof(val))) {
    UnoModule* m = module_upsert(slot);
    if (m) { strncpy(m->value, val, sizeof(m->value) - 1); m->value[sizeof(m->value) - 1] = 0; }
  } else {
    // Red-team RFID-аудит: "RFA uid..." починає, "RFA sec..." накопичує, "RFA end..." завершує.
    char uid[21], type[18], verdict[14];
    if (uno_parse_rfa_uid(line, uid, sizeof(uid), type, sizeof(type))) {
      s_auditing = true;
      s_audit.valid = false;
      strncpy(s_audit.uid, uid, sizeof(s_audit.uid) - 1);   s_audit.uid[sizeof(s_audit.uid) - 1] = 0;
      strncpy(s_audit.type, type, sizeof(s_audit.type) - 1); s_audit.type[sizeof(s_audit.type) - 1] = 0;
      s_audit.cracked = s_audit.total = 0; s_audit.verdict[0] = 0;
      s_auditBodyLen = 0; s_auditBody[0] = 0;
      auditAppend(line);
    } else if (s_auditing && uno_is_rfa(line)) {
      auditAppend(line);
      int c, t;
      if (uno_parse_rfa_end(line, &c, &t, verdict, sizeof(verdict))) {
        s_audit.cracked = c; s_audit.total = t;
        strncpy(s_audit.verdict, verdict, sizeof(s_audit.verdict) - 1); s_audit.verdict[sizeof(s_audit.verdict) - 1] = 0;
        s_audit.valid = true; s_audit.seq++; s_auditing = false;
      }
    }
  }
}

void uno_link_loop() {
  while (Serial1.available()) {
    char c = (char)Serial1.read();
    if (c == '\n' || c == '\r') {
      if (s_len) { s_buf[s_len] = 0; handle_line(s_buf); s_len = 0; s_lastRxMs = millis(); }
    } else if (s_len < (int)sizeof(s_buf) - 1) {
      s_buf[s_len++] = c;
    }
  }
}

const UnoSensors& uno_link_sensors() { return s_sensors; }
const UnoEnv& uno_link_env() { return s_env; }
int uno_link_free_ram() { return s_unoFreeRam; }
uint32_t uno_link_uptime_s() { return s_unoUptime; }
bool uno_link_connected() { return s_lastRxMs != 0 && (millis() - s_lastRxMs) < 2000; }
long uno_link_last_rx_age_ms() { return s_lastRxMs ? (long)(millis() - s_lastRxMs) : -1; }
uint32_t uno_link_last_rfid() { return s_last_rfid; }
uint32_t uno_link_rfid_seq() { return s_rfid_seq; }

// --- Модульний хаб ---
int uno_link_module_count() { return s_moduleCount; }

const UnoModule& uno_link_module(int i) {
  static const UnoModule empty;
  if (i < 0 || i >= s_moduleCount) return empty;
  return s_modules[i];
}

void uno_link_send_set(int slot, const char* args) {
  char b[40]; int n = uno_build_set(slot, args, b, sizeof(b));
  if (n > 0) Serial1.write((const uint8_t*)b, n);
}

void uno_link_send_scan() {
  char b[8]; int n = uno_build_scan(b, sizeof(b));
  if (n > 0) Serial1.write((const uint8_t*)b, n);
}

const RfAudit& uno_link_audit()      { return s_audit; }
const char*    uno_link_audit_body() { return s_auditBody; }

// --- RFID write/clone ---
void uno_link_send_write(int block, const uint8_t* data16) {
  char b[48]; int n = uno_build_write(block, data16, b, sizeof(b));
  if (n > 0) Serial1.write((const uint8_t*)b, n);
}
int  uno_link_write_ok()   { return s_wrOk; }
int  uno_link_write_fail() { return s_wrFail; }
void uno_link_reset_write_counters() { s_wrOk = 0; s_wrFail = 0; }
