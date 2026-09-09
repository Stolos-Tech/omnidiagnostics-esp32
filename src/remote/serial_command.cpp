#include "serial_command.h"
#include <Arduino.h>
#include <string.h>
#include <stdlib.h>

static SerialDispatchFn s_dispatch = nullptr;
static char   s_line[512];
static size_t s_len = 0;
// Відповідь тримаємо на КУПІ (не в статичному DRAM — його впритул): телеметрія
// (netscan/airscan/spectrum) не влазить у 512Б. 6КБ купи вистачає на всі відповіді.
static char*  s_out = nullptr;
static const size_t S_OUT_CAP = 6144;

void serial_command_begin(SerialDispatchFn dispatch) {
  s_dispatch = dispatch;
  s_len = 0;
  if (!s_out) s_out = (char*)malloc(S_OUT_CAP);
}

static void handle_line(char* line) {
  // службові
  if (strcmp(line, "PING") == 0) { Serial.println("PONG"); return; }
  if (strcmp(line, "REBOOT") == 0) { Serial.println("RES 0 200 {\"reboot\":true}"); delay(80); ESP.restart(); return; }
  if (strncmp(line, "REQ ", 4) != 0) return;    // не наш кадр -> ігнор (напр. лог)

  // REQ <id> <METHOD> <path> [body...]
  char* p = line + 4;
  char* id = strsep(&p, " ");
  char* method = p ? strsep(&p, " ") : nullptr;
  char* path = p ? strsep(&p, " ") : nullptr;
  char* body = p ? p : (char*)"";
  if (!id || !method || !path) { Serial.println("RES 0 400 {\"error\":\"bad-req\"}"); return; }

  if (!s_out) { Serial.printf("RES %s 500 {\"error\":\"no-buf\"}\n", id); return; }
  int status = 501;
  s_out[0] = '\0';
  if (s_dispatch) status = s_dispatch(method, path, body, s_out, S_OUT_CAP);
  if (!s_out[0]) snprintf(s_out, S_OUT_CAP, "{}");

  Serial.printf("RES %s %d %s\n", id, status, s_out);
}

void serial_command_poll() {
  while (Serial.available()) {
    int ch = Serial.read();
    if (ch == '\n' || ch == '\r') {
      if (s_len > 0) { s_line[s_len] = '\0'; handle_line(s_line); s_len = 0; }
    } else if (s_len < sizeof(s_line) - 1) {
      s_line[s_len++] = (char)ch;
    } else {
      s_len = 0;   // overflow -> drop
    }
  }
}
