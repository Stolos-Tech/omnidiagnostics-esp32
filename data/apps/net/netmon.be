# NETMON — живий стан WiFi: IP, RSSI (dBm) зі шкалою якості, сусіди в ARP-таблиці.
# Категорія: net. API: wifi_ip/wifi_rssi/arp_count.

def app_draw()
  display_clear()
  display_text(6, 2, "NET MONITOR")

  display_text(6, 20, "IP: " + wifi_ip())

  var r = wifi_rssi()
  display_text(6, 40, "RSSI: " + str(r) + " dBm")
  # -90..-30 dBm -> 0..226 px; колір за якістю
  var q = (r + 90) * 226 / 60
  if q < 0 q = 0 end
  if q > 226 q = 226 end
  var col = 0xF800
  if r > -75 col = 0xFD20 end
  if r > -60 col = 0x07E0 end
  display_rect(6, 52, 228, 10, 0x8410)
  display_fill_rect(7, 53, q, 8, col)

  var quality = "poor"
  if r > -75 quality = "ok" end
  if r > -60 quality = "good" end
  if r > -45 quality = "excellent" end
  display_text(6, 68, "Link: " + quality)

  display_text(6, 88, "ARP hosts: " + str(arp_count()))
  display_text(6, 108, "S5 = exit")
end

def app_button(id)
end
