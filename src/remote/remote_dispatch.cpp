#include "remote_dispatch.h"
#include "protocol.h"
#include "session.h"
#include "../kernel/input_queue.h"

RemoteAction remote_handle_incoming(const char* json) {
  RemoteEvent ev = protocol_parse_incoming(json);

  // Побитий/порожній/задовгий/незрозумілий JSON — просто ігноруємо.
  if (ev.type == REV_INVALID || ev.type == REV_NONE) return RA_NONE;

  if (!session_is_authenticated()) {
    // До автентифікації приймається ЛИШЕ PIN. Усе інше мовчки відкидається.
    if (ev.type == REV_PIN) {
      AuthResult r = session_authenticate(ev.pin);
      if (r == AUTH_LOCKED) return RA_DISCONNECT;  // вичерпано спроби
    }
    return RA_NONE;
  }

  // Сесія автентифікована — події від телефону йдуть у ту саму чергу, що й кнопки.
  switch (ev.type) {
    case REV_BUTTON:
      input_queue().push_button(ev.btn);
      break;
    case REV_TEXT:
      // Ввід тексту (напр. WiFi-пароль) — у ту саму чергу; активний застосунок
      // отримає його через App::text(field, value).
      input_queue().push_text(ev.field, ev.value);
      break;
    case REV_INDEX:
      // Прямий тап пункту меню/списку у веб — вибір за індексом.
      input_queue().push_index(ev.index);
      break;
    case REV_BACK:
      // Веб-кнопка "назад" — вихід у лаунчер (як довге утримання лівої).
      input_queue().push_back();
      break;
    case REV_PIN:
      // повторний PIN після auth — ігноруємо
      break;
    default:
      break;
  }
  return RA_NONE;
}
