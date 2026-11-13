#include "Chapters.h"
#include <cstring>
namespace jd {
void Chapters::close() {
  if (source) {
    fclose(source);
  }
  if (dest) {
    fclose(dest);
  }
  source = dest = nullptr;
  line.clear();
  count = lineAt = 0;
  shortLine = true;
  started = false;
  base.clear();
}
void Chapters::start(const Str& path, const Str& id, EncodingInfo enc) {
  if (started || id.empty()) {
    return;
  }
  started = true;
  Str dir = root + "/.jingdu/cache";
  if (!cacheRoom(dir, 0) || !makedirs(dir)) {
    return;
  }
  base = dir + "/toc-" + id;
  Str complete;
  if (loadRecord(base + ".done", complete) && complete == std::to_string(length(base)) &&
      length(base) % sizeof(Item) == 0) {
    count = length(base) / sizeof(Item);
    return;
  }
  source = fopen(path.c_str(), "rb");
  dest = fopen((base + ".work").c_str(), "wb");
  if (!source || !dest) {
    if (source) {
      fclose(source);
    }
    if (dest) {
      fclose(dest);
    }
    source = dest = nullptr;
    return;
  }
  encoding = enc;
  lineAt = enc.bom;
  fseek(source, lineAt, SEEK_SET);
}
void Chapters::finishLine() {
  if (!shortLine || line.empty() || count >= 20000) {
    return;
  }
  auto first = line.find_first_not_of(" \t");
  if (first == Str::npos) {
    return;
  }
  auto t = line.substr(first);
  bool heading = t == "序章" || t == "前言" || t == "后记" || lower(t).compare(0, 8, "chapter ") == 0;
  if (t.compare(0, 3, "第") == 0) {
    for (const char* s : {"章", "节", "回", "卷", "部"}) {
      auto pos = t.find(s, 3);
      if (pos != Str::npos && pos < 38) {
        heading = true;
      }
    }
  }
  if (!heading) {
    return;
  }
  Item item{};
  item.offset = lineAt;
  snprintf(item.title, sizeof item.title, "%s", t.c_str());
  item.crc = checksum(&item, offsetof(Item, crc));
  if (cacheRoom(base, sizeof item) && fwrite(&item, sizeof item, 1, dest) == 1) {
    ++count;
    fflush(dest);
  }
}
void Chapters::tick() {
  if (!source || !dest) {
    return;
  }
  for (int n = 0; n < 96; n++) {
    uint32_t cp = decode(source, encoding.enc);
    if (cp == UINT32_MAX) {
      if (!ferror(source)) {
        finishLine();
        if (durable(dest)) {
          fclose(dest);
          dest = nullptr;
          remove(base.c_str());
          if (rename((base + ".work").c_str(), base.c_str()) == 0) {
            saveRecord(base + ".done", std::to_string(length(base)));
          }
        }
      }
      if (dest) {
        fclose(dest);
      }
      fclose(source);
      source = dest = nullptr;
      return;
    }
    if (cp == 10 || cp == 13) {
      finishLine();
      line.clear();
      shortLine = true;
      lineAt = ftell(source);
      continue;
    }
    if (line.size() > 88) {
      shortLine = false;
      continue;
    }
    if (cp < 128) {
      line += char(cp);
    } else if (cp < 2048) {
      line += char(192 | (cp >> 6));
      line += char(128 | (cp & 63));
    } else if (cp < 65536) {
      line += char(224 | (cp >> 12));
      line += char(128 | ((cp >> 6) & 63));
      line += char(128 | (cp & 63));
    } else {
      shortLine = false;
    }
  }
}
bool Chapters::get(unsigned i, Item& item) const {
  if (i >= count) {
    return false;
  }
  FILE* f = fopen((dest ? base + ".work" : base).c_str(), "rb");
  if (!f) {
    return false;
  }
  bool ok = !fseek(f, i * sizeof item, SEEK_SET) && fread(&item, sizeof item, 1, f) == 1 &&
            item.crc == checksum(&item, offsetof(Item, crc));
  fclose(f);
  return ok;
}
Str Chapters::title(unsigned i) const {
  Item a{};
  return get(i, a) ? a.title : "";
}
uint32_t Chapters::anchor(unsigned i) const {
  Item a{};
  return get(i, a) ? a.offset : 0;
}
Str Chapters::active(uint32_t pos) const {
  unsigned lo = 0, hi = count;
  while (lo < hi) {
    auto i = (lo + hi) / 2;
    if (anchor(i) <= pos) {
      lo = i + 1;
    } else {
      hi = i;
    }
  }
  return lo ? title(lo - 1) : "";
}
}  // namespace jd
