# ESP32-OS server v2 — deploy (draft, DO NOT run until powered on)

New modules: db, db_schema.sql, profiles, usbctl, boardproxy, board_supervisor,
healthmon, admin_api, emergency_bot.

## config.json — new keys (add)
```json
{
  "db_path": "/opt/espos/telemetry.db",
  "supervisor_poll_s": 20,
  "supervisor_miss_limit": 3,
  "health_sample_s": 15,
  "admin_chats": [],
  "manageable_services": ["espos-gateway","espos-collector","espos-dashboard","espos-supervisor","espos-healthmon","espos-bot"],
  "editable_config_keys": ["health_sample_s","supervisor_poll_s","supervisor_miss_limit","netscan_interval_s","watch_services"],
  "watch_services": ["espos-gateway","espos-collector","espos-dashboard"]
}
```

## deps
```
python3 -m pip install pyserial psutil python-telegram-bot
```

## sudo rights (server reboot / service mgmt from API run as espos user)
Grant the dashboard/bot user NOPASSWD for the exact commands via /etc/sudoers.d/espos:
```
espos ALL=(root) NOPASSWD: /bin/systemctl reboot, /bin/systemctl poweroff, \
  /bin/systemctl start espos-*, /bin/systemctl stop espos-*, /bin/systemctl restart espos-*
```
(If services run as root, sudo not needed.)

## install
```
cp systemd/espos-*.service /etc/systemd/system/
systemctl daemon-reload
systemctl enable --now espos-healthmon espos-supervisor espos-bot
systemctl restart espos-dashboard   # picks up admin_api blueprint
python3 db.py                        # create schema
```

## API surface (all under gateway token X-Gateway-Token)
- GET  /admin/health
- GET  /admin/server/services · POST /admin/server/service?name=&action=
- POST /admin/server/reboot?confirm=yes[&mode=poweroff]
- GET/POST /admin/server/config
- GET  /admin/boards · POST /admin/boards/discover · POST /admin/boards/<id>/reboot
- ANY  /board/<id>/<path>        (App->Server->Board; USB bridged, else HTTP)
- GET  /lib/networks · /lib/networks/<id> · /lib/devices · /lib/devices/<id>
- POST /lib/devices/<id>/trust?trust= · POST /lib/target?kind=&id=&label=
- POST /admin/phone/wifi/import · GET /admin/phone/wifi
