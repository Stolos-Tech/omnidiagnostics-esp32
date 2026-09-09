#include "net_util.h"
#include <stdio.h>
#include <string.h>

int net_ip_to_str(uint32_t ip, char* out, size_t out_size) {
  if (!out || out_size == 0) return 0;
  return snprintf(out, out_size, "%u.%u.%u.%u",
                  (unsigned)((ip >> 24) & 0xFF), (unsigned)((ip >> 16) & 0xFF),
                  (unsigned)((ip >> 8) & 0xFF),  (unsigned)(ip & 0xFF));
}

bool net_str_to_ip(const char* s, uint32_t* out) {
  if (!s) return false;
  unsigned a, b, c, d;
  int consumed = 0;
  if (sscanf(s, "%u.%u.%u.%u%n", &a, &b, &c, &d, &consumed) != 4) return false;
  if (s[consumed] != '\0') return false;             // зайві символи після IP
  if (a > 255 || b > 255 || c > 255 || d > 255) return false;
  if (out) *out = net_make_ip(a, b, c, d);
  return true;
}

bool net_subnet_range(uint32_t ip, uint32_t mask,
                      uint32_t* first_host, uint32_t* last_host, uint32_t* count) {
  uint32_t base = ip & mask;
  uint32_t bcast = base | (~mask);
  if (bcast <= base + 1) {                            // /31, /32 — хостів нема
    if (count) *count = 0;
    return false;
  }
  uint32_t fh = base + 1;
  uint32_t lh = bcast - 1;
  if (first_host) *first_host = fh;
  if (last_host)  *last_host = lh;
  if (count)      *count = lh - fh + 1;
  return true;
}

uint32_t net_prefix_to_mask(int prefix) {
  if (prefix <= 0) return 0;
  if (prefix >= 32) return 0xFFFFFFFFu;
  return 0xFFFFFFFFu << (32 - prefix);
}

bool net_parse_cidr(const char* s, uint32_t* ip, uint32_t* mask) {
  if (!s) return false;
  // розділити на адресну частину і префікс
  char addr[40];
  int prefix = -1;
  const char* slash = nullptr;
  for (const char* p = s; *p; p++) if (*p == '/') { slash = p; break; }
  if (slash) {
    size_t len = (size_t)(slash - s);
    if (len >= sizeof(addr)) return false;
    for (size_t i = 0; i < len; i++) addr[i] = s[i];
    addr[len] = '\0';
    // префікс
    const char* pp = slash + 1;
    if (*pp == '\0') return false;
    int v = 0;
    for (; *pp; pp++) { if (*pp < '0' || *pp > '9') return false; v = v * 10 + (*pp - '0'); if (v > 32) return false; }
    prefix = v;
  } else {
    snprintf(addr, sizeof(addr), "%s", s);
  }

  // адреса: повний "a.b.c.d" або скорочений "a.b.c" (=> доповнити .0, /24)
  unsigned a, b, c, d;
  int consumed = 0;
  if (sscanf(addr, "%u.%u.%u.%u%n", &a, &b, &c, &d, &consumed) == 4 && addr[consumed] == '\0') {
    // повний
  } else if (sscanf(addr, "%u.%u.%u%n", &a, &b, &c, &consumed) == 3 && addr[consumed] == '\0') {
    d = 0;
    if (prefix < 0) prefix = 24;   // три октети => /24
  } else {
    return false;
  }
  if (a > 255 || b > 255 || c > 255 || d > 255) return false;
  if (prefix < 0) prefix = 24;     // без префікса => /24

  if (ip)   *ip = net_make_ip(a, b, c, d);
  if (mask) *mask = net_prefix_to_mask(prefix);
  return true;
}

bool net_parse_hostport(const char* s, char* host_out, size_t host_size,
                        uint16_t* port_out, uint16_t default_port) {
  if (!s || !host_out || host_size == 0) return false;
  while (*s == ' ' || *s == '\t') s++;              // пропустити пробіли
  // знайти останню ':' (щоб не плутати з IPv6 — але тут прості host:port)
  const char* colon = nullptr;
  for (const char* p = s; *p; p++) if (*p == ':') colon = p;

  size_t host_len = colon ? (size_t)(colon - s) : strlen(s);
  // прибрати хвостові пробіли хоста
  while (host_len > 0 && (s[host_len-1] == ' ' || s[host_len-1] == '\t')) host_len--;
  if (host_len == 0 || host_len >= host_size) return false;

  uint16_t port = default_port;
  if (colon) {
    const char* pp = colon + 1;
    while (*pp == ' ') pp++;
    if (*pp == '\0') return false;
    long v = 0;
    for (; *pp; pp++) {
      if (*pp == ' ') break;
      if (*pp < '0' || *pp > '9') return false;
      v = v * 10 + (*pp - '0');
      if (v > 65535) return false;
    }
    if (v < 1) return false;
    port = (uint16_t)v;
  }

  for (size_t i = 0; i < host_len; i++) host_out[i] = s[i];
  host_out[host_len] = '\0';
  if (port_out) *port_out = port;
  return true;
}

static int hex_val(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool net_parse_mac(const char* s, uint8_t out[6]) {
  if (!s) return false;
  uint8_t tmp[6];
  int byte_i = 0;
  int hi = -1;
  for (const char* p = s; *p; p++) {
    if (*p == ':' || *p == '-') { if (hi >= 0) return false; continue; }  // роздільник між парами
    int v = hex_val(*p);
    if (v < 0) return false;
    if (hi < 0) { hi = v; }
    else { if (byte_i >= 6) return false; tmp[byte_i++] = (uint8_t)((hi << 4) | v); hi = -1; }
  }
  if (hi >= 0 || byte_i != 6) return false;    // непарна кількість шіснадцяткових цифр або не 6 байт
  if (out) memcpy(out, tmp, 6);
  return true;
}

int wol_build_packet(const uint8_t mac[6], uint8_t* out, size_t out_size) {
  if (!mac || !out || out_size < 102) return 0;
  memset(out, 0xFF, 6);
  for (int i = 0; i < 16; i++) memcpy(out + 6 + i * 6, mac, 6);
  return 102;
}

int net_parse_ports(const char* s, uint16_t* out, int max) {
  if (!s || !out || max <= 0) return 0;
  int n = 0;
  const char* p = s;
  while (*p && n < max) {
    while (*p == ' ' || *p == ',' || *p == '\t') p++;   // пропустити роздільники
    if (*p == '\0') break;
    if (*p < '0' || *p > '9') { while (*p && *p != ',') p++; continue; }  // сміття -> до коми
    long v = 0;
    while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; if (v > 65535) break; }
    if (v >= 1 && v <= 65535) out[n++] = (uint16_t)v;
    while (*p && *p != ',') p++;   // хвіст до наступної коми
  }
  return n;
}
