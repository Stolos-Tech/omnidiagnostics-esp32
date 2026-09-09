/*
 * main.cpp — прошивка Arduino UNO R3 як I/O-копроцесора esp32-os (red-team форк).
 *
 * Роль у зв'язці "T-Display + UNO":
 *   ESP32 (T-Display) — комунікація (WiFi/BT/пульт з ПК-телефону), екран, меню,
 *   важка обробка (графіки, FFT, статистика).
 *   UNO — знімає й попередньо обробляє датчики + RFID (RC522), віддає ESP32
 *   готові числа по UART. Мотори/актуатори (серво/реле/степер) ПРИБРАНО: цей форк —
 *   аналіз бездротових/мережевих систем, не механіка (керування турбіною живе
 *   в окремому TURBO OS-проєкті).
 *
 * Протокол лінка — див. link_proto.h (дзеркало src/drivers/uno_proto.h у
 * головному ESP32-проєкті; при зміні формату рядка правити ОБИДВА файли):
 *   UNO -> ESP32:  "S <pot> <reed> <ir_hex>"    ~10 Гц (базові датчики)
 *                  "ENV <temp_x10> <hum_x10>"   ~0.5 Гц (DHT11)
 *                  "RFID <uid_hex>" + "RFA ..."  (тег/аудит RC522)
 *   ESP32 -> UNO:  "SCAN"   "SET <slot> <args>"  (модульний хаб)
 *
 * ПІНАУТ (актуатори прибрано — D6/D7/D8, A1..A4 ВІЛЬНІ під майбутні модулі,
 * напр. nRF24L01+ по SPI):
 *   A0  — потенціометр (радіо-аналізатор: pot -> частота через uno_pot_to_freq_khz)
 *   D2  — reed-датчик (INPUT_PULLUP, магнітний контакт; звільнений пін tilt)
 *   D3  — IR приймач OUT (захоплення бездротових IR-кодів; VCC 5V, GND)
 *   D6  — IR LED-передавач (replay захоплених кодів; LED+резистор через шілд-хедер)
 *   D4  — SoftwareSerial RX (<- ESP32 TX GPIO22, напряму)
 *   D5  — SoftwareSerial TX (-> ESP32 RX GPIO37, ЧЕРЕЗ дільник 1к/2к до 3.3V!)
 *   D9  — RC522 RST
 *   D10 — RC522 SS
 *   D11 — RC522 MOSI (апаратний SPI)
 *   D12 — RC522 MISO (апаратний SPI)
 *   D13 — RC522 SCK  (апаратний SPI) — RC522 живиться ЛИШЕ 3.3V!
 *   A5  — DHT11 data (підтягуючий резистор ~10к до 5V, якщо нема на модулі)
 *   ВІЛЬНІ: D6, D7, D8, A1, A2, A3, A4 — під майбутні red-team модулі. (nRF24 живе
 *           на ESP32, НЕ тут — drivers/nrf24 у головному проєкті.)
 *
 *   Живлення: гніздо UNO 7-9V -> 5V рейка -> ESP32 5V(VBUS); спільна GND.
 *
 * SENSOR SHIELD V5.0 (юзер має): UNO-шилд, що виводить КОЖЕН пін у 3-pin хедер
 *   S/V/G -> модулі втикаються без пайки. Наші піни лягають прямо (0 змін коду):
 *   reed->D2, IR->D3, pot->A0, DHT->A5, RC522 SPI->D9-D13. Лінк ESP: сигнали в
 *   хедери D4(RX)/D5(TX через дільник 1к/2к). ГОТЧА: RC522 VCC = 3.3V (з 3V3-піна
 *   шилда на Bluetooth-хедері), НЕ з 5V-піна D-хедера! SD-хедер = D10-D13 SPI
 *   (D10 клеше з RC522-SS).
 */
#include <Arduino.h>
#include <SoftwareSerial.h>
#include <SPI.h>
#include <MFRC522.h>
#include <DHT.h>
#include <IRremote.hpp>
#include "link_proto.h"

