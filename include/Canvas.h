#pragma once
#include "Storage.h"
#include <FtFont.h>
#include <memory>
namespace jd {
struct Canvas {
  static constexpr int W = 240, H = 416;
  uint8_t bits[W * H / 8];
  void clear();
  void dot(int x, int y, bool black = true);
  void rect(int x, int y, int w, int h, bool black = true);
  void homeIcon(unsigned index, int x, int y);
  void icon(const char* name, int x, int y);
  int number(int x, int baseline, const char* digits, bool large = false);
  void border(int x, int y, int w, int h);
  static int bitmapAdvance(uint32_t cp, int px);
  void bitmapChar(int x, int y, uint32_t cp, int px);
  static int studyAdvance(uint32_t cp, int px);
  int studyChar(int x, int y, uint32_t cp, int px, bool black = true);
  static int textWidth(const Str&, int px = 15, bool study = false);
  void ui(int x, int y, const Str& s, int px = 16, int maxW = 232, bool black = true, bool study = false);
};
class BodyFont {
 public:
  enum class CacheMode { Automatic, Group40, Group80, Global80 };

 private:
  struct Glyph {
    uint32_t cp;
    int8_t dx, dy;
    uint8_t w, h, advance;
    uint8_t bitmap[128];
  };
  struct GlyphRecord {
    Glyph glyph;
    uint32_t crc;
  };
  static constexpr unsigned denseSlots = 21664;
  struct GlyphFile {
    int fd = -1;
    unsigned page = UINT32_MAX;
    unsigned dirty = 0;
  };
  struct DiskGlyphs {
    GlyphFile main, extra;
    uint8_t present[(denseSlots + 7) / 8]{}, extraPresent[32]{};
    char signature[65]{};
    int legacy = -1;
    uint32_t legacyCount = 0;
    uint32_t written = 0;
  };
  std::unique_ptr<DiskGlyphs> disk;
  void clearGlyphs();
  void openGlyphCache();
  void closeGlyphCache();
  bool flushGlyphs(GlyphFile&, uint8_t* bits, unsigned bytes);
  bool openGlyphFile(GlyphFile&, uint8_t* bits, unsigned count, unsigned page, bool create);
  GlyphFile* glyphSlot(uint32_t cp, bool create, unsigned& slot, uint8_t*& bits);
  bool readGlyph(uint32_t cp, Glyph& glyph, bool remember = true);
  bool readLegacyGlyph(uint32_t cp, Glyph& glyph);
  void saveGlyph(const Glyph& glyph);
  const Glyph* rasterGlyph(uint32_t cp, Glyph& dest);
  uint32_t nextMapped(uint32_t after);
  uint32_t warmCode = 0x4dff;
  unsigned warmRange = 0, sinceCheckpoint = 0;
  bool loadWarmCursor = true;
  struct StreamCmap {
    uint32_t base = 0, groups = 0, stride = 0, samples = 0, first[128]{};
    bool constant = false;
  };
  std::unique_ptr<StreamCmap> streamCmap;
  void openStreamCmap(unsigned minBytes);
  uint32_t glyphId(uint32_t cp);
  std::unique_ptr<freeink::font::FtFont> face;
  int fontFd = -1;
  Glyph hot[40]{};
  std::unique_ptr<Glyph[]> extraHot[2];
  uint8_t recency[120]{};
  unsigned hotBanks = 0;
  CacheMode cacheMode = CacheMode::Automatic;
  void resetRecency();
  Glyph& globalSlot(unsigned index) {
    return index < 40 ? hot[index] : extraHot[0][index - 40];
  }
  uint32_t widthKeys[256]{};
  uint8_t widths[256]{};
  struct Block {
    uint32_t base = UINT32_MAX;
    uint16_t bytes = 0;
    uint8_t data[512];
  } blocks[4];
  uint32_t streamSize = 0;
  Str cachePath;
  static unsigned long readFont(void*, unsigned long, unsigned char*, unsigned long);
  Str identity;
  int size = 20, ink = 1;
  const Glyph* get(uint32_t cp);
  const freeink::font::GlyphBitmap* oversized = nullptr;
  unsigned oversizedThreshold = 0;

 public:
  struct {
    uint32_t ram = 0, tf = 0, raster = 0, fallback = 0, blank = 0, unmapped = 0, recordReads = 0;
  } stats;
  Str path;
  ~BodyFont() {
    close();
  }
  void close();
  bool select(const Str& path, int px, int tone);
  // One mapped character per idle slice; the hot glyph and width sets stay untouched.
  void warmOne();
  uint32_t cacheInstance = 0;
  bool cacheForPage(uint32_t cp);
  bool flushPageGlyphs();
  bool allWarmed();
  void primeGlyph(uint32_t cp) {
    (void)get(cp);
  }
  unsigned warmed = 0;
  unsigned hotCapacity() const {
    return 40 * (1 + hotBanks);
  }
  void sizeHotCache(bool allowGrowth);
  void setCacheMode(CacheMode mode);
  unsigned requestsAt80 = 0;
#ifdef ARDUINO
  struct {
    uint64_t ram = 0, tf = 0, raster = 0;
  } cycles;
#endif
  int advance(uint32_t cp);
  bool draw(Canvas& c, int x, int y, uint32_t cp, unsigned style = 0);
  int px() const {
    return size;
  }
  Str generation() const {
    return identity;
  }
  bool usesStreamCmap() const {
    return bool(streamCmap);
  }
  bool active() const {
    return bool(face);
  }
};
}  // namespace jd
