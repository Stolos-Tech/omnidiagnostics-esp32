#include <Preferences.h>
#include "rfid_acl.h"

static uint32_t s_list[RFID_ACL_MAX];
static int      s_n = 0;

void rfid_acl_begin() {
  Preferences p;
  if (p.begin("rfidacl", true)) {
    int n = p.getInt("n", 0);
    if (n < 0) n = 0;
    if (n > RFID_ACL_MAX) n = RFID_ACL_MAX;
    size_t got = p.getBytes("list", s_list, (size_t)n * sizeof(uint32_t));
    s_n = (got == (size_t)n * sizeof(uint32_t)) ? n : 0;
    p.end();
  }
}

static void save() {
  Preferences p;
  if (p.begin("rfidacl", false)) {
    p.putInt("n", s_n);
    p.putBytes("list", s_list, (size_t)s_n * sizeof(uint32_t));
    p.end();
  }
}

int rfid_acl_count() { return s_n; }

uint32_t rfid_acl_get(int i) { return (i >= 0 && i < s_n) ? s_list[i] : 0; }

bool rfid_acl_contains(uint32_t uid) {
  for (int i = 0; i < s_n; i++) if (s_list[i] == uid) return true;
  return false;
}

bool rfid_acl_add(uint32_t uid) {
  if (rfid_acl_contains(uid)) return true;
  if (s_n >= RFID_ACL_MAX) return false;
  s_list[s_n++] = uid;
  save();
  return true;
}

bool rfid_acl_remove(uint32_t uid) {
  for (int i = 0; i < s_n; i++) {
    if (s_list[i] == uid) {
      for (int j = i + 1; j < s_n; j++) s_list[j - 1] = s_list[j];
      s_n--;
      save();
      return true;
    }
  }
  return false;
}

void rfid_acl_clear() { s_n = 0; save(); }
