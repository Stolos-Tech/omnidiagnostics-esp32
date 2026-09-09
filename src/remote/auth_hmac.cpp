#include "auth_hmac.h"
#include <Arduino.h>
#include <Preferences.h>
#include <esp_random.h>
#include <mbedtls/md.h>
#include <string.h>
#include <stdio.h>

static uint8_t s_seed[32];
static bool    s_ready = false;
static char    s_nonce[33] = "";     // 16 байт -> 32 hex + '\0'
static uint32_t s_nonce_ts = 0;

static void bytes_to_hex(const uint8_t* b, int n, char* out) {
  static const char* H = "0123456789abcdef";
  for (int i = 0; i < n; i++) { out[i * 2] = H[b[i] >> 4]; out[i * 2 + 1] = H[b[i] & 0xF]; }
  out[n * 2] = '\0';
}
static int hex_val(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}
static int hex_to_bytes(const char* h, uint8_t* out, int maxn) {
  int len = strlen(h); if (len % 2 || len / 2 > maxn) return -1;
  for (int i = 0; i < len / 2; i++) {
    int hi = hex_val(h[i * 2]), lo = hex_val(h[i * 2 + 1]);
    if (hi < 0 || lo < 0) return -1;
    out[i] = (uint8_t)((hi << 4) | lo);
  }
  return len / 2;
}

void auth_hmac_begin() {
  Preferences p; p.begin("authseed", true);
  size_t n = p.getBytes("seed", s_seed, sizeof(s_seed));
  p.end();
  if (n != sizeof(s_seed)) {                    // немає -> сильний 256-bit seed із апаратного RNG
    for (int i = 0; i < 8; i++) { uint32_t r = esp_random(); memcpy(s_seed + i * 4, &r, 4); }
    Preferences w; w.begin("authseed", false); w.putBytes("seed", s_seed, sizeof(s_seed)); w.end();
  }
  s_ready = true;
}

bool auth_hmac_ready() { return s_ready; }

bool auth_hmac_seed_hex(char* out, size_t cap) {
  if (!s_ready || cap < 65) return false;
  bytes_to_hex(s_seed, 32, out);
  return true;
}

void auth_hmac_challenge(char* out, size_t cap) {
  uint8_t n[16];
  for (int i = 0; i < 4; i++) { uint32_t r = esp_random(); memcpy(n + i * 4, &r, 4); }
  bytes_to_hex(n, 16, s_nonce);
  s_nonce_ts = millis();
  snprintf(out, cap, "%s", s_nonce);
}

static bool hmac_sha256(const uint8_t* key, int klen, const uint8_t* msg, int mlen, uint8_t* out) {
  const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  return info && mbedtls_md_hmac(info, key, klen, msg, mlen, out) == 0;
}

bool auth_hmac_verify(const char* nonce_hex, const char* resp_hex) {
  if (!s_ready || !s_nonce[0]) return false;
  if (strcmp(nonce_hex, s_nonce) != 0) return false;          // лише щойно виданий nonce
  if (millis() - s_nonce_ts > 60000) { s_nonce[0] = '\0'; return false; }  // TTL 60с
  uint8_t nb[16];
  if (hex_to_bytes(nonce_hex, nb, 16) != 16) return false;
  uint8_t mac[32];
  if (!hmac_sha256(s_seed, 32, nb, 16, mac)) return false;
  char machex[65]; bytes_to_hex(mac, 32, machex);
  s_nonce[0] = '\0';                                          // one-time nonce (навіть при невдачі)
  if (strlen(resp_hex) != 64) return false;
  int diff = 0;                                               // константо-часове порівняння
  for (int i = 0; i < 64; i++) diff |= (machex[i] ^ resp_hex[i]);
  return diff == 0;
}
