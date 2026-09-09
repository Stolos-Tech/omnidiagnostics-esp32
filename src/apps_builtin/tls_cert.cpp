#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <string.h>
#include <stdio.h>
#include <mbedtls/ssl.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/x509_crt.h>
#include "tls_cert.h"
#include "../drivers/display.h"
#include "../remote/protocol.h"

static WiFiClient s_tcp;

static int bio_send(void* ctx, const unsigned char* b, size_t l) {
  (void)ctx; int w = s_tcp.write(b, l);
  return w > 0 ? w : MBEDTLS_ERR_SSL_WANT_WRITE;
}
static int bio_recv(void* ctx, unsigned char* b, size_t l) {
  (void)ctx;
  if (s_tcp.available() == 0) return s_tcp.connected() ? MBEDTLS_ERR_SSL_WANT_READ : 0;
  int r = s_tcp.read(b, l);
  return r > 0 ? r : MBEDTLS_ERR_SSL_WANT_READ;
}

// Витягує значення поля з тексту mbedtls_x509_crt_info ("<key> ... : <value>\n").
static void extract_field(const char* info, const char* key, char* out, size_t osz) {
  out[0] = 0;
  const char* p = strstr(info, key);
  if (!p) return;
  p = strchr(p, ':'); if (!p) return; p++;
  while (*p == ' ') p++;
  const char* e = p; while (*e && *e != '\n' && *e != '\r') e++;
  size_t n = (size_t)(e - p); if (n >= osz) n = osz - 1;
  memcpy(out, p, n); out[n] = 0;
}

bool TlsCertApp::connected() const { return WiFi.status() == WL_CONNECTED; }

void TlsCertApp::fetch() {
  ok_ = false; subject_[0] = issuer_[0] = expires_[0] = err_[0] = 0;
  // TLS-рукостискання mbedtls потребує ~40КБ купи (record-буфери). На повністю
  // завантаженій прошивці (AP+STA+WS+веб+спрайт 64КБ) стільки може не бути —
  // не тратимо 8с на приречений handshake, а чесно кажемо про причину.
  uint32_t heap = ESP.getFreeHeap();
  if (heap < 45000) { snprintf(err_, sizeof(err_), "low RAM: %u B (TLS needs ~45K)", (unsigned)heap); phase_ = DONE; return; }
  if (!s_tcp.connect(host_, 443, 8000)) { snprintf(err_, sizeof(err_), "TCP :443 not open"); phase_ = DONE; return; }

  mbedtls_ssl_context ssl; mbedtls_ssl_config conf;
  mbedtls_ctr_drbg_context ctr; mbedtls_entropy_context ent;
  mbedtls_ssl_init(&ssl); mbedtls_ssl_config_init(&conf);
  mbedtls_ctr_drbg_init(&ctr); mbedtls_entropy_init(&ent);

  const char* pers = "esp32os-tls";
  int ret = 0;
  if (mbedtls_ctr_drbg_seed(&ctr, mbedtls_entropy_func, &ent, (const unsigned char*)pers, strlen(pers)) != 0 ||
      mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT) != 0) {
    snprintf(err_, sizeof(err_), "mbedtls init");
  } else {
    mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_NONE);   // не перевіряємо, лише читаємо
    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctr);
    mbedtls_ssl_setup(&ssl, &conf);
    mbedtls_ssl_set_hostname(&ssl, host_);                       // SNI
    mbedtls_ssl_set_bio(&ssl, nullptr, bio_send, bio_recv, nullptr);

    uint32_t start = millis();
    while ((ret = mbedtls_ssl_handshake(&ssl)) != 0) {
      if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) break;
      if (millis() - start > 8000) { snprintf(err_, sizeof(err_), "timeout (heap %u B)", (unsigned)ESP.getFreeHeap()); break; }
      delay(10);
    }
    if (ret == 0) {
      const mbedtls_x509_crt* crt = mbedtls_ssl_get_peer_cert(&ssl);
      if (crt) {
        char info[1024];
        int n = mbedtls_x509_crt_info(info, sizeof(info) - 1, "", crt);
        if (n > 0) {
          info[n] = 0;
          extract_field(info, "subject name", subject_, sizeof(subject_));
          extract_field(info, "issuer name",  issuer_,  sizeof(issuer_));
          extract_field(info, "expires on",   expires_, sizeof(expires_));
          ok_ = true;
        } else snprintf(err_, sizeof(err_), "crt_info fail");
      } else snprintf(err_, sizeof(err_), "cert unavailable (peer)");
    } else if (!err_[0]) {
      snprintf(err_, sizeof(err_), "handshake err -0x%04X", (unsigned)(-ret));
    }
    mbedtls_ssl_close_notify(&ssl);
  }

  s_tcp.stop();
  mbedtls_ssl_free(&ssl); mbedtls_ssl_config_free(&conf);
  mbedtls_ctr_drbg_free(&ctr); mbedtls_entropy_free(&ent);
  phase_ = DONE;
}

