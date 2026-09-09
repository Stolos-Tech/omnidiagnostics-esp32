#include "input_queue.h"
#include <string.h>

bool InputQueue::push_button(ButtonId id) {
  if (size_ >= CAPACITY) return false;
  buf_[tail_].type = EV_BUTTON;
  buf_[tail_].btn = id;
  tail_ = (tail_ + 1) % CAPACITY;
  size_++;
  return true;
}

static void copy_field(char* dst, size_t n, const char* src) {
  if (!src) { dst[0] = '\0'; return; }
  size_t i = 0;
  for (; src[i] && i < n - 1; i++) dst[i] = src[i];
  dst[i] = '\0';
}

bool InputQueue::push_text(const char* field, const char* value) {
  if (size_ >= CAPACITY) return false;
  buf_[tail_].type = EV_TEXT;
  copy_field(buf_[tail_].field, EV_FIELD_MAX, field);
  copy_field(buf_[tail_].value, EV_VALUE_MAX, value);
  tail_ = (tail_ + 1) % CAPACITY;
  size_++;
  return true;
}

bool InputQueue::push_back() {
  if (size_ >= CAPACITY) return false;
  buf_[tail_].type = EV_BACK;
  tail_ = (tail_ + 1) % CAPACITY;
  size_++;
  return true;
}

bool InputQueue::push_index(int idx) {
  if (size_ >= CAPACITY) return false;
  buf_[tail_].type = EV_INDEX;
  buf_[tail_].index = idx;
  tail_ = (tail_ + 1) % CAPACITY;
  size_++;
  return true;
}

bool InputQueue::pop(InputEvent& out) {
  if (size_ == 0) return false;
  out = buf_[head_];
  head_ = (head_ + 1) % CAPACITY;
  size_--;
  return true;
}

void InputQueue::clear() {
  head_ = tail_ = size_ = 0;
}

InputQueue& input_queue() {
  static InputQueue q;
  return q;
}
