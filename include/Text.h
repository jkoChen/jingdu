#pragma once
#include "Canvas.h"
namespace jd {
enum class Encoding { Utf8, Gb, Utf16le, Utf16be, Utf32le, Utf32be };
struct EncodingInfo {
  Encoding enc;
  unsigned bom;
};
EncodingInfo detect(FILE* file);
uint32_t decode(FILE* file, Encoding enc);
struct Layout {
  static constexpr int minSize = 18, maxSize = 30;
  bool showProgress = true;
  int bodyBottom() const {
    return showProgress ? 402 : 416;
  }
  int size = 20, margin = 16, gap = 7, tone = 1;
  bool rich = false;
};
struct Placed {
  uint32_t cp;
  uint16_t x, y;
};
struct Page {
  uint32_t start = 0, end = 0;
  bool eof = false;
  unsigned count = 0;
  Placed chars[630];
};
struct Cursor {
  int x = 0, y = 40, wordX = 0;
  long wordStart = -1, previousPosition = -1;
  unsigned wordIndex = 0;
};
bool scanPage(FILE*, EncodingInfo, uint32_t, const Layout&, BodyFont&, Page&, Cursor&, unsigned, bool&);
bool layoutPage(FILE*, EncodingInfo, uint32_t offset, const Layout&, BodyFont&, Page&);
}  // namespace jd
