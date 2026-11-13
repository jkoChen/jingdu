#include "ViewData.h"

namespace jd {
Str displayName(const Str& path) {
  Str name = leaf(path);
  auto ext = name.rfind('.');
  if (ext != Str::npos && ext != 0) {
    name.resize(ext);
  }
  return name;
}
Str serialize(const JsonDocument& d) {
  Str s;
  serializeJson(d, s);
  return s;
}
bool readDoc(const Str& p, JsonDocument& d) {
  bool present;
  FILE* f = openRecord(p, 16384, present);
  if (!f) {
    return !present;
  }
  RecordReader input{f};
  int first = fgetc(f);
  bool ok = !ferror(f);
  if (first != EOF) {
    ungetc(first, f);
    ok = !deserializeJson(d, input);
    int c;
    while (ok && (c = fgetc(f)) != EOF) {
      ok = c == ' ' || c == '\t' || c == '\n' || c == '\r';
    }
    ok = ok && !ferror(f);
  }
  fclose(f);
  if (!ok) {
    d.clear();
  }
  return ok;
}
bool readMarks(const Str& path, JsonDocument& doc) {
  return readDoc(path, doc) && (doc.isNull() || doc.is<JsonArray>());
}
Str str(JsonVariantConst v) {
  const char* s = v.as<const char*>();
  return s ? s : "";
}
Anchor anchor(JsonVariantConst v) {
  return {v["chapter"] | 0u, v["byte"] | 0u};
}
}  // namespace jd
