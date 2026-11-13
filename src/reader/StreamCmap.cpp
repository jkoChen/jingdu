// FreeType can open a face even when its Unicode cmap allocation failed.
// Read its format 12/13 groups through the existing sector cache in that case.
#include "Canvas.h"
#include <algorithm>
#include <cstring>
#include <new>

namespace jd {
namespace {
uint16_t u16(const uint8_t* p) {
  return uint16_t(p[0]) << 8 | p[1];
}
uint32_t u32(const uint8_t* p) {
  return uint32_t(u16(p)) << 16 | u16(p + 2);
}
}  // namespace

void BodyFont::openStreamCmap(unsigned minBytes) {
  uint8_t buf[16];
  auto read = [&](uint32_t pos, unsigned n) { return readFont(this, pos, buf, n) == n; };
  if (!read(0, 16)) {
    return;
  }
  uint32_t sfnt = 0;
  if (!memcmp(buf, "ttcf", 4)) {
    sfnt = u32(buf + 12);
    if (!read(sfnt, 12)) {
      return;
    }
  }
  unsigned tables = u16(buf + 4);
  if (tables > (streamSize - sfnt - 12) / 16) {
    return;
  }
  uint32_t cmap = 0, bytes = 0;
  for (unsigned i = 0; i < tables; ++i) {
    if (!read(sfnt + 12 + 16 * i, 16)) {
      return;
    }
    if (!memcmp(buf, "cmap", 4)) {
      cmap = u32(buf + 8);
      bytes = u32(buf + 12);
      break;
    }
  }
  if (bytes < minBytes || bytes < 4 || cmap > streamSize || bytes > streamSize - cmap || !read(cmap, 4) ||
      u16(buf)) {
    return;
  }
  unsigned maps = u16(buf + 2);
  if (maps > (bytes - 4) / 8) {
    return;
  }
  for (unsigned i = maps; i > 0; --i) {
    if (!read(cmap + 4 + 8 * (i - 1), 8)) {
      return;
    }
    unsigned platform = u16(buf), enc = u16(buf + 2);
    if (!(platform == 0 || (platform == 3 && enc == 10))) {
      continue;
    }
    uint32_t sub = u32(buf + 4);
    if (bytes < 16 || sub > bytes - 16 || !read(cmap + sub, 16)) {
      continue;
    }
    unsigned format = u16(buf);
    uint32_t length = u32(buf + 4), groups = u32(buf + 12);
    if ((format != 12 && format != 13) || length < 16 || length > bytes - sub || !groups ||
        groups > (length - 16) / 12 || groups > 0x110000) {
      continue;
    }
    auto map = std::unique_ptr<StreamCmap>(new (std::nothrow) StreamCmap);
    if (!map) {
      return;
    }
    map->base = cmap + sub + 16;
    map->groups = groups;
    map->constant = format == 13;
    map->stride = std::max<uint32_t>(32, (groups + 127) / 128);
    map->samples = (groups + map->stride - 1) / map->stride;
    for (uint32_t b = 0; b < map->samples; ++b) {
      if (!read(map->base + 12 * b * map->stride, 4)) {
        return;
      }
      map->first[b] = u32(buf);
      if (b && map->first[b] <= map->first[b - 1]) {
        return;
      }
    }
    streamCmap = std::move(map);
    return;
  }
}

uint32_t BodyFont::nextMapped(uint32_t after) {
  if (!streamCmap) {
    return face->nextMapped(after);
  }
  const auto& map = *streamCmap;
  auto end = std::upper_bound(map.first, map.first + map.samples, after);
  uint32_t lo = end == map.first ? 0 : uint32_t(end - map.first - 1) * map.stride;
  uint32_t hi = std::min(lo + map.stride + 1, map.groups);
  uint8_t group[12];
  while (lo < hi) {
    uint32_t mid = lo + (hi - lo) / 2;
    if (readFont(this, map.base + 12 * mid, group, sizeof group) != sizeof group) {
      return 0;
    }
    if (u32(group + 4) <= after) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  if (lo >= map.groups || readFont(this, map.base + 12 * lo, group, sizeof group) != sizeof group) {
    return 0;
  }
  return std::max(after + 1, u32(group));
}

uint32_t BodyFont::glyphId(uint32_t cp) {
  if (!streamCmap) {
    return face->glyphId(cp);
  }
  const auto& map = *streamCmap;
  auto end = std::upper_bound(map.first, map.first + map.samples, cp);
  if (end == map.first) {
    return 0;
  }
  uint32_t lo = uint32_t(end - map.first - 1) * map.stride;
  uint32_t hi = std::min(lo + map.stride, map.groups);
  while (lo < hi) {
    uint32_t mid = lo + (hi - lo) / 2;
    uint8_t group[12];
    if (readFont(this, map.base + 12 * mid, group, sizeof group) != sizeof group) {
      return 0;
    }
    uint32_t start = u32(group), last = u32(group + 4);
    if (cp < start) {
      hi = mid;
    } else if (cp > last) {
      lo = mid + 1;
    } else {
      uint32_t id = u32(group + 8);
      uint32_t delta = map.constant ? 0 : cp - start;
      return id <= 65535 && delta <= 65535 - id ? id + delta : 0;
    }
  }
  return 0;
}
}  // namespace jd