#define PIN_POT         A0
#define PIN_REED        2   // reed-датчик (магнітний контакт; звільнений пін tilt)
#define PIN_IR          3
#define PIN_IRTX        6   // IR LED-передавач (replay захоплених кодів) — вільний PWM-пін
#define SS_RX           4   // <- ESP32 TX
#define SS_TX           5   // -> ESP32 RX (через дільник рівня)
#define PIN_RC522_RST   9
#define PIN_RC522_SS    10
#define PIN_DHT         A5
// Джойстик KY-023 (навігація меню ESP32; UNO читає осі й шле "JOY x y sw"):
#define PIN_JOY_X       A1   // VRx (0..1023)
#define PIN_JOY_Y       A2   // VRy (0..1023)
#define PIN_JOY_SW      7    // кнопка джойстика (INPUT_PULLUP, натиск=LOW)
// ВІЛЬНІ: D8, A3, A4 (D6->IR TX, D7->JOY SW, A1/A2->джойстик). Під майбутні модулі.

#define DHT_TYPE           DHT11

// Bring-up: коли true, телеметрія також друкується в USB-Serial і команди
// SCAN/SET приймаються з USB-монітора (перевірка UNO без ESP32).
#define BRINGUP_DEBUG true

SoftwareSerial link(SS_RX, SS_TX);
MFRC522 rfid(PIN_RC522_SS, PIN_RC522_RST);
DHT dht(PIN_DHT, DHT_TYPE);

static unsigned long pendingIr = 0;
static unsigned long tSendSensors = 0;
static unsigned long tSendEnv = 0;
static unsigned long tSendJoy = 0;
static unsigned long tSendStat = 0;

// Вільна SRAM UNO (байт) — між кінцем купи й вершиною стека. Метрика здоров'я плати.
static int freeRam() {
  extern int __heap_start, *__brkval;
  int v;
  return (int)&v - (__brkval == 0 ? (int)&__heap_start : (int)__brkval);
}
static char cmd[64]; static int cmdLen = 0;   // буфер лінка (64: "WRITE <block> <32hex>" ~45 симв)
static char dbg[64]; static int dbgLen = 0;   // буфер USB (bring-up)
static bool s_dhtOk = false;                   // presence DHT11: останнє читання успішне
static IRData s_lastIr;                         // остання декодована IR-команда (для replay)
static bool   s_lastIrValid = false;            // валідна команда з відомим протоколом

// --- Модульний хаб (Фаза 1): самоописовий реєстр можливостей UNO ---------------
// Слоти-входи: 0 pot, 1 reed, 2 dht11, 3 rc522. Виходів-актуаторів більше нема
// (мотори/реле прибрано). presence для детектованих (DHT/RC522) реальний, для
// решти входів — 1 (завжди присутні). Нові модулі (nRF24 тощо) додаються сюди
// новими слотами, коли їх фізично під'єднають.
struct Cap { uint8_t slot; const char* type; const char* name; };
static const Cap CAPS[] = {
  { 0,  "analog",  "pot"   },
  { 1,  "switch",  "reed"  },
  { 2,  "env",     "dht11" },
  { 3,  "rfid",    "rc522" },
  { 30, "ir",      "irtx"  },   // IR-передавач: SET 30 -> replay останнього коду
};
static const int CAP_COUNT = sizeof(CAPS) / sizeof(CAPS[0]);

static bool rc522Present() {
  byte v = rfid.PCD_ReadRegister(MFRC522::VersionReg);
  return v != 0x00 && v != 0xFF;   // 0x91/0x92 = справжній RC522
}

static int capPresent(uint8_t slot) {
  switch (slot) {
    case 2:  return s_dhtOk ? 1 : 0;       // DHT11 — за останнім читанням
    case 3:  return rc522Present() ? 1 : 0;// RC522 — за версією чипа
    default: return 1;                     // входи pot/reed
  }
}

static void emitCaps(Stream& out, bool dbg) {
  char line[48];
  for (int i = 0; i < CAP_COUNT; i++) {
    link_build_cap(CAPS[i].slot, CAPS[i].type, CAPS[i].name, capPresent(CAPS[i].slot), line, sizeof(line));
    out.print(line);
    if (dbg) Serial.print(line);
  }
}

