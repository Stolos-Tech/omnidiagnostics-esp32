#pragma once
#include <stddef.h>
// Серійний командний міст (task 6). Дає серверу керувати платою по USB, коли
// WiFi зайнятий/перезапускається (маршрут App -> Server -> Board):
//   ->  "REQ <id> <METHOD> <path> [json-body]"
//   <-  "RES <id> <status> <json>"
// Плюс службові: "PING"->"PONG", "REBOOT"->ESP.restart().
// Диспетчер (той самий роутер, що й REST) реєструється транспортом.

// Повертає HTTP-подібний статус; заповнює out JSON-тілом (out_cap — розмір буфера).
typedef int (*SerialDispatchFn)(const char* method, const char* path,
                                const char* body, char* out, size_t out_cap);

void serial_command_begin(SerialDispatchFn dispatch);
void serial_command_poll();     // викликати щоцикл у loop()
