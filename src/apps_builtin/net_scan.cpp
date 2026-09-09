#include <Arduino.h>
#include <string.h>
#include <WiFi.h>
#include <ESP32Ping.h>
#include <lwip/sockets.h>
#include <lwip/etharp.h>
#include <lwip/netif.h>
#include <fcntl.h>
#include <errno.h>
#include "net_scan.h"
#include "../kernel/shared_store.h"
#include "../drivers/sd_store.h"
#include "../drivers/display.h"
#include "../kernel/net_util.h"
#include "../kernel/format_util.h"
#include "../kernel/intents.h"
#include "../kernel/device_id.h"
#include "../remote/protocol.h"
#include "../drivers/logger.h"

#define PROBE_MS       300   // таймаут TCP-проби (мс)
#define PORT_MS        300   // таймаут скану порту (для банера — лише вже відкриті)
#define PARALLEL_MS    500   // таймаут ПАРАЛЕЛЬНОЇ фази виявлення відкритих портів

// Типові TCP-порти для деталей хоста (розширено — до MAX_PORTS=16).
// Розширений набір поширених портів за замовчуванням (~32) — покриває майже всі
// реальні сервіси в домашній/офісній мережі. Повний скан 1-65535 — окрема опція
// на обраний хост (S5 у DETAIL, з живими логами прогресу).
static const uint16_t PORTS[] = {
  21, 22, 23, 25, 53, 80, 110, 111, 135, 139, 143, 161, 443, 445, 515, 554,
  631, 993, 995, 1723, 1883, 1900, 3306, 3389, 5000, 5353, 5900, 6379, 8080, 8443, 8266, 9100 };
static const int PORT_COUNT = sizeof(PORTS) / sizeof(PORTS[0]);

// Паралельний скан портів: усі candidate-порти пробуються ОДНОЧАСНО через
// неблокуючі сокети + select(), замість послідовного TCP-connect (16 портів
// послідовно по 300мс = до 4.8с; паралельно — вкладаємось у ~PARALLEL_MS).
// any_response (опційний): true, якщо ХОЧ ОДИН сокет отримав визначену відповідь —
// або з'єднався, або дістав RST (ECONNREFUSED). І те, й те доводить, що хост живий:
// саме RST відрізняє "порт закритий, але машина є" від "тиша/фаєрвол DROP".
static void scan_ports_parallel(uint32_t ip, const uint16_t* ports, int count,
                                 bool* open_mask, int timeout_ms,
                                 bool* any_response = nullptr) {
  if (any_response) *any_response = false;
  int fds[NetScanApp::MAX_PORTS];
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(ip);

  int active = 0;
  for (int i = 0; i < count; i++) {
    open_mask[i] = false;
    fds[i] = socket(AF_INET, SOCK_STREAM, 0);
    if (fds[i] < 0) continue;
    fcntl(fds[i], F_SETFL, fcntl(fds[i], F_GETFL, 0) | O_NONBLOCK);
    addr.sin_port = htons(ports[i]);
    connect(fds[i], (struct sockaddr*)&addr, sizeof(addr));  // очікувано -1/EINPROGRESS
    active++;
  }

  uint32_t t0 = millis();
  while (active > 0 && millis() - t0 < (uint32_t)timeout_ms) {
    fd_set wfds; FD_ZERO(&wfds);
    int maxfd = -1;
    for (int i = 0; i < count; i++) {
      if (fds[i] < 0) continue;
      FD_SET(fds[i], &wfds);
      if (fds[i] > maxfd) maxfd = fds[i];
    }
    struct timeval tv = { 0, 50000 };  // крок опитування 50мс
    if (select(maxfd + 1, nullptr, &wfds, nullptr, &tv) <= 0) continue;
    for (int i = 0; i < count; i++) {
      if (fds[i] < 0 || !FD_ISSET(fds[i], &wfds)) continue;
      int err = 0; socklen_t len = sizeof(err);
      getsockopt(fds[i], SOL_SOCKET, SO_ERROR, &err, &len);
      open_mask[i] = (err == 0);
      if (any_response && (err == 0 || err == ECONNREFUSED)) *any_response = true;
      close(fds[i]);
      fds[i] = -1;
      active--;
    }
  }
  for (int i = 0; i < count; i++) if (fds[i] >= 0) close(fds[i]);
}

