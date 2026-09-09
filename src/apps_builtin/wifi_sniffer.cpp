#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include "wifi_sniffer.h"
#include "../drivers/display.h"
#include "../remote/protocol.h"
#include "../remote/remote_control.h"
#include "../drivers/power_guard_hw.h"
#include "../kernel/power_guard.h"

#define MAX_RUN_MS 90000   // авто-вихід: promiscuous рубить мережу, не даємо крутитись вічно

#define DWELL_MS 250          // затримка на каналі перед хопом
#define MAX_APS 32
#define MAX_CLIENTS 48
#define RING_SZ 64            // мусить бути степенем двійки (SPSC lock-free)

enum Enc { ENC_OPEN, ENC_WEP, ENC_WPA, ENC_WPA2, ENC_WPA3 };
static const char* ENC_LBL[] = { "OPN", "WEP", "WPA", "WPA2", "WPA3" };

// --- Компактне спостереження, яке кладе callback ---
struct Sighting {
  uint8_t kind;      // 0=AP(beacon/probe-resp) 1=client-probe 2=client-link 3=deauth/disassoc
  int8_t  rssi;
  uint8_t channel;
  uint8_t enc;
  uint8_t a[6];      // AP BSSID (kind0) або MAC клієнта (kind1/2/3)
  uint8_t b[6];      // повʼязаний BSSID (kind2)
  char    ssid[33];  // SSID AP (kind0) або probe-SSID (kind1)
};

// --- SPSC lock-free ring: producer = WiFi-задача, consumer = loop() ---
static volatile uint32_t s_head = 0;   // пише лише producer
static volatile uint32_t s_tail = 0;   // пише лише consumer
static Sighting s_ring[RING_SZ];

// SPSC: продюсер — promiscuous-callback (ядро 0), консюмер — main loop (ядро 1). __sync_synchronize()
// між записом даних і публікацією індексу ОБОВ'ЯЗКОВИЙ (інакше крос-ядерна гонка: новий індекс зі
// старими даними). Волатайл сам не впорядковує не-volatile запис ring відносно volatile-індексу.
static inline void ring_push(const Sighting& s) {
  uint32_t h = s_head;
  if (h - s_tail >= RING_SZ) return;     // повний -> дропаємо (не блокуємо WiFi-задачу)
  s_ring[h & (RING_SZ - 1)] = s;
  __sync_synchronize();                  // дані видимі ДО публікації s_head
  s_head = h + 1;                        // публікація
}
static inline bool ring_pop(Sighting& out) {
  uint32_t t = s_tail;
  if (t == s_head) return false;         // порожньо
  out = s_ring[t & (RING_SZ - 1)];
  __sync_synchronize();                  // дочитати слот ДО звільнення (advance s_tail)
  s_tail = t + 1;
  return true;
}

// --- Агреговані таблиці (лише головний потік) ---
struct ApRec  { uint8_t bssid[6]; char ssid[33]; uint8_t channel; int8_t rssi; uint8_t enc; uint16_t beacons; uint32_t last; };
struct CliRec { uint8_t mac[6];   uint8_t bssid[6]; int8_t rssi; char probe[33]; bool assoc; uint32_t last; };

static ApRec  s_aps[MAX_APS];
static CliRec s_clis[MAX_CLIENTS];
static int    s_ap_n = 0, s_cli_n = 0;
static uint32_t s_frames = 0, s_deauth = 0, s_probes = 0;
static bool   s_promisc_on = false;

