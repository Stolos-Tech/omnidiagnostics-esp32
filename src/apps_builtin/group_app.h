// GroupApp — контейнер, що об'єднує кілька споріднених інструментів під ОДНИМ
// пунктом меню з вибором режиму. Тримає вказівники на вже існуючі під-апки й
// делегує їм усе (init/loop/draw/button/text/remote_state) — логіку під-апок
// не переписуємо, їхнє веб-дзеркалення (сторінки net_scan/http_get/...) працює
// як раніше. Додається лише шар селектора режиму + пункт "< Back".
#pragma once
#include "../kernel/app_interface.h"

class GroupApp : public App {
public:
  struct Member { App* app; const char* label; };
  // page — імʼя сторінки селектора для веб-дзеркалення/HELP (напр. "discover").
  void configure(const char* name, const char* page, const Member* members, int count);

  const char* name() const override { return name_; }
  void init() override;
  void loop() override;
  void draw() override;
  void button(ButtonId id) override;
  void select_index(int idx) override;
  void text(const char* field, const char* value) override;
  void on_exit() override;
  bool handle_back() override;
  bool wants_exit() const override { return wants_exit_; }
  std::string remote_state() override;

private:
  const char*   name_    = "Group";
  const char*   page_    = "group";
  const Member* members_ = nullptr;
  int  count_  = 0;
  int  mode_   = -1;     // -1 = селектор режиму; інакше індекс активного члена
  int  cursor_ = 0;      // курсор у селекторі (0..count_, останнє = "< Back")
  bool wants_exit_ = false;
  void enter(int i);
};
