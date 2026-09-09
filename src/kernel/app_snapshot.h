// Спільний хелпер авто-снапшоту результату аналіз-апки на SD. Викликається З ДВОХ
// місць: main exit_to_launcher (топ-рівневі апки) і GroupApp при виході члена (апки
// в групах виходять у селектор групи, НЕ в лаунчер -> інакше їхній снапшот не писався б).
// Device-only (ArduinoJson + sd_store) — не в native-фільтрі.
#pragma once
class App;

// Якщо app->auto_snapshot() і SD змонтовано: парсить items з remote_state() і дописує
// у SD /logs/results.log. No-op інакше.
void app_snapshot_if_enabled(App* app);
