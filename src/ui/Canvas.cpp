#include "Canvas.h"
#include "HeapRoom.h"
#ifdef ARDUINO
#include <esp_cpu.h>
#endif
#include <algorithm>
#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <new>
namespace jd {
struct BitmapGlyph {
  uint32_t offset;
  int8_t left, top;
  uint8_t advance, width, height;
};
#include "../generated/ui_font.inc"
void Canvas::clear() {
  memset(bits, 255, sizeof bits);
}
void Canvas::dot(int x, int y, bool black) {
  if (unsigned(x) >= W || unsigned(y) >= H) {
    return;
  }
  auto& b = bits[y * 30 + x / 8];
  uint8_t mask = 0x80 >> (x & 7);
  if (black) {
    b &= ~mask;
  } else {
    b |= mask;
  }
}
void Canvas::rect(int x, int y, int w, int h, bool b) {
  for (int i = std::max(y, 0); i < std::min(y + h, H); ++i) {
    for (int j = std::max(x, 0); j < std::min(x + w, W); ++j) {
      dot(j, i, b);
    }
  }
}
void Canvas::border(int x, int y, int w, int h) {
  rect(x, y, w, 1);
  rect(x, y + h - 1, w, 1);
  rect(x, y, 1, h);
  rect(x + w - 1, y, 1, h);
}
struct Symbol {
  const char* name;
  uint8_t count;
  const uint8_t* xy;
};
static const uint8_t icon_back[] = {11, 2, 4, 9, 11, 16};
static const uint8_t icon_next[] = {5, 3, 11, 9, 5, 15};
static const uint8_t icon_lock[] = {4, 8, 4, 5, 7, 2, 10, 2, 13, 5, 13, 8, 15, 8, 15, 16, 2, 16, 2, 8, 14, 8};
static const uint8_t icon_shelf[] = {1,  15, 1,  2,  4,  2,  4,  15, 7,  15, 7,  3,
                                     10, 3,  10, 15, 15, 14, 12, 3,  15, 2,  18, 14};
static const uint8_t icon_folder[] = {1, 15, 1, 3, 7, 3, 9, 6, 17, 6, 17, 15, 1, 15};
static const uint8_t icon_settings[] = {3, 2, 3, 15};
static const uint8_t icon_wifi[] = {1, 6, 4, 4, 8, 3, 12, 4, 15, 6};
static const uint8_t icon_book[] = {2,  2,  7,  2, 9,  4, 11, 2, 16, 2, 16,
                                    14, 11, 14, 9, 16, 7, 14, 2, 14, 2, 2};
static const uint8_t icon_thermo[] = {7,  11, 7,  3,  9, 1,  11, 3,  11, 11, 13, 13,
                                      13, 16, 11, 18, 7, 18, 5,  16, 5,  13, 7,  11};
static const uint8_t icon_drop[] = {9, 1, 15, 10, 16, 13, 15, 16, 12, 18, 7, 18, 4, 16, 3, 13, 4, 10, 9, 1};
static const uint8_t icon_file[] = {3, 1, 12, 1, 16, 5, 16, 17, 3, 17, 3, 1};
static const uint8_t icon_plus[] = {2, 9, 16, 9};
static const uint8_t icon_tick[] = {2, 9, 7, 14, 16, 4};
static const Symbol symbols[] = {
    {"back", 3, icon_back},    {"next", 3, icon_next},     {"lock", 11, icon_lock},
    {"shelf", 12, icon_shelf}, {"folder", 7, icon_folder}, {"settings", 2, icon_settings},
    {"wifi", 5, icon_wifi},    {"book", 11, icon_book},    {"thermo", 12, icon_thermo},
    {"drop", 10, icon_drop},   {"file", 6, icon_file},     {"plus", 2, icon_plus},
    {"tick", 3, icon_tick}};
static constexpr uint32_t homeIcons[][18] = {
    {// shelf
     0x0, 0xee00, 0xaae0, 0xaaa0, 0xea90, 0xae50, 0xaa50, 0xaa48, 0xaa28, 0xaa28, 0xaa24, 0xaa14, 0xee1c, 0x0,
     0x1fffe, 0x0, 0x0, 0x0},
    {// english
     0x0, 0x0, 0x3000, 0x4800, 0x4800, 0x4800, 0x8478, 0x8404, 0xfc04, 0x1027c, 0x10284, 0x10284, 0x10276,
     0x0, 0x0, 0x0, 0x0, 0x0},
    {// folder
     0x0, 0x0, 0xf800, 0x10400, 0x103fc, 0x10002, 0x10002, 0x1fffc, 0x10002, 0x10002, 0x10002, 0x10002,
     0x10002, 0xfffc, 0x0, 0x0, 0x0, 0x0},
    {// settings
     0x0, 0x0, 0x1c00, 0x1400, 0x1f7fe, 0x1400, 0x1c00, 0x70, 0x50, 0x1ffde, 0x50, 0x70, 0x3800, 0x2800,
     0x1effe, 0x2800, 0x3800, 0x0},
};
void Canvas::homeIcon(unsigned index, int x, int y) {
  if (index >= std::size(homeIcons)) {
    return;
  }
  for (unsigned row = 0; row < 18; ++row) {
    for (unsigned bit = 0; bit < 18; ++bit) {
      if (homeIcons[index][row] & (0x20000u >> bit)) {
        dot(x + bit, y + row);
      }
    }
  }
}
void Canvas::icon(const char* name, int x, int y) {
  for (const auto& sym : symbols) {
    if (!strcmp(sym.name, name)) {
      for (int i = 1; i < sym.count; i++) {
        int a = sym.xy[2 * i - 2], b = sym.xy[2 * i - 1], nx = sym.xy[2 * i], ny = sym.xy[2 * i + 1];
        int span = std::max(abs(nx - a), abs(ny - b));
        for (int n = 0; n <= span; n++) {
          dot(x + a + (nx - a) * n / std::max(1, span), y + b + (ny - b) * n / std::max(1, span));
        }
      }
      break;
    }
  }
  if (!strcmp(name, "home")) {
    for (int i = 0; i < 9; ++i) {
      dot(x + 1 + i, y + 10 - i);
      dot(x + 9 + i, y + 2 + i);
    }
    rect(x + 3, y + 9, 1, 8);
    rect(x + 15, y + 9, 1, 8);
    rect(x + 3, y + 16, 5, 1);
    rect(x + 11, y + 16, 5, 1);
    rect(x + 7, y + 11, 1, 6);
    rect(x + 11, y + 11, 1, 6);
    rect(x + 7, y + 11, 5, 1);
  }
  if (!strcmp(name, "minus")) {
    rect(x + 2, y + 9, 15, 1);
  }
  if (!strcmp(name, "plus")) {
    rect(x + 9, y + 2, 1, 15);
  }
  if (!strcmp(name, "settings")) {
    rect(x + 9, y + 2, 1, 14);
    rect(x + 15, y + 2, 1, 14);
    rect(x + 1, y + 5, 5, 3);
    rect(x + 7, y + 11, 5, 3);
    rect(x + 13, y + 6, 5, 3);
  }
}
struct NumericGlyph {
  uint16_t offset;
  int8_t left, top;
  uint8_t width, height, advance;
};
#include "../generated/clock_font.inc"
int Canvas::number(int x, int baseline, const char* digits, bool large) {
  const auto* glyphs = large ? timeGlyphs : readingGlyphs;
  const auto* pixels = large ? timePixels : readingPixels;
  const char alphabet[] = "0123456789:.-";
  for (; *digits; ++digits) {
    const char* match = strchr(alphabet, *digits);
    if (!match) {
      continue;
    }
    const auto& g = glyphs[match - alphabet];
    size_t stride = (g.width + 7) / 8;
    for (unsigned row = 0; row < g.height; ++row) {
      const uint8_t* line = pixels + g.offset + row * stride;
      for (unsigned bit = 0; bit < g.width; ++bit) {
        if (line[bit / 8] & (128 >> (bit % 8))) {
          dot(x + g.left + int(bit), baseline + g.top + int(row));
        }
      }
    }
    x += g.advance;
  }
  return x;
}
static const BitmapGlyph& tile(uint32_t cp, int px) {
  auto end = std::end(codepoints);
  auto it = std::lower_bound(std::begin(codepoints), end, cp);
  if (it == end || *it != cp) {
    it = std::lower_bound(std::begin(codepoints), end, 0x25a1);
  }
  auto index = it - codepoints;
  return px <= 12 ? metrics12[index] : metrics15[index];
}
static int step(uint32_t cp, int px) {
  return tile(cp, px).advance;
}
static void bitmap(Canvas& c, int x, int y, uint32_t cp, int px, bool black) {
  const auto& g = tile(cp, px);
  const auto* rows = (px <= 12 ? rows12 : rows15) + g.offset;
  x += g.left;
  y += g.top + std::max(0, (px - (px <= 12 ? 12 : 15)) / 2);
  for (int j = 0; j < g.height; j++) {
    for (int i = 0; i < g.width; i++) {
      if (rows[j] & (0x8000 >> i)) {
        c.dot(x + i, y + j, black);
      }
    }
  }
}
int Canvas::bitmapAdvance(uint32_t cp, int px) {
  return step(cp, px);
}
void Canvas::bitmapChar(int x, int y, uint32_t cp, int px) {
  bitmap(*this, x, y, cp, px, true);
}
struct StudyGlyph {
  uint16_t cp, offset;
  int8_t left, top;
  uint8_t width, height, advance;
};
#include "../generated/study_font.inc"
static const StudyGlyph* studyGlyph(uint32_t cp, int px) {
  if (cp > 0xffff) {
    return nullptr;
  }
  const auto* first = px <= 12 ? glyphs12 : px <= 15 ? glyphs15 : glyphs20;
  const auto* end = px <= 12 ? std::end(glyphs12) : px <= 15 ? std::end(glyphs15) : std::end(glyphs20);
  auto g = std::lower_bound(first, end, cp, [](const StudyGlyph& g, uint32_t c) { return g.cp < c; });
  return g != end && g->cp == cp ? g : nullptr;
}
int Canvas::studyAdvance(uint32_t cp, int px) {
  const auto* g = studyGlyph(cp, px);
  return g ? g->advance : -1;
}
int Canvas::studyChar(int x, int y, uint32_t cp, int px, bool black) {
  const auto* g = studyGlyph(cp, px);
  if (!g) {
    return -1;
  }
  const auto* data = (px <= 12 ? studyPixels12 : px <= 15 ? studyPixels15 : studyPixels20) + g->offset;
  for (int row = 0; row < g->height; ++row) {
    for (int col = 0; col < g->width; ++col) {
      int bit = row * g->width + col;
      if (data[bit / 8] & (0x80 >> (bit % 8))) {
        dot(x + g->left + col, y + g->top + row, black);
      }
    }
  }
  return g->advance;
}
static int uiAdvance(uint32_t cp, int px, bool study) {
  int n = study ? Canvas::studyAdvance(cp, px) : -1;
  return n >= 0 ? n : step(cp, px);
}
int Canvas::textWidth(const Str& s, int px, bool study) {
  const char *p = s.data(), *end = p + s.size();
  int width = 0;
  while (p < end) {
    width += uiAdvance(nextChar(p, end), px, study);
  }
  return width;
}
void Canvas::ui(int x, int y, const Str& s, int px, int maxW, bool b, bool study) {
  int start = x;
  const char *p = s.data(), *end = p + s.size();
  while (p < end) {
    auto cp = nextChar(p, end);
    int advance = uiAdvance(cp, px, study);
    if (x - start + advance > maxW) {
      break;
    }
    if (!study || studyChar(x, y, cp, px, b) < 0) {
      bitmap(*this, x, y, cp, px, b);
    }
    x += advance;
  }
}
unsigned long BodyFont::readFont(void* context, unsigned long pos, unsigned char* out, unsigned long count) {
  auto& font = *static_cast<BodyFont*>(context);
  if (!count || pos >= font.streamSize) {
    return 0;
  }
  count = std::min<unsigned long>(count, font.streamSize - pos);
  auto readRange = [&](uint32_t start, unsigned char* data, unsigned long bytes) {
    for (int attempt = 0; attempt < 2; ++attempt) {
      if (lseek(font.fontFd, start, SEEK_SET) != static_cast<off_t>(start)) {
        continue;
      }
      unsigned long done = 0;
      while (done < bytes) {
        ssize_t n = ::read(font.fontFd, data + done, bytes - done);
        if (n < 0 && errno == EINTR) {
          continue;
        }
        if (n <= 0) {
          break;
        }
        done += n;
      }
      if (done == bytes) {
        return true;
      }
    }
    return false;
  };
  if (count >= 512) {
    return readRange(pos, out, count) ? count : 0;
  }
  unsigned long copied = 0;
  while (copied < count) {
    auto base = uint32_t(pos & ~511UL);
    auto& block = font.blocks[(base / 512) % 4];
    if (block.base != base) {
      block.base = UINT32_MAX;
      auto expected = std::min<unsigned long>(sizeof block.data, font.streamSize - base);
      if (!readRange(base, block.data, expected)) {
        break;
      }
      block.bytes = expected;
      block.base = base;
    }
    unsigned offset = pos - base;
    if (offset >= block.bytes) {
      break;
    }
    auto n = std::min<unsigned long>(block.bytes - offset, count - copied);
    memcpy(out + copied, block.data + offset, n);
    copied += n;
    pos += n;
  }
  return copied;
}
void BodyFont::resetRecency() {
  for (unsigned i = 0; i < sizeof recency; ++i) {
    recency[i] = i % 6;
  }
}
void BodyFont::setCacheMode(CacheMode mode) {
  sizeHotCache(false);
  for (auto& glyph : hot) {
    glyph.cp = 0;
  }
  resetRecency();
  cacheMode = mode;
}
void BodyFont::sizeHotCache(bool allowGrowth) {
  if (!allowGrowth || !byteHeapRoom(1, 32 * 1024)) {
    if (hotBanks) {
      // Global replacement may put a glyph outside its base-cache set.
      if (cacheMode == CacheMode::Global80) {
        for (auto& glyph : hot) {
          glyph.cp = 0;
        }
      }
      for (auto& bank : extraHot) {
        bank.reset();
      }
      hotBanks = 0;
      resetRecency();
    }
    return;
  }
  constexpr size_t bankBytes = sizeof(Glyph) * 40;
  unsigned maximum = cacheMode == CacheMode::Automatic ? 2 : cacheMode == CacheMode::Group40 ? 0 : 1;
  unsigned reserve = cacheMode == CacheMode::Automatic ? 48 * 1024 : 32 * 1024;
  if (!face || hotBanks >= maximum || !byteHeapRoom(bankBytes, reserve)) {
    return;
  }
  auto bank = std::unique_ptr<Glyph[]>(new (std::nothrow) Glyph[40]{});
  if (!bank) {
    return;
  }
  extraHot[hotBanks++] = std::move(bank);
  if (cacheMode == CacheMode::Global80) {
    unsigned used = 0, empty = 80;
    for (unsigned i = 0; i < 80; ++i) {
      if (globalSlot(i).cp) {
        recency[used++] = i;
      } else {
        recency[--empty] = i;
      }
    }
  }
}
void BodyFont::clearGlyphs() {
  sizeHotCache(false);
  resetRecency();
  closeGlyphCache();
  warmCode = 0x4dff;
  warmRange = sinceCheckpoint = 0;
  loadWarmCursor = true;
  oversized = nullptr;
  memset(widthKeys, 0, sizeof widthKeys);
  memset(hot, 0, sizeof hot);
}
void BodyFont::close() {
  clearGlyphs();
  for (auto& block : blocks) {
    block.base = UINT32_MAX;
  }
  streamCmap.reset();
  face.reset();
  if (fontFd >= 0) {
    ::close(fontFd);
  }
  fontFd = -1;
  path.clear();
  identity.clear();
}
bool BodyFont::select(const Str& wanted, int px, int tone) {
  if (wanted == path && (wanted.empty() || face)) {
    if (size != px || tone != ink) {
      clearGlyphs();
      size = px;
      ink = tone;
      // FreeType takes the pixel size at each glyph load. Keep the open
      // stream and its digest job across a layout change.
      if (!identity.empty()) {
        openGlyphCache();
      }
    }
    return true;
  }
  close();
  size = px;
  ink = tone;
  if (wanted.empty()) {
    return true;
  }
  Str full = root + wanted;
  // The embedded newlib refills an unbuffered FILE byte by byte. The dedicated
  // descriptor reads whole sectors while keeping FreeType's absolute offsets.
  fontFd = ::open(full.c_str(), O_RDONLY);
  if (fontFd < 0) {
    return false;
  }
  face.reset(new (std::nothrow) freeink::font::FtFont);
  streamSize = length(full);
  identity = fontCacheIdentity(full, fontFd, streamSize);
  openStreamCmap(16 * 1024);
  if (identity.empty() || !face ||
      !face->initStream(readFont, this, streamSize, px, 400, false, bool(streamCmap))) {
    close();
    return false;
  }
  if (!streamCmap && !face->hasNativeUnicodeMap()) {
    openStreamCmap(0);
  }
  path = wanted;
  openGlyphCache();
  return true;
}

// A mapped space, joiner, or variation selector may legitimately have no ink.
static bool needsInk(uint32_t c) {
  return c > 0x20 && c != 0xa0 && c != 0xad && c != 0x1680 && c != 0x180e && !(c >= 0x2000 && c <= 0x200f) &&
         !(c >= 0x2028 && c <= 0x202f) && !(c >= 0x205f && c <= 0x206f) && c != 0x3000 && c != 0xfeff &&
         !(c >= 0xfe00 && c <= 0xfe0f) && !(c >= 0xe0100 && c <= 0xe01ef);
}
const BodyFont::Glyph* BodyFont::get(uint32_t cp) {
  oversized = nullptr;
  if (!face || cp == 0) {
    return nullptr;
  }
#ifdef ARDUINO
  uint32_t startCycles = esp_cpu_get_cycle_count();
#endif
  if (hotBanks == 1) {
    ++requestsAt80;
  }
  const bool global = hotBanks == 1 && cacheMode == CacheMode::Global80;
  const unsigned set = global ? 0 : cp % 20;
  auto* order = recency + set * 6;
  const unsigned ways = global ? 80 : 2 + 2 * hotBanks;
  auto slot = [&](unsigned way) -> Glyph& {
    if (global) {
      return globalSlot(way);
    }
    return way < 2 ? hot[2 * set + way] : extraHot[way / 2 - 1][2 * set + way % 2];
  };
  unsigned rank = 0;
  while (rank < ways && slot(order[rank]).cp != cp) {
    ++rank;
  }
  const bool found = rank < ways;
  if (!found) {
    rank = ways - 1;
  }
  const unsigned way = order[rank];
  memmove(order + 1, order, rank);
  order[0] = way;
  auto& g = slot(way);
  if (found) {
    ++stats.ram;
#ifdef ARDUINO
    cycles.ram += uint32_t(esp_cpu_get_cycle_count() - startCycles);
#endif
    return &g;
  }
  if (readGlyph(cp, g)) {
    ++stats.tf;
#ifdef ARDUINO
    cycles.tf += uint32_t(esp_cpu_get_cycle_count() - startCycles);
#endif
    return &g;
  }
  ++stats.raster;
  auto result = rasterGlyph(cp, g);
#ifdef ARDUINO
  cycles.raster += uint32_t(esp_cpu_get_cycle_count() - startCycles);
#endif
  return result;
}
const BodyFont::Glyph* BodyFont::rasterGlyph(uint32_t cp, Glyph& g) {
  auto id = glyphId(cp);
  if (!id && needsInk(cp)) {
    ++stats.unmapped;
    return nullptr;
  }
  auto* b = face->rasterizeGlyph26_6(id, uint32_t(size) * 64);
  if (!b || b->advance < 0 || b->advance > 255) {
    return nullptr;
  }
  unsigned threshold = ink == 0 ? 160 : ink == 2 ? 80 : 118;
  unsigned pixels = unsigned(b->width) * b->height;
  if (pixels && !b->pixels) {
    return nullptr;
  }
  if (pixels && needsInk(cp)) {
    unsigned peak = *std::max_element(b->pixels, b->pixels + pixels);
    if (!peak) {
      ++stats.blank;
      return nullptr;
    }
    if (peak < threshold) {
      threshold = (peak + 1) / 2;
    }
  }
  if (b->width > 255 || b->height > 255 || pixels > sizeof(g.bitmap) * 8 || b->advance > 64 ||
      b->xoff < -128 || b->xoff > 127 || b->yoff < -128 || b->yoff > 127) {
    // Borrow this FT slot until the next font operation. Avoid growing all hot
    // cache entries for a rare wide glyph or unusually tall combining mark.
    oversized = b;
    oversizedThreshold = threshold;
    return nullptr;
  }
  g = {};
  g.w = b->width;
  g.h = b->height;
  g.advance = b->advance;
  g.dx = b->xoff;
  g.dy = b->yoff;
  for (unsigned i = 0; i < pixels; ++i) {
    if (b->pixels && b->pixels[i] >= threshold) {
      g.bitmap[i / 8] |= 0x80 >> (i & 7);
    }
  }
  const unsigned used = (unsigned(g.w) * g.h + 7) / 8;
  if (needsInk(cp) && std::none_of(g.bitmap, g.bitmap + used, [](uint8_t byte) { return byte != 0; })) {
    ++stats.blank;
#ifdef ARDUINO
    if (stats.blank <= 8) {
      printf("JD_EMPTY_GLYPH cp=%lu id=%u w=%u h=%u adv=%u\n", (unsigned long)cp, unsigned(id), g.w, g.h,
             g.advance);
    }
#endif
    return nullptr;
  }
  g.cp = cp;
  saveGlyph(g);
  return &g;
}
bool BodyFont::flushPageGlyphs() {
  return disk && flushGlyphs(disk->main, disk->present, sizeof disk->present) &&
         flushGlyphs(disk->extra, disk->extraPresent, sizeof disk->extraPresent);
}
bool BodyFont::cacheForPage(uint32_t cp) {
  if (!face || !disk || !cacheInstance) return false;
  unsigned slot;
  uint8_t* bits;
  auto present = [&]() {
    return glyphSlot(cp, false, slot, bits) && (bits[slot / 8] & (1u << (slot % 8)));
  };
  if (present()) return true;
  Glyph temp{};
  if (readLegacyGlyph(cp, temp)) return present();
  if (!glyphId(cp) && needsInk(cp)) return true;  // The normal fallback supplies this character.
  ++stats.raster;
  auto glyph = rasterGlyph(cp, temp);
  bool tooLarge = oversized != nullptr;
  oversized = nullptr;
  return glyph ? present() : tooLarge;
}
bool BodyFont::allWarmed() {
  if (!cacheInstance) return false;
  if (loadWarmCursor) {
    loadWarmCursor = false;
    Str state;
    unsigned instance, range, cp;
    if (loadRecord(cachePath + ".warm", state, 64) &&
        sscanf(state.c_str(), "%u %u %u", &instance, &range, &cp) == 3 &&
        instance == cacheInstance && range <= 3 && cp <= 0x10ffff) {
      warmRange = range;
      warmCode = cp;
    }
  }
  return warmRange == 3;
}
void BodyFont::warmOne() {
  if (!face || !disk || disk->main.fd < 0 || allWarmed() || !spareGlyphRoom(44544)) return;
  // Basic Han, then the remainder of the font's Unicode map, without scanning unmapped holes.
  uint32_t cp = nextMapped(warmCode);
  uint32_t last = warmRange == 0 ? 0x9fff : warmRange == 1 ? 0x4dff : 0x10ffff;
  if (!cp || cp <= warmCode || cp > last) {
    ++warmRange;
    warmCode = warmRange == 2 ? 0x9fff : 0;
  } else {
    Glyph scratch{};
    if (!readGlyph(cp, scratch, false)) {
      auto before = disk->written;
      const bool cacheable = rasterGlyph(cp, scratch) != nullptr;
      oversized = nullptr;
      if (cacheable && disk->written == before) return;
      if (disk->written > before) {
        ++warmed;
      }
    }
    warmCode = cp;
  }
  if (++sinceCheckpoint >= 128 || warmRange == 3) {
    if (flushGlyphs(disk->main, disk->present, sizeof disk->present) &&
        flushGlyphs(disk->extra, disk->extraPresent, sizeof disk->extraPresent)) {
      saveRecord(cachePath + ".warm",
                 std::to_string(cacheInstance) + " " + std::to_string(warmRange) + " " + std::to_string(warmCode),
                 64);
    }
    sinceCheckpoint = 0;
  }
}

// Keep the original embedded bitmap at the original three sizes. Larger
// fallback text uses integer pixel coverage from the built-in 15 px master.
static int bodyAdvance(uint32_t cp, int px) {
  return px <= 22 ? step(cp, px) : (step(cp, 15) * px + 7) / 15;
}
static void bodyBitmap(Canvas& canvas, int x, int y, uint32_t cp, int px) {
  if (px <= 22) {
    bitmap(canvas, x, y, cp, px, true);
    return;
  }
  const auto& glyph = tile(cp, 15);
  const auto* rows = rows15 + glyph.offset;
  auto scale = [px](int value) { return (value * px + 7) / 15; };
  for (int row = 0; row < glyph.height; ++row) {
    for (int col = 0; col < glyph.width; ++col) {
      if (!(rows[row] & (0x8000 >> col))) {
        continue;
      }
      int left = scale(glyph.left + col), top = scale(glyph.top + row);
      canvas.rect(x + left, y + top, scale(glyph.left + col + 1) - left, scale(glyph.top + row + 1) - top);
    }
  }
}

int BodyFont::advance(uint32_t cp) {
  if (!face || !cp) {
    return bodyAdvance(cp, size);
  }
  unsigned slot = (cp ^ (cp >> 8)) & 255;
  if (widthKeys[slot] == cp) {
    return widths[slot];
  }
  auto* glyph = get(cp);
  if (!glyph) {
    return oversized ? oversized->advance : bodyAdvance(cp, size);
  }
  widthKeys[slot] = cp;
  widths[slot] = glyph->advance;
  return glyph->advance;
}
bool BodyFont::draw(Canvas& c, int x, int y, uint32_t cp, unsigned style) {
  if (auto g = get(cp)) {
    for (int j = 0; j < g->h; ++j) {
      for (int i = 0; i < g->w; ++i) {
        int k = j * g->w + i;
        if (g->bitmap[k / 8] & (0x80 >> (k & 7))) {
          int shift = (style & 2) ? (int(g->h) - j) / 5 : 0;
          int a = x + g->dx + i + shift, b = y + size - 2 + g->dy + j;
          c.dot(a, b);
          if (style & 1) {
            c.dot(a + 1, b);
          }
        }
      }
    }
    return true;
  }
  if (oversized) {
    for (unsigned j = 0; j < oversized->height; ++j) {
      for (unsigned i = 0; i < oversized->width; ++i) {
        if (oversized->pixels[j * oversized->width + i] < oversizedThreshold) {
          continue;
        }
        int dx = x + oversized->xoff + int(i) + ((style & 2) ? (oversized->height - j) / 5 : 0);
        int dy = y + size - 2 + oversized->yoff + int(j);
        c.dot(dx, dy);
        if (style & 1) {
          c.dot(dx + 1, dy);
        }
      }
    }
    return true;
  }
  if (needsInk(cp)) {
    bodyBitmap(c, x, y, cp, size);
  }
  if (face && needsInk(cp)) {
    ++stats.fallback;
    return false;
  }
  return true;
}
}  // namespace jd
