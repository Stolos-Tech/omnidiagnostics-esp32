// Обгортка Berry VM для esp32-os. Чистий шар (лише berry.h) — компілюється
// і на пристрої, і на хості (native-тести з реальною libberry).
// Апаратні речі (heap-замір, native-функції) підключаються ззовні, не тут.
#pragma once
#include <stdbool.h>

struct bvm;  // forward — щоб не тягнути berry.h у споживачів

// Створює VM. Повертає false, якщо не вдалося (нема пам'яті).
bool berry_vm_init();

// Реєструє одну native-функцію в глобальному просторі VM.
// f має тип bntvfunc (int(*)(bvm*)) — приймаємо void* щоб не тягнути berry.h у .h.
void berry_vm_regfunc(const char* name, int (*f)(bvm*));

// Компілює й виконує рядок-скрипт. name — мітка джерела для повідомлень.
// Повертає true при успіху. При помилці — текст доступний через berry_vm_last_error().
bool berry_vm_run_string(const char* name, const char* code);

// Викликає глобальну Berry-функцію без аргументів (напр. "app_draw").
// Повертає true при успіху; false якщо функції нема, вона не викликна, або кинула виняток.
// Помилка виконання — у berry_vm_last_error().
bool berry_vm_call_global(const char* name);

// Викликає глобальну Berry-функцію з двома рядковими аргументами (напр.
// "app_text"). Якщо функції нема — тихо повертає false (це не помилка: не всі
// скрипти обробляють текст). Помилка виконання — у berry_vm_last_error().
bool berry_vm_call_global_ss(const char* name, const char* a, const char* b);

// Виконує рядок-скрипт із ЗАХОПЛЕННЯМ виводу print() у out (для /script/run —
// запуск скрипта з телефона/чату без заливки, з поверненням тексту). out завжди
// '\0'-термінований. Повертає true при успіху; помилка — у berry_vm_last_error().
bool berry_vm_run_capture(const char* name, const char* code, char* out, unsigned int out_cap);

// Останнє повідомлення про помилку (синтаксис/виконання) або "" якщо не було.
const char* berry_vm_last_error();

// Прямий доступ до VM (для розширеної реєстрації native_api). nullptr до init.
bvm* berry_vm_raw();
