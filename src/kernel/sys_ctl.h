// Системні команди (reboot / passive) від будь-якого джерела — веб, serial-консоль,
// меню. Джерело лише ПРОСИТЬ; виконує їх головний цикл (main loop) у безпечній точці.
// Device-only, без зовнішніх залежностей.
#pragma once

// Ребут плати. sys_request_reboot() — попросити; main викликає sys_take_reboot()
// і, якщо true, робить ESP.restart(). (Заміна зламаній фізичній RST-кнопці.)
void sys_request_reboot();
bool sys_take_reboot();

// Пасивний режим: екран вимкнено, але плата працює й тримає мережу/WS. Вихід —
// будь-яке фізичне натискання кнопки (обробляється в main). Замість авто-згасання.
void sys_set_passive(bool on);
bool sys_passive();
