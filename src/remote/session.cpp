#include "session.h"
#include <stdio.h>
#include <string.h>

static RemoteMode s_mode = MODE_OFF;
static char s_pin[8] = "";
static bool s_authed = false;
static int s_attempts = 0;
static bool s_locked = false;

// Fallback-RNG на випадок, якщо хук не встановлено (детермінований, НЕ для продакшену
// — на пристрої обов'язково session_set_rng(esp_random)).
static uint32_t s_seed = 0x12345678u;
static uint32_t default_rng() {
  // xorshift32 — аби не повертати константу; реальну випадковість дає esp_random
  s_seed ^= s_seed << 13;
  s_seed ^= s_seed >> 17;
  s_seed ^= s_seed << 5;
  return s_seed;
}
static uint32_t (*s_rng)() = default_rng;

void session_set_rng(uint32_t (*rng)()) {
  if (rng) s_rng = rng;
}

void session_init() {
  s_mode = MODE_OFF;
  s_pin[0] = '\0';
  s_authed = false;
  s_attempts = 0;
  s_locked = false;
}

static void generate_pin() {
  uint32_t code = s_rng() % 1000000u;          // 0..999999
  snprintf(s_pin, sizeof(s_pin), "%06u", (unsigned)code);  // провідні нулі
}

void session_set_mode(RemoteMode mode) {
  s_mode = mode;
  session_reset_auth();
  if (mode == MODE_OFF) {
    s_pin[0] = '\0';
  } else {
    generate_pin();  // новий режим -> новий PIN
  }
}

RemoteMode session_active_mode() { return s_mode; }

const char* session_pin() { return s_pin; }

static bool is_six_digits(const char* pin) {
  if (!pin) return false;
  for (int i = 0; i < 6; i++) if (pin[i] < '0' || pin[i] > '9') return false;
  return pin[6] == '\0';
}

void session_set_pin(const char* pin) {
  if (s_mode == MODE_OFF) return;       // нема активної сесії — нічого фіксувати
  if (!is_six_digits(pin)) return;      // невалідний формат — ігноруємо, лишаємо згенерований
  snprintf(s_pin, sizeof(s_pin), "%s", pin);
}

bool session_is_authenticated() { return s_authed; }

AuthResult session_authenticate(const char* pin) {
  if (s_mode == MODE_OFF) return AUTH_WRONG;   // немає активної сесії — нічого приймати
  if (s_locked) return AUTH_LOCKED;
  if (s_authed) return AUTH_OK;                // вже автентифіковані

  if (pin && strcmp(pin, s_pin) == 0) {
    s_authed = true;
    s_attempts = 0;
    return AUTH_OK;
  }

  // невдала спроба
  s_attempts++;
  if (s_attempts >= SESSION_MAX_ATTEMPTS) {
    s_locked = true;
    return AUTH_LOCKED;
  }
  return AUTH_WRONG;
}

void session_reset_auth() {
  s_authed = false;
  s_attempts = 0;
  s_locked = false;
}

void session_force_auth() {
  if (s_mode == MODE_OFF) { s_mode = MODE_WIFI; generate_pin(); }
  s_authed = true;
  s_attempts = 0;
  s_locked = false;
}
