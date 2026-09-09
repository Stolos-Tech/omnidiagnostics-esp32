// Чисті утиліти роботи з IPv4 та підмережами — без мережевого стеку, тестуються
// на хості. IP представлений як uint32 у порядку a.b.c.d (a — старший байт).
#pragma once
#include <stdint.h>
#include <stddef.h>

// Складає uint32 з чотирьох октетів (a.b.c.d, a — старший).
inline uint32_t net_make_ip(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
  return ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)c << 8) | d;
}

// Форматує IP у "a.b.c.d". Повертає довжину рядка.
int net_ip_to_str(uint32_t ip, char* out, size_t out_size);

// Парсить "a.b.c.d" -> uint32. false якщо формат невірний.
bool net_str_to_ip(const char* s, uint32_t* out);

// Обчислює діапазон хостів підмережі за IP і маскою.
// Заповнює first_host/last_host/count (усі опційні). Повертає false для масок,
// де хостів нема (/31, /32). Базова адреса = ip & mask, broadcast = base | ~mask,
// хости — між ними.
bool net_subnet_range(uint32_t ip, uint32_t mask,
                      uint32_t* first_host, uint32_t* last_host, uint32_t* count);

// Маска з довжини префікса: net_prefix_to_mask(24) -> 255.255.255.0. 0..32.
uint32_t net_prefix_to_mask(int prefix);

// Парсить CIDR/базу підмережі у ip+mask:
//   "192.168.1.0/24" -> ip=192.168.1.0, mask=255.255.255.0
//   "192.168.1"      -> ip=192.168.1.0, mask=/24 (три октети = /24)
//   "10.0.0.5/16"    -> ip=10.0.0.5,    mask=255.255.0.0
// false якщо формат невірний. Префікс за замовчуванням /24.
bool net_parse_cidr(const char* s, uint32_t* ip, uint32_t* mask);

// Парсить список портів "80,443, 22" у out[] (до max). Повертає кількість
// (0 якщо жодного валідного). Ігнорує пробіли, відкидає >65535 і нечислові.
int net_parse_ports(const char* s, uint16_t* out, int max);

// Парсить "host:port" або "host" (тоді port = default_port). host — IP або імʼя,
// копіюється в host_out. Повертає false, якщо host порожній чи порт невалідний.
bool net_parse_hostport(const char* s, char* host_out, size_t host_size,
                        uint16_t* port_out, uint16_t default_port);

// Парсить MAC-адресу у будь-якому з форматів "AA:BB:CC:DD:EE:FF",
// "AA-BB-CC-DD-EE-FF" чи "AABBCCDDEEFF" -> 6 байт out[6]. false якщо невалідна.
bool net_parse_mac(const char* s, uint8_t out[6]);

// Будує Wake-on-LAN magic packet (102 байти: 6×0xFF + 16×MAC) у out.
// out_size має бути >= 102. Повертає записану довжину (0 якщо замало місця).
int wol_build_packet(const uint8_t mac[6], uint8_t* out, size_t out_size);
