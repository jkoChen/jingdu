#pragma once
#include "Text.h"
namespace jd {
class Chapters {
  struct Item {
    uint32_t offset;
    char title[96];
    uint32_t crc;
  };
  FILE *source = nullptr, *dest = nullptr;
  Str base, line;
  EncodingInfo encoding{};
  uint32_t lineAt = 0, count = 0;
  bool shortLine = true, started = false;
  void finishLine();
  bool get(unsigned, Item&) const;

 public:
  ~Chapters() {
    close();
  }
  void close();
  void start(const Str& path, const Str& id, EncodingInfo);
  void tick();
  bool pending() const {
    return !started || source != nullptr;
  }
  unsigned size() const {
    return count;
  }
  Str title(unsigned index) const;
  uint32_t anchor(unsigned index) const;
  Str active(uint32_t pos) const;
};
}  // namespace jd
