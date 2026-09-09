-- ESP32-OS central store (SQLite). Relational Library of networks & devices +
-- board registry + server health + phone wifi import. Additive to telemetry.db.
PRAGMA journal_mode = WAL;
PRAGMA foreign_keys = ON;

-- ---- boards physically managed by the server (USB or WiFi) --------------------
CREATE TABLE IF NOT EXISTS boards (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    board_uid     TEXT UNIQUE,            -- stable chip id / MAC
    name          TEXT,                   -- human name
    kind          TEXT DEFAULT 'esp32',   -- esp32 | uno
    transport     TEXT DEFAULT 'wifi',    -- usb | wifi
    serial_port   TEXT,                   -- COMx / /dev/ttyUSBx when usb
    baud          INTEGER DEFAULT 115200,
    url           TEXT,                   -- http base when wifi
    last_seen     INTEGER,
    state         TEXT DEFAULT 'unknown', -- online | offline | frozen | rebooting
    fw_version    TEXT,
    created_at    INTEGER
);

-- ---- NETWORK LIBRARY ----------------------------------------------------------
CREATE TABLE IF NOT EXISTS networks (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    bssid         TEXT,                   -- may be NULL for logical nets
    ssid          TEXT,
    band          TEXT,                   -- 2.4 | 5
    channel       INTEGER,
    encryption    TEXT,                   -- OPEN | WEP | WPA | WPA2 | WPA3
    hidden        INTEGER DEFAULT 0,
    vendor        TEXT,                   -- AP OUI vendor
    first_seen    INTEGER,
    last_seen     INTEGER,
    times_seen    INTEGER DEFAULT 1,
    max_rssi      INTEGER,
    min_rssi      INTEGER,
    is_twin       INTEGER DEFAULT 0,      -- evil-twin flag
    is_saved      INTEGER DEFAULT 0,      -- imported from phone / known
    notes         TEXT,
    UNIQUE(bssid, ssid)
);
CREATE INDEX IF NOT EXISTS ix_networks_ssid ON networks(ssid);

CREATE TABLE IF NOT EXISTS network_history (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    network_id    INTEGER REFERENCES networks(id) ON DELETE CASCADE,
    ts            INTEGER,
    rssi          INTEGER,
    channel       INTEGER,
    clients       INTEGER,
    source_board  INTEGER REFERENCES boards(id)
);
CREATE INDEX IF NOT EXISTS ix_nethist_net ON network_history(network_id, ts);

-- ---- DEVICE LIBRARY -----------------------------------------------------------
CREATE TABLE IF NOT EXISTS device_profiles (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    mac           TEXT UNIQUE,
    ip            TEXT,
    hostname      TEXT,
    vendor        TEXT,                   -- OUI vendor of NIC
    category      TEXT,                   -- devintel classification
    os_guess      TEXT,
    is_random_mac INTEGER DEFAULT 0,
    is_gateway    INTEGER DEFAULT 0,
    name          TEXT,                   -- user label
    trust         TEXT DEFAULT 'unknown', -- trusted | unknown | blocked
    first_seen    INTEGER,
    last_seen     INTEGER,
    times_seen    INTEGER DEFAULT 1,
    open_ports    TEXT,                   -- csv
    intel_json    TEXT,                   -- full devintel dump
    notes         TEXT
);
CREATE INDEX IF NOT EXISTS ix_dev_ip ON device_profiles(ip);

CREATE TABLE IF NOT EXISTS device_history (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    device_id     INTEGER REFERENCES device_profiles(id) ON DELETE CASCADE,
    ts            INTEGER,
    ip            TEXT,
    event         TEXT,                   -- seen | join | leave | scan | alert
    detail        TEXT,
    source_board  INTEGER REFERENCES boards(id)
);
CREATE INDEX IF NOT EXISTS ix_devhist_dev ON device_history(device_id, ts);

-- ---- relation: which device seen on which network -----------------------------
CREATE TABLE IF NOT EXISTS device_network (
    device_id     INTEGER REFERENCES device_profiles(id) ON DELETE CASCADE,
    network_id    INTEGER REFERENCES networks(id) ON DELETE CASCADE,
    first_seen    INTEGER,
    last_seen     INTEGER,
    PRIMARY KEY (device_id, network_id)
);

-- ---- phone-exported wifi credentials -----------------------------------------
CREATE TABLE IF NOT EXISTS phone_wifi (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    ssid          TEXT,
    psk           TEXT,                   -- stored server-side (tailnet-private)
    encryption    TEXT,
    source_device TEXT,                   -- phone id
    imported_at   INTEGER,
    UNIQUE(ssid)
);

-- ---- targeting: user-selected focus for tools --------------------------------
CREATE TABLE IF NOT EXISTS targets (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    kind          TEXT,                   -- device | network
    ref_id        INTEGER,                -- device_profiles.id or networks.id
    label         TEXT,
    created_at    INTEGER,
    active        INTEGER DEFAULT 1
);

-- ---- SERVER HEALTH / shutdown root-cause -------------------------------------
CREATE TABLE IF NOT EXISTS server_health (
    ts            INTEGER PRIMARY KEY,
    load1         REAL, load_pct INTEGER,
    mem_used_pct  INTEGER, mem_avail_mb INTEGER,
    disk_used_pct INTEGER,
    cpu_temp_c    REAL,
    volts_json    TEXT,                   -- any accessible voltage rails
    uptime_s      INTEGER,
    undervolt     INTEGER DEFAULT 0,      -- throttling/undervoltage flag
    services_json TEXT
);

CREATE TABLE IF NOT EXISTS shutdown_events (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    detected_at   INTEGER,                -- when server came back / logged
    boot_id       TEXT,
    prev_uptime_s INTEGER,
    clean         INTEGER,                -- 1 = graceful, 0 = sudden
    last_temp_c   REAL,
    last_load     REAL,
    suspected     TEXT,                   -- thermal | power | oom | kernel | unknown
    evidence      TEXT
);
