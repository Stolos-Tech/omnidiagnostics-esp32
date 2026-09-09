// Рендер головного меню лаунчера у спрайт (Arduino/TFT-залежний шар,
// відокремлений від LauncherModel, який тестується на хості).
#pragma once
#include "launcher.h"

void launcher_draw(const LauncherModel& model);
