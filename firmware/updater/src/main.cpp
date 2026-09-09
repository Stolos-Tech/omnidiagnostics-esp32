// factory-updater: пише ота_0 з SD /firmware.bin, тоді стартує ота_0.
// Входимо сюди лише коли головний застосунок явно виставив boot=factory (POST /fw/apply).
// Fallback: нема SD/файлу -> одразу boot ота_0 (не застрягаємо у factory).
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <esp_ota_ops.h>

#define SD_SCK 2
#define SD_MOSI 15
#define SD_MISO 21
#define SD_CS 33

static SPIClass hspi(HSPI);

static void boot_app_and_reset() {
  const esp_partition_t* app = esp_partition_find_first(
      ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, nullptr);
  if (app) esp_ota_set_boot_partition(app);
  delay(100);
  esp_restart();
}

static bool flash_from_sd() {
  hspi.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS, hspi)) { Serial.println("SD fail"); return false; }
  if (!SD.exists("/firmware.bin")) { Serial.println("no image"); return false; }
  File f = SD.open("/firmware.bin", FILE_READ);
  if (!f) return false;
  size_t total = f.size();
  if (total == 0) { f.close(); return false; }

  const esp_partition_t* target = esp_ota_get_next_update_partition(nullptr);
  if (!target) { f.close(); return false; }
  esp_ota_handle_t h;
  if (esp_ota_begin(target, total, &h) != ESP_OK) { f.close(); return false; }

  uint8_t buf[4096];
  size_t done = 0;
  while (f.available()) {
    int n = f.read(buf, sizeof(buf));
    if (n <= 0) break;
    if (esp_ota_write(h, buf, n) != ESP_OK) { f.close(); esp_ota_abort(h); return false; }
    done += n;
    Serial.printf("%u/%u\n", (unsigned)done, (unsigned)total);
  }
  f.close();
  if (esp_ota_end(h) != ESP_OK) { Serial.println("verify fail"); return false; }
  if (esp_ota_set_boot_partition(target) != ESP_OK) return false;
  SD.remove("/firmware.bin");   // спожито -> наступний boot іде в ота_0
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("== ESP32-OS updater ==");
  bool ok = flash_from_sd();
  Serial.println(ok ? "flashed OK -> boot app" : "no-op -> boot app");
  boot_app_and_reset();
}

void loop() {}