// ---------- promiscuous callback (WiFi-задача) ----------
static void parse_ies(const uint8_t* d, int len, int off,
                      char* ssid, uint8_t* ch_ie, bool* rsn, bool* wpa, bool* sae) {
  int i = off;
  while (i + 2 <= len) {
    uint8_t id = d[i], l = d[i + 1];
    if (i + 2 + (int)l > len) break;
    const uint8_t* v = d + i + 2;
    if (id == 0) {                                  // SSID
      int n = l > 32 ? 32 : l;
      memcpy(ssid, v, n); ssid[n] = 0;
    } else if (id == 3 && l >= 1) {                 // DS-параметр -> канал
      *ch_ie = v[0];
    } else if (id == 48) {                          // RSN -> WPA2/WPA3
      *rsn = true;
      for (int k = 0; k + 4 <= l; k++)              // AKM SAE 00-0F-AC-08 -> WPA3
        if (v[k] == 0x00 && v[k+1] == 0x0F && v[k+2] == 0xAC && v[k+3] == 0x08) { *sae = true; break; }
    } else if (id == 221 && l >= 4 &&               // vendor WPA (Microsoft OUI, type 1)
               v[0] == 0x00 && v[1] == 0x50 && v[2] == 0xF2 && v[3] == 0x01) {
      *wpa = true;
    }
    i += 2 + l;
  }
}

static void promisc_cb(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_MGMT && type != WIFI_PKT_DATA) return;
  wifi_promiscuous_pkt_t* p = (wifi_promiscuous_pkt_t*)buf;
  const uint8_t* d = p->payload;
  int len = p->rx_ctrl.sig_len;
  if (len < 24) return;                             // повний MAC-заголовок

  uint8_t ftype = (d[0] >> 2) & 0x3;
  uint8_t sub   = (d[0] >> 4) & 0xF;
  const uint8_t* a1 = d + 4;
  const uint8_t* a2 = d + 10;
  const uint8_t* a3 = d + 16;

  Sighting s; memset(&s, 0, sizeof(s));
  s.rssi = p->rx_ctrl.rssi;
  s.channel = p->rx_ctrl.channel;

  if (ftype == 0) {                                 // MGMT
    if (sub == 8 || sub == 5) {                     // beacon / probe-response
      s.kind = 0;
      memcpy(s.a, a3, 6);                           // BSSID
      bool privacy = (len >= 36) && (d[34] & 0x10);
      bool rsn = false, wpa = false, sae = false;
      uint8_t ch_ie = s.channel;
      if (len > 36) parse_ies(d, len, 36, s.ssid, &ch_ie, &rsn, &wpa, &sae);
      if (ch_ie >= 1 && ch_ie <= 14) s.channel = ch_ie;
      s.enc = !privacy ? ENC_OPEN : sae ? ENC_WPA3 : rsn ? ENC_WPA2 : wpa ? ENC_WPA : ENC_WEP;
    } else if (sub == 4) {                          // probe-request (клієнт шукає мережу)
      s.kind = 1;
      memcpy(s.a, a2, 6);
      bool rsn = false, wpa = false, sae = false; uint8_t ch = s.channel;
      if (len > 24) parse_ies(d, len, 24, s.ssid, &ch, &rsn, &wpa, &sae);
    } else if (sub == 0 || sub == 2) {              // assoc / reassoc request
      s.kind = 2; memcpy(s.a, a2, 6); memcpy(s.b, a3, 6);
    } else if (sub == 10 || sub == 12) {            // disassoc / deauth
      s.kind = 3; memcpy(s.a, a2, 6); memcpy(s.b, a3, 6);
    } else return;
  } else {                                          // DATA -> звʼязок клієнт<->AP
    uint8_t tods = d[1] & 0x01, fromds = d[1] & 0x02;
    s.kind = 2;
    if (tods && !fromds)      { memcpy(s.b, a1, 6); memcpy(s.a, a2, 6); }  // AP=a1, клієнт=a2
    else if (!tods && fromds) { memcpy(s.a, a1, 6); memcpy(s.b, a2, 6); }  // клієнт=a1, AP=a2
    else return;                                    // IBSS/WDS — пропускаємо
  }
  ring_push(s);
}

