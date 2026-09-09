# PINGER — ping шлюзу/цілі, показує RTT і доступність. S1 = повторити, S2 = змінити ціль.
# Категорія: net. API: ping(host)->ms(-1 якщо нема), dns_resolve(host), arp_ip(0)=шлюз?

# Список цілей: шлюз мережі + публічні resolver-и (для перевірки виходу в інтернет).
var targets = ["192.168.50.1", "1.1.1.1", "8.8.8.8"]
var idx = 0
var last = -2          # -2 = ще не пінгували

def do_ping()
  last = ping(targets[idx])
end

def app_draw()
  display_clear()
  display_text(6, 2, "PINGER")
  display_text(6, 22, "Target: " + targets[idx])

  if last == -2
    display_text(6, 46, "S1 = ping")
  elif last < 0
    display_text(6, 46, "unreachable")
    display_fill_rect(6, 62, 40, 10, 0xF800)
  else
    display_text(6, 46, "RTT: " + str(last) + " ms")
    # 0..200ms -> смуга (зелена швидко, червона повільно)
    var w = last
    if w > 226 w = 226 end
    var col = 0x07E0
    if last > 60 col = 0xFD20 end
    if last > 150 col = 0xF800 end
    display_rect(6, 62, 228, 10, 0x8410)
    display_fill_rect(7, 63, w, 8, col)
  end

  display_text(6, 90, "S1=ping S2=next")
  display_text(6, 108, "S5 = exit")
end

def app_button(id)
  if id == 1
    do_ping()
  elif id == 2
    idx = (idx + 1) % size(targets)
    last = -2
  end
end
