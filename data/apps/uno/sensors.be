# Живі датчики з Arduino UNO по лінку (потенціометр / reed / IR / DHT11).
# Категорія: uno. Дані йдуть через drivers/uno_link.

def app_draw()
  display_clear()
  display_text(6, 2, "UNO SENSORS")

  var pot = uno_pot()
  display_text(6, 22, "Pot:  " + str(pot))

  # смуга заповнення за потенціометром (0..1023 -> 0..228 px)
  display_rect(6, 38, 228, 10, 0x8410)
  display_fill_rect(7, 39, pot * 226 / 1023, 8, 0x05FF)

  display_text(6, 54, "Reed: " + (uno_reed() ? "CLOSED" : "open"))

  var ir = uno_ir()
  if ir != 0
    display_text(6, 70, "IR:   " + str(ir))
  else
    display_text(6, 70, "IR:   -")
  end

  display_text(6, 86, "T: " + str(uno_temp()) + "C  H: " + str(uno_hum()) + "%")
  display_text(6, 108, "S5 = vyhid")
end

def app_button(id)
end
