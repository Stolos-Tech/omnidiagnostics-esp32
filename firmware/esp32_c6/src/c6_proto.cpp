#include "c6_proto.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

C6Cmd c6_parse_cmd(const char* line, long* out_arg, int* out_hop) {
  if (out_arg) *out_arg = 0;
  if (out_hop) *out_hop = 0;
  if (!line) return C6_NONE;
  if (!strncmp(line, "SCAN WIFI", 9)) return C6_SCAN_WIFI;
  if (!strncmp(line, "MON", 3)) {
    const char* a = line + 3; while (*a == ' ') a++;
    if (!strncmp(a, "HOP", 3)) { if (out_hop) *out_hop = 1; }
    else if (out_arg) *out_arg = atol(a);
    return C6_MON;
  }
  if (!strncmp(line, "SNIFF", 5)) { if (out_arg) *out_arg = atol(line + 5); return C6_SNIFF; }
  if (!strncmp(line, "DEAUTH", 6)) return C6_DEAUTH;
  if (!strncmp(line, "RF24", 4)) return C6_RF24;
  if (!strncmp(line, "SUBGHZ", 6)) { if (out_arg) *out_arg = atol(line + 6); return C6_SUBGHZ; }
  if (!strncmp(line, "BLE", 3)) return C6_BLE;
  if (!strncmp(line, "Z15", 3)) return C6_Z15;
  if (!strncmp(line, "STOP", 4)) return C6_STOP;
  if (!strncmp(line, "STAT", 4)) return C6_STAT;
  return C6_NONE;
}

int c6_build_wifi(const char* ssid, const char* bssid, int rssi, int ch, const char* enc, char* o, size_t c) {
  return snprintf(o, c, "WIFI %s %s %d %d %s\n", ssid && ssid[0] ? ssid : "(hidden)", bssid, rssi, ch, enc);
}
int c6_build_monch(int ch, int count, char* o, size_t c) { return snprintf(o, c, "MONCH %d %d\n", ch, count); }
int c6_build_sniff(const char* mac, int rssi, int count, char* o, size_t c) { return snprintf(o, c, "SNIF %s %d %d\n", mac, rssi, count); }
int c6_build_deauth(int count, const char* src, char* o, size_t c) { return snprintf(o, c, "DEA %d %s\n", count, src ? src : "?"); }
int c6_build_rf24(int ch, int count, char* o, size_t c) { return snprintf(o, c, "R24 %d %d\n", ch, count); }
int c6_build_subghz(long f, int dbm, char* o, size_t c) { return snprintf(o, c, "SGZ %ld %d\n", f, dbm); }
int c6_build_ble(const char* addr, int rssi, const char* name, char* o, size_t c) { return snprintf(o, c, "BLE %s %d %s\n", addr, rssi, name && name[0] ? name : "-"); }
int c6_build_z15(int ch, int energy, const char* pan, char* o, size_t c) { return snprintf(o, c, "Z15 %d %d %s\n", ch, energy, pan ? pan : "0"); }
int c6_build_stat(int heap, int t10, unsigned long up, char* o, size_t c) { return snprintf(o, c, "STAT %d %d %lu\n", heap, t10, up); }
int c6_build_evt(const char* type, const char* detail, char* o, size_t c) { return snprintf(o, c, "EVT %s %s\n", type, detail ? detail : ""); }
