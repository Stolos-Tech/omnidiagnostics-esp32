#pragma once
// WiFi-аналіз на C6 (порт з головного проєкту: wifi_analyzer/channel_monitor/
// wifi_sniffer/deauth_alert). Sink — колбек видачі рядка в лінк. Стаби: підключити
// promiscuous-callback esp_wifi_set_promiscuous + канал-hop при перенесенні коду.
typedef void (*C6Sink)(const char*);

void wifi_scan_once(C6Sink out);              // разовий AP-скан -> WIFI ...
void wifi_monitor_begin(int ch);              // ch<0 = hop 1..13
void wifi_monitor_pump(C6Sink out);           // періодично шле MONCH <ch> <count>
void wifi_sniff_begin(int ch);
void wifi_sniff_pump(C6Sink out);             // SNIF <mac> <rssi> <count>
void wifi_deauth_watch_begin();
void wifi_deauth_pump(C6Sink out);            // DEA <count> <src>
void wifi_analysis_stop();                    // promiscuous off, mode NULL
