#pragma once
// Захист від bootloop (task 8). Якщо застосунок з автозапуску «завис/крашнув»
// до позначки стабільності — не запускати його знову (safe-mode -> лаунчер).
// Мережеві/важкі аналізатори НІКОЛИ не авто-відновлюються після ребута: якщо
// плата ребутнулась під час аналізу, аналіз не рестартується сам (нема endless-loop).
void boot_state_begin();                             // у setup ДО автозапуску
bool boot_state_should_autostart(const char* name);  // false -> йти в лаунчер
void boot_state_mark_stable();                       // коли плата працює стабільно (~15с)
int  boot_state_crash_count();
