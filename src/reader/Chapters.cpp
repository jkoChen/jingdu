#include "Chapters.h"
#include <cstring>
#include <climits>
#include <unistd.h>
namespace jd {
namespace {
// The previous writer stopped recording after 20000 headings, even if it scanned further.
constexpr unsigned legacyLimit = 20000;
}
void Chapters::checkpoint() {
  if (dest && !failure && lineAt > savedAt && durable(dest) &&
      saveRecord(base + ".cursor", std::to_string(lineAt) + " " + std::to_string(count) + " v2", 64)) {
    savedAt = lineAt;
  }
}
bool Chapters::resume(const Str& work, uint32_t sourceBytes) {
  Str record;
  unsigned offset, entries;
  if (!loadRecord(base + ".cursor", record, 64) ||
      sscanf(record.c_str(), "%u %u", &offset, &entries) != 2 ||
      offset < encoding.bom || offset > sourceBytes || entries > INT32_MAX / sizeof(Item) ||
      (entries >= legacyLimit && record != std::to_string(offset) + " " + std::to_string(entries) + " v2")) return false;
  dest = fopen(work.c_str(), "r+b");
  if (!dest) return false;
  const auto bytes = entries * sizeof(Item);
  Item last{};
  bool valid = length(work) >= bytes;
  if (valid && entries) {
    valid = fseek(dest, bytes - sizeof(Item), SEEK_SET) == 0 && fread(&last, sizeof last, 1, dest) == 1 &&
            last.offset < offset && last.crc == checksum(&last, offsetof(Item, crc));
  }
  if (valid && (length(work) == bytes || ftruncate(fileno(dest), bytes) == 0) &&
      fseek(dest, bytes, SEEK_SET) == 0 && !fseek(source, offset, SEEK_SET)) {
    count = entries;
    scanned = lineAt = savedAt = offset;
    return true;
  }
  fclose(dest);
  dest = nullptr;
  return false;
}
void Chapters::close() {
  checkpoint();
  if (source) {
    fclose(source);
  }
  if (dest) {
    fclose(dest);
  }
  source = dest = nullptr;
  line.clear();
  count = lineAt = savedAt = 0;
  scanned = total = 0;
  complete = false;
  failure = nullptr;
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
  base = dir + "/toc-" + id;
  Str marker;
  if (access(base.c_str(), F_OK) == 0 && loadRecord(base + ".done", marker)) {
    const uint32_t bytes = length(base);
    const Str size = std::to_string(bytes);
    if (bytes <= INT32_MAX && bytes % sizeof(Item) == 0 &&
        (marker == size + " v2" || (bytes / sizeof(Item) < legacyLimit && marker == size))) {
      count = bytes / sizeof(Item);
      complete = true;
      return;
    }
  }
  if (!makedirs(dir)) {
    failure = "章节目录写入失败";
    return;
  }
  source = fopen(path.c_str(), "rb");
  if (!source) {
    failure = "书籍读取失败";
    return;
  }
  encoding = enc;
  total = length(path);
  if (resume(base + ".work", total)) return;
  dest = fopen((base + ".work").c_str(), "wb");
  if (!dest) {
    failure = "章节目录写入失败";
    return;
  }
  scanned = lineAt = enc.bom;
}
void Chapters::finishLine() {
  if (!shortLine || line.empty()) {
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
  if (count >= INT32_MAX / sizeof(Item)) {
    failure = "章节目录超过支持范围";
    return;
  }
  Item item{};
  item.offset = lineAt;
  snprintf(item.title, sizeof item.title, "%s", t.c_str());
  item.crc = checksum(&item, offsetof(Item, crc));
  if (cacheRoom(base, sizeof item) && fwrite(&item, sizeof item, 1, dest) == 1) {
    ++count;
    // Periodic checkpoints flush the buffered records and publish the matching source offset.
  } else {
    failure = "章节目录写入失败";
  }
}
void Chapters::tick(unsigned byteBudget) {
  if (!source || !dest || failure) return;
  TextBytes in(source, scanned, 120);
  const auto start = scanned;
  while (scanned - start < byteBudget) {
    uint32_t cp = in.decode(encoding.enc);
    scanned = in.tell();
    if (in.failed()) {
      failure = "书籍读取失败";
      return;
    }
    if (cp == UINT32_MAX) {
      if (scanned != total) {
        failure = "书籍读取失败";
        return;
      }
      finishLine();
      if (failure) return;
      bool ok = durable(dest);
      ok = fclose(dest) == 0 && ok;
      dest = nullptr;
      fclose(source);
      source = nullptr;
      if (!ok || (access(base.c_str(), F_OK) == 0 && remove(base.c_str())) ||
          rename((base + ".work").c_str(), base.c_str()) ||
          !saveRecord(base + ".done", std::to_string(count * sizeof(Item)) + " v2")) {
        failure = "章节目录写入失败";
        return;
      }
      complete = true;
      return;
    }
    if (cp == 10 || cp == 13) {
      finishLine();
      if (failure) return;
      line.clear();
      shortLine = true;
      lineAt = scanned;
      if (lineAt - savedAt >= 65536) checkpoint();
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
  if (i >= size()) return false;
  FILE* f = fopen(base.c_str(), "rb");
  if (!f) return false;
  const bool ok = readItem(f, i, item);
  fclose(f);
  return ok;
}
bool Chapters::readItem(FILE* file, unsigned i, Item& item) {
  return !fseek(file, i * sizeof(Item), SEEK_SET) && fread(&item, sizeof item, 1, file) == 1 &&
         item.crc == checksum(&item, offsetof(Item, crc));
}
Str Chapters::title(unsigned i) const {
  Item a{};
  return get(i, a) ? a.title : "";
}
uint32_t Chapters::anchor(unsigned i) const {
  Item a{};
  return get(i, a) ? a.offset : 0;
}
int Chapters::activeIndex(uint32_t pos) const {
  if (!size()) return -1;
  FILE* file = fopen(base.c_str(), "rb");
  if (!file) return -1;
  Item item{};
  unsigned lo = 0, hi = count;
  while (lo < hi) {
    auto i = (lo + hi) / 2;
    if (!readItem(file, i, item)) {
      fclose(file);
      return -1;
    }
    if (item.offset <= pos) {
      lo = i + 1;
    } else {
      hi = i;
    }
  }
  fclose(file);
  return int(lo) - 1;
}
Str Chapters::active(uint32_t pos) const {
  int i = activeIndex(pos);
  return i < 0 ? Str() : title(unsigned(i));
}
}  // namespace jd
