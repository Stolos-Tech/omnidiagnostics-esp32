# SYSMON — живий монітор стану плати: heap, температура, CPU, батарея, uptime.
# Категорія: system. API: free_heap/heap_total/sys_temp/cpu_mhz/adc_read_battery/millis.

def bar(x, y, w, frac, col)
  display_rect(x, y, w, 8, 0x8410)
  var fill = frac
  if fill < 0 fill = 0 end
  if fill > w - 2 fill = w - 2 end
  display_fill_rect(x + 1, y + 1, fill, 6, col)
end

def app_draw()
  display_clear()
  display_text(6, 2, "SYSTEM MONITOR")

  var heap = free_heap()
  var total = heap_total()
  display_text(6, 20, "Heap: " + str(int(heap / 1024)) + "K free")
  # частка ВІЛЬНОЇ памʼяті -> зелена смуга (0..226)
  bar(6, 32, 228, heap * 226 / total, 0x05FF)

  var t = sys_temp()
  display_text(6, 48, "Temp: " + str(int(t)) + "C")
  # 30..85C -> смуга; понад 65 — бурштин, понад 75 — червоний
  var col = 0x07E0
  if t > 65 col = 0xFD20 end
  if t > 75 col = 0xF800 end
  bar(6, 60, 228, (t - 30) * 226 / 55, col)

  display_text(6, 76, "CPU:  " + str(cpu_mhz()) + " MHz")

  var mv = adc_read_battery()
  display_text(6, 92, "Batt: " + str(mv) + " mV")

  var up = int(millis() / 1000)
  display_text(6, 108, "Up: " + str(int(up / 60)) + "m " + str(up % 60) + "s   S5=exit")
end

def app_button(id)
end