// ---------- агрегація (головний потік) ----------
static bool mac_bad(const uint8_t* m) {             // broadcast/multicast (I/G біт) — не клієнт
  if (m[0] & 0x01) return true;
  uint8_t z = 0; for (int i = 0; i < 6; i++) z |= m[i];
  return z == 0;
}
static void upsert_ap(const Sighting& s) {
  for (int i = 0; i < s_ap_n; i++) if (memcmp(s_aps[i].bssid, s.a, 6) == 0) {
    s_aps[i].rssi = s.rssi; s_aps[i].channel = s.channel; s_aps[i].enc = s.enc;
    if (s.ssid[0]) memcpy(s_aps[i].ssid, s.ssid, 33);
    s_aps[i].beacons++; s_aps[i].last = millis(); return;
  }
  if (s_ap_n >= MAX_APS) return;
  ApRec& a = s_aps[s_ap_n++];
  memcpy(a.bssid, s.a, 6); memcpy(a.ssid, s.ssid, 33);
  a.channel = s.channel; a.rssi = s.rssi; a.enc = s.enc; a.beacons = 1; a.last = millis();
}
static void upsert_cli(const uint8_t* mac, const uint8_t* bssid, int8_t rssi, const char* probe) {
  if (mac_bad(mac)) return;
  bool has_ap = bssid && !mac_bad(bssid);
  for (int i = 0; i < s_cli_n; i++) if (memcmp(s_clis[i].mac, mac, 6) == 0) {
    s_clis[i].rssi = rssi; s_clis[i].last = millis();
    if (has_ap) { memcpy(s_clis[i].bssid, bssid, 6); s_clis[i].assoc = true; }
    if (probe && probe[0]) memcpy(s_clis[i].probe, probe, 33);
    return;
  }
  if (s_cli_n >= MAX_CLIENTS) return;
  CliRec& c = s_clis[s_cli_n++];
  memset(&c, 0, sizeof(c));
  memcpy(c.mac, mac, 6); c.rssi = rssi; c.last = millis();
  if (has_ap) { memcpy(c.bssid, bssid, 6); c.assoc = true; }
  if (probe && probe[0]) memcpy(c.probe, probe, 33);
}
static void clear_tables() {
  s_ap_n = s_cli_n = 0; s_frames = s_deauth = s_probes = 0;
  s_tail = s_head;                                  // скинути ring
}

// ---------- App ----------
void WifiSnifferApp::init() {
  wants_exit_ = false; page_ = SUMMARY;
  if (remote_active_mode() == MODE_BT) { phase_ = BT_BLOCKED; return; }
  if (!power_guard_ok())               { phase_ = LOW_POWER;  return; }
  phase_ = RUNNING;
  was_wifi_ = (remote_active_mode() == MODE_WIFI);
  clear_tables();
  cur_channel_ = 1; t_hop_ = millis(); t_start_ = millis();
  WiFi.mode(WIFI_MODE_STA);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(&promisc_cb);
  esp_wifi_set_channel(cur_channel_, WIFI_SECOND_CHAN_NONE);
  s_promisc_on = true;
}

void WifiSnifferApp::on_exit() {
  if (s_promisc_on) { esp_wifi_set_promiscuous(false); s_promisc_on = false; }
  remote_restore_after_promiscuous(was_wifi_);   // відновити SoftAP+web+STA, які забрав promiscuous
}

void WifiSnifferApp::loop() {
  if (phase_ != RUNNING) return;
  uint32_t now = millis();
  if (now - t_start_ > MAX_RUN_MS) { wants_exit_ = true; return; }   // авто-вихід -> мережа назад
  if (now - t_hop_ >= DWELL_MS) {
    t_hop_ = now;
    cur_channel_ = (cur_channel_ % 13) + 1;
    esp_wifi_set_channel(cur_channel_, WIFI_SECOND_CHAN_NONE);
  }
  Sighting s;
  int budget = 48;                                  // обмежити роботу за тік
  while (budget-- > 0 && ring_pop(s)) {
    s_frames++;
    switch (s.kind) {
      case 0: upsert_ap(s); break;
      case 1: s_probes++; upsert_cli(s.a, nullptr, s.rssi, s.ssid); break;
      case 2: upsert_cli(s.a, s.b, s.rssi, nullptr); break;
      case 3: s_deauth++; break;
    }
  }

  // Рядок статистики в Serial (раз на 3с) — під час активного sniffer радіо
  // зайняте promiscuous-режимом, тож SoftAP лежить і веб-дзеркалення недоступне;
  // USB-Serial лишається єдиним «живим» вікном у захоплення (корисно headless).
  static uint32_t t_dbg = 0;
  if (now - t_dbg > 3000) {
    t_dbg = now;
    Serial.printf("[SNIFF] ch%d APs=%d cli=%d frames=%lu deauth=%lu probes=%lu\n",
                  cur_channel_, s_ap_n, s_cli_n, (unsigned long)s_frames,
                  (unsigned long)s_deauth, (unsigned long)s_probes);
  }
}