static IPAddress to_addr(uint32_t ip) {
  return IPAddress((ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF);
}

// IPAddress -> uint32 у порядку a.b.c.d (a — старший байт), як вимагає net_util.
// ВАЖЛИВО: прямий каст (uint32_t)IPAddress дає ЗВОРОТНИЙ порядок (0x6532A8C0 для
// 192.168.50.101) — через це автовизначення підмережі раніше сканувало сміттєвий
// діапазон і не знаходило нікого, а перевірка "не пінгувати себе" не спрацьовувала.
static uint32_t ip_to_u32(const IPAddress& a) {
  return ((uint32_t)a[0] << 24) | ((uint32_t)a[1] << 16) |
         ((uint32_t)a[2] << 8)  |  (uint32_t)a[3];
}

// MAC хоста з ARP-кешу lwIP. Запис з'являється після TCP-connect/пінга до хоста
// (net_scan робить і те, і те). false, якщо в кеші нема (напр. host не відповів).
// ip — внутрішній a.b.c.d MSB; to_addr()->IPAddress->(uint32) дає lwip-порядок.
static bool arp_lookup(uint32_t ip, uint8_t mac_out[6]) {
  ip4_addr_t addr;
  addr.addr = (uint32_t)to_addr(ip);
  struct eth_addr* eth = nullptr;
  const ip4_addr_t* ipret = nullptr;
  struct netif* nif = netif_default;
  if (!nif) return false;
  if (etharp_find_addr(nif, &addr, &eth, &ipret) < 0 || !eth) return false;
  memcpy(mac_out, eth->addr, 6);
  return true;
}

// TCP-connect до порту + зчитування банера сервісу (перший рядок відповіді).
// true = порт відкритий. banner заповнюється (може бути порожнім).
static bool tcp_probe(uint32_t ip, uint16_t port, int timeout_ms, char* banner, int bn) {
  if (banner && bn > 0) banner[0] = '\0';
  WiFiClient c;
  if (!c.connect(to_addr(ip), port, timeout_ms)) { c.stop(); return false; }
  // веб-порти самі банер не шлють — провокуємо відповідь HEAD-запитом
  if (port == 80 || port == 8080 || port == 8266) c.print("HEAD / HTTP/1.0\r\n\r\n");
  uint32_t t0 = millis();
  while (c.available() == 0 && millis() - t0 < 400) delay(10);
  if (banner && bn > 0) {
    int j = 0;
    while (c.available() && j < bn - 1) {
      char ch = c.read();
      if (ch == '\r' || ch == '\n') { if (j > 0) break; else continue; }  // перший рядок
      banner[j++] = (ch >= 32 && ch < 127) ? ch : '.';
    }
    banner[j] = '\0';
  }
  c.stop();
  return true;
}

// ── Глибокий аналіз пристрою (remote-API) ─────────────────────────────────────
// Компактна таблиця мітка-сервісу для портів + категоризація за відкритими портами
// (порт devintel.classify із сервера на плату). Автономно, без сервера.
struct PortLabel { uint16_t port; const char* label; };
static const PortLabel PORT_LABELS[] = {
  {21,"ftp"},{22,"ssh"},{23,"telnet"},{25,"smtp"},{53,"dns"},{80,"http"},{110,"pop3"},
  {135,"msrpc"},{139,"netbios"},{143,"imap"},{161,"snmp"},{443,"https"},{445,"smb"},
  {515,"printer"},{554,"rtsp"},{631,"ipp"},{993,"imaps"},{995,"pop3s"},{1723,"pptp"},
  {1883,"mqtt"},{1900,"upnp"},{3306,"mysql"},{3389,"rdp"},{5000,"upnp"},{5353,"mdns"},
  {5900,"vnc"},{6379,"redis"},{8080,"http-alt"},{8443,"https-alt"},{8266,"esp"},{9100,"printer"},
};
static const char* port_label(uint16_t p) {
  for (auto& e : PORT_LABELS) if (e.port == p) return e.label;
  return "?";
}
static const char* categorize_ports(const uint16_t* open, int n, bool random_mac) {
  auto has = [&](uint16_t p) { for (int i = 0; i < n; i++) if (open[i] == p) return true; return false; };
  if (has(3389))                       return "Windows-ПК (RDP)";
  if (has(445) || has(139))            return "Windows-ПК або NAS (SMB)";
  if (has(9100) || has(515) || has(631)) return "принтер";
  if (has(554) || has(5900))           return "камера/відео";
  if (has(1883))                       return "IoT/MQTT";
  if (has(8266))                       return "ESP-пристрій";
  if (has(22))                         return "Linux/сервер (SSH)";
  if (has(80) || has(443) || has(8080) || has(8443)) return "пристрій з веб-інтерфейсом";
  if (n == 0 && random_mac)            return "смартфон/приватний (рандомний MAC)";
  if (n == 0)                          return "закритий/сплячий пристрій";
  return "невідомий пристрій";
}

bool net_scan_device_json(const char* ip_str, char* out, size_t cap) {
  if (!out || cap < 32) return false;
  if (WiFi.status() != WL_CONNECTED) { snprintf(out, cap, "{\"error\":\"offline\"}"); return false; }
  IPAddress a;
  if (!a.fromString(ip_str)) { snprintf(out, cap, "{\"error\":\"bad-ip\"}"); return false; }
  uint32_t ip = ip_to_u32(a);

  // ВАЖЛИВО: scan_ports_parallel має внутрішній fds[MAX_PORTS] — не можна передавати
  // більше за MAX_PORTS портів за раз (інакше переповнення стека -> креш). Скануємо чанками.
  bool open_mask[PORT_COUNT]; bool resp = false;
  for (int off = 0; off < PORT_COUNT; off += NetScanApp::MAX_PORTS) {
    int cnt = PORT_COUNT - off; if (cnt > NetScanApp::MAX_PORTS) cnt = NetScanApp::MAX_PORTS;
    bool chunk_resp = false;
    scan_ports_parallel(ip, PORTS + off, cnt, open_mask + off, PARALLEL_MS, &chunk_resp);
    if (chunk_resp) resp = true;
  }
  uint16_t open_ports[PORT_COUNT]; int n = 0;
  for (int i = 0; i < PORT_COUNT; i++) if (open_mask[i]) open_ports[n++] = PORTS[i];

  uint8_t mac[6]; bool have_mac = arp_lookup(ip, mac);
  bool random_mac = have_mac && (mac[0] & 0x02);

  // Збірка JSON вручну (малий буфер, w — курсор; кожен snprintf у cap-w).
  size_t w = 0;
  #define NS_APP(...) do { if (w < cap) { int _r = snprintf(out + w, cap - w, __VA_ARGS__); \
                          if (_r > 0) w += (size_t)_r; if (w >= cap) w = cap - 1; } } while (0)
  NS_APP("{\"ip\":\"%s\",", ip_str);
  if (have_mac) NS_APP("\"mac\":\"%02X:%02X:%02X:%02X:%02X:%02X\",",
                       mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  NS_APP("\"random_mac\":%s,\"alive\":%s,\"ports\":[",
         random_mac ? "true" : "false", (resp || n > 0) ? "true" : "false");
  for (int i = 0; i < n; i++) NS_APP("%s%u", i ? "," : "", open_ports[i]);
  NS_APP("],\"port_labels\":{");
  for (int i = 0; i < n; i++) NS_APP("%s\"%u\":\"%s\"", i ? "," : "", open_ports[i], port_label(open_ports[i]));
  NS_APP("},\"category\":\"%s\"}", categorize_ports(open_ports, n, random_mac));
  #undef NS_APP
  return true;
}

