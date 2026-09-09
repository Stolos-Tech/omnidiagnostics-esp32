// Challenge-response HMAC-SHA256 автентифікація — ДОДАТКОВО до PIN (dual-auth).
// Незмінний per-link seed (256-bit) у NVS: ніколи не передається (крім enroll під auth) і
// ніколи не змінюється → розсинхрон/локаут неможливі by-design. Доказ щоразу свіжий із nonce
// (анти-replay), не залежить від годинника. Стандарти: HMAC RFC 2104, ідея HOTP RFC 4226.
#pragma once
#include <stddef.h>

void auth_hmac_begin();                                    // завантажити/згенерувати seed з NVS
bool auth_hmac_ready();                                    // seed провізовано
bool auth_hmac_seed_hex(char* out, size_t cap);            // seed hex (лише під enroll, auth-only!)
void auth_hmac_challenge(char* out, size_t cap);           // згенерувати+запам'ятати nonce (hex)
bool auth_hmac_verify(const char* nonce_hex, const char* resp_hex);  // HMAC(seed,nonce)==resp ?
