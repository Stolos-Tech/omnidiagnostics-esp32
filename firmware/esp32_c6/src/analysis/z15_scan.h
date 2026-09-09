#pragma once
// 802.15.4 (Zigbee/Thread/Matter) енергоскан — НОВА здатність, якої нема на ESP32.
// C6 має радіо 802.15.4. Скан каналів 11..26, енергія + PAN-ID. Sink — колбек лінка.
typedef void (*C6Sink)(const char*);
void z15_scan_begin();
void z15_scan_pump(C6Sink out);   // Z15 <ch> <energy> <pan_hex>
