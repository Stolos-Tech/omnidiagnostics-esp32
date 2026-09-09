// Крос-модульне сховище проаналізованих даних (RAM). Модуль-ВИРОБНИК (Net Scan,
// ARP, WiFi Analyzer...) пушить сюди результати; модуль-СПОЖИВАЧ (HTTP GET, DNS,
// TCP, WoL, WiFi Setup) читає їх БЕЗ повторного скану. Це єдиний шар даних, який
// SD-логер згодом лише персистить (коли фізично буде SD-модуль) — cross-module
// sharing працює вже зараз із RAM. Чиста логіка (без Arduino) -> native-тести.
#pragma once
#include <stdint.h>
#include <stddef.h>

#define SHARED_MAX_HOSTS 24
#define SHARED_MAX_NETS  20

struct SharedHost { char ip[16]; char note[24]; };          // note: vendor/ports/label (опційно)
struct SharedNet  { char ssid[24]; int ch; int rssi; char enc[10]; };

void shared_store_reset();

// Виробники. host дедупиться за ip; net — за ssid (лишає найсильніший rssi).
// note/enc можуть бути "" — тоді просто не оновлюються.
void shared_add_host(const char* ip, const char* note);
void shared_add_net(const char* ssid, int ch, int rssi, const char* enc);

// Споживачі. Індекс поза межами -> nullptr / 0.
int  shared_host_count();
const SharedHost* shared_host(int i);
int  shared_net_count();
const SharedNet*  shared_net(int i);