// індекси top-N AP за RSSI (спадання)
static int top_aps(int* order, int cap) {
  int n = s_ap_n < cap ? s_ap_n : cap; (void)n;
  int used = 0;
  bool taken[MAX_APS] = { false };
  for (int k = 0; k < cap && used < s_ap_n; k++) {
    int best = -1;
    for (int i = 0; i < s_ap_n; i++)
      if (!taken[i] && (best < 0 || s_aps[i].rssi > s_aps[best].rssi)) best = i;
    if (best < 0) break;
    taken[best] = true; order[used++] = best;
  }
  return used;
}
// індекси top-N клієнтів за свіжістю (останні першими)
static int top_clis(int* order, int cap) {
  int used = 0;
  bool taken[MAX_CLIENTS] = { false };
  for (int k = 0; k < cap && used < s_cli_n; k++) {
    int best = -1;
    for (int i = 0; i < s_cli_n; i++)
      if (!taken[i] && (best < 0 || s_clis[i].last > s_clis[best].last)) best = i;
    if (best < 0) break;
    taken[best] = true; order[used++] = best;
  }
  return used;
}
static int clients_of(const uint8_t* bssid) {
  int c = 0;
  for (int i = 0; i < s_cli_n; i++)
    if (s_clis[i].assoc && memcmp(s_clis[i].bssid, bssid, 6) == 0) c++;
  return c;
}
static uint16_t rssi_col(int8_t r) { return r >= -60 ? C_GOOD : r >= -75 ? C_ACCENT : C_DIM; }

void WifiSnifferApp::topBar(const char* title) {
  display_top_bar(title);
  TFT_eSprite& spr = display_sprite();
  if (phase_ == RUNNING) {
    spr.setTextDatum(TR_DATUM); spr.setTextColor(C_WARN, C_PANEL);
    char cb[8]; snprintf(cb, sizeof(cb), "ch%d", cur_channel_);
    spr.drawString(cb, SCR_W - 6, 3, 2);
  }
}

void WifiSnifferApp::drawSummary() {
  TFT_eSprite& spr = display_sprite();
  topBar("WIFI SNIFFER");
  spr.setTextDatum(TL_DATUM);
  char b[40];
  spr.setTextColor(C_TEXT, C_BG);
  snprintf(b, sizeof(b), "APs: %d", s_ap_n);        spr.drawString(b, 10, 24, 4);
  snprintf(b, sizeof(b), "Clients: %d", s_cli_n);   spr.drawString(b, 10, 52, 4);
  spr.setTextColor(C_DIM, C_BG);
  snprintf(b, sizeof(b), "Frames: %lu  Probes: %lu", (unsigned long)s_frames, (unsigned long)s_probes);
  spr.drawString(b, 10, 84, 1);
  spr.setTextColor(s_deauth ? C_BAD : C_DIM, C_BG);
  snprintf(b, sizeof(b), "Deauth/disassoc: %lu", (unsigned long)s_deauth);
  spr.drawString(b, 10, 96, 1);
  spr.setTextColor(C_DIM, C_BG);
  spr.drawString("Passive only, no injection", 10, SCR_H - 22, 1);
  spr.drawString("S2=page  S5=clear", 10, SCR_H - 12, 1);
}

