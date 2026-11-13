#if defined(ARDUINO) || defined(JD_LAN_TEST)
#include "ScreenReceiver.h"
#include <algorithm>
#include <cstring>
#include <new>
#include <strings.h>

namespace jd {
namespace {
constexpr char pbm[] = "P4\n240 416\n";
constexpr unsigned frameSize = sizeof(Canvas::bits) + sizeof(pbm) - 1;

// Negative results indicate malformed input, including truncated and overlong UTF-8.
int scalar(const uint8_t* data, unsigned length, unsigned& pos) {
  unsigned a = data[pos++];
  if (a < 128) {
    return a;
  }
  if (a < 194 || a > 244) {
    return -1;
  }
  unsigned tails = a < 224 ? 1 : a < 240 ? 2 : 3;
  unsigned cp = a & (127 >> (tails + 1));
  if (length - pos < tails) {
    return -1;
  }
  for (unsigned i = 0; i < tails; ++i) {
    unsigned b = data[pos++];
    if ((b & 192) != 128) {
      return -1;
    }
    cp = (cp << 6) | (b & 63);
  }
  if ((tails == 2 && cp < 2048) || (tails == 3 && cp < 65536) || (cp >= 0xd800 && cp <= 0xdfff) ||
      cp > 0x10ffff) {
    return -1;
  }
  return int(cp);
}
// Native bitmaps live in flash. The first wireless text frame must work
// without opening the reading font or touching its RAM and TF caches.
const char* textPage(const uint8_t* bytes, unsigned size, Canvas* out) {
  constexpr int px = 15, line = 24;
  int x = 16, y = 16;
  for (unsigned pos = 0; pos < size;) {
    int cp = scalar(bytes, size, pos);
    if (cp < 0) {
      return "invalid_text";
    }
    if (cp == '\r' || cp == '\n') {
      if (cp == '\r' && pos < size && bytes[pos] == '\n') {
        ++pos;
      }
      x = 16;
      y += line;
      if (y + px > Canvas::H - 16) {
        return "text_overflow";
      }
      continue;
    }
    if (cp == '\t') {
      cp = ' ';
    }
    if (cp < 32 || (cp >= 127 && cp <= 159)) {
      return "invalid_text";
    }
    int advance = Canvas::bitmapAdvance(cp, px);
    if (x + advance > Canvas::W - 16) {
      x = 16;
      y += line;
    }
    if (y + px > Canvas::H - 16) {
      return "text_overflow";
    }
    if (out) {
      out->bitmapChar(x, y, cp, px);
    }
    x += advance;
  }
  return nullptr;
}
char* trim(char* p) {
  while (*p == ' ' || *p == '\t') {
    ++p;
  }
  auto end = p + strlen(p);
  while (end > p && (end[-1] == ' ' || end[-1] == '\t')) {
    *--end = 0;
  }
  return p;
}
}  // namespace
void ScreenReceiver::closeClient() {
  client.stop();
  for (auto& b : blocks) {
    b.reset();
  }
  headSize = used = size = 0;
  phase = Phase::Header;
}
bool ScreenReceiver::listen() {
  if (!listening) {
    server.begin();
#ifdef ARDUINO
    listening = bool(server);
#else
    listening = true;
#endif
  }
  return listening;
}
void ScreenReceiver::disconnect() {
  closeClient();
  if (listening) {
    server.end();
  }
  listening = false;
}
void ScreenReceiver::stop() {
  disconnect();
  target.reset();
}
void ScreenReceiver::reply(unsigned status, const char* reason) {
  char body[96];
  int length = snprintf(body, sizeof body, "{\"%s\":\"%s\"}\n", status == 202 ? "status" : "error", reason);
  client.printf(
      "HTTP/1.1 %u %s\r\nContent-Type: application/json\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s",
      status, status == 202 ? "Accepted" : "Error", length, body);
  if (status != 202) {
    // Draining only needs the socket. Release even a partially allocated request now.
    for (auto& block : blocks) {
      block.reset();
    }
    phase = Phase::Drain;
    lastByte = millis();
  } else {
    closeClient();
  }
}
void ScreenReceiver::parse(const Str& key) {
  char* next = strstr(header, "\r\n");
  if (!next) {
    return reply(400, "invalid_header");
  }
  *next = 0;
  if (strcmp(header, "PUT /api/v1/screen HTTP/1.1") && strcmp(header, "PUT /api/v1/screen HTTP/1.0")) {
    return reply(404, "unknown_endpoint");
  }
  char *type = nullptr, *auth = nullptr, *len = nullptr;
  for (auto p = next + 2; *p && *p != '\r'; p = next + 2) {
    next = strstr(p, "\r\n");
    if (!next) {
      return reply(400, "invalid_header");
    }
    *next = 0;
    char* colon = strchr(p, ':');
    if (!colon) {
      return reply(400, "invalid_header");
    }
    *colon = 0;
    char* val = trim(colon + 1);
    char** slot = nullptr;
    if (!strcasecmp(p, "Content-Length")) {
      slot = &len;
    } else if (!strcasecmp(p, "Content-Type")) {
      slot = &type;
    } else if (!strcasecmp(p, "X-Jingdu-Key")) {
      slot = &auth;
    } else if (!strcasecmp(p, "Transfer-Encoding")) {
      return reply(400, "length_required");
    }
    if (slot) {
      if (*slot) {
        return reply(400, "duplicate_header");
      }
      *slot = val;
    }
  }
  if (!auth || key.empty() || key != auth) {
    return reply(401, "invalid_key");
  }
  if (!len || !*len) {
    return reply(411, "length_required");
  }
  if (!type) {
    return reply(415, "unsupported_type");
  }
  char* param = strchr(type, ';');
  if (param) {
    *param++ = 0;
  }
  if (!strcasecmp(trim(type), "text/plain") && (!param || !strcasecmp(trim(param), "charset=utf-8"))) {
    phase = Phase::Text;
  } else if (!param && !strcasecmp(trim(type), "image/x-portable-bitmap")) {
    phase = Phase::Bitmap;
  } else {
    return reply(415, "unsupported_type");
  }
  unsigned limit = phase == Phase::Text ? 4096 : frameSize;
  for (char* p = len; *p; ++p) {
    if (*p < '0' || *p > '9') {
      return reply(400, "invalid_length");
    }
    size = size * 10 + unsigned(*p - '0');
    if (size > limit) {
      return reply(413, "too_large");
    }
  }
  if (phase == Phase::Bitmap && size != frameSize) {
    return reply(422, "invalid_frame");
  }
  // Fragmented ESP32 heap can still supply small input blocks. Text is contiguous for decoding.
  unsigned block = phase == Phase::Text ? 4096 : blockSize;
  for (unsigned i = 0; i * block < size; ++i) {
    blocks[i].reset(new (std::nothrow) uint8_t[std::min(block, size - i * block)]);
    if (!blocks[i]) {
      return reply(503, "low_memory");
    }
  }
}
bool ScreenReceiver::commit() {
  if (phase == Phase::Bitmap && memcmp(blocks[0].get(), pbm, sizeof(pbm) - 1)) {
    reply(422, "invalid_frame");
    return false;
  }
  if (phase == Phase::Text && size) {
    if (const char* err = textPage(blocks[0].get(), size, nullptr)) {
      reply(422, err);
      return false;
    }
  }
  if (!size) {
    bool changed = bool(target);
    target.reset();
    reply(202, "accepted");
    return changed;
  }
  bool first = !target;
  if (first) {
    target.reset(new (std::nothrow) Canvas);
  }
  if (!target) {
    reply(503, "low_memory");
    return false;
  }
  auto previous = first ? 0 : checksum(target->bits, sizeof target->bits);
  if (phase == Phase::Text) {
    target->clear();
    textPage(blocks[0].get(), size, target.get());
  } else {
    for (unsigned i = 0; i < sizeof target->bits; ++i) {
      unsigned off = i + sizeof(pbm) - 1;
      target->bits[i] = ~blocks[off / blockSize][off % blockSize];
    }
  }
  bool changed = first || previous != checksum(target->bits, sizeof target->bits);
  reply(202, "accepted");
  return changed;
}
bool ScreenReceiver::poll(const Str& key) {
  if (!listening) {
    return false;
  }
  if (!client) {
    closeClient();
    client = server.accept();
    if (!client) {
      return false;
    }
    started = lastByte = millis();
  }
  if (millis() - started > 15000 || millis() - lastByte > (phase == Phase::Drain ? 250u : 4000u)) {
    closeClient();
    return false;
  }
  int available = std::min(2048, client.available());
  if (phase == Phase::Header) {
    while (available-- > 0) {
      int c = client.read();
      if (c < 0) {
        break;
      }
      lastByte = millis();
      if (!c || headSize == sizeof(header) - 1) {
        reply(400, "invalid_header");
        break;
      }
      header[headSize++] = char(c);
      if (headSize >= 4 && !memcmp(header + headSize - 4, "\r\n\r\n", 4)) {
        header[headSize] = 0;
        parse(key);
        break;
      }
    }
  }
  if (phase == Phase::Text || phase == Phase::Bitmap) {
    unsigned block = phase == Phase::Text ? 4096 : blockSize;
    unsigned n =
        std::min<unsigned>({size - used, block - used % block, unsigned(std::min(2048, client.available()))});
    if (n) {
      int got = client.read(blocks[used / block].get() + used % block, n);
      if (got > 0) {
        used += unsigned(got);
        lastByte = millis();
      }
    }
    if (used == size) {
      return commit();
    }
  } else if (phase == Phase::Drain) {
    uint8_t scratch[256];
    int n = std::min<int>(sizeof scratch, client.available());
    if (n > 0) {
      client.read(scratch, n);
    }
  }
  if (!client.available() && !client.connected()) {
    closeClient();
  }
  return false;
}
}  // namespace jd
#endif
