// Пошук виробника камери за OUI (перші 3 байти MAC/BSSID) — чиста логіка (native-тести).
// КУРАТОРНИЙ стартовий список камер-спеціалістів (Hikvision/Dahua/Reolink/Foscam/Wyze/
// Amcrest/Axis/Vivotek) — НЕ вичерпний; розширювати за живою OUI-базою. Матч = ЙМОВІРНА
// камера (виробник може мати й не-камери; аналогові 2.4GHz-камери ловить 2.4G Analyzer).
#pragma once
#include <stdint.h>

// Назва виробника камери, якщо OUI у списку; інакше nullptr.
const char* oui_camera_vendor(uint8_t b0, uint8_t b1, uint8_t b2);

// Парсить "AA:BB:CC:DD:EE:FF" -> перші 3 байти OUI. false, якщо формат не той.
bool oui_parse3(const char* bssid, uint8_t* b0, uint8_t* b1, uint8_t* b2);