// Узагальнена дія SET <slot> <args>. Актуаторів у цьому форку нема; лишена як
// точка розширення під майбутні керовані модулі (напр. SET-режими nRF24).
static void handleSet(int slot, const char* args) {
  if (!capPresent((uint8_t)slot)) return;
  (void)args;
  if (slot == 30 && s_lastIrValid) {         // replay останньої захопленої IR-команди
    IrSender.write(&s_lastIr);
    IrReceiver.restartAfterSend();           // повернути приймач у роботу після передачі
  }
}

static bool writeBlock(byte block, const byte* data);   // RFID write (визначення нижче, з MF_KEYS)

static void handleCmd(Stream& out, bool dbg, const char* s) {
  int slot; char args[24];
  int block; byte data[16];
  if (strcmp(s, "IRT") == 0) {                 // діагностика: пустити відомий NEC-код через D6 (петльовий тест RX)
    IrSender.sendNEC(0x00, 0x45, 0);
    IrReceiver.restartAfterSend();             // повернути приймач у роботу після передачі
    out.print("IRT sent\r\n");
    if (dbg) Serial.print("IRT sent\r\n");
    return;
  }
  if (link_is_scan(s)) { emitCaps(out, dbg); return; }
  if (link_parse_write(s, &block, data)) {           // RFID clone/write: WRITE -> WRES
    bool ok = writeBlock((byte)block, data);
    char line[24]; link_build_wres(block, ok ? 1 : 0, line, sizeof(line));
    out.print(line);
    if (dbg) Serial.print(line);
    return;
  }
  if (link_parse_set(s, &slot, args, sizeof(args))) { handleSet(slot, args); return; }
}

// Читає рядок зі stream у buf; на '\n'/'\r' виконує команду. Відповіді (SCAN->CAP)
// йдуть у ТОЙ САМИЙ потік s; dbg=true додатково ехо в USB-Serial (лише для лінка).
static void feed(Stream& s, char* buf, int& len, bool dbg) {
  while (s.available()) {
    char c = s.read();
    if (c == '\n' || c == '\r') { if (len) { buf[len] = 0; handleCmd(s, dbg, buf); len = 0; } }
    else if (len < 63) buf[len++] = c;
  }
}

// --- RED-TEAM RFID-АУДИТ ------------------------------------------------------
// На тап карти проганяємо словник відомих дефолтних ключів Mifare Classic по всіх
// секторах: які піддались = картку можна читати/клонувати. Рапорт на лінк:
//   RFA uid <hex> sak <hex> <type>
//   RFA sec <n> <A|B> <key6hex>          (по кожному зламаному сектору)
//   RFA end <cracked>/<total> <verdict>  (SECURED/PARTIAL/WIDE-OPEN/NOT-CLASSIC)
// Розширений словник відомих дефолтних/поширених ключів Mifare Classic (публічні
// dic-списки: заводські, MAD/NDEF, транспортні, access, готельні). Перший, що
// підійшов, зупиняє перебір -> дефолтні картки все одно швидкі.
static const byte MF_KEYS[][6] PROGMEM = {
  {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF},  // заводський дефолт (найчастіший)
  {0x00,0x00,0x00,0x00,0x00,0x00},  // нульовий
  {0xA0,0xA1,0xA2,0xA3,0xA4,0xA5},  // MAD key A
  {0xB0,0xB1,0xB2,0xB3,0xB4,0xB5},  // MAD key B
  {0xC0,0xC1,0xC2,0xC3,0xC4,0xC5},
  {0xD0,0xD1,0xD2,0xD3,0xD4,0xD5},
  {0xD3,0xF7,0xD3,0xF7,0xD3,0xF7},  // NDEF public
  {0xAA,0xBB,0xCC,0xDD,0xEE,0xFF},
  {0x4D,0x3A,0x99,0xC3,0x51,0xDD},
  {0x1A,0x98,0x2C,0x7E,0x45,0x9A},
  {0x71,0x4C,0x5C,0x88,0x6E,0x97},
  {0x58,0x7E,0xE5,0xF9,0x35,0x0F},
  {0xA0,0x47,0x8C,0xC3,0x90,0x91},
  {0x53,0x3C,0xB6,0xC7,0x23,0xF6},
  {0x8F,0xD0,0xA4,0xF2,0x56,0xE9},
  {0x5C,0x8F,0xF9,0x99,0x0D,0xA2},
  {0x75,0xCC,0xB5,0x9C,0x9B,0xED},
  {0xB1,0x27,0xC6,0xF4,0x14,0x36},
  {0x4B,0x79,0x1B,0xEA,0x7B,0xCC},
  {0xFC,0x00,0x01,0x87,0x78,0xF7},  // транспорт/access
  {0x02,0x97,0x92,0x7C,0x0F,0x77},
  {0xEE,0x00,0x42,0xF8,0x88,0x40},
  {0x72,0x2B,0xFC,0xC5,0x37,0x5F},  // ТЦ/access
  {0x00,0x00,0x00,0x00,0x00,0x01},
};
static const int MF_NKEYS = sizeof(MF_KEYS) / sizeof(MF_KEYS[0]);

