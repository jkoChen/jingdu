#include "Epub.h"
#ifdef ARDUINO
#include <Arduino.h>
#endif
#include "Canvas.h"
#include "HeapRoom.h"
#include <render/ImageRenderer.h>
#include <epub/ImageProbe.h>
#include <cstring>
#include <new>
#include <fcntl.h>
#include <unistd.h>
#include <algorithm>
using namespace freeink::book;
namespace jd {
void Epub::useHistory(const Str& path) {
  releaseHistory();
  history = fopen(path.c_str(), "w+b");
  if (history) {
    historyPath = path;
  }
}
void Epub::releaseHistory() {
  if (!history) {
    return;
  }
  fclose(history);
  history = nullptr;
  remove(historyPath.c_str());
  historyPath.clear();
}
Epub::~Epub() {
  sax.close();
  releaseHistory();
  if (output) {
    fclose(output);
  }
  if (zip >= 0) {
    ::close(zip);
  }
  if (!part.empty()) {
    remove(part.c_str());
  }
}
int32_t Epub::readAt(uint64_t pos, void* dst, uint32_t n) {
  if (pos > INT32_MAX || (zipPosition != pos && lseek(zip, pos, SEEK_SET) < 0)) {
    zipPosition = UINT64_MAX;
    return -1;
  }
  const auto got = ::read(zip, dst, n);
  if (got < 0) {
    zipPosition = UINT64_MAX;
    return -1;
  }
  zipPosition = pos + got;
  return int(got);
}
bool Epub::acquireWork() {
  constexpr size_t bytes = 16 * 1024;
  if (!workBuf) {
    workBuf.reset(new (std::nothrow) uint8_t[bytes]);
  }
  if (!workBuf) {
    return false;
  }
  work.init(workBuf.get(), bytes);
  return true;
}
void Epub::releaseWork() {
  work.init(nullptr, 0);
  workBuf.reset();
}
bool Epub::open(const Str& path, const Str& id, bool* needsCatalog) {
  total = length(path);
  zip = ::open(path.c_str(), O_RDONLY);
  zipPosition = 0;
  error.clear();
  if (zip < 0 || !total) {
    error = "EPUB 文件为空或读取失败";
    return false;
  }
  dir = root + "/.jingdu/cache";
  token = id;
  uint32_t offset, bytes;
  uint16_t entries = 0;
#ifdef ARDUINO
  const uint32_t locateStart = millis();
#endif
  auto status = ZipCatalog::locateCentralDirectory(*this, &offset, &bytes, &entries);
#ifdef ARDUINO
  Serial.printf("JD_OPEN_PART locate_ms=%u entries=%u\n", millis() - locateStart, entries);
#endif
  if (status == BookStatus::Ok && !acquireWork()) {
    status = BookStatus::OutOfMemory;
  }
  // A small archive may already have a disk index for a long navigation document.
  // Reopening it should not parse the whole navigation document to rediscover that.
  bool useCatalog = entries > 48 || access((dir + "/catalog2-" + key(path).substr(0, 8)).c_str(), F_OK) == 0;
  if (status == BookStatus::Ok && !useCatalog) {
    size_t capacity = 4096 + 200 * entries;
    metaBuf.reset(new (std::nothrow) uint8_t[capacity]);
    if (metaBuf) {
      meta.init(metaBuf.get(), capacity);
      status = book.open(*this, meta, work);
    } else {
      status = BookStatus::OutOfMemory;
    }
  }
  if (workBuf && (status == BookStatus::OutOfMemory || (status == BookStatus::Ok && useCatalog))) {
    metaBuf.reset();
    work.reset();
    status = openCatalog(path, entries, needsCatalog);
  }
#ifdef ARDUINO
  Serial.printf("JD_EPUB entries=%u indexed=%d status=%s meta=%u work=%u free=%u largest=%u\n", entries,
                catalog.isOpen(), bookStatusName(status), unsigned(meta.highWater()),
                unsigned(work.highWater()), ESP.getFreeHeap(), ESP.getMaxAllocHeap());
#endif
  if (status != BookStatus::Ok) {
    if (error.empty()) {
      error = status == BookStatus::OutOfMemory ? "EPUB 索引内存不足" : Str("EPUB ") + bookStatusName(status);
    }
    return false;
  }
  releaseWork();
  return count() > 0;
}
BookStatus Epub::openCatalog(const Str& path, unsigned entries, bool* needsCatalog) {
  if (!makedirs(dir)) {
    return BookStatus::IoError;
  }
  auto target = dir + "/catalog2-" + key(path).substr(0, 8);
  catalogFile.setPath(target);
  const bool split = entries > 256;
  // Only reuse a full directory check in this mount after confirming the
  // file stamp. Card removal/upload invalidates every proof via its epoch.
  struct Proof {
    Str stamp;
    unsigned epoch = 0;
    uint32_t crc = 0;
  };
  static Proof recent[3];
  static unsigned next = 0;
  // A known identity is already bound to this mounted path; avoid a second
  // tail seek into a large archive. On an unverified path use sampled proof.
  const Str sourceStamp = token.empty() ? fileStamp(path) : key(path + ":" + token);
  const unsigned epoch = fileIdentityEpoch();
  const uint32_t* checked = nullptr;
  for (auto& proof : recent) {
    if (!sourceStamp.empty() && proof.epoch == epoch && proof.stamp == sourceStamp) {
      checked = &proof.crc;
    }
  }
  auto load = [&]() {
    size_t bytes = 0;
    auto st = BookCatalog::residentBytes(catalogFile, &bytes, split);
    if (st != BookStatus::Ok) {
      return st;
    }
    if (bytes == 0) {
      return BookStatus::Stale;
    }
    if (bytes > 128 * 1024) {
      error = "此书目录的常驻数据超过支持范围";
      return BookStatus::Unsupported;
    }
    if (!byteHeapRoom(bytes, 0)) {
      error = "当前可用连续内存不足以读取目录";
      return BookStatus::OutOfMemory;
    }
    metaBuf.reset(new (std::nothrow) uint8_t[bytes]);
    if (!metaBuf) {
      return BookStatus::OutOfMemory;
    }
    meta.init(metaBuf.get(), bytes);
#ifdef ARDUINO
    const uint32_t started = millis();
#endif
    auto result = catalog.open(*this, catalogFile, meta, work, split, checked);
#ifdef ARDUINO
    Serial.printf("JD_OPEN_PART catalog_ms=%u proof=%s resident=%u status=%s\n", millis() - started,
                  checked ? "mounted" : "full", unsigned(bytes), bookStatusName(result));
#endif
    if (result == BookStatus::Ok && !checked && !sourceStamp.empty())
      recent[next++ % 3] = {sourceStamp, epoch, catalog.fingerprint()};
    return result;
  };
  if (catalogFile.exists("")) {
    const auto cached = load();
    if (cached != BookStatus::Stale && cached != BookStatus::NotFound) {
      return cached;
    }
  }
  // ZIP64 and unrepresentable counts are rejected by the ZIP parser. For
  // ordinary ZIPs the merge work is on TF, so count alone is not a RAM limit.
  if (needsCatalog) {
    *needsCatalog = true;
    return BookStatus::NotFound;  // Caller puts the cold-index status on screen before building.
  }
  checked = nullptr;  // a replacement must verify the newly published footer
  const size_t estimatedDiskBytes = 8192 + (entries > 1024 ? 128u : 40u) * entries;
  const size_t bytes = split ? 8192u : estimatedDiskBytes;
  if (!cacheRoom(target, estimatedDiskBytes)) {
    error = "存储卡缓存空间不足";
    return BookStatus::IoError;
  }
  metaBuf.reset();
  metaBuf.reset(new (std::nothrow) uint8_t[bytes]);
  if (!metaBuf) {
    return BookStatus::OutOfMemory;
  }
  meta.init(metaBuf.get(), bytes);
  work.reset();
  // OPF and NCX passes run serially. Reuse one 32 KiB TF ring during the
  // cold build, leaving data RAM available to Expat and Wi-Fi.
  useHistory(target + ".inflate");
  // Keep the common small-directory path in RAM. Large ZIPs spill the two
  // lookup tables before allocating storage for spine resolution and Expat.
  const bool spill = entries > 1024;
  const Str workPath = target + ".tables";
  FILE* tables = spill ? fopen(workPath.c_str(), "w+b") : nullptr;
  auto st = spill && !tables ? BookStatus::IoError
                             : BookCatalog::build(*this, catalogFile, meta, &work, split, tables);
  if (tables) {
    fclose(tables);
    remove(workPath.c_str());
  }
#ifdef ARDUINO
  Serial.printf("JD_CATALOG_BUILD entries=%u split=%d spill=%d arena=%u peak=%u refused=%u status=%s\n",
                entries, split, spill, unsigned(bytes), unsigned(meta.highWater()),
                unsigned(meta.failedAllocSize()), bookStatusName(st));
#endif
  releaseHistory();
  metaBuf.reset();
  work.reset();
  return st == BookStatus::Ok ? load() : st;
}
Str Epub::chapterProof(unsigned i) {
  ZipEntry entry{};
  Str path;
  if (catalog.isOpen()) {
    char href[512]{};
    if (catalog.spineHref(i, href, sizeof href) != BookStatus::Ok ||
        catalog.spineEntry(i, &entry) != BookStatus::Ok) {
      return {};
    }
    path = href;
  } else {
    auto item = book.spineItem(i);
    auto resource = item ? book.zip().find(item->href) : nullptr;
    if (!resource) {
      return {};
    }
    path = item->href;
    entry = *resource;
  }
  uint8_t header[30]{};
  if (readAt(entry.localHeaderOffset, header, sizeof header) != sizeof header ||
      memcmp(header, "PK\x03\x04", 4)) {
    return {};
  }
  return key(path + "\n" + std::to_string(entry.uncompressedSize) + " " +
             std::to_string(entry.compressedSize) +
             Str(reinterpret_cast<const char*>(header), sizeof header));
}
int Epub::tocForSpine(unsigned i) const {
  if (i >= count()) {
    return -1;
  }
  if (catalog.isOpen()) {
    return catalog.tocCount() ? catalog.tocIndexForSpine(i) : int(i);
  }
  if (!book.tocCount()) {
    return int(i);
  }
  // Small in-memory books only. Prefer the first target in the closest preceding spine.
  for (int s = int(i); s >= 0; --s) {
    auto item = book.spineItem(s);
    if (!item) {
      continue;
    }
    for (unsigned n = 0; n < book.tocCount(); n++) {
      auto t = book.tocEntry(n);
      if (t && t->href && !strcmp(t->href, item->href)) {
        return int(n);
      }
    }
  }
  return -1;
}
Str Epub::title(unsigned i) const {
  if (catalog.isOpen()) {
    int toc = catalog.tocIndexForSpine(i);
    if (toc >= 0) {
      return tocName(toc);
    }
    char href[192]{};
    return catalog.spineHref(i, href, sizeof href) == BookStatus::Ok ? leaf(href) : Str();
  }
  auto item = book.spineItem(i);
  if (!item) {
    return {};
  }
  for (unsigned n = 0; n < book.tocCount(); n++) {
    auto t = book.tocEntry(n);
    if (t && t->href && !strcmp(t->href, item->href) && *t->title) {
      return t->title;
    }
  }
  return leaf(item->href);
}
static const char* local(const char* n) {
  auto colon = strrchr(n, ':');
  return colon ? colon + 1 : n;
}
static bool block(const char* n) {
  return !strcmp(n, "p") || !strcmp(n, "div") || !strcmp(n, "li") || !strcmp(n, "br") ||
         !strcmp(n, "blockquote") || (n[0] == 'h' && n[1] >= '1' && n[1] <= '6' && !n[2]);
}
void Epub::emit(const char* p, size_t n) {
  if (failed || !n) {
    return;
  }
  if (window) {
    uint32_t from = std::max(written, windowStart), to = std::min<uint32_t>(written + n, windowStart + win);
    if (to > from) {
      memcpy(window.get() + from - windowStart, p + from - written, to - from);
      held = to - windowStart;
    }
    written += n;
    tail = p[n - 1];
    return;
  }
  if (written + n > 8 * 1024 * 1024 || fwrite(p, 1, n, output) != n) {
    failed = true;
    stopParse = true;
    return;
  }
  written += n;
  tail = p[n - 1];
}
void Epub::newline() {
  if (tail != '\n') {
    emit("\n", 1);
  }
}
void Epub::onStartElement(const char* q, const char** at) {
  auto n = local(q);
  if (!wantedFragment.empty()) {
    for (int j = 0; at && at[j]; j += 2) {
      if ((!strcmp(local(at[j]), "id") || !strcmp(local(at[j]), "name")) && wantedFragment == at[j + 1]) {
        foundAt = written;
      }
    }
  }
  if (!strcmp(n, "body")) {
    ++body;
  }
  if (!strcmp(n, "style") || !strcmp(n, "script")) {
    ++hidden;
  }
  if (!body || hidden) {
    return;
  }
  if (!strcmp(n, "b") || !strcmp(n, "strong")) {
    ++bold;
  }
  if (!strcmp(n, "em") || !strcmp(n, "i")) {
    ++italic;
  }
  if (block(n)) {
    newline();
  }
  if (!strcmp(n, "img") || !strcmp(n, "image")) {
    Str href, alt;
    for (int i = 0; at && at[i]; i += 2) {
      auto k = local(at[i]);
      const char* value = at[i + 1];
      if ((!strcmp(k, "src") || !strcmp(k, "href")) && strnlen(value, 512) < 512 &&
          strncmp(value, "data:", 5)) {
        href = value;
      } else if (!strcmp(k, "alt")) {
        size_t n = strnlen(value, 256);
        if (n == 256) {
          while (n && (uint8_t(value[n]) & 0xc0) == 0x80) {
            --n;
          }
        }
        alt.assign(value, n);
      }
    }
    Str absolute = parent(chapterHref) + "/" + href;
    Str resolved;
    size_t from = 0;
    while (from < absolute.size()) {
      auto end = absolute.find('/', from);
      auto step = absolute.substr(from, end - from);
      if (step == "..") {
        resolved = parent(resolved);
      } else if (!step.empty() && step != ".") {
        resolved = resolved.empty() ? step : resolved + "/" + step;
      }
      if (end == Str::npos) {
        break;
      }
      from = end + 1;
    }
    int index = -1;
    if (catalog.isOpen()) {
      index = catalog.entryIndex(resolved.c_str());
    } else {
      for (unsigned i = 0; i < book.manifestCount(); ++i) {
        if (resolved == book.manifestItem(i)->href) {
          index = i;
          break;
        }
      }
    }
    if (index >= 0 && index <= 65535) {
      char marker[8];
      snprintf(marker, sizeof marker, "\x1b%04x", unsigned(index));
      newline();
      emit(marker, 5);
      newline();
      return;
    }
    if (alt.empty()) {
      alt = "[图]";
    }
    emit(alt.data(), alt.size());
  }
}

void Epub::onEndElement(const char* q) {
  auto n = local(q);
  if ((!strcmp(n, "b") || !strcmp(n, "strong")) && bold) {
    --bold;
  }
  if ((!strcmp(n, "i") || !strcmp(n, "em")) && italic) {
    --italic;
  }
  if (body && !hidden && block(n)) {
    newline();
  }
  if (!strcmp(n, "body") && body) {
    --body;
  }
  if ((!strcmp(n, "style") || !strcmp(n, "script")) && hidden) {
    --hidden;
  }
}
void Epub::richText(const char* s, size_t n) {
  if (!bold && !italic) {
    emit(s, n);
    return;
  }
  char chunk[192];
  size_t count = 0;
  for (unsigned i = 0; i < n; i++) {
    if (count > sizeof(chunk) - 4) {
      emit(chunk, count);
      count = 0;
    }
    if ((uint8_t(s[i]) & 192) != 128) {
      chunk[count++] = 29;
      chunk[count++] = '0' + (bold ? 1 : 0) + (italic ? 2 : 0);
    }
    chunk[count++] = s[i];
  }
  emit(chunk, count);
}
void Epub::onText(const char* s, int n) {
  if (!body || hidden) {
    return;
  }
  int lo = 0;
  for (int i = 0; i < n; i++) {
    if (static_cast<unsigned char>(s[i]) <= 32) {
      richText(s + lo, i - lo);
      if (tail != ' ' && tail != '\n') {
        emit(" ", 1);
      }
      lo = i + 1;
    }
  }
  richText(s + lo, n - lo);
}
void Epub::stopChapter() {
  sax.close();
  releaseHistory();
  releaseWork();
  if (output) {
    fclose(output);
    output = nullptr;
  }
  if (!part.empty()) {
    remove(part.c_str());
  }
  part.clear();
}
Str Epub::textSlot(unsigned spine) const {
  return dir + "/chapter-" + key(token + ":" + std::to_string(spine) +
                                 (catalog.isOpen() ? ":catalog-v2" : ":text-v4")).substr(0, 2);
}
void Epub::restoreText(const TextSnapshot& saved) {
  stopChapter();
  selected = saved.spine;
  written = saved.bytes;
  ready = textPath = saved.file;
  window.reset();
  windowStart = held = 0;
  complete = true;
  failed = stopParse = false;
}
bool Epub::chapter(unsigned i, bool stageForFont) {
  stopChapter();
  selected = i;
  window.reset();
  windowStart = held = 0;
  work.reset();
  complete = failed = stopParse = false;
  body = hidden = bold = italic = 0;
  written = 0;
  tail = '\n';
  const ZipEntry* entry = nullptr;
  if (catalog.isOpen()) {
    char href[512]{};
    if (catalog.spineEntry(i, &chapterEntry) == BookStatus::Ok &&
        catalog.spineHref(i, href, sizeof href) == BookStatus::Ok) {
      chapterHref = href;
      entry = &chapterEntry;
    }
  } else if (auto item = book.spineItem(i)) {
    chapterHref = item->href;
    entry = book.zip().find(item->href);
  }
  if (!entry || entry->uncompressedSize > 16 * 1024 * 1024) {
    error = "章节过大或缺失";
    return false;
  }
  Str id = key(token + ":" + std::to_string(i) + (catalog.isOpen() ? ":catalog-v2" : ":text-v4"));
  chapterId = id;
  ready = dir + "/chapter-" + id.substr(0, 2);
  stamp = ready + ".stamp";
  part = ready + ".part";
  Str record;
#ifdef ARDUINO
  const uint32_t stampStart = millis();
#endif
  if (wantedFragment.empty() && !forceWindow && !token.empty() && loadRecord(stamp, record) &&
      record.size() == 129 && record.substr(0, 64) == id && digest(ready) == record.substr(65)) {
    complete = true;
    textPath = ready;
    written = length(ready);
#ifdef ARDUINO
    Serial.printf("JD_OPEN_PART chapter=%u reused=1 verify_ms=%u bytes=%u\n", i, millis() - stampStart,
                  written);
#endif
    return true;
  }
  if (forceWindow || !cacheRoom(part, std::min<uint32_t>(8u << 20, entry->uncompressedSize)) ||
      !makedirs(dir) || (output = fopen(part.c_str(), "w+b")) == nullptr) {
    window.reset(new (std::nothrow) char[win]);
    if (!window) {
      error = "无空间展开当前章";
      return false;
    }
  }
  chapterBytes = entry->uncompressedSize;
  if (stageForFont && output && entry->method == 8 && chapterBytes > 4096) {
    useHistory(part + ".inflate");
  }
  if (!acquireWork()) {
    error = "章节解码内存不足";
    return false;
  }
  auto result = sax.open(*this, *entry, work, *this, true);
  if (result != BookStatus::Ok) {
    error = bookStatusName(result);
    return false;
  }
  textPath = part;
  return true;
}
bool Epub::ensure(uint32_t want, unsigned maxSteps, bool measure) {
  if (window && !measure) {
    uint32_t start = want > 4096 ? want - 4096 : 0;
    if (start < windowStart || start > windowStart + held ||
        (written > windowStart + held && start + 4096 > windowStart + held)) {
      forceWindow = true;
      if (!chapter(selected)) {
        return false;
      }
      forceWindow = false;
      windowStart = start;
    } else if (start > windowStart) {
      unsigned drop = start - windowStart;
      memmove(window.get(), window.get() + drop, held - drop);
      held -= drop;
      windowStart = start;
    }
  }
  if (complete) {
    return true;
  }
  for (unsigned i = 0; i < maxSteps && written < want; i++) {
    bool end = false;
    auto result = sax.feedChunk(&end);
    if (result != BookStatus::Ok || failed) {
      error = result == BookStatus::OutOfMemory ? "章节解码内存不足" : "章节解码失败";
      return false;
    }
    if (end) {
      sax.close();
      releaseHistory();
      releaseWork();
      if (window) {
        complete = true;
        return true;
      }
      if (!durable(output)) {
        error = "章节写入失败";
        return false;
      }
      fclose(output);
      output = nullptr;
      remove(ready.c_str());
      if (rename(part.c_str(), ready.c_str())) {
        error = "缓存提交失败";
        return false;
      }
      auto hash = digest(ready);
      if (!token.empty() && !hash.empty()) {
        saveRecord(stamp, chapterId + " " + hash);
      }
      complete = true;
      textPath = ready;
      return true;
    }
  }
  if (output && fflush(output)) {
    error = "TF 写入失败";
    return false;
  }
  return true;
}
Str Epub::tocName(unsigned i) const {
  if (catalog.isOpen()) {
    if (!catalog.tocCount()) {
      return title(i);
    }
    BookCatalog::TocItem item;
    char name[160]{}, fragment[4]{};
    return catalog.tocItem(i, &item, name, sizeof name, fragment, sizeof fragment) == BookStatus::Ok ? name
                                                                                                     : "";
  }
  if (!book.tocCount()) {
    return title(i);
  }
  auto t = book.tocEntry(i);
  return t ? t->title : "";
}
bool Epub::target(unsigned i, unsigned& spine, Str& fragment) const {
  fragment.clear();
  if (catalog.isOpen()) {
    if (!catalog.tocCount()) {
      spine = i;
      return i < count();
    }
    BookCatalog::TocItem item;
    char f[192]{}, title[4]{};
    if (catalog.tocItem(i, &item, title, sizeof title, f, sizeof f) != BookStatus::Ok ||
        item.spineIndex < 0) {
      return false;
    }
    spine = item.spineIndex;
    fragment = f;
    return true;
  }
  if (!book.tocCount()) {
    spine = i;
    return i < count();
  }
  auto t = book.tocEntry(i);
  if (!t) {
    return false;
  }
  for (unsigned n = 0; n < count(); n++) {
    if (!strcmp(t->href, book.spineItem(n)->href)) {
      spine = n;
      fragment = t->fragment ? t->fragment : "";
      return true;
    }
  }
  return false;
}
bool Epub::image(unsigned index, Canvas& canvas, int y, int maxWidth) {
  ZipEntry indexed;
  const ZipEntry* entry = nullptr;
  if (catalog.isOpen()) {
    if (catalog.entryAt(index, &indexed) == BookStatus::Ok) {
      entry = &indexed;
    }
  } else if (auto item = book.manifestItem(index)) {
    entry = book.zip().find(item->href);
  }
  if (!entry || entry->uncompressedSize > (4u << 20)) {
    return false;
  }
  std::unique_ptr<uint8_t[]> buffer;
  Arena temp;
  // Image decoding is optional. Keep enough byte-addressable heap for
  // navigation, fonts and saving the reading position after the panel refresh.
  size_t cap = entry->method == 0 ? 20 * 1024 : 26 * 1024;
  if (!byteHeapRoom(cap, 22 * 1024)) {
    return false;
  }
  buffer.reset(new (std::nothrow) uint8_t[cap]);
  if (!buffer) {
    return false;
  }
  temp.init(buffer.get(), cap);
  auto* scratch = &temp;
  ImageInfo info{};
  if (probeImage(*this, *entry, *scratch, &info) != BookStatus::Ok || !info.width || !info.height ||
      info.width > 2048 || info.height > 2048) {
    return false;
  }
  int w = std::min<int>(maxWidth, info.width), h = std::max(1, int(info.height) * w / info.width);
  if (h > 88) {
    h = 88;
    w = std::max(1, int(info.width) * h / info.height);
  }
  PageImage image{"", 0, 0, uint16_t(w), uint16_t(h)};
  struct Blit {
    Canvas* c;
    int x, y;
  } blit{&canvas, (240 - w) / 2, y};
  auto row = [](void* ctx, uint16_t line, const uint8_t* gray, uint16_t width) {
    auto& b = *static_cast<Blit*>(ctx);
    uint8_t bits[30]{};
    if (width > 240) {
      return false;
    }
    ditherRowOrdered(gray, width, line, bits);
    for (unsigned x = 0; x < width; x++) {
      b.c->dot(b.x + x, b.y + line, (bits[x / 8] & (128u >> (x % 8))) != 0);
    }
    return true;
  };
  return ImageRenderer::renderEntry(*this, *entry, image, *scratch, row, &blit) == BookStatus::Ok;
}
FILE* Epub::openText() {
  if (output) {
    if (fflush(output) || fseek(output, 0, SEEK_SET)) {
      return nullptr;
    }
    return output;
  }
  return window ? fmemopen(window.get(), held, "rb") : fopen(textPath.c_str(), "rb");
}
bool Epub::closeText(FILE* stream) {
  if (stream != output) {
    return fclose(stream) == 0;
  }
  bool ok = !ferror(stream);
  clearerr(stream);
  if (!ok || fseek(stream, 0, SEEK_END)) {
    failed = true;
    error = "章节缓存读取失败";
    return false;
  }
  return true;
}

}  // namespace jd
