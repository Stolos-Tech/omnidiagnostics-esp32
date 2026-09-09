// mDNS-анонс самої плати: esp32os.local + сервіс _http._tcp:80.
// Єдина точка керування MDNS — і для анонсу, і для браузера сервісів
// (щоб не конфліктували begin/end). Ідемпотентно.
#pragma once

void mdns_service_start();     // піднімає MDNS.begin(<кастомне ім'я>) один раз (STA має бути up)
void mdns_service_stop();      // MDNS.end() (щоб перезапустити з новим ім'ям)
bool mdns_service_running();