// Порти для виявлення живих хостів: беремо кілька типових ОДНОЧАСНО, бо хост із
// закритим 80 (але відкритим 443/22/8080) інакше лишався б непоміченим.
static const uint16_t LIVE_PORTS[] = { 80, 443, 22, 8080, 445 };
static const int LIVE_PORT_COUNT = sizeof(LIVE_PORTS) / sizeof(LIVE_PORTS[0]);

// Чи живий хост: один ПАРАЛЕЛЬНИЙ залп по кількох портах. Живий = будь-який
// відкрився АБО відповів RST. За часом це те саме, що стара одинична проба
// порту 80, але покриття значно ширше.
static bool host_up(uint32_t ip) {
  bool open_mask[LIVE_PORT_COUNT];
  bool responded = false;
  scan_ports_parallel(ip, LIVE_PORTS, LIVE_PORT_COUNT, open_mask, PROBE_MS, &responded);
  return responded;
}

bool NetScanApp::connected() const { return WiFi.status() == WL_CONNECTED; }

void NetScanApp::start_sweep() {
  host_count_ = 0;
  cursor_ = 0;
  sweep_i_ = 0;
  if (custom_range_) {                            // заданий користувачем діапазон
    first_host_ = custom_base_;
    sweep_total_ = custom_count_;
    phase_ = SWEEP;
    return;
  }
  uint32_t ip = ip_to_u32(WiFi.localIP());      // a.b.c.d, старший байт першим
  uint32_t mask = ip_to_u32(WiFi.subnetMask());
  uint32_t fh, lh, cnt;
  if (!net_subnet_range(ip, mask, &fh, &lh, &cnt)) {
    sweep_total_ = 0;
    phase_ = LIST;   // нема хостів для свіпу
    return;
  }
  first_host_ = fh;
  sweep_total_ = cnt > (uint32_t)MAX_SWEEP ? (uint32_t)MAX_SWEEP : cnt;
  phase_ = SWEEP;
}

