// Прошивка ота_0 по WiFi через SD: головний застосунок приймає образ, стрімить його
// на SD (/firmware.bin + /firmware.md5), а тоді ребутиться у factory-updater, який
// пише ота_0. Тут — тільки staging на SD + перемикання boot на factory.
#pragma once
#include <stdint.h>
#include <stddef.h>

bool     fw_factory_present();      // чи є factory-партиція (updater вшитий -> apply безпечний)
bool     fw_stage_begin();          // відкрити SD /firmware.bin на запис (SD має бути змонтована)
bool     fw_stage_write(const uint8_t* buf, size_t n);
bool     fw_stage_end(const char* md5hex);  // закрити + записати /firmware.md5 (може бути nullptr)
void     fw_stage_abort();          // закрити й видалити недописаний
uint32_t fw_staged_size();          // розмір /firmware.bin на SD (0 = нема)
bool     fw_apply();                // boot->factory + restart. false = нема factory/образу
