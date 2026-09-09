#include "shared_store.h"
#include <string.h>
#include <stdio.h>

static SharedHost s_hosts[SHARED_MAX_HOSTS];
static int        s_hostN = 0;
static SharedNet  s_nets[SHARED_MAX_NETS];
static int        s_netN = 0;

static void cpy(char* d, size_t n, const char* s) { if (!s) s = ""; snprintf(d, n, "%s", s); }

void shared_store_reset() { s_hostN = 0; s_netN = 0; }

void shared_add_host(const char* ip, const char* note) {
  if (!ip || !ip[0]) return;
  for (int i = 0; i < s_hostN; i++)
    if (strcmp(s_hosts[i].ip, ip) == 0) {                 // дедуп: оновити note, якщо новий непорожній
      if (note && note[0]) cpy(s_hosts[i].note, sizeof(s_hosts[i].note), note);
      return;
    }
  if (s_hostN >= SHARED_MAX_HOSTS) return;
  cpy(s_hosts[s_hostN].ip,   sizeof(s_hosts[s_hostN].ip),   ip);
  cpy(s_hosts[s_hostN].note, sizeof(s_hosts[s_hostN].note), note);
  s_hostN++;
}

void shared_add_net(const char* ssid, int ch, int rssi, const char* enc) {
  if (!ssid || !ssid[0]) return;
  for (int i = 0; i < s_netN; i++)
    if (strcmp(s_nets[i].ssid, ssid) == 0) {              // дедуп за ssid: лишаємо найсильніший
      if (rssi > s_nets[i].rssi) { s_nets[i].rssi = rssi; s_nets[i].ch = ch; }
      return;
    }
  if (s_netN >= SHARED_MAX_NETS) return;
  cpy(s_nets[s_netN].ssid, sizeof(s_nets[s_netN].ssid), ssid);
  s_nets[s_netN].ch = ch; s_nets[s_netN].rssi = rssi;
  cpy(s_nets[s_netN].enc, sizeof(s_nets[s_netN].enc), enc);
  s_netN++;
}

int  shared_host_count() { return s_hostN; }
const SharedHost* shared_host(int i) { return (i >= 0 && i < s_hostN) ? &s_hosts[i] : nullptr; }
int  shared_net_count()  { return s_netN; }
const SharedNet*  shared_net(int i)  { return (i >= 0 && i < s_netN) ? &s_nets[i] : nullptr; }
