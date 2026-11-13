#pragma once
#include "Reader.h"
#include <ArduinoJson.h>

namespace jd {
inline constexpr int kRefreshIntervals[] = {5, 10, 20, 50, 100};
struct RecordReader {
  FILE* file;
  int read() {
    return fgetc(file);
  }
  size_t readBytes(char* out, size_t size) {
    return fread(out, 1, size, file);
  }
};
Str displayName(const Str& path);
Str serialize(const JsonDocument& doc);
bool readDoc(const Str& path, JsonDocument& doc);
bool readMarks(const Str& path, JsonDocument& doc);
Str str(JsonVariantConst value);
Anchor anchor(JsonVariantConst value);
}  // namespace jd
