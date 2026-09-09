// Чисті утиліти форматування для діагностичних екранів — без заліза,
// тестуються на хості.
#pragma once
#include <stdint.h>
#include <stddef.h>

// Форматує тривалість у людський рядок:
//   < доби:  "1h 02m 03s"  (години можуть бути 0: "0h 05m 09s")
//   >= доби: "2d 03h 04m"
void format_uptime(uint32_t total_sec, char* out, size_t out_size);

// Форматує розмір у байтах:  "512 B" / "48 KB" / "3.94 MB".
void format_bytes(uint32_t bytes, char* out, size_t out_size);

// Форматує секунди у "MM:SS" (або "H:MM:SS" від години і більше). Для секундоміра/таймера.
void format_mmss(uint32_t total_sec, char* out, size_t out_size);
