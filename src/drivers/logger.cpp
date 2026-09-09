#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>
#include <time.h>
#include "logger.h"
#include "../kernel/log_ring.h"

void os_log(const char* fmt, ...) {
  char msg[LogRing::LINE];
  va_list a;
  va_start(a, fmt);
  vsnprintf(msg, sizeof(msg), fmt, a);
  va_end(a);

  // Префікс часу (для консолі логів): реальний HH:MM:SS, якщо NTP синхронізовано,
  // інакше uptime у форматі +M:SS (щоб теж було видно послідовність подій).
  char line[LogRing::LINE];
  time_t t = time(nullptr);
  struct tm tmv;
  if (t > 1700000000 && localtime_r(&t, &tmv)) {   // ~2023+ -> час реальний
    char ts[10]; strftime(ts, sizeof(ts), "%H:%M:%S", &tmv);
    snprintf(line, sizeof(line), "%s %s", ts, msg);
  } else {
    uint32_t s = millis() / 1000;
    snprintf(line, sizeof(line), "+%lu:%02lu %s", (unsigned long)(s / 60), (unsigned long)(s % 60), msg);
  }
  Serial.println(line);
  log_ring().push(line);
}