void WifiSnifferApp::drawAps() {
  TFT_eSprite& spr = display_sprite();
  topBar("SNIFFER: APs");
  int order[6]; int n = top_aps(order, 6);
  int y = 20;
  spr.setTextDatum(TL_DATUM);
  if (n == 0) { spr.setTextColor(C_DIM, C_BG); spr.drawString("(scanning...)", 10, 40, 2); }
  for (int i = 0; i < n; i++) {
    ApRec& a = s_aps[order[i]];
    const char* ss = a.ssid[0] ? a.ssid : "<hidden>";
    char nm[15]; snprintf(nm, sizeof(nm), "%-14.14s", ss);
    spr.setTextColor(C_TEXT, C_BG); spr.drawString(nm, 6, y, 1);
    char meta[20];
    snprintf(meta, sizeof(meta), "c%-2d %-4s %dcl", a.channel, ENC_LBL[a.enc], clients_of(a.bssid));
    spr.setTextColor(C_DIM, C_BG); spr.drawString(meta, 108, y, 1);
    spr.setTextDatum(TR_DATUM); spr.setTextColor(rssi_col(a.rssi), C_BG);
    char rb[6]; snprintf(rb, sizeof(rb), "%d", a.rssi); spr.drawString(rb, SCR_W - 6, y, 1);
    spr.setTextDatum(TL_DATUM);
    y += 17;
  }
  spr.setTextColor(C_DIM, C_BG); spr.setTextDatum(TR_DATUM);
  char tot[10]; snprintf(tot, sizeof(tot), "%d APs", s_ap_n);
  spr.drawString(tot, SCR_W - 6, SCR_H - 11, 1); spr.setTextDatum(TL_DATUM);
}

void WifiSnifferApp::drawClients() {
  TFT_eSprite& spr = display_sprite();
  topBar("SNIFFER: Clients");
  int order[6]; int n = top_clis(order, 6);
  int y = 20;
  spr.setTextDatum(TL_DATUM);
  if (n == 0) { spr.setTextColor(C_DIM, C_BG); spr.drawString("(no clients yet)", 10, 40, 2); }
  for (int i = 0; i < n; i++) {
    CliRec& c = s_clis[order[i]];
    char mac[10]; snprintf(mac, sizeof(mac), "%02X:%02X:%02X", c.mac[3], c.mac[4], c.mac[5]);
    spr.setTextColor(C_TEXT, C_BG); spr.drawString(mac, 6, y, 1);
    char info[24];
    if (c.assoc) {                                  // приєднаний -> показати SSID його AP якщо знаємо
      const char* apn = nullptr;
      for (int k = 0; k < s_ap_n; k++) if (memcmp(s_aps[k].bssid, c.bssid, 6) == 0) { apn = s_aps[k].ssid; break; }
      if (apn && apn[0]) snprintf(info, sizeof(info), "->%-.12s", apn);
      else snprintf(info, sizeof(info), "->%02X%02X%02X", c.bssid[3], c.bssid[4], c.bssid[5]);
    } else if (c.probe[0]) snprintf(info, sizeof(info), "~%-.13s", c.probe);
    else snprintf(info, sizeof(info), "~(broadcast)");
    spr.setTextColor(c.assoc ? C_GOOD : C_DIM, C_BG); spr.drawString(info, 66, y, 1);
    spr.setTextDatum(TR_DATUM); spr.setTextColor(rssi_col(c.rssi), C_BG);
    char rb[6]; snprintf(rb, sizeof(rb), "%d", c.rssi); spr.drawString(rb, SCR_W - 6, y, 1);
    spr.setTextDatum(TL_DATUM);
    y += 17;
  }
  spr.setTextColor(C_DIM, C_BG); spr.setTextDatum(TR_DATUM);
  char tot[12]; snprintf(tot, sizeof(tot), "%d clients", s_cli_n);
  spr.drawString(tot, SCR_W - 6, SCR_H - 11, 1); spr.setTextDatum(TL_DATUM);
}

void WifiSnifferApp::drawExit() {
  TFT_eSprite& spr = display_sprite();
  topBar("SNIFFER: Exit");
  spr.setTextDatum(MC_DATUM);
  spr.setTextColor(C_TEXT, C_BG);
  spr.drawString("Exit? (STA will recover)", SCR_W / 2, SCR_H / 2 - 10, 2);
  spr.setTextColor(C_ACCENT, C_BG);
  spr.drawString("S5 = exit   S2 = next", SCR_W / 2, SCR_H / 2 + 18, 2);
}

