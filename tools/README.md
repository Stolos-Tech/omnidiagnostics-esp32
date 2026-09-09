# tools — службові скрипти розробки (на ПК, не на платі)

## esp_ws.py
Мінімальний WebSocket-клієнт до веб-інтерфейсу плати **без зовнішніх залежностей**
(лише стандартна бібліотека). Дає програмний доступ до тих самих команд, що й
веб-сторінка: авторизація PIN, навігація меню, читання дзеркаленого стану.

```python
import sys; sys.path.insert(0, "tools")
from esp_ws import WS

w = WS("192.168.50.53")          # IP плати в домашній мережі
w.send({"pin": "036163"})          # PIN з екрана плати
print(w.recv_json(lambda m: "page" in m))

w.send({"back": True})             # у лаунчер
w.send({"idx": 15})                # запустити застосунок за індексом
w.send({"btn": "S2"})              # кнопка (S1..S5)
w.send({"text": "host", "value": "example.com"})   # текстове поле
```

Запускати інтерпретатором PlatformIO (у системному Python може не бути pyserial
для супутніх скриптів):
`~/.platformio/penv/Scripts/python.exe script.py`

## Провізіонінг WiFi без телефона
Плата має службову консоль на USB-Serial (`kernel/serial_console.cpp`), 115200:

```
wifi <ssid>|<password>    # під'єднатись і запам'ятати мережу
remote wifi | remote off  # увімкнути/вимкнути віддалений режим
nets                      # список збережених мереж
ip                        # поточний IP/статус STA
help
```

Креденшали йдуть у NVS і **ніколи не потрапляють у код** (тож не витікають у git).
