// Апаратна обгортка над drivers/uno_proto (чистий протокол) — UART1 на
// GPIO37(RX, через дільник 1к/2к)/GPIO22(TX) @9600, як в артефакті-схемі.
// init() з setup(), loop() з loop() — неблокуюче, кешує останні значення.
#pragma once
#include <stdint.h>
#include "uno_proto.h"

void uno_link_init();
void uno_link_loop();

const UnoSensors& uno_link_sensors();
const UnoEnv&     uno_link_env();
int               uno_link_free_ram();   // вільна SRAM UNO (байт); -1 = ще не приходило
uint32_t          uno_link_uptime_s();   // аптайм UNO (с)
bool              uno_link_connected();   // true, якщо телеметрія від UNO приходила <2с тому
long              uno_link_last_rx_age_ms(); // вік останнього рядка від UNO (мс); -1 = ще нічого не приходило
uint32_t          uno_link_last_rfid();  // 0 = ще не було жодного тега
uint32_t          uno_link_rfid_seq();   // росте на кожен новий тег (щоб застосунок помітив зміну)

// --- Модульний хаб (Фаза 1): динамічний реєстр модулів із CAP/EVT ---
#define UNO_MAX_MODULES 20

int               uno_link_module_count();
const UnoModule&  uno_link_module(int i);   // i поза межами -> порожній модуль (slot=-1)
void              uno_link_send_set(int slot, const char* args);
void              uno_link_send_scan();     // попросити UNO переслати всі CAP

// --- Red-team RFID-аудит ---
const RfAudit&    uno_link_audit();         // останній аудит (valid=false доки не було)
const char*       uno_link_audit_body();    // повний текст RFA-рядків (для звіту)

// --- RFID write/clone ---
void uno_link_send_write(int block, const uint8_t* data16);  // "WRITE <block> <hex>" -> UNO
int  uno_link_write_ok();          // к-ть успішних записів (WRES ... 1) з останнього reset
int  uno_link_write_fail();        // к-ть невдалих (WRES ... 0)
void uno_link_reset_write_counters();
