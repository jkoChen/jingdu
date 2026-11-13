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
  bool shortLine = true, started = false, complete = false;
  uint32_t scanned = 0, total = 0;
  uint32_t savedAt = 0;
  const char* failure = nullptr;
  void checkpoint();
  bool resume(const Str& work, uint32_t sourceBytes);
  void finishLine();
  bool get(unsigned, Item&) const;
  static bool readItem(FILE*, unsigned, Item&);

 public:
  ~Chapters() {
    close();
  }
  void close();
  void start(const Str& path, const Str& id, EncodingInfo);
  void tick(unsigned byteBudget = 4096);
  void saveProgress() { checkpoint(); }
  bool pending() const {
    return !complete && !failure;
  }
  bool ready() const { return complete; }
  const char* error() const { return failure; }
  unsigned percent() const {
    if (complete) return 100;
    if (!total) return 0;
    return scanned >= total ? 99 : unsigned(uint64_t(scanned) * 100 / total);
  }
  unsigned size() const {
    return complete ? count : 0;
  }
  Str title(unsigned index) const;
  uint32_t anchor(unsigned index) const;
  int activeIndex(uint32_t pos) const;
  Str active(uint32_t pos) const;
};
}  // namespace jd
