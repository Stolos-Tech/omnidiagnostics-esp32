#include "launcher.h"

void LauncherModel::clear() {
  count_ = 0;
  cursor_ = 0;
  for (int i = 0; i < MAX_ITEMS; i++) items_[i] = nullptr;
}

bool LauncherModel::add_item(const char* name) {
  if (name == nullptr || count_ >= MAX_ITEMS) return false;
  items_[count_++] = name;
  return true;
}

const char* LauncherModel::item_name(int idx) const {
  if (idx < 0 || idx >= count_) return nullptr;
  return items_[idx];
}

void LauncherModel::move_prev() {
  if (count_ == 0) return;
  cursor_ = (cursor_ + count_ - 1) % count_;
}

void LauncherModel::move_next() {
  if (count_ == 0) return;
  cursor_ = (cursor_ + 1) % count_;
}

void LauncherModel::set_cursor(int idx) {
  if (idx >= 0 && idx < count_) cursor_ = idx;
}

int LauncherModel::select() const {
  if (count_ == 0) return -1;
  return cursor_;
}