static void emitLine(const char* line) {
  link.print(line);
  if (BRINGUP_DEBUG) Serial.print(line);
}

static void toHex(const byte* b, byte n, char* out) {
  static const char H[] = "0123456789ABCDEF";
  for (byte i = 0; i < n; i++) { out[i*2] = H[b[i] >> 4]; out[i*2+1] = H[b[i] & 0x0F]; }
  out[n*2] = 0;
}

static const char* mfTypeShort(MFRC522::PICC_Type t) {
  switch (t) {
    case MFRC522::PICC_TYPE_MIFARE_MINI: return "MifareMini";
    case MFRC522::PICC_TYPE_MIFARE_1K:   return "Classic1K";
    case MFRC522::PICC_TYPE_MIFARE_4K:   return "Classic4K";
    case MFRC522::PICC_TYPE_MIFARE_UL:   return "Ultralight";
    case MFRC522::PICC_TYPE_ISO_14443_4: return "ISO14443-4";
    default:                             return "Unknown";
  }
}

// Пере-вибір карти: невдала Mifare-авторизація halt-ає картку, тож перед наступним
// ключем треба WakeupA+Select, інакше все далі провалиться.
static bool mfReselect() {
  byte atqa[2]; byte sz = sizeof(atqa);
  MFRC522::StatusCode s = rfid.PICC_WakeupA(atqa, &sz);
  if (s != MFRC522::STATUS_OK && s != MFRC522::STATUS_COLLISION) return false;
  return rfid.PICC_Select(&rfid.uid, 0) == MFRC522::STATUS_OK;
}

static byte mfTrailer(int sector) {
  return (sector < 32) ? (byte)(sector * 4 + 3) : (byte)(128 + (sector - 32) * 16 + 15);
}

// Пробує авторизувати блок усіма відомими ключами (A і B по черзі). Повертає:
//   1  — успіх: key містить ключ, *out_idx = індекс у MF_KEYS, *out_type = 'A'/'B';
//   0  — усі ключі відкинуто, картка ще на місці;
//  -1  — картка зникла (reselect провалився) -> перервати роботу.
// Після кожної невдалої спроби StopCrypto1+reselect (halted-картку інакше не
// авторизувати далі) — та сама логіка потрібна і аудиту (auditCard), і запису
// (writeBlock), тож живе тут єдиним джерелом.
static int mfTryAuth(byte block, MFRC522::MIFARE_Key& key, int* out_idx, char* out_type) {
  for (int k = 0; k < MF_NKEYS; k++) {
    for (int ab = 0; ab < 2; ab++) {
      memcpy_P(key.keyByte, MF_KEYS[k], 6);
      byte cmd = ab == 0 ? MFRC522::PICC_CMD_MF_AUTH_KEY_A : MFRC522::PICC_CMD_MF_AUTH_KEY_B;
      if (rfid.PCD_Authenticate(cmd, block, &key, &rfid.uid) == MFRC522::STATUS_OK) {
        if (out_idx)  *out_idx = k;
        if (out_type) *out_type = (ab == 0) ? 'A' : 'B';
        return 1;
      }
      rfid.PCD_StopCrypto1();
      if (!mfReselect()) return -1;   // картка зникла
    }
  }
  return 0;
}

