#include "Reader.h"
#include "HeapRoom.h"
#ifdef ARDUINO
#include <Arduino.h>
#endif
#include <algorithm>
#include <cstring>
#include <new>
namespace jd {
void Reader::close() {
  font.close();
  closeBook();
}
void Reader::cancelAhead() {
  preText.reset();
  canPrefetch = started = scanned = false;
  drawn = 0;
  prefetchCompleteInk = true;
  preCursor = {};
  nextExcerpt.clear();
}
void Reader::closeBook() {
  font.sizeHotCache(false);
  cancelAhead();
  nextFrame.reset();
  suspendedFont.clear();
  layoutReady = false;
  warmCursor = {};
  txtChapters.close();
  verification.reset();
  epub.reset();
  id.clear();
  path.clear();
  textPath.clear();
  cacheBasis.clear();
  layoutId.clear();
  indexPath.clear();
  excerpt.clear();
  at = {};
  chapterLength = nextByte = 0;
  eof = false;
  currentChapter = -1;
  history = 0;
  pending = false;
}
bool Reader::selectFont(const Str& f) {
  suspendedFont.clear();
  layoutReady = false;
  warmCursor = {};
  bool ok = font.select(f, layout.size, layout.tone);
  identifyLayout();
  history = 0;
  cancelAhead();
  return ok;
}
void Reader::showProgress(bool visible) {
  if (layout.showProgress == visible) {
    return;
  }
  layout.showProgress = visible;
  warmCursor = {};
  layoutReady = false;
  history = 0;
  identifyLayout();
}
void Reader::identifyLayout() {
  cancelAhead();
  cacheBasis = font.generation();
  layoutId = key(id + (epub ? "-epub-layout-8-" : "-layout-7-") + font.generation() + "-" +
                 std::to_string(layout.size) + "-" + std::to_string(layout.gap) + "-" +
                 std::to_string(layout.margin) + "-" + std::to_string(layout.bodyBottom()));
}
Str Reader::locationProof(Anchor location) {
  if (epub) {
    return epub->chapterProof(location.chapter);
  }
  if (path.empty() || location.chapter || location.byte > length(textPath)) {
    return {};
  }
  FILE* f = fopen(textPath.c_str(), "rb");
  if (!f) {
    return {};
  }
  char bytes[192];
  auto start = location.byte > 64 ? location.byte - 64 : 0;
  bool ok = fseek(f, start, SEEK_SET) == 0;
  size_t n = ok ? fread(bytes, 1, sizeof bytes, f) : 0;
  ok = ok && !ferror(f);
  fclose(f);
  return ok ? key(std::to_string(location.byte) + " " + Str(bytes, n)) : Str();
}
void Reader::commitIdentity() {
  while (verification.running()) {
    verification.step();
#ifdef ARDUINO
    yield();
#endif
  }
  if (id.empty() && !verification.value().empty()) {
    id = verification.value();
    if (epub) {
      epub->verified(id);
    }
    identifyLayout();
  }
}
void Reader::background(bool idle) {
  if (path.empty()) {
    return;
  }
  if (verification.running()) {
    verification.step();
    if (!verification.running()) {
      commitIdentity();
    }
  }
  if (cacheBasis != font.generation()) {
    identifyLayout();
  }
  if (idle) {
    prefetch();
    scanChapters();
  }
}
void Reader::scanChapters() {
  if (!epub && !id.empty()) {
    txtChapters.start(textPath, id, enc);
    txtChapters.tick();
  }
}
bool Reader::open(const Str& name, const Str& f, Anchor a, FileIdentity* prepared) {
  closeBook();
  error.clear();
  if (!userPath(name)) {
    font.close();
    return false;
  }
  Str full = root + name;
  if (!verification.begin(full, prepared)) {
    font.close();
    error = "文件读取失败";
    return false;
  }
  path = name;
  id = verification.value();
  layout.rich = ends(name, ".epub");
  if (ends(name, ".epub")) {
    font.close();
    epub.reset(new (std::nothrow) Epub);
    if (!epub || !epub->open(full, id.empty() ? Str() : id)) {
      error = epub ? epub->error : "内存不足";
      close();
      return false;
    }
  } else {
    FILE* fp = fopen(full.c_str(), "rb");
    if (!fp) {
      close();
      return false;
    }
    enc = detect(fp);
    fclose(fp);
    textPath = full;
  }
  selectFont(f);
  jump(a);
  return true;
}
void Reader::jump(Anchor a) {
  warmCursor = {};
  layoutReady = false;
  seekChapterEnd = false;
  requestedFraction = -1;
  seekByte = UINT32_MAX;
  if (!started || a.chapter != at.chapter || a.byte != preAt) {
    cancelAhead();
  }
  canPrefetch = false;
  at = a;
  if (epub) {
    if (at.chapter >= epub->count()) {
      at = {};
    }
  } else {
    at.chapter = 0;
    at.byte = std::min(at.byte, length(textPath));
  }
  pending = true;
}
void Reader::jumpChapter(unsigned i) {
  history = 0;
  if (!epub) {
    jump({0, txtChapters.anchor(i)});
    return;
  }
  unsigned n = 0;
  Str fragment;
  if (!epub->target(i, n, fragment)) {
    return;
  }
  jump({n, 0});
  epub->wantedFragment = fragment;
  epub->foundAt = UINT32_MAX;
  currentChapter = -1;
}
bool Reader::prepare(uint32_t need) {
  if (!epub) {
    return true;
  }
  if (currentChapter != int(at.chapter)) {
    // FreeType, the frame ahead, and the XHTML parser need not be resident
    // at the same time. Finish the chapter text on TF, then restore the face.
    if (font.active()) {
      suspendedFont = font.path;
      font.close();
    }
    cancelAhead();
    nextFrame.reset();
    if (!epub->chapter(at.chapter, !suspendedFont.empty())) {
      error = epub->error;
      return false;
    }
    currentChapter = at.chapter;
    chapterLength = 0;
    enc = {Encoding::Utf8, 0};
  }
  if (requestedFraction >= 0) {
    if (!epub->ensure(UINT32_MAX, 4, true)) {
      error = epub->error;
      return false;
    }
    if (!epub->complete) {
      return true;
    }
    chapterLength = epub->available();
    uint32_t target = requestedFraction == 100
                          ? (chapterLength > 1024 ? chapterLength - 1024 : 0)
                          : uint32_t((uint64_t(chapterLength) * requestedFraction + 99) / 100);
    seekChapterEnd = requestedFraction == 100;
    requestedFraction = -1;
    seekByte = target;
    // Retain the preceding bytes to identify UTF-8, styles and image records.
    at.byte = target > 6 ? target - 6 : 0;
  }
  if (seekByte != UINT32_MAX) {
    need = at.byte + 4096;
  }
  if (!epub->wantedFragment.empty()) {
    need = epub->available() + 4096;
  }
  bool stageWholeChapter = !suspendedFont.empty() && epub->fileBacked();
  if (!epub->ensure(stageWholeChapter ? UINT32_MAX : need)) {
    error = epub->error;
    return false;
  }
  if (!suspendedFont.empty() && (epub->complete || (!stageWholeChapter && epub->available() >= need))) {
    if (!font.select(suspendedFont, layout.size, layout.tone)) {
      error = "无法恢复正文字体";
      return false;
    }
    suspendedFont.clear();
    identifyLayout();
  }
  textPath = epub->textPath;
  if (seekByte != UINT32_MAX && (epub->complete || epub->available() >= need)) {
    FILE* f = epub->openText();
    if (!f) {
      error = "章节读取失败";
      return false;
    }
    uint32_t pos = at.byte;
    bool ok = fseek(f, pos - epub->base(), SEEK_SET) == 0;
    while (ok && pos < seekByte) {
      int ch = fgetc(f);
      if (ch == EOF) {
        break;
      }
      unsigned bytes = ch == 27 ? 5 : ch == 29 ? 2 : ch >= 0xf0 ? 4 : ch >= 0xe0 ? 3 : ch >= 0xc0 ? 2 : 1;
      ok = !fseek(f, bytes - 1, SEEK_CUR);
      pos += bytes;
    }
    ok = !ferror(f) && ok;
    if (!epub->closeText(f) || !ok) {
      error = "章节定位失败";
      return false;
    }
    at.byte = std::min(pos, chapterLength);
    seekByte = UINT32_MAX;
  }
  if (!epub->wantedFragment.empty() && (epub->foundAt != UINT32_MAX || epub->complete)) {
    at.byte = epub->foundAt == UINT32_MAX ? 0 : epub->foundAt;
    epub->wantedFragment.clear();
  }
  return true;
}
int Reader::chapterPreparation() const {
  return epub && !suspendedFont.empty() ? int(epub->decodedPercent()) : -1;
}
bool Reader::awaitingText(uint32_t need) const {
  return epub && (requestedFraction >= 0 || seekByte != UINT32_MAX || !suspendedFont.empty() ||
                  !epub->wantedFragment.empty() || (!epub->complete && epub->available() < need));
}
struct CachedPage {
  char signature[65];
  char snippet[100];
  uint32_t next, eof, crc;
};
bool Reader::cached(Canvas& c, bool writing) {
  if (id.empty() || (font.active() && font.generation().empty())) {
    return false;
  }
  Str sig = key(layoutId + "-" + std::to_string(at.chapter) + "-" + std::to_string(at.byte) + "-" +
                std::to_string(layout.tone));
  Str dir = root + "/.jingdu/cache";
  Str slot = dir + "/page-" + id.substr(0, 1) + "-" + std::to_string((unsigned char)sig[0] % 8);
  const int first = 30 * 34, bytes = 30 * (layout.bodyBottom() - 34);
  if (writing && (!cacheRoom(slot, std::max<int>(0, int(sizeof(CachedPage)) + bytes - int(length(slot)))) ||
                  !makedirs(dir))) {
    return false;
  }
  FILE* f = fopen(slot.c_str(), writing ? "wb" : "rb");
  if (!f) {
    return false;
  }
  CachedPage h{};
  bool ok;
  if (writing) {
    memcpy(h.signature, sig.c_str(), 65);
    snprintf(h.snippet, sizeof h.snippet, "%s", excerpt.c_str());
    h.next = nextByte;
    h.eof = eof;
    h.crc = checksum(c.bits + first, bytes) ^ h.next ^ h.eof ^ checksum(h.snippet, sizeof h.snippet);
    ok = fwrite(&h, sizeof h, 1, f) == 1 && fwrite(c.bits + first, 1, bytes, f) == bytes;
  } else {
    ok = fread(&h, sizeof h, 1, f) == 1 && !memcmp(h.signature, sig.c_str(), 65) && h.eof <= 1 &&
         (h.next > at.byte || h.eof) && fread(c.bits + first, 1, bytes, f) == bytes &&
         (checksum(c.bits + first, bytes) ^ h.next ^ h.eof ^ checksum(h.snippet, sizeof h.snippet)) == h.crc;
    if (ok) {
      h.snippet[99] = 0;
      excerpt = h.snippet;
      eof = h.eof;
      nextByte = h.next;
    }
  }
  fclose(f);
  return ok;
}
bool Reader::restoreCachedBody(Canvas& canvas) {
  return !path.empty() && !pending && cached(canvas, false);
}
struct Edge {
  uint32_t a, b, crc;
};
void Reader::saveIndex(uint32_t a, uint32_t b) {
  if (b <= a || id.empty() || (font.active() && font.generation().empty())) {
    return;
  }
  FILE* f = fopen(indexPath.c_str(), "r+b");
  if (!f) {
    f = fopen(indexPath.c_str(), "w+b");
  }
  if (!f) {
    return;
  }
  fseek(f, 0, SEEK_END);
  auto n = ftell(f);
  Edge e{};
  bool append = (n == 0 && a <= enc.bom);
  if (n >= long(sizeof e) && n % sizeof e == 0) {
    fseek(f, -long(sizeof e), SEEK_END);
    append = fread(&e, sizeof e, 1, f) == 1 && e.crc == checksum(&e, 8) && e.b == a;
  }
  if (append && n < 4 * 1024 * 1024 && cacheRoom(indexPath, sizeof e)) {
    e = {a, b, 0};
    e.crc = checksum(&e, 8);
    fseek(f, 0, SEEK_END);
    fwrite(&e, sizeof e, 1, f);
  }
  fclose(f);
}
bool Reader::skipTowardsEnd() {
  if (!seekChapterEnd) {
    return false;
  }
  if (eof || nextByte <= at.byte) {
    seekChapterEnd = false;
    return false;
  }
  at.byte = nextByte;
  pending = true;
  return true;
}
// Prepare one real page in small slices. Vertical progress follows the same line breaks as normal reading.
int Reader::preparePage(unsigned& percent) {
  if (!prepare(at.byte > UINT32_MAX - 4096 ? UINT32_MAX : at.byte + 4096)) {
    return -1;
  }
  if (awaitingText(at.byte + 4096)) {
    return 0;
  }
  FILE* f = epub ? epub->openText() : fopen(textPath.c_str(), "rb");
  if (!f) {
    return -1;
  }
  bool finished = false;
  bool ok =
      scanPage(f, enc, at.byte - (epub ? epub->base() : 0), layout, font, page, warmCursor, 8, finished);
  bool closed = epub ? epub->closeText(f) : fclose(f) == 0;
  if (!ok || !closed) {
    return -1;
  }
  percent =
      finished ? 100u : unsigned(std::clamp((warmCursor.y - 40) * 100 / (layout.bodyBottom() - 40), 0, 99));
  layoutReady = finished;
  return finished ? 1 : 0;
}
bool Reader::paint(Canvas& c) {
  pending = false;
  preText.reset();
  background();
  if (!prepare(at.byte > UINT32_MAX - 4096 ? UINT32_MAX : at.byte + 4096)) {
    return false;
  }
  if (awaitingText(at.byte + 4096)) {
    pending = true;
    return false;
  }
  indexPath = root + "/.jingdu/cache/index-" + layoutId + "-" + std::to_string(at.chapter);
  bool resumeAhead = started && preAt == at.byte && !seekChapterEnd;
#ifdef ARDUINO
  uint32_t t0 = millis(), laid = t0, painted = t0;
  auto stats = font.stats;
#endif
  auto finish = [&](const char* source) {
#ifdef ARDUINO
    Serial.printf(
        "JD_PAGE byte=%u chapter=%u source=%s layout=%u draw=%u cache=%u ram=%u tf=%u raster=%u fallback=%u "
        "blank=%u unmapped=%u size=%d margin=%d gap=%d ink=%d byte_free=%u byte_largest=%u\n",
        at.byte, at.chapter, source, laid - t0, painted - laid, millis() - painted,
        font.stats.ram - stats.ram, font.stats.tf - stats.tf, font.stats.raster - stats.raster,
        font.stats.fallback - stats.fallback, font.stats.blank - stats.blank,
        font.stats.unmapped - stats.unmapped, layout.size, layout.margin, layout.gap, layout.tone,
        unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
        unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
#endif
    cancelAhead();
    preAt = nextByte;
    canPrefetch = !eof;
  };
  if (!resumeAhead && cached(c, false)) {
    layoutReady = false;
    warmCursor = {};
    if (skipTowardsEnd()) {
      return false;
    }
    finish("disk");
    return true;
  }
  if (resumeAhead) {
    memcpy(c.bits + 30 * 34, nextFrame->bits + 30 * 34, 30 * (layout.bodyBottom() - 34));
    excerpt = nextExcerpt;
    layoutReady = scanned;
    warmCursor = preCursor;
  } else {
    cancelAhead();
    c.rect(0, 34, 240, layout.bodyBottom() - 34, false);
  }
  bool ok = layoutReady;
  if (!layoutReady) {
    FILE* f = epub ? epub->openText() : fopen(textPath.c_str(), "rb");
    if (!f) {
      error = "读卡失败";
      return false;
    }
    bool done = false;
    ok = scanPage(f, enc, at.byte - (epub ? epub->base() : 0), layout, font, page, warmCursor, UINT32_MAX,
                  done) &&
         done;
    bool closed = epub ? epub->closeText(f) : fclose(f) == 0;
    ok = ok && closed;
  }
  layoutReady = false;
  warmCursor = {};
  if (epub) {
    page.start += epub->base();
    page.end += epub->base();
  }
  if (!ok) {
    error = "无法排版";
    return false;
  }
  if (epub && epub->fileBacked() && page.eof && page.end < epub->available()) {
    error = "章节缓存读取不完整";
    return false;
  }
#ifdef ARDUINO
  if (epub) {
    Serial.printf("JD_CHAPTER chapter=%u byte=%u end=%u available=%u complete=%d glyphs=%u\n", at.chapter,
                  at.byte, page.end, epub->available(), epub->complete, page.count);
  }
#endif
  nextByte = page.end;
  eof = page.eof && (!epub || epub->complete);
  if (skipTowardsEnd()) {
    saveIndex(page.start, page.end);
    return false;
  }
#ifdef ARDUINO
  laid = millis();
#endif
  if (!resumeAhead) {
    excerpt.clear();
  }
  bool imagesOk = drawChars(c, resumeAhead ? drawn : 0, page.count, excerpt);
#ifdef ARDUINO
  painted = millis();
#endif
  if (imagesOk && (!resumeAhead || prefetchCompleteInk)) {
    cached(c, true);
  }
  saveIndex(page.start, page.end);
  finish(resumeAhead ? (scanned && drawn == page.count ? "prefetch" : "partial") : "render");
  return true;
}
bool Reader::drawChars(Canvas& c, unsigned begin, unsigned end, Str& excerpt) {
  bool imagesOk = true;
  for (unsigned i = begin; i < end; i++) {
    const auto& p = page.chars[i];
    if (p.x & 0x8000) {
      if (!byteHeapRoom(26 * 1024, 22 * 1024)) {
        font.sizeHotCache(false);
        if (&c != nextFrame.get()) {
          nextFrame.reset();
        }
      }
      if (!epub || !epub->image(p.cp, c, p.y, 240 - 2 * layout.margin)) {
        c.border(layout.margin, p.y, 240 - 2 * layout.margin, 84);
        c.ui(layout.margin + 8, p.y + 29, "图片暂不可显示", 12);
        imagesOk = false;
      }
      continue;
    }
    if (i < 24) {
      char b[5] = {};
      auto cp = p.cp & 0x1fffff;
      if (cp < 128) {
        b[0] = cp;
      } else if (cp < 2048) {
        b[0] = 0xc0 | (cp >> 6);
        b[1] = 0x80 | (cp & 63);
      } else if (cp < 65536) {
        b[0] = 0xe0 | (cp >> 12);
        b[1] = 0x80 | ((cp >> 6) & 63);
        b[2] = 0x80 | (cp & 63);
      } else {
        b[0] = 0xf0 | (cp >> 18);
        b[1] = 0x80 | ((cp >> 12) & 63);
        b[2] = 0x80 | ((cp >> 6) & 63);
        b[3] = 0x80 | (cp & 63);
      }
      excerpt += b;
    }
    if (!font.draw(c, p.x, p.y, p.cp & 0x1fffff, p.cp >> 24)) {
      imagesOk = false;
    }
  }
  return imagesOk;
}
void Reader::prefetch(unsigned quota) {
  if (!canPrefetch || (scanned && drawn == page.count) || path.empty() || pending ||
      (epub && (!epub->complete || epub->base()))) {
    return;
  }
  if (!nextFrame) {
    if (!byteHeapRoom(sizeof(Canvas), 22 * 1024)) {
      return;
    }
    nextFrame.reset(new (std::nothrow) Canvas);
    if (!nextFrame) {
      return;
    }
  }
  if (!started) {
    nextFrame->clear();
    started = true;
  }
  if (!scanned) {
    if (!preText) {
      preText.reset(fopen(textPath.c_str(), "rb"));
    }
    bool ok = preText && scanPage(preText.get(), enc, preAt, layout, font, page, preCursor, quota, scanned);
    if (scanned) {
      preText.reset();
    }
    if (!ok) {
      cancelAhead();
    }
    return;
  }
  auto end = std::min(page.count, drawn + quota);
  // Decode images on demand. Speculation must yield to touch input.
  for (unsigned i = drawn; i < end; ++i) {
    if (page.chars[i].x & 0x8000) {
      cancelAhead();
      return;
    }
  }
  if (!drawChars(*nextFrame, drawn, end, nextExcerpt)) {
    prefetchCompleteInk = false;
  }
  drawn = end;
}
bool Reader::forward() {
  if (pending) {
    return false;
  }
  Anchor n = at;
  if (!eof && nextByte > at.byte) {
    n.byte = nextByte;
  } else if (eof && epub && n.chapter + 1 < epub->count()) {
    ++n.chapter;
    n.byte = 0;
  } else {
    return false;
  }
  last[historyEnd] = at;
  historyEnd = (historyEnd + 1) % 16;
  history = std::min(16u, history + 1);
  jump(n);
  return true;
}
uint32_t Reader::align(uint32_t pos) {
  if (enc.enc == Encoding::Utf32le || enc.enc == Encoding::Utf32be) {
    return pos & ~3u;
  }
  FILE* f = fopen(textPath.c_str(), "rb");
  if (!f) {
    return 0;
  }
  if (enc.enc == Encoding::Utf16le || enc.enc == Encoding::Utf16be) {
    pos &= ~1u;
    fseek(f, pos, SEEK_SET);
    int a = fgetc(f), b = fgetc(f);
    if (a >= 0 && b >= 0) {
      int unit = enc.enc == Encoding::Utf16le ? a + 256 * b : 256 * a + b;
      if (unit >= 0xdc00 && unit <= 0xdfff && pos >= 2) {
        pos -= 2;
      }
    }
  } else if (enc.enc == Encoding::Utf8) {
    fseek(f, pos, SEEK_SET);
    int c = fgetc(f);
    while (c >= 128 && c < 192) {
      ++pos;
      c = fgetc(f);
    }
  } else if (enc.enc == Encoding::Gb && pos) {
    // A GB18030 trail can look like ASCII, including digits. Find a byte
    // that cannot occur inside a multibyte sequence, then decode forward.
    uint8_t buf[256];
    uint32_t start = pos;
    bool found = false;
    while (start && !found) {
      unsigned n = std::min<uint32_t>(start, sizeof buf);
      start -= n;
      if (fseek(f, start, SEEK_SET) || fread(buf, 1, n, f) != n) {
        break;
      }
      while (n) {
        unsigned ch = buf[--n];
        if (ch < 0x30 || (ch >= 0x3a && ch < 0x40) || ch == 0x7f) {
          start += n + 1;
          found = true;
          break;
        }
      }
    }
    fseek(f, start, SEEK_SET);
    while (start < pos && decode(f, enc.enc) != UINT32_MAX) {
      long next = ftell(f);
      if (next <= long(start)) {
        break;
      }
      start = uint32_t(next);
    }
    pos = start;
  }
  fclose(f);
  return pos;
}
bool Reader::backward() {
  if (pending) {
    return false;
  }
  if (history) {
    --history;
    historyEnd = (historyEnd + 15) % 16;
    Anchor a = last[historyEnd];
    jump(a);
    return true;
  }
  if (at.byte <= enc.bom) {
    if (!at.chapter) {
      return false;
    }
    jump({at.chapter - 1, 0});
    seekChapterEnd = true;
    return true;
  }
  uint32_t target = align(at.byte > 512 ? at.byte - 512 : 0);
  FILE* f = fopen(indexPath.c_str(), "rb");
  if (f) {
    fseek(f, 0, SEEK_END);
    long hi = ftell(f) / sizeof(Edge), lo = 0;
    while (lo < hi) {
      long mid = (lo + hi) / 2;
      Edge e{};
      fseek(f, mid * sizeof e, SEEK_SET);
      if (fread(&e, sizeof e, 1, f) != 1 || e.crc != checksum(&e, 8)) {
        break;
      }
      if (e.b == at.byte) {
        target = e.a;
        break;
      }
      if (e.b < at.byte) {
        lo = mid + 1;
      } else {
        hi = mid;
      }
    }
    fclose(f);
  }
  if (target >= at.byte) {
    target = 0;
  }
  jump({at.chapter, target});
  return true;
}
void Reader::percent(unsigned pct) {
  pct = std::min(100u, pct);
  history = 0;
  if (epub) {
    unsigned scaled = pct * epub->count();
    jump({std::min<unsigned>(epub->count() - 1, scaled / 100), 0});
    unsigned fraction = pct == 100 ? 100 : scaled % 100;
    if (fraction) {
      requestedFraction = fraction;
    }
    return;
  }
  auto n = length(textPath);
  uint32_t target = pct == 100 ? (n > 1024 ? n - 1024 : 0) : uint32_t((uint64_t(n) * pct + 99) / 100);
  jump({0, align(target)});
  seekChapterEnd = pct == 100;
}
unsigned Reader::chapterProgress() const {
  if (!epub) {
    return progress();
  }
  if (!pending && eof) {
    return 100;
  }
  auto n = epub->complete ? epub->available() : chapterLength;
  return unsigned(uint64_t(std::min(at.byte, n)) * 100 / std::max<uint32_t>(1, n));
}
unsigned Reader::progress() const {
  if (path.empty()) {
    return 0;
  }
  if (!pending && eof && (!epub || at.chapter + 1 == epub->count())) {
    return 100;
  }
  if (epub) {
    auto n = epub->complete ? epub->available() : chapterLength;
    double part = n ? double(std::min(at.byte, n)) / n : 0;
    return unsigned(100 * (at.chapter + part) / std::max(1u, epub->count()));
  }
  return unsigned(uint64_t(at.byte) * 100 / std::max<uint32_t>(1, length(textPath)));
}
}  // namespace jd
