#include "Canvas.h"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#ifdef ARDUINO
#include <esp_random.h>
#else
#include <random>
#endif
#include <new>
#include <sys/stat.h>
#include <unistd.h>

namespace jd {
namespace {
struct Header {
  char magic[4], key[65], reserved[3];
  uint32_t page;
};
constexpr unsigned mainPage = UINT32_MAX;
// Three 144-byte records fit inside a single TF sector without straddling two reads.
constexpr unsigned recordsPerSector = 3, sector = 512, slotBytes = 144;
unsigned startOfData(unsigned slots) {
  return (sizeof(Header) + (slots + 7) / 8 + sector - 1) / sector * sector;
}
unsigned recordOffset(unsigned slots, unsigned slot) {
  return startOfData(slots) + slot / recordsPerSector * sector + slot % recordsPerSector * slotBytes;
}
int denseSlot(uint32_t cp) {
  if (cp <= 0xff) return cp;
  if (cp >= 0x4e00 && cp <= 0x9fff) return 432 + cp - 0x4e00;
  if (cp >= 0x2000 && cp <= 0x206f) return 256 + cp - 0x2000;
  if (cp >= 0x3000 && cp <= 0x303f) return 368 + cp - 0x3000;
  if (cp >= 0xff00 && cp <= 0xffef) return 21424 + cp - 0xff00;
  return -1;
}
bool readAt(int fd, unsigned offset, void* data, size_t size) {
  return ::lseek(fd, offset, SEEK_SET) == off_t(offset) && ::read(fd, data, size) == ssize_t(size);
}
bool writeAt(int fd, unsigned offset, const void* data, size_t size) {
  return ::lseek(fd, offset, SEEK_SET) == off_t(offset) && ::write(fd, data, size) == ssize_t(size);
}
void closeFd(int& fd) {
  if (fd >= 0) ::close(fd);
  fd = -1;
}
}  // namespace

bool BodyFont::flushGlyphs(GlyphFile& file, uint8_t* bits, unsigned bytes) {
  if (!file.dirty) return true;
  // A power cut may lose recent hits. Publish presence only after the records reach the card.
  if (::fsync(file.fd) || !writeAt(file.fd, sizeof(Header), bits, bytes) || ::fsync(file.fd)) return false;
  file.dirty = 0;
  return true;
}

bool BodyFont::openGlyphFile(GlyphFile& file, uint8_t* bits, unsigned count, unsigned page, bool create) {
  const unsigned bytes = (count + 7) / 8;
  if (file.page == page && file.fd >= 0) return true;
  if (file.fd >= 0) {
    if (!flushGlyphs(file, bits, bytes)) return false;
    closeFd(file.fd);
  }
  file = {};
  file.page = page;
  Str name = cachePath;
  if (page != mainPage) name += "-" + std::to_string(page);
  Header expected{{'J', 'D', 'G', '3'}, {}, {}, page};
  memcpy(expected.key, disk->signature, sizeof expected.key);
  Header actual{};
  file.fd = ::open(name.c_str(), O_RDWR);
  const bool existed = file.fd >= 0;
  if (existed) {
    if (readAt(file.fd, 0, &actual, sizeof actual) && !memcmp(&actual, &expected, sizeof actual) &&
        readAt(file.fd, sizeof actual, bits, bytes)) return true;
    closeFd(file.fd);
  } else if (errno != ENOENT || !create) {
    return false;
  }
  if (!create) return false;
  const unsigned total = startOfData(count) + (count + recordsPerSector - 1) / recordsPerSector * sector;
  const auto previous = length(name);
  if (!cacheRoom(name, total - std::min<uint32_t>(total, previous))) return false;
  // Publish a fresh file only after reservation and an explicit zero presence map.
  Str tmp = name + ".part";
  int fd = ::open(tmp.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0666);
  if (fd < 0) return false;
  std::memset(bits, 0, bytes);
  const uint8_t zero = 0;
  bool ok = writeAt(fd, total - 1, &zero, 1) && writeAt(fd, 0, &expected, sizeof expected) &&
            writeAt(fd, sizeof expected, bits, bytes) && ::fsync(fd) == 0;
  closeFd(fd);
  if (ok && existed) ::remove(name.c_str());
  if (!ok || ::rename(tmp.c_str(), name.c_str())) {
    ::remove(tmp.c_str());
    return false;
  }
  file.fd = ::open(name.c_str(), O_RDWR);
  return file.fd >= 0;
}

void BodyFont::openGlyphCache() {
  const Str dir = root + "/.jingdu/cache";
  if (!makedirs(dir) || !cacheRoom(dir, 0)) return;
  disk.reset(new (std::nothrow) DiskGlyphs);
  if (!disk) return;
  // Reuse v2's font/size/ink identity. File namespace and header distinguish layouts.
  Str signature = key(identity + "-glyph-v2-" + std::to_string(size) + "-" + std::to_string(ink));
  memcpy(disk->signature, signature.c_str(), sizeof disk->signature);
  cachePath = dir + "/glyph3-" + signature;
  openGlyphFile(disk->main, disk->present, denseSlots, mainPage, true);
  if (disk->main.fd >= 0) {
    // An instance tag in the unused padding lets coverage records distinguish a rebuilt file.
    const unsigned pos = startOfData(denseSlots) - sizeof(cacheInstance);
    if (readAt(disk->main.fd, pos, &cacheInstance, sizeof(cacheInstance)) && !cacheInstance) {
#ifdef ARDUINO
      cacheInstance = esp_random() | 1u;
#else
      cacheInstance = std::random_device{}() | 1u;
#endif
      if (!writeAt(disk->main.fd, pos, &cacheInstance, sizeof(cacheInstance)) || ::fsync(disk->main.fd))
        cacheInstance = 0;
    }
  }
  Str legacy = dir + "/glyph2-" + signature;
  disk->legacy = ::open(legacy.c_str(), O_RDONLY);
  if (disk->legacy >= 0) {
    char header[69];
    if (readAt(disk->legacy, 0, header, sizeof header) && !memcmp(header, "JDG2", 4) &&
        !memcmp(header + 4, disk->signature, sizeof disk->signature)) {
      const auto n = length(legacy);
      disk->legacyCount = n > 2117 ? std::min<uint32_t>(65535u, (n - 2117) / 148) : 0;
    }
    if (!disk->legacyCount) closeFd(disk->legacy);
  }
  if (disk->main.fd < 0 && disk->legacy < 0) closeGlyphCache();
}

void BodyFont::closeGlyphCache() {
  if (disk) {
    flushGlyphs(disk->main, disk->present, sizeof disk->present);
    flushGlyphs(disk->extra, disk->extraPresent, sizeof disk->extraPresent);
    closeFd(disk->main.fd);
    closeFd(disk->extra.fd);
    closeFd(disk->legacy);
    disk.reset();
  }
  cacheInstance = 0;
  cachePath.clear();
}

BodyFont::GlyphFile* BodyFont::glyphSlot(uint32_t cp, bool create, unsigned& slot, uint8_t*& bits) {
  if (!disk || disk->main.fd < 0 || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return nullptr;
  const int dense = denseSlot(cp);
  if (dense >= 0) {
    slot = unsigned(dense);
    bits = disk->present;
    return &disk->main;
  }
  slot = cp & 255;
  bits = disk->extraPresent;
  if (!openGlyphFile(disk->extra, bits, 256, cp >> 8, create)) return nullptr;
  return &disk->extra;
}

bool BodyFont::readLegacyGlyph(uint32_t cp, Glyph& glyph) {
  if (disk->legacy < 0) return false;
  struct LegacyRecord { Glyph glyph; uint16_t previous; uint32_t crc; } rec{};
  static_assert(sizeof(rec) == 148);
  uint16_t node = 0;
  unsigned bucket = (cp * 2654435761u) >> 22;
  if (!readAt(disk->legacy, 69 + 2 * bucket, &node, sizeof node)) return false;
  while (node && node <= disk->legacyCount) {
    ++stats.recordReads;
    if (!readAt(disk->legacy, 2117 + (node - 1) * sizeof rec, &rec, sizeof rec) ||
        rec.crc != checksum(&rec, offsetof(LegacyRecord, crc))) return false;
    if (rec.glyph.cp == cp && unsigned(rec.glyph.w) * rec.glyph.h <= 1024 && rec.glyph.advance <= 64) {
      glyph = rec.glyph;
      saveGlyph(glyph);
      return true;
    }
    if (rec.previous >= node) return false;
    node = rec.previous;
  }
  return false;
}

bool BodyFont::readGlyph(uint32_t cp, Glyph& glyph, bool remember) {
  if (!disk) return false;
  unsigned slot = 0;
  uint8_t* bits = nullptr;
  auto* file = glyphSlot(cp, false, slot, bits);
  bool hit = false;
  if (file && (bits[slot / 8] & (1u << (slot % 8)))) {
    GlyphRecord rec{};
    static_assert(sizeof rec == slotBytes);
    ++stats.recordReads;
    const unsigned count = file == &disk->main ? denseSlots : 256;
    hit = readAt(file->fd, recordOffset(count, slot), &rec, sizeof rec) &&
          rec.glyph.cp == cp && rec.crc == checksum(&rec.glyph, sizeof rec.glyph) &&
          unsigned(rec.glyph.w) * rec.glyph.h <= 1024 && rec.glyph.advance <= 64;
    if (hit) glyph = rec.glyph;
    else bits[slot / 8] &= ~(1u << (slot % 8));
  }
  if (!hit && !readLegacyGlyph(cp, glyph)) return false;
  if (remember) {
    const unsigned width = (cp ^ (cp >> 8)) & 255;
    widthKeys[width] = cp;
    widths[width] = glyph.advance;
  }
  return true;
}

void BodyFont::saveGlyph(const Glyph& glyph) {
  unsigned slot = 0;
  uint8_t* bits = nullptr;
  auto* file = glyphSlot(glyph.cp, true, slot, bits);
  if (!file) return;
  const unsigned count = file == &disk->main ? denseSlots : 256;
  GlyphRecord rec{};
  static_assert(sizeof rec == slotBytes);
  rec.glyph = glyph;
  rec.crc = checksum(&rec.glyph, sizeof rec.glyph);
  if (!writeAt(file->fd, recordOffset(count, slot), &rec, sizeof rec)) return;
  bits[slot / 8] |= 1u << (slot % 8);
  ++disk->written;
  if (++file->dirty >= 64) flushGlyphs(*file, bits, (count + 7) / 8);
}
}  // namespace jd