static void auditCard() {
  char uidhex[21]; toHex(rfid.uid.uidByte, rfid.uid.size, uidhex);
  MFRC522::PICC_Type t = MFRC522::PICC_GetType(rfid.uid.sak);
  char line[64];
  snprintf(line, sizeof(line), "RFA uid %s sak %02X %s\n", uidhex, rfid.uid.sak, mfTypeShort(t));
  emitLine(line);

  int total = 0, cracked = 0;
  if (t == MFRC522::PICC_TYPE_MIFARE_MINI || t == MFRC522::PICC_TYPE_MIFARE_1K || t == MFRC522::PICC_TYPE_MIFARE_4K) {
    total = (t == MFRC522::PICC_TYPE_MIFARE_4K) ? 40 : (t == MFRC522::PICC_TYPE_MIFARE_MINI) ? 5 : 16;
    MFRC522::MIFARE_Key key;
    for (int s = 0; s < total; s++) {
      byte trailer = mfTrailer(s);
      int uk = -1; char kt = 'A';
      int a = mfTryAuth(trailer, key, &uk, &kt);
      if (a < 0) break;              // картка зникла -> завершити аудит
      if (a > 0) {
        cracked++;
        char khex[13]; memcpy_P(key.keyByte, MF_KEYS[uk], 6); toHex(key.keyByte, 6, khex);
        snprintf(line, sizeof(line), "RFA sec %d %c %s\n", s, kt, khex);
        emitLine(line);
        // Дамп даних-блоків сектора (крім трейлера з ключами): "RFA dat <block> <32hex>"
        int firstBlk = (s < 32) ? s * 4 : 128 + (s - 32) * 16;
        int nData    = (s < 32) ? 3 : 15;
        for (int bi = 0; bi < nData; bi++) {
          byte blk = (byte)(firstBlk + bi);
          byte buf[18]; byte sz = sizeof(buf);
          if (rfid.MIFARE_Read(blk, buf, &sz) == MFRC522::STATUS_OK) {
            char dhex[33]; toHex(buf, 16, dhex);
            snprintf(line, sizeof(line), "RFA dat %d %s\n", blk, dhex);
            emitLine(line);
          }
        }
        rfid.PCD_StopCrypto1();   // картка лишається активною для наступного сектора
      }
    }
  }
  const char* verdict = (total == 0) ? "NOT-CLASSIC" : (cracked == 0) ? "SECURED"
                        : (cracked == total) ? "WIDE-OPEN" : "PARTIAL";
  snprintf(line, sizeof(line), "RFA end %d/%d %s\n", cracked, total, verdict);
  emitLine(line);
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}

// Запис 16 байт у блок Mifare Classic (clone дата-блоків). WakeupA+Select картки
// (могла бути halted після попереднього блоку), авторизація сектора відомими
// ключами (той самий словник, що аудит), MIFARE_Write. Safety: блок 0 (manufacturer,
// потребує magic-карти) і трейлери сектора (ключі/access-біти) НЕ пишемо.
static bool writeBlock(byte block, const byte* data) {
  if (block == 0) return false;                        // manufacturer-блок -> лише magic-карта
  if (block < 128 && (block & 3) == 3) return false;   // трейлер сектора 1K -> не чіпаємо

  byte atqa[2]; byte sz = sizeof(atqa);
  MFRC522::StatusCode w = rfid.PICC_WakeupA(atqa, &sz);
  if (w != MFRC522::STATUS_OK && w != MFRC522::STATUS_COLLISION) {
    if (!rfid.PICC_IsNewCardPresent()) return false;
  }
  if (!rfid.PICC_ReadCardSerial()) return false;       // select + заповнити uid

  MFRC522::MIFARE_Key key;
  int a = mfTryAuth(block, key, nullptr, nullptr);
  if (a < 0) return false;                                                  // картка зникла
  if (a == 0) { rfid.PICC_HaltA(); rfid.PCD_StopCrypto1(); return false; }  // усі ключі провалились
  bool ok = rfid.MIFARE_Write(block, (byte*)data, 16) == MFRC522::STATUS_OK;
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
  return ok;
}

