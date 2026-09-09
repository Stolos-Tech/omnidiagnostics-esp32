# I2C-SCAN — сканує шину I2C (0x03..0x77), показує знайдені адреси й підказку пристрою.
# Категорія: gpio. API: i2c_probe(addr)->bool. S1 = пересканувати.

var found = []
var scanned = false

def hex(n)
  var d = "0123456789ABCDEF"
  return d[int(n / 16)] + d[n % 16]
end

def scan()
  found = []
  var a = 0x03
  while a <= 0x77
    if i2c_probe(a)
      found.push(a)
    end
    a = a + 1
  end
  scanned = true
end

# коротка підказка «що це» за типовою адресою
def hint(a)
  if a == 0x3C || a == 0x3D return "OLED" end
  if a == 0x27 || a == 0x3F return "LCD" end
  if a == 0x68 return "RTC/IMU" end
  if a == 0x76 || a == 0x77 return "BME/BMP" end
  if a == 0x48 return "ADS/temp" end
  if a == 0x50 return "EEPROM" end
  return ""
end

def app_draw()
  display_clear()
  display_text(6, 2, "I2C SCANNER")

  if !scanned
    display_text(6, 40, "S1 = scan bus")
    display_text(6, 108, "S5 = exit")
    return
  end

  if size(found) == 0
    display_text(6, 30, "no devices found")
  else
    display_text(6, 22, "found: " + str(size(found)))
    var y = 40
    var i = 0
    while i < size(found) && i < 4
      var a = found[i]
      display_text(6, y, "0x" + hex(a) + "  " + hint(a))
      y = y + 16
      i = i + 1
    end
  end
  display_text(6, 108, "S1=rescan  S5=exit")
end

def app_button(id)
  if id == 1
    scan()
  end
end