void NetScanApp::init() {
  wants_exit_ = false;
  if (!connected()) { phase_ = NOT_CONN; return; }
  start_sweep();
}

void NetScanApp::loop() {
  if (phase_ == SWEEP) {
    if (sweep_i_ >= sweep_total_) {
      sd_persist_shared();   // instant-save: скан завершено -> сортований дамп на SD (no-op якщо не змонт.)
      phase_ = LIST; return;
    }
    uint32_t ip = first_host_ + sweep_i_;
    // не пінгуємо власну адресу
    if (ip != ip_to_u32(WiFi.localIP()) && host_up(ip) && host_count_ < MAX_HOSTS) {
      hosts_[host_count_++] = ip;
      char ips[20]; net_ip_to_str(ip, ips, sizeof(ips));
      shared_add_host(ips, "net_scan");        // крос-модульний store: інші модулі юзають без рескану
      os_log("[SCAN] host up: %s", ips);       // живий лог -> веб-консоль у реальному часі
    }
    sweep_i_++;
  }
}

void NetScanApp::run_portscan() {
  open_count_ = 0;
  const uint16_t* ports = custom_port_count_ > 0 ? custom_ports_ : PORTS;
  int pc = custom_port_count_ > 0 ? custom_port_count_ : PORT_COUNT;

  char dips[20]; net_ip_to_str(detail_ip_, dips, sizeof(dips));
  os_log("[SCAN] port scan %s (%d ports)...", dips, pc);

  // scan_ports_parallel має внутрішній fds[MAX_PORTS] — не можна передавати >MAX_PORTS
  // портів за раз. Скануємо ЧАНКАМИ по MAX_PORTS, щоб покрити ВСІ дефолтні порти (32),
  // а не мовчки перші 16 (раніше pc клемпився до MAX_PORTS і різав другу половину).
  // Банер знімаємо одразу для відкритих портів чанку; open_ports_/banner_ (розмір
  // MAX_PORTS) захищені лічильником open_count_ < MAX_PORTS.
  for (int off = 0; off < pc && open_count_ < MAX_PORTS; off += MAX_PORTS) {
    int cnt = pc - off; if (cnt > MAX_PORTS) cnt = MAX_PORTS;
    bool open_mask[MAX_PORTS];
    scan_ports_parallel(detail_ip_, ports + off, cnt, open_mask, PARALLEL_MS);
    for (int i = 0; i < cnt && open_count_ < MAX_PORTS; i++) {
      if (!open_mask[i]) continue;
      char b[48];
      tcp_probe(detail_ip_, ports[off + i], PORT_MS, b, sizeof(b));
      open_ports_[open_count_] = ports[off + i];
      snprintf(banner_[open_count_], sizeof(banner_[0]), "%s", b);
      open_count_++;
      os_log("[SCAN] %s:%u OPEN", dips, ports[off + i]);   // живий лог відкритих портів
    }
  }
  os_log("[SCAN] %s done: %d open", dips, open_count_);
  // RTT через ICMP (хост живий -> швидка відповідь). Пінг заодно свіжить ARP.
  detail_rtt_ = Ping.ping(to_addr(detail_ip_), 1) ? (int)Ping.averageTime() : -1;

  // Фінгерпринт: MAC з ARP-кешу (заповнений після TCP/пінга) -> вендор -> тип.
  detail_mac_[0] = detail_vendor_[0] = detail_type_[0] = '\0';
  uint8_t mac[6];
  bool have_mac = arp_lookup(detail_ip_, mac);
  char cat = DC_UNKNOWN;
  if (have_mac) {
    snprintf(detail_mac_, sizeof(detail_mac_), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    const char* v = net_oui_vendor(mac);
    snprintf(detail_vendor_, sizeof(detail_vendor_), "%s", v[0] ? v : "?");
    cat = net_oui_category(mac);
  } else {
    snprintf(detail_vendor_, sizeof(detail_vendor_), "?");
  }
  bool is_gw = (detail_ip_ == ip_to_u32(WiFi.gatewayIP()));
  bool mac_local = have_mac && net_mac_is_local(mac);
  snprintf(detail_type_, sizeof(detail_type_), "%s",
           net_device_type(is_gw, cat, mac_local, open_ports_, open_count_));
}

void NetScanApp::text(const char* field, const char* value) {
  if (!field || !value) return;
  if (strcmp(field, "range") == 0) {
    uint32_t ip, mask, fh, lh, cnt;
    if (net_parse_cidr(value, &ip, &mask) && net_subnet_range(ip, mask, &fh, &lh, &cnt)) {
      custom_base_ = fh;
      custom_count_ = cnt > (uint32_t)MAX_SWEEP ? (uint32_t)MAX_SWEEP : cnt;
      custom_range_ = true;
      if (connected()) start_sweep();   // одразу перезапустити свіп із новим діапазоном
    }
  } else if (strcmp(field, "ports") == 0) {
    custom_port_count_ = net_parse_ports(value, custom_ports_, MAX_PORTS);  // 0 => стандартні
  }
}

void NetScanApp::draw() {
  TFT_eSprite& spr = display_sprite();
  spr.fillSprite(C_BG);
  spr.fillRect(0, 0, SCR_W, 16, C_PANEL);
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(C_ACCENT, C_PANEL);
  spr.drawString("NET SCAN", 6, 3, 2);

  char b[48];

  if (phase_ == NOT_CONN) {
    spr.setTextColor(C_WARN, C_BG);
    spr.drawString("Not connected", 8, 30, 2);
    spr.setTextColor(C_DIM, C_BG);
    spr.drawString("Zapusty 'WiFi Setup'", 8, 52, 1);
    spr.drawString("S2=exit", 8, SCR_H - 12, 1);
    return;
  }

  if (phase_ == SWEEP) {
    spr.setTextDatum(TR_DATUM); spr.setTextColor(C_DIM, C_PANEL);
    snprintf(b, sizeof(b), "%u/%u", (unsigned)sweep_i_, (unsigned)sweep_total_);
    spr.drawString(b, SCR_W - 6, 3, 2);
    spr.setTextDatum(TL_DATUM);
    spr.setTextColor(C_TEXT, C_BG);
    spr.drawString("Scanning devices...", 8, 26, 2);
    // прогрес-бар
    int bx = 8, by = 50, bw = SCR_W - 16, bh = 12;
    spr.drawRoundRect(bx, by, bw, bh, 2, C_DIM);
    int w = sweep_total_ ? (int)((bw - 4) * sweep_i_ / sweep_total_) : 0;
    spr.fillRoundRect(bx + 2, by + 2, w, bh - 4, 1, C_ACCENT);
    snprintf(b, sizeof(b), "Found: %d", host_count_);
    spr.setTextColor(C_GOOD, C_BG);
    spr.drawString(b, 8, 72, 2);
    spr.setTextColor(C_DIM, C_BG);
    spr.drawString("S5=stop", 8, SCR_H - 12, 1);
    return;
  }

  if (phase_ == PORTSCAN) {
    spr.setTextColor(C_TEXT, C_BG);
    spr.drawString("Port scan...", 8, 40, 2);
    spr.setTextColor(C_DIM, C_BG);
    net_ip_to_str(detail_ip_, b, sizeof(b));
    spr.drawString(b, 8, 62, 2);
    return;
  }

  if (phase_ == DETAIL) {
    net_ip_to_str(detail_ip_, b, sizeof(b));
    spr.setTextColor(C_ACCENT, C_BG); spr.drawString(b, 8, 20, 2);
    if (detail_rtt_ >= 0) snprintf(b, sizeof(b), "%d ms", detail_rtt_);
    else                  snprintf(b, sizeof(b), "n/a");
    spr.setTextDatum(TR_DATUM); spr.setTextColor(C_DIM, C_BG);
    spr.drawString(b, SCR_W - 8, 20, 2); spr.setTextDatum(TL_DATUM);

    // тип пристрою — головна нова інформація, помітно
    spr.setTextColor(C_GOOD, C_BG);
    spr.drawString(detail_type_[0] ? detail_type_ : "?", 8, 38, 2);
    // вендор + MAC
    spr.setTextColor(C_TEXT, C_BG);
    char vl[40];
    snprintf(vl, sizeof(vl), "%s", detail_vendor_[0] ? detail_vendor_ : "?");
    spr.drawString(vl, 8, 56, 1);
    spr.setTextColor(C_DIM, C_BG);
    spr.drawString(detail_mac_[0] ? detail_mac_ : "MAC: n/a (ne v ARP)", 8, 68, 1);

    // відкриті порти
    char pl[80]; int pos = 0; pos += snprintf(pl, sizeof(pl), "Porty: ");
    for (int i = 0; i < open_count_; i++)
      pos += snprintf(pl + pos, sizeof(pl) - pos, "%u ", open_ports_[i]);
    if (!open_count_) snprintf(pl + pos, sizeof(pl) - pos, "(none)");
    spr.setTextColor(open_count_ ? C_GOOD : C_DIM, C_BG);
    spr.drawString(pl, 8, 82, 1);
    // банер першого відкритого порту з непорожньою відповіддю
    for (int i = 0; i < open_count_; i++) {
      if (banner_[i][0]) {
        char bl[44]; snprintf(bl, sizeof(bl), "%u: %.34s", open_ports_[i], banner_[i]);
        spr.setTextColor(C_ACCENT, C_BG);
        spr.drawString(bl, 8, 96, 1);
        break;
      }
    }
    spr.setTextColor(C_DIM, C_BG);
    spr.drawString("S5=HTTP GET :80  S2=back", 8, SCR_H - 12, 1);
    return;
  }

  // LIST
  spr.setTextDatum(TR_DATUM); spr.setTextColor(C_DIM, C_PANEL);
  snprintf(b, sizeof(b), "%d host", host_count_);
  spr.drawString(b, SCR_W - 6, 3, 2);
  spr.setTextDatum(TL_DATUM);
  const int VISIBLE = 5, ROW_H = 18, total = items_total();
  int first = cursor_ - VISIBLE / 2;
  if (first < 0) first = 0;
  if (first > total - VISIBLE) first = total - VISIBLE;
  if (first < 0) first = 0;
  for (int row = 0; row < VISIBLE; row++) {
    int idx = first + row;
    if (idx >= total) break;
    int y = 20 + row * ROW_H;
    bool sel = (idx == cursor_);
    if (sel) spr.fillRoundRect(4, y, SCR_W - 8, ROW_H - 2, 3, C_PANEL);
    spr.setTextColor(sel ? C_ACCENT : C_TEXT, sel ? C_PANEL : C_BG);
    if (idx < host_count_) net_ip_to_str(hosts_[idx], b, sizeof(b));
    else                   snprintf(b, sizeof(b), "< Back");
    spr.drawString(b, 10, y + 1, 2);
  }
  spr.setTextColor(C_DIM, C_BG);
  spr.drawString("S2=next  S5=select", 8, SCR_H - 12, 1);
}

void NetScanApp::button(ButtonId id) {
  if (phase_ == NOT_CONN) { if (id == BTN_S2 || id == BTN_S5) wants_exit_ = true; return; }

  if (phase_ == SWEEP) {
    if (id == BTN_S5) phase_ = LIST;   // стоп -> показати знайдене
    return;
  }
  if (phase_ == DETAIL) {
    if (id == BTN_S2) { phase_ = LIST; return; }
    if (id == BTN_S5) {                  // відкрити хост у HTTP GET (порт 80)
      char url[24]; net_ip_to_str(detail_ip_, url, sizeof(url));
      intent_open_url(url);
      wants_exit_ = true;                // ядро підхопить намір і перемкне застосунок
    }
    return;
  }
  if (phase_ == PORTSCAN) return;       // під час скану ігноруємо

  // LIST
  if (id == BTN_S2)      cursor_ = (cursor_ + 1) % items_total();
  else if (id == BTN_S1) cursor_ = (cursor_ + items_total() - 1) % items_total();
  else if (id == BTN_S5) select_current();
}

void NetScanApp::select_current() {
  if (phase_ != LIST) return;
  if (cursor_ >= host_count_) { wants_exit_ = true; return; }  // "< Back"
  detail_ip_ = hosts_[cursor_];
  phase_ = PORTSCAN;
  run_portscan();      // блокуючий скан портів (~2с)
  phase_ = DETAIL;
}

void NetScanApp::select_index(int idx) {
  if (phase_ == LIST && idx >= 0 && idx < items_total()) {
    cursor_ = idx;
    select_current();
  }
}

std::string NetScanApp::remote_state() {
  char lines[MAX_HOSTS + 2][40];
  const char* items[MAX_HOSTS + 2];
  int n = 0;
  if (phase_ == SWEEP) {
    snprintf(lines[n], 40, "Scan %u/%u", (unsigned)sweep_i_, (unsigned)sweep_total_); items[n] = lines[n]; n++;
    snprintf(lines[n], 40, "Found %d", host_count_); items[n] = lines[n]; n++;
    return protocol_build_menu("net_scan", items, n, -1);
  }
  if (phase_ == DETAIL) {
    net_ip_to_str(detail_ip_, lines[n], 40); items[n] = lines[n]; n++;
    snprintf(lines[n], 40, "Typ: %s", detail_type_[0] ? detail_type_ : "?"); items[n] = lines[n]; n++;
    snprintf(lines[n], 40, "Vendor: %s", detail_vendor_[0] ? detail_vendor_ : "?"); items[n] = lines[n]; n++;
    if (detail_mac_[0]) { snprintf(lines[n], 40, "MAC: %s", detail_mac_); items[n] = lines[n]; n++; }
    snprintf(lines[n], 40, detail_rtt_ >= 0 ? "RTT %d ms" : "RTT n/a", detail_rtt_); items[n] = lines[n]; n++;
    for (int i = 0; i < open_count_ && n < MAX_HOSTS + 2; i++) {
      if (banner_[i][0]) snprintf(lines[n], 40, "%u %.32s", open_ports_[i], banner_[i]);
      else               snprintf(lines[n], 40, "port %u open", open_ports_[i]);
      items[n] = lines[n]; n++;
    }
    return protocol_build_menu("net_scan_host", items, n, -1);
  }
  // LIST
  int cnt = host_count_ < MAX_HOSTS ? host_count_ : MAX_HOSTS;
  for (int i = 0; i < cnt; i++) { net_ip_to_str(hosts_[i], lines[i], 40); items[i] = lines[i]; }
  return protocol_build_menu("net_scan", items, cnt, cursor_ < cnt ? cursor_ : -1);
}
