// Білий список UID карт у NVS — для RFID-доступу/розблокування. UID береться з
// "RFID <uid>" лінка UNO (uno_link_last_rfid, перші 4 байти = 0 для 7-байтних).
#pragma once
#include <stdint.h>

#define RFID_ACL_MAX 10

void     rfid_acl_begin();               // завантажити з NVS (у setup)
int      rfid_acl_count();
uint32_t rfid_acl_get(int i);            // i поза межами -> 0
bool     rfid_acl_contains(uint32_t uid);
bool     rfid_acl_add(uint32_t uid);     // false якщо список повний; true якщо додано або вже був
bool     rfid_acl_remove(uint32_t uid);  // прибрати конкретний UID
void     rfid_acl_clear();