void TlsCertApp::init() {
  wants_exit_ = false;
  if (!connected()) { phase_ = NOT_CONN; return; }
  phase_ = FETCHING; started_ = false;
}

void TlsCertApp::loop() {
  if (phase_ != FETCHING) return;
  if (!started_) { started_ = true; return; }   // дати намалювати кадр "Handshake..."
  fetch();
}

void TlsCertApp::draw() {
  TFT_eSprite& spr = display_sprite();
  spr.fillSprite(C_BG);
  spr.fillRect(0, 0, SCR_W, 16, C_PANEL);
  spr.setTextDatum(TL_DATUM); spr.setTextColor(C_ACCENT, C_PANEL);
  spr.drawString("TLS CERT", 6, 3, 2);
  char b[76];

  if (phase_ == NOT_CONN) {
    spr.setTextColor(C_WARN, C_BG); spr.drawString("Not connected", 8, 30, 2);
    spr.setTextColor(C_DIM, C_BG);  spr.drawString("Zapusty 'WiFi Setup'", 8, 52, 1);
    spr.drawString("S2=exit", 8, SCR_H - 12, 1);
    return;
  }
  spr.setTextColor(C_DIM, C_BG); spr.drawString(host_, 8, 18, 1);

  if (phase_ == FETCHING) { spr.setTextColor(C_TEXT, C_BG); spr.drawString("Handshake :443 ...", 8, 44, 2); return; }

  if (!ok_) {
    spr.setTextColor(C_BAD, C_BG); spr.drawString("Failed", 8, 38, 2);
    spr.setTextColor(C_DIM, C_BG); spr.drawString(err_, 8, 60, 1);
    spr.setTextColor(C_ACCENT, C_BG); spr.drawString("S5 = retry", 8, SCR_H - 12, 1);
    return;
  }
  spr.setTextColor(C_TEXT, C_BG);
  snprintf(b, sizeof(b), "CN: %.60s", subject_); spr.drawString(b, 8, 32, 1);
  spr.setTextColor(C_DIM, C_BG);
  snprintf(b, sizeof(b), "Issuer: %.56s", issuer_); spr.drawString(b, 8, 52, 1);
  spr.setTextColor(C_GOOD, C_BG);
  snprintf(b, sizeof(b), "Expires: %.30s", expires_); spr.drawString(b, 8, 74, 2);
  spr.setTextColor(C_ACCENT, C_BG); spr.drawString("S5 = again", 8, SCR_H - 12, 1);
}

void TlsCertApp::button(ButtonId id) {
  if (phase_ == NOT_CONN) { if (id == BTN_S2 || id == BTN_S5) wants_exit_ = true; return; }
  if (id == BTN_S5) { phase_ = FETCHING; started_ = false; }
}

void TlsCertApp::text(const char* field, const char* value) {
  if (field && strcmp(field, "host") == 0 && value && value[0]) {
    snprintf(host_, sizeof(host_), "%s", value);
    if (connected()) { phase_ = FETCHING; started_ = false; }
  }
}

std::string TlsCertApp::remote_state() {
  if (phase_ == FETCHING) { const char* it[1] = { "Handshake..." }; return protocol_build_menu("tls_cert", it, 1, -1); }
  char lines[4][48];
  const char* items[4]; int n = 0;
  snprintf(lines[n], 48, "host %.40s", host_); items[n] = lines[n]; n++;
  if (ok_) {
    snprintf(lines[n], 48, "CN %.42s", subject_); items[n] = lines[n]; n++;
    snprintf(lines[n], 48, "iss %.41s", issuer_); items[n] = lines[n]; n++;
    snprintf(lines[n], 48, "exp %.41s", expires_); items[n] = lines[n]; n++;
  } else {
    snprintf(lines[n], 48, "%.44s", err_); items[n] = lines[n]; n++;
  }
  return protocol_build_menu("tls_cert", items, n, -1);
}