void WifiSnifferApp::draw() {
  TFT_eSprite& spr = display_sprite();
  spr.fillSprite(C_BG);
  if (phase_ == BT_BLOCKED) {
    topBar("WIFI SNIFFER");
    spr.setTextColor(C_WARN, C_BG); spr.drawString("Active Remote: BT", 8, 30, 2);
    spr.setTextColor(C_DIM, C_BG);  spr.drawString("BT i WiFi ne mozhut razom", 8, 52, 1);
    spr.setTextColor(C_ACCENT, C_BG); spr.drawString("S2/S5 = exit", 8, SCR_H - 14, 2);
    return;
  }
  if (phase_ == LOW_POWER) {
    topBar("WIFI SNIFFER");
    spr.setTextColor(C_WARN, C_BG); spr.drawString("Voltage too low", 8, 30, 2);
    spr.setTextColor(C_DIM, C_BG);
    char b[32]; snprintf(b, sizeof(b), "%d mV (need %d)", power_guard_last_mv(), POWER_GUARD_MIN_MV);
    spr.drawString(b, 8, 52, 1);
    spr.setTextColor(C_ACCENT, C_BG); spr.drawString("S2/S5 = exit", 8, SCR_H - 14, 2);
    return;
  }
  switch (page_) {
    case APS:       drawAps();     break;
    case CLIENTS:   drawClients(); break;
    case PAGE_EXIT: drawExit();    break;
    default:        drawSummary(); break;
  }
}

void WifiSnifferApp::button(ButtonId id) {
  if (phase_ == BT_BLOCKED || phase_ == LOW_POWER) {
    if (id == BTN_S2 || id == BTN_S5) wants_exit_ = true;
    return;
  }
  if (id == BTN_S2) { page_ = (Page)((page_ + 1) % PAGE_COUNT); return; }
  if (id == BTN_S5) {
    if (page_ == PAGE_EXIT) wants_exit_ = true;
    else clear_tables();
  }
}

std::string WifiSnifferApp::remote_state() {
  char lines[6][40];
  const char* items[6];
  int n = 0;
  if (page_ == APS) {
    int order[6]; int m = top_aps(order, 5);
    for (int i = 0; i < m && n < 6; i++) {
      ApRec& a = s_aps[order[i]];
      snprintf(lines[n], sizeof(lines[n]), "%-.12s c%d %s %ddBm",
               a.ssid[0] ? a.ssid : "<hidden>", a.channel, ENC_LBL[a.enc], a.rssi);
      items[n] = lines[n]; n++;
    }
    if (m == 0) { snprintf(lines[0], sizeof(lines[0]), "scanning ch%d...", cur_channel_); items[0] = lines[0]; n = 1; }
  } else if (page_ == CLIENTS) {
    int order[6]; int m = top_clis(order, 5);
    for (int i = 0; i < m && n < 6; i++) {
      CliRec& c = s_clis[order[i]];
      if (c.assoc) snprintf(lines[n], sizeof(lines[n]), "%02X:%02X:%02X ->AP %ddBm", c.mac[3], c.mac[4], c.mac[5], c.rssi);
      else snprintf(lines[n], sizeof(lines[n]), "%02X:%02X:%02X ~%-.10s", c.mac[3], c.mac[4], c.mac[5], c.probe[0] ? c.probe : "bcast");
      items[n] = lines[n]; n++;
    }
    if (m == 0) { snprintf(lines[0], sizeof(lines[0]), "no clients yet"); items[0] = lines[0]; n = 1; }
  } else {
    snprintf(lines[0], sizeof(lines[0]), "APs: %d", s_ap_n);
    snprintf(lines[1], sizeof(lines[1]), "Clients: %d", s_cli_n);
    snprintf(lines[2], sizeof(lines[2]), "Frames: %lu", (unsigned long)s_frames);
    snprintf(lines[3], sizeof(lines[3]), "Deauth: %lu", (unsigned long)s_deauth);
    snprintf(lines[4], sizeof(lines[4]), "ch%d  (passive)", cur_channel_);
    for (int i = 0; i < 5; i++) items[i] = lines[i];
    n = 5;
  }
  return protocol_build_menu("wifi_sniffer", items, n, -1);
}
