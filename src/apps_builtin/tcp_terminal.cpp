#include <Arduino.h>
#include <string.h>
#include <WiFi.h>
#include "tcp_terminal.h"
#include "../drivers/display.h"
#include "../kernel/net_util.h"
#include "../remote/protocol.h"

// Один TCP-клієнт (єдина інстанція застосунку) — тримаємо у .cpp, щоб заголовок
// лишався без залежності від WiFi.
static WiFiClient s_client;

void TcpTerminalApp::init() {
  wants_exit_ = false;
  rx_len_ = 0; rx_[0] = '\0';
}

bool TcpTerminalApp::connected() const { return WiFi.status() == WL_CONNECTED; }

void TcpTerminalApp::rx_append(const char* data, int len) {
  for (int i = 0; i < len; i++) {
    if (rx_len_ >= RX_MAX) {                 // переповнення — відкинути першу половину
      int keep = RX_MAX / 2;
      memmove(rx_, rx_ + (RX_MAX - keep), keep);
      rx_len_ = keep;
    }
    char c = data[i];
    rx_[rx_len_++] = (c == '\n' || c == '\r' || (c >= 32 && c < 127)) ? c : '.';
  }
  rx_[rx_len_] = '\0';
}

void TcpTerminalApp::connect_target() {
  disconnect();
  if (host_[0] == '\0') return;
  rx_len_ = 0; rx_[0] = '\0';
  IPAddress ip;
  bool ok;
  // host_ може бути IP або імʼям — WiFiClient.connect приймає обидва
  ok = s_client.connect(host_, port_, 5000);
  Serial.printf("[TCP] connect %s:%u -> %d\n", host_, port_, ok);
  if (ok) { const char* m = "[connected]\n"; rx_append(m, strlen(m)); }
  else    { const char* m = "[connect failed]\n"; rx_append(m, strlen(m)); }
}

void TcpTerminalApp::disconnect() {
  if (s_client.connected()) s_client.stop();
}

void TcpTerminalApp::loop() {
  if (!s_client.connected()) return;
  char buf[64];
  int n = 0;
  while (s_client.available() && n < (int)sizeof(buf)) buf[n++] = (char)s_client.read();
  if (n > 0) rx_append(buf, n);
}

void TcpTerminalApp::text(const char* field, const char* value) {
  if (!field || !value) return;
  if (strcmp(field, "target") == 0) {
    if (net_parse_hostport(value, host_, sizeof(host_), &port_, 23))
      connect_target();
  } else if (strcmp(field, "line") == 0) {
    if (s_client.connected()) {
      s_client.print(value);
      s_client.print("\n");
      // локальне відлуння надісланого
      rx_append("> ", 2); rx_append(value, strlen(value)); rx_append("\n", 1);
    }
  }
}

void TcpTerminalApp::draw() {
  TFT_eSprite& spr = display_sprite();
  spr.fillSprite(C_BG);
  spr.fillRect(0, 0, SCR_W, 16, C_PANEL);
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(C_ACCENT, C_PANEL);
  spr.drawString("TCP TERM", 6, 3, 2);
  bool tcp_up = s_client.connected();
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(tcp_up ? C_GOOD : C_DIM, C_PANEL);
  spr.drawString(tcp_up ? "conn" : "idle", SCR_W - 6, 3, 2);
  spr.setTextDatum(TL_DATUM);

  if (!connected()) {
    spr.setTextColor(C_WARN, C_BG); spr.drawString("Not connected (STA)", 8, 30, 2);
    spr.setTextColor(C_DIM, C_BG);  spr.drawString("Zapusty 'WiFi Setup'", 8, 52, 1);
    spr.drawString("S2=exit", 8, SCR_H - 12, 1);
    return;
  }

  // рядок цілі
  spr.setTextColor(C_DIM, C_BG);
  char t[44];
  if (host_[0]) snprintf(t, sizeof(t), "%.30s:%u", host_, port_);
  else          snprintf(t, sizeof(t), "target: z telefona");
  spr.drawString(t, 8, 18, 1);

  // вікно отриманого тексту: wrap ~44 симв, показуємо хвіст (останні рядки)
  const int WRAP = 44, ROWS = 6, ROW_Y0 = 30, ROW_H = 12;
  // порахувати рядки з rx_ (враховуючи '\n')
  // збираємо усі рядки, показуємо останні ROWS
  static char linebuf[16][WRAP + 1];  // кільце рядків
  int lc = 0;
  int col = 0;
  linebuf[0][0] = '\0';
  for (int i = 0; i < rx_len_; i++) {
    char c = rx_[i];
    if (c == '\n' || col >= WRAP) {
      linebuf[lc % 16][col] = '\0';
      lc++; col = 0;
      linebuf[lc % 16][0] = '\0';
      if (c == '\n') continue;
    }
    linebuf[lc % 16][col++] = c;
  }
  linebuf[lc % 16][col] = '\0';
  int total_lines = lc + 1;
  int start = total_lines > ROWS ? total_lines - ROWS : 0;
  spr.setTextColor(C_TEXT, C_BG);
  for (int r = 0; r < ROWS && start + r < total_lines; r++)
    spr.drawString(linebuf[(start + r) % 16], 8, ROW_Y0 + r * ROW_H, 1);

  spr.setTextColor(C_DIM, C_BG);
  spr.drawString(tcp_up ? "S5=disconnect S2=exit" : "S5=reconnect S2=exit", 8, SCR_H - 11, 1);
}

void TcpTerminalApp::button(ButtonId id) {
  if (id == BTN_S2) { disconnect(); wants_exit_ = true; return; }
  if (id == BTN_S5) {
    if (s_client.connected()) disconnect();
    else if (host_[0]) connect_target();
  }
}

std::string TcpTerminalApp::remote_state() {
  char l0[48], l1[80];
  snprintf(l0, sizeof(l0), "%s %.24s:%u", s_client.connected() ? "[conn]" : "[idle]",
           host_[0] ? host_ : "(no target)", port_);
  // останні ~70 символів rx як другий рядок
  int off = rx_len_ > 70 ? rx_len_ - 70 : 0;
  snprintf(l1, sizeof(l1), "%.72s", rx_ + off);
  const char* items[2] = { l0, l1 };
  return protocol_build_menu("tcp_term", items, 2, -1);
}
