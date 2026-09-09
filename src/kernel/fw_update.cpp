#include "fw_update.h"
#include "../drivers/sd_store.h"
#include <SD.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>

static File s_fw;

static const esp_partition_t* factory_part() {
  return esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, nullptr);
}

bool fw_factory_present() { return factory_part() != nullptr; }

bool fw_stage_begin() {
  if (!sd_mounted()) return false;
  sd_bus_select();
  if (SD.exists("/firmware.bin")) SD.remove("/firmware.bin");
  s_fw = SD.open("/firmware.bin", FILE_WRITE);
  return (bool)s_fw;
}

bool fw_stage_write(const uint8_t* buf, size_t n) {
  if (!s_fw) return false;
  sd_bus_select();
  return s_fw.write(buf, n) == n;
}

bool fw_stage_end(const char* md5hex) {
  if (!s_fw) return false;
  sd_bus_select();
  s_fw.close();
  if (md5hex && md5hex[0]) {
    File m = SD.open("/firmware.md5", FILE_WRITE);
    if (m) { m.print(md5hex); m.close(); }
  }
  return true;
}

void fw_stage_abort() {
  if (s_fw) { s_fw.close(); }
  sd_bus_select();
  if (SD.exists("/firmware.bin")) SD.remove("/firmware.bin");
}

uint32_t fw_staged_size() {
  if (!sd_mounted()) return 0;
  sd_bus_select();
  if (!SD.exists("/firmware.bin")) return 0;
  File f = SD.open("/firmware.bin", FILE_READ);
  if (!f) return 0;
  uint32_t sz = f.size(); f.close();
  return sz;
}

bool fw_apply() {
  const esp_partition_t* fac = factory_part();
  if (!fac) return false;
  if (fw_staged_size() == 0) return false;
  if (esp_ota_set_boot_partition(fac) != ESP_OK) return false;
  esp_restart();
  return true;   // недосяжно
}
