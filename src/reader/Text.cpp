#include "Text.h"
#include <algorithm>
#include <cstring>
namespace jd {
#include "../generated/gb_table.inc"
EncodingInfo detect(FILE* f) {
  unsigned char b[1024];
  rewind(f);
  size_t n = fread(b, 1, sizeof b, f);
  rewind(f);
  if (n >= 4 && memcmp(b, "\xff\xfe\0\0", 4) == 0) {
    return {Encoding::Utf32le, 4};
  }
  if (n >= 4 && memcmp(b, "\0\0\xfe\xff", 4) == 0) {
    return {Encoding::Utf32be, 4};
  }
  if (n >= 2 && b[0] == 255 && b[1] == 254) {
    return {Encoding::Utf16le, 2};
  }
  if (n >= 2 && b[0] == 254 && b[1] == 255) {
    return {Encoding::Utf16be, 2};
  }
  if (n >= 3 && memcmp(b, "\xef\xbb\xbf", 3) == 0) {
    return {Encoding::Utf8, 3};
  }
  const char *p = (char*)b, *end = p + n;
  while (p < end) {
    const char* start = p;
    auto cp = nextChar(p, end);
    if (cp == 0xfffd && !(end - start >= 3 && !memcmp(start, "\xef\xbf\xbd", 3)) &&
        !(n == sizeof b && end - start < 4)) {
      return {Encoding::Gb, 0};
    }
  }
  return {Encoding::Utf8, 0};
}
constexpr uint32_t Stop = UINT32_MAX;
static int unit(FILE* f, bool le) {
  int a = fgetc(f), b = fgetc(f);
  if (b < 0) {
    return -1;
  }
  return le ? a | (b << 8) : (a << 8) | b;
}
uint32_t decode(FILE* f, Encoding enc) {
  if (enc == Encoding::Utf16le || enc == Encoding::Utf16be) {
    bool le = enc == Encoding::Utf16le;
    int a = unit(f, le);
    if (a < 0) {
      return Stop;
    }
    if (a >= 0xd800 && a <= 0xdbff) {
      int b = unit(f, le);
      if (b >= 0xdc00 && b <= 0xdfff) {
        return 0x10000 + ((a - 0xd800) << 10) + (b - 0xdc00);
      }
      if (b >= 0) {
        fseek(f, -2, SEEK_CUR);
      }
      return 0xfffd;
    }
    return a >= 0xdc00 && a <= 0xdfff ? 0xfffd : a;
  }
  if (enc == Encoding::Utf32le || enc == Encoding::Utf32be) {
    bool le = enc == Encoding::Utf32le;
    int a = unit(f, le), b = unit(f, le);
    if (a < 0 || b < 0) {
      return Stop;
    }
    uint32_t v = le ? uint32_t(a) | (uint32_t(b) << 16) : (uint32_t(a) << 16) | b;
    return v > 0x10ffff || (v >= 0xd800 && v <= 0xdfff) ? 0xfffd : v;
  }
  int a = fgetc(f);
  if (a == EOF) {
    return Stop;
  }
  if (a < 128) {
    return a;
  }
  if (enc == Encoding::Gb) {
    if (a < 129 || a > 254) {
      return 0xfffd;
    }
    int b = fgetc(f);
    if (b >= 48 && b <= 57) {
      int c = fgetc(f), d = fgetc(f);
      if (c < 129 || c > 254 || d < 48 || d > 57) {
        return 0xfffd;
      }
      unsigned idx = (((a - 129) * 10 + b - 48) * 126 + c - 129) * 10 + d - 48;
      auto it = std::lower_bound(std::begin(ranges), std::end(ranges), idx,
                                 [](Range r, unsigned v) { return r.last < v; });
      return it != std::end(ranges) && idx >= it->first ? it->unicode + idx - it->first : 0xfffd;
    }
    if (b < 64 || b == 127 || b > 254) {
      if (b >= 0) {
        ungetc(b, f);
      }
      return 0xfffd;
    }
    return gb2[a - 129][b - 64 - (b > 127)];
  }
  char buf[4] = {char(a)};
  int count = a >= 0xf0 && a <= 0xf4 ? 4 : a >= 0xe0 && a < 0xf0 ? 3 : a >= 0xc2 && a < 0xe0 ? 2 : 1;
  for (int i = 1; i < count; ++i) {
    int b = fgetc(f);
    if (b < 0) {
      return 0xfffd;
    }
    if ((b & 192) != 128) {
      ungetc(b, f);
      return 0xfffd;
    }
    buf[i] = b;
  }
  const char* p = buf;
  return nextChar(p, buf + count);
}
bool scanPage(FILE* f, EncodingInfo enc, uint32_t offset, const Layout& l, BodyFont& font, Page& p,
              Cursor& cursor, unsigned quota, bool& finished) {
  finished = false;
  if (!cursor.x) {
    p.start = std::max<uint32_t>(offset, enc.bom);
    p.end = p.start;
    p.count = 0;
    p.eof = false;
    if (fseek(f, p.start, SEEK_SET)) {
      return false;
    }
    cursor.x = l.margin;
  } else if (fseek(f, p.end, SEEK_SET)) {
    return false;
  }
  int& x = cursor.x;
  int& y = cursor.y;
  long& wordStart = cursor.wordStart;
  unsigned& wordIndex = cursor.wordIndex;
  int& wordX = cursor.wordX;
  const int right = 240 - l.margin, bottom = l.bodyBottom();
  while (quota-- && y + l.size <= bottom && p.count < std::size(p.chars)) {
    long pos = ftell(f);
    uint32_t cp = decode(f, enc.enc);
    if (cp == Stop) {
      if (ferror(f)) {
        return false;
      }
      p.eof = true;
      break;
    }
    unsigned style = 0;
    if (l.rich && cp == 29) {
      int flag = fgetc(f);
      if (flag >= '1' && flag <= '3') {
        style = flag - '0';
      }
      cp = decode(f, enc.enc);
      if (cp == Stop) {
        p.eof = true;
        break;
      }
    }
    if (l.rich && cp == 27) {
      char ref[5] = {};
      if (fread(ref, 1, 4, f) != 4) {
        p.eof = true;
        break;
      }
      if (y + 88 > bottom) {
        fseek(f, pos, SEEK_SET);
        y = bottom;
        break;
      }
      p.chars[p.count++] = {uint32_t(strtoul(ref, nullptr, 16)), 0x8000, uint16_t(y)};
      y += 88;
      x = l.margin;
      wordStart = -1;
      continue;
    }
    if (cp == '\r') {
      long next = ftell(f);
      if (decode(f, enc.enc) != '\n') {
        fseek(f, next, SEEK_SET);
      }
      cp = '\n';
    }
    if (cp == '\n') {
      x = l.margin;
      y += l.size + l.gap;
      wordStart = -1;
      continue;
    }
    if (cp < 32) {
      cp = ' ';
    }
    bool word =
        (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') || (cp >= '0' && cp <= '9') || cp == '\'';
    if (word && wordStart < 0) {
      wordStart = pos;
      wordIndex = p.count;
      wordX = x;
    } else if (!word) {
      wordStart = -1;
    }
    int adv = font.advance(cp);
    if (x + adv > right && x > l.margin) {
      long resume = pos;
      if (word && wordStart >= 0 && wordX > l.margin) {
        resume = wordStart;
        p.count = wordIndex;
      } else if (cursor.previousPosition >= 0 && p.count && p.chars[p.count - 1].y == y &&
                 p.chars[p.count - 1].x > l.margin) {
        auto inSet = [](const char32_t* set, uint32_t value) {
          while (*set) {
            if (*set++ == value) {
              return true;
            }
          }
          return false;
        };
        auto previous = p.chars[p.count - 1].cp & 0x1fffff;
        if (inSet(U"，。！？、：；）】》」』”’", cp) || inSet(U"（【《「『“‘", previous)) {
          resume = cursor.previousPosition;
          --p.count;
        }
      }
      if (fseek(f, resume, SEEK_SET)) {
        return false;
      }
      x = l.margin;
      y += l.size + l.gap;
      wordStart = -1;
      continue;
    }
    cursor.previousPosition = pos;
    p.chars[p.count++] = {cp | (style << 24), uint16_t(x), uint16_t(y)};
    x += adv;
  }
  auto end = ftell(f);
  if (end < 0) {
    return false;
  }
  p.end = end;
  finished = p.eof || y + l.size > bottom || p.count >= std::size(p.chars);
  if (finished && !p.eof) {
    int ch = fgetc(f);
    if (ch == EOF && !ferror(f)) {
      p.eof = true;
    } else if (ch >= 0) {
      ungetc(ch, f);
    }
  }
  return !ferror(f);
}
bool layoutPage(FILE* f, EncodingInfo e, uint32_t at, const Layout& l, BodyFont& font, Page& p) {
  Cursor cur;
  bool done = false;
  return scanPage(f, e, at, l, font, p, cur, UINT32_MAX, done) && done;
}

}  // namespace jd
