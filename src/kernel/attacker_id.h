// Евристики для «Attacker Detect» (WiFi, за звичайним сканом) — чиста логіка.
// Pwnagotchi мовить beacon'и, де SSID = JSON-рядок (напр. {"name":"..","pwnd_tot":N}),
// що вкрай нетипово для легітимного AP. Evil-twin (той самий SSID з різним BSSID)
// рахується в апці. Це евристики виявлення, не доказ.
#pragma once

// true, якщо SSID схожий на pwnagotchi (JSON-рядок: починається з '{' і має лапки).
bool attacker_is_pwnagotchi_ssid(const char* ssid);