void setup() {
  pinMode(PIN_REED, INPUT_PULLUP);
  pinMode(PIN_JOY_SW, INPUT_PULLUP);   // кнопка джойстика active-LOW
  dht.begin();

  SPI.begin();
  rfid.PCD_Init();

  link.begin(9600);
  if (BRINGUP_DEBUG) Serial.begin(9600);
  IrReceiver.begin(PIN_IR, ENABLE_LED_FEEDBACK);
  IrSender.begin(PIN_IRTX, false, 0);   // IR-передавач для replay (без власного LED-фідбеку)

  emitCaps(link, BRINGUP_DEBUG);   // оголосити можливості одразу на старті
}

void loop() {
  // 1) команди від ESP32 — з лінка і (для bring-up) з USB-монітора
  feed(link, cmd, cmdLen, BRINGUP_DEBUG);
  if (BRINGUP_DEBUG) feed(Serial, dbg, dbgLen, false);

  // 2) IR (тримаємо останній код до відправки)
  if (IrReceiver.decode()) {
    unsigned long code = IrReceiver.decodedIRData.decodedRawData;
    if (code) {
      pendingIr = code;
      s_lastIr = IrReceiver.decodedIRData;                     // повна структура для replay
      s_lastIrValid = (IrReceiver.decodedIRData.protocol != UNKNOWN);
    }
    IrReceiver.resume();
  }

  // 3) RFID — SS/RST/MOSI/MISO/SCK лише RC522, шина не ділиться ні з чим
  static uint32_t tLastAudit = 0;
  static uint32_t lastAuditUid = 0;
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    uint32_t uid = 0;
    for (byte i = 0; i < rfid.uid.size && i < 4; i++) uid = (uid << 8) | rfid.uid.uidByte[i];
    char line[24];
    link_build_rfid(uid, line, sizeof(line));   // сумісність: коротка подія тегу (RFID <uid>)
    emitLine(line);
    // Кулдаун: не повторювати важкий аудит, поки та сама картка лежить на рідері.
    // Інша картка -> аудит одразу; та сама -> не частіше ніж раз на 4с.
    if (uid != lastAuditUid || millis() - tLastAudit > 4000) {
      auditCard();               // сам робить HaltA/StopCrypto1
      lastAuditUid = uid; tLastAudit = millis();
    } else {
      rfid.PICC_HaltA(); rfid.PCD_StopCrypto1();
    }
  }

  // 4) базові датчики кожні 100 мс
  if (millis() - tSendSensors >= 100) {
    tSendSensors = millis();
    int pot = analogRead(PIN_POT);
    bool reed = digitalRead(PIN_REED) == LOW;   // pullup: магніт/замкнуто = LOW
    char line[32];
    link_build_sensors(pot, reed, pendingIr, line, sizeof(line));
    link.print(line);
    if (BRINGUP_DEBUG) Serial.print(line);
    pendingIr = 0;
  }

  // 6) джойстик кожні 50 мс (20 Гц — чуйна навігація меню на ESP32)
  if (millis() - tSendJoy >= 50) {
    tSendJoy = millis();
    int jx = analogRead(PIN_JOY_X);
    int jy = analogRead(PIN_JOY_Y);
    int jsw = digitalRead(PIN_JOY_SW) == LOW ? 1 : 0;   // pullup: натиск = LOW
    char line[24];
    link_build_joy(jx, jy, jsw, line, sizeof(line));
    link.print(line);
    if (BRINGUP_DEBUG) Serial.print(line);
  }

  // 7) метрики UNO кожні 2 с (вільна SRAM + аптайм) — для моніторингу плати в додатку
  if (millis() - tSendStat >= 2000) {
    tSendStat = millis();
    char line[24];
    link_build_stat(freeRam(), millis() / 1000UL, line, sizeof(line));
    link.print(line);
    if (BRINGUP_DEBUG) Serial.print(line);
  }

  // 5) DHT11 кожні 2 с (частіше — датчик просто повертає старі дані)
  if (millis() - tSendEnv >= 2000) {
    tSendEnv = millis();
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    s_dhtOk = (!isnan(t) && !isnan(h));      // presence для CAP слота 2
    if (s_dhtOk) {
      char line[24];
      link_build_env((int)(t * 10), (int)(h * 10), line, sizeof(line));
      link.print(line);
      if (BRINGUP_DEBUG) Serial.print(line);
    }
  }
}
