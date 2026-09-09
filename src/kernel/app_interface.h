// Базовий інтерфейс застосунку esp32-os.
// Той самий контракт, що й для Berry-скриптів у Фазі 3:
// app_name / app_init / app_loop / app_draw / app_button.
// Жодних апаратних включень — файл використовується і в native-тестах.
#pragma once
#include <stdint.h>
#include <string>

// Кнопки плати: S1=13, S2=17, S3=26, S4=27, S5=25 (піни — у drivers/buttons)
enum ButtonId : uint8_t {
  BTN_S1 = 0,
  BTN_S2,
  BTN_S3,
  BTN_S4,
  BTN_S5,
  BTN_COUNT
};

class App {
public:
  virtual ~App() {}
  virtual const char* name() const = 0;
  virtual void init() = 0;             // виклик при запуску з лаунчера
  virtual void loop() = 0;             // неблокуючий, щоциклу
  virtual void draw() = 0;             // малювання у повноекранний спрайт
  virtual void button(ButtonId id) = 0; // подія з єдиної черги (кнопка чи телефон)
  virtual void text(const char* field, const char* value) { (void)field; (void)value; } // ввід тексту з телефона
  virtual void select_index(int idx) { (void)idx; } // прямий вибір пункту списку за індексом (тап у веб)
  virtual void on_exit() {}            // виклик ядром при виході (звільнити ресурси)
  virtual bool wants_exit() const { return false; } // застосунок просить вихід у лаунчер
  // Обробка «назад» (EV_BACK: футер Back / довге утримання лівої). true = застосунок
  // САМ спожив назад (лишився активним, напр. група -> назад у свій селектор);
  // false (типово) = ядро виходить у лаунчер. Робить навігацію ієрархічною.
  virtual bool handle_back() { return false; }
  // Логічний стан для дзеркалення на клієнта (JSON). Порожній рядок -> ядро шле
  // типовий {"page":"<name>"}. Застосунок може віддати багатший стан (список тощо).
  virtual std::string remote_state() { return std::string(); }
  // true -> ядро на виході з апки авто-логує її результат (items з remote_state) у SD
  // /logs/results.log. Для живих аналізаторів (WiFi/BT/Radio/EMF), що не зберігають звіт.
  virtual bool auto_snapshot() const { return false; }
};
