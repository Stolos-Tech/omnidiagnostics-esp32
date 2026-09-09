#include <Arduino.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rfid_clone.h"
#include "../drivers/display.h"
#include "../drivers/uno_link.h"
#include "../remote/protocol.h"

#define WR_INTERVAL_MS 150   // пейсинг WRITE-команд (щоб не переповнити RX UNO й дати час на запис)

static int hexnib(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

void RfidCloneApp::init() {
  wants_exit_ = false;
  phase_ = WAIT_SRC;
  src_seq_ = uno_link_audit().seq;   // ігнорувати вже наявний старий аудит
  nblk_ = 0; wr_idx_ = 0; src_uid_[0] = 0;
  uno_link_send_scan();              // оживити presence rc522
}

// Парсимо дамп аудиту ("RFA dat <block> <32hex>") у список дата-блоків для запису.
void RfidCloneApp::capture_from_audit() {
  const RfAudit& a = uno_link_audit();
  snprintf(src_uid_, sizeof(src_uid_), "%s", a.uid);
  const char* body = uno_link_audit_body();
  nblk_ = 0;
  const char* p = body;
  while (*p && nblk_ < MAX_BLK) {
    const char* nl = strchr(p, '\n');
    if (strncmp(p, "RFA dat ", 8) == 0) {
      int block = atoi(p + 8);
      const char* q = p + 8;
      while (*q == ' ') q++;
      while (*q && *q != ' ') q++;   // пропустити число блоку
      while (*q == ' ') q++;         // до hex
      uint8_t d[16]; bool okhex = true;
      for (int i = 0; i < 16; i++) {
        int hi = hexnib(q[i * 2]), lo = hexnib(q[i * 2 + 1]);
        if (hi < 0 || lo < 0) { okhex = false; break; }
        d[i] = (uint8_t)((hi << 4) | lo);
      }
      bool trailer = (block < 128 && (block & 3) == 3);
      if (okhex && block != 0 && !trailer) {   // не чіпаємо manufacturer/трейлери
        blk_[nblk_] = block;
        memcpy(data_[nblk_], d, 16);
        nblk_++;
      }
    }
    if (!nl) break;
    p = nl + 1;
  }
  phase_ = READY;
}

void RfidCloneApp::loop() {
  if (phase_ == WAIT_SRC) {
    const RfAudit& a = uno_link_audit();
    if (a.valid && a.seq != src_seq_) { src_seq_ = a.seq; capture_from_audit(); }
    return;
  }
  if (phase_ == WRITING) {
    if (wr_idx_ >= nblk_) {
      if (millis() - t_wr_ > 400) phase_ = DONE;   // дати UNO дописати останній блок
      return;
    }
    if (millis() - t_wr_ >= WR_INTERVAL_MS) {
      t_wr_ = millis();
      uno_link_send_write(blk_[wr_idx_], data_[wr_idx_]);
      wr_idx_++;
    }
  }
}

void RfidCloneApp::topBar(const char* title) {
  display_top_bar(title);
  TFT_eSprite& spr = display_sprite();
  spr.setTextDatum(TR_DATUM); spr.setTextColor(C_DIM, C_PANEL);
  spr.drawString("red-team", SCR_W - 6, 3, 2);
  spr.setTextDatum(TL_DATUM);
}

void RfidCloneApp::draw() {
  TFT_eSprite& spr = display_sprite();
  spr.fillSprite(C_BG);
  topBar("RFID CLONE");
  char b[40];

  if (phase_ == WAIT_SRC) {
    spr.setTextColor(C_TEXT, C_BG);
    spr.drawString("1) Priklady SOURCE kartu", 8, 40, 2);
    spr.setTextColor(C_DIM, C_BG);
    spr.drawString("audit + dump poide avtomatychno", 8, 64, 1);
    spr.drawString("S2 = exit", 8, SCR_H - 12, 1);
    return;
  }
  if (phase_ == READY) {
    spr.setTextColor(C_TEXT, C_BG);
    snprintf(b, sizeof(b), "Src: %s", src_uid_); spr.drawString(b, 8, 22, 2);
    snprintf(b, sizeof(b), "%d blokiv gotovo do klonu", nblk_);
    spr.setTextColor(nblk_ ? C_GOOD : C_WARN, C_BG); spr.drawString(b, 8, 44, 2);
    spr.setTextColor(C_ACCENT, C_BG);
    spr.drawString("2) Priklady TARGET + S5", 8, 68, 2);
    spr.setTextColor(C_DIM, C_BG);
    spr.drawString(nblk_ ? "S5=pysaty  S2=exit" : "nema danyh (WIDE-OPEN?)  S2=exit", 8, SCR_H - 12, 1);
    return;
  }
  if (phase_ == WRITING) {
    spr.setTextColor(C_TEXT, C_BG);
    snprintf(b, sizeof(b), "Pyshu %d / %d", wr_idx_, nblk_);
    spr.drawString(b, 8, 40, 4);
    int bw = nblk_ ? (SCR_W - 16) * wr_idx_ / nblk_ : 0;
    spr.drawRect(8, 74, SCR_W - 16, 10, C_GRID);
    spr.fillRect(9, 75, bw, 8, C_ACCENT);
    spr.setTextColor(C_DIM, C_BG);
    spr.drawString("trymay TARGET na rideri", 8, SCR_H - 12, 1);
    return;
  }
  // DONE
  int ok = uno_link_write_ok(), fail = uno_link_write_fail();
  spr.setTextColor(fail == 0 && ok > 0 ? C_GOOD : C_WARN, C_BG);
  snprintf(b, sizeof(b), "Gotovo: ok %d / fail %d", ok, fail);
  spr.drawString(b, 8, 44, 2);
  spr.setTextColor(C_DIM, C_BG);
  spr.drawString("UID ne klonovano (treba magic)", 8, 66, 1);
  spr.setTextColor(C_ACCENT, C_BG);
  spr.drawString("S5 = shche raz   S2 = exit", 8, SCR_H - 12, 1);
}

void RfidCloneApp::button(ButtonId id) {
  if (id == BTN_S2) { wants_exit_ = true; return; }
  if (id == BTN_S5) {
    if (phase_ == READY && nblk_ > 0) {
      uno_link_reset_write_counters();
      wr_idx_ = 0; t_wr_ = millis();
      phase_ = WRITING;
    } else if (phase_ == DONE) {
      phase_ = WAIT_SRC;
      src_seq_ = uno_link_audit().seq;   // чекати НОВИЙ source-тап
      nblk_ = 0; wr_idx_ = 0; src_uid_[0] = 0;
      uno_link_send_scan();
    }
  }
}

std::string RfidCloneApp::remote_state() {
  char lines[4][40];
  const char* items[4];
  int n = 0;
  if (phase_ == WAIT_SRC) {
    snprintf(lines[n], 40, "Tap SOURCE card on RC522"); items[n] = lines[n]; n++;
  } else if (phase_ == READY) {
    snprintf(lines[n], 40, "Src: %s", src_uid_); items[n] = lines[n]; n++;
    snprintf(lines[n], 40, "%d data blocks ready", nblk_); items[n] = lines[n]; n++;
    snprintf(lines[n], 40, "Tap TARGET, S5 = write"); items[n] = lines[n]; n++;
  } else if (phase_ == WRITING) {
    snprintf(lines[n], 40, "Writing %d/%d...", wr_idx_, nblk_); items[n] = lines[n]; n++;
  } else {
    snprintf(lines[n], 40, "Done: ok %d / fail %d", uno_link_write_ok(), uno_link_write_fail());
    items[n] = lines[n]; n++;
    snprintf(lines[n], 40, "UID not cloned (needs magic)"); items[n] = lines[n]; n++;
  }
  return protocol_build_menu("rfid_clone", items, n, -1);
}
