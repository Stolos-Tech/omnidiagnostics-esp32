#include "line_assembler.h"
#include <string.h>

void LineAssembler::reset() {
  len_ = 0;
  overflow_ = false;
  buf_[0] = '\0';
}

bool LineAssembler::feed(char c, char* out, size_t out_size) {
  if (c == '\n' || c == '\r') {
    // кінець рядка
    bool have_line = (len_ > 0 && !overflow_);
    if (have_line && out && out_size > 0) {
      size_t n = (size_t)len_ < out_size - 1 ? (size_t)len_ : out_size - 1;
      memcpy(out, buf_, n);
      out[n] = '\0';
    }
    // скидаємо стан рядка (і прапорець overflow) для наступного
    len_ = 0;
    overflow_ = false;
    buf_[0] = '\0';
    return have_line;
  }

  if (overflow_) return false;  // рядок уже задовгий — ігноруємо до роздільника

  if (len_ >= MAX_LINE) {
    overflow_ = true;  // перевищили ліміт — відкидаємо весь рядок
    return false;
  }

  buf_[len_++] = c;
  return false;
}
