#include "UsbTools.h"
#include <FontAlloc.h>
#include "Version.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <algorithm>
#include <unistd.h>
namespace jd {
namespace {
int hexValue(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}
bool fontName(const Str& s) {
  return !s.empty() && s.find('/') == Str::npos && userPath("/" + s) && (ends(s, ".ttf") || ends(s, ".otf"));
}
}  // namespace
void UsbTools::cancel() {
  if (output) {
    fclose(output);
  }
  if (!temporary.empty()) {
    remove(temporary.c_str());
  }
  output = nullptr;
  temporary.clear();
  target.clear();
  received = 0;
}
void UsbTools::command(const Str& input, App& app, Device& dev, bool ready) {
  if (input.compare(0, 6, "CACHE ") == 0) {
    auto& font = app.reader.font;
    using Mode = BodyFont::CacheMode;
    if (!ready || dev.transferring || app.preparingFont()) {
      Serial.println("JD BUSY");
      return;
    }
    if (input == "CACHE AUTO") {
      font.setCacheMode(Mode::Automatic);
    } else if (input == "CACHE GROUP40") {
      font.setCacheMode(Mode::Group40);
    } else if (input == "CACHE GROUP80") {
      font.setCacheMode(Mode::Group80);
    } else if (input == "CACHE GLOBAL80") {
      font.setCacheMode(Mode::Global80);
    } else {
      Serial.println("JD ERROR CACHE");
      return;
    }
    Serial.println("JD CACHE OK");
    return;
  }
  if (input == "GLYPHS") {
    auto& f = app.reader.font;
    Serial.printf(
        "JD GLYPHS mhz=%u ram=%u tf=%u raster=%u ram_cycles=%llu tf_cycles=%llu raster_cycles=%llu warmed=%u "
        "slots=%u at80=%u\n",
        getCpuFrequencyMhz(), f.stats.ram, f.stats.tf, f.stats.raster, f.cycles.ram, f.cycles.tf,
        f.cycles.raster, f.warmed, f.hotCapacity(), f.requestsAt80);
    return;
  }
  if (input == "INFO") {
    constexpr auto byteCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    multi_heap_info_t info{};
    heap_caps_get_info(&info, byteCaps);
    Serial.printf(
        "JD INFO view=%u card=%d wifi=%d scanning=%d count=%u clock=%lld font=%s active=%d heap=%u stage=%u "
        "percent=%u chapter=%u byte=%u progress=%u firmware=" JINGDU_VERSION
        " size=%d gap=%d margin=%d full_every=%u paged_cmap=%d raster_total=%u tf_total=%u fallback_total=%u "
        "auto_clock_days=%d last_ntp=%lld byte_free=%u byte_largest=%u byte_used=%u free_blocks=%u "
        "used_blocks=%u font_live=%u font_peak=%u\n",
        unsigned(app.view), dev.card, dev.wifi, dev.scanning, unsigned(dev.networks.size()),
        (long long)dev.clock(), app.reader.font.path.c_str(), app.reader.font.active(), ESP.getFreeHeap(),
        app.fontStageNumber(), app.fontPercent(), app.reader.at.chapter, app.reader.at.byte,
        app.reader.progress(), app.reader.layout.size, app.reader.layout.gap, app.reader.layout.margin,
        app.fullEvery(), app.reader.font.usesStreamCmap(), app.reader.font.stats.raster,
        app.reader.font.stats.tf, app.reader.font.stats.fallback, dev.autoClockDays,
        (long long)dev.lastNetworkSync, unsigned(info.total_free_bytes), unsigned(info.largest_free_block),
        unsigned(info.total_allocated_bytes), unsigned(info.free_blocks), unsigned(info.allocated_blocks),
        unsigned(fiFontLiveBytes()), unsigned(fiFontPeakBytes()));
    return;
  }
  if (input == "LOCK" && ready && !dev.transferring && app.view != View::Password && !app.preparingFont()) {
    app.sleep();
    Serial.println("JD LOCK");
    return;
  }
  if (input == "SHOT") {
    // During the final lock refresh the frame is already immutable; deep sleep follows BUSY.
    if ((!ready && !(app.view == View::Lock && !app.dirty)) || app.view == View::Password) {
      Serial.println("JD BUSY");
      return;
    }
    auto& bits = app.canvas.bits;
    Serial.printf("JD FRAME %u %08x\n", unsigned(sizeof bits), unsigned(checksum(bits, sizeof bits)));
    const char* hex = "0123456789abcdef";
    for (size_t start = 0; start < sizeof bits; start += 64) {
      char row[129];
      size_t n = std::min(size_t(64), sizeof bits - start);
      for (size_t i = 0; i < n; i++) {
        auto b = bits[start + i];
        row[2 * i] = hex[b >> 4];
        row[2 * i + 1] = hex[b & 15];
      }
      row[2 * n] = 0;
      Serial.printf("JD PIX %s\n", row);
    }
    Serial.println("JD END");
    return;
  }
  if (app.sleepRequested()) {
    Serial.println("JD BUSY");
    return;
  }
  if (input.compare(0, 5, "DRAG ") == 0) {
    int x, y, rx, ry;
    unsigned held;
    char extra;
    if (!ready || app.view == View::Password ||
        sscanf(input.c_str() + 5, "%d %d %d %d %u %c", &x, &y, &rx, &ry, &held, &extra) != 5 ||
        unsigned(x) >= Canvas::W || unsigned(rx) >= Canvas::W || unsigned(y) >= Canvas::H ||
        unsigned(ry) >= Canvas::H || held > 10000) {
      Serial.println("JD BUSY");
      return;
    }
    app.tap(x, y, rx, ry, held);
    Serial.println("JD DRAG");
    return;
  }
  if (input.compare(0, 7, "READAT ") == 0) {
    unsigned chapter, byte;
    char extra;
    bool valid = sscanf(input.c_str() + 7, "%u %u %c", &chapter, &byte, &extra) == 2;
    Serial.println(ready && valid && app.jumpFromReader({chapter, byte}) ? "JD READAT" : "JD BUSY");
    return;
  }
  if (input.compare(0, 4, "TAP ") == 0) {
    int x, y;
    char extra;
    if (!ready || app.view == View::Password || sscanf(input.c_str() + 4, "%d %d %c", &x, &y, &extra) != 2 ||
        x < 0 || x >= 240 || y < 0 || y >= 416) {
      Serial.println("JD BUSY");
      return;
    }
    app.tap(x, y, x, y);
    Serial.println("JD TAP");
    return;
  }
  if (input.compare(0, 5, "TIME ") == 0) {
    char* end = nullptr;
    auto t = strtoll(input.c_str() + 5, &end, 10);
    bool ok = end && !*end && t >= 1577836800LL && t < 4102444800LL && dev.setTime(t);
    if (ok) {
      app.networkChanged();
    }
    Serial.println(ok ? "JD TIME OK" : "JD TIME ERROR");
    return;
  }
  if (input.compare(0, 5, "FONT ") == 0) {
    Str name = input.substr(5);
    if (!dev.card || !fontName(name)) {
      Serial.println("JD ERROR FONT");
      return;
    }
    Str path = root + "/fonts/" + name;
    auto hash = digest(path);
    Serial.printf("JD FONT %lu %s\n", (unsigned long)length(path), hash.c_str());
    return;
  }
  if (input == "FONTS") {
    if (!dev.card) {
      Serial.println("JD NO CARD");
      return;
    }
    bool more = true;
    unsigned offset = 0;
    while (more && offset < 128) {
      auto entries = list(root + "/fonts", offset, 8, more, true);
      for (const auto& e : entries) {
        Serial.printf("JD ENTRY %c %s\n", e.folder ? 'D' : 'F', e.name.c_str());
      }
      offset += entries.size();
      if (entries.empty()) {
        break;
      }
    }
    Serial.println("JD END");
    return;
  }
  if (input.compare(0, 9, "COPYFONT ") == 0) {
    auto split = input.find(' ', 9), nameAt = split == Str::npos ? split : input.find(' ', split + 1);
    if (!dev.card || output || dev.serving || nameAt == Str::npos) {
      Serial.println("JD ERROR ARGUMENT");
      return;
    }
    auto hash = input.substr(9, split - 9), family = input.substr(split + 1, nameAt - split - 1),
         name = input.substr(nameAt + 1);
    if (hash.size() != 64 || !userPath("/" + family) || family.find('/') != Str::npos || !fontName(name)) {
      Serial.println("JD ERROR ARGUMENT");
      return;
    }
    Str source;
    for (const char* folder : {"/fonts/", "/.fonts/"}) {
      auto path = root + folder + family + "/Regular.ttf";
      if (length(path) > 0 && length(path) <= 32u * 1024 * 1024 && digest(path) == hash) {
        source = path;
        break;
      }
    }
    if (source.empty()) {
      Serial.println("JD NOTFOUND");
      return;
    }
    auto to = root + "/fonts/" + name;
    if (access(to.c_str(), F_OK) == 0 || !makedirs(root + "/fonts")) {
      Serial.println("JD ERROR EXISTS");
      return;
    }
    auto temp = root + "/fonts/.jingdu-font.tmp";
    FILE *in = fopen(source.c_str(), "rb"), *out = fopen(temp.c_str(), "wb");
    bool ok = in && out;
    if (ok) {
      uint8_t buf[1024];
      size_t n;
      while ((n = fread(buf, 1, sizeof buf, in))) {
        if (fwrite(buf, 1, n, out) != n) {
          ok = false;
          break;
        }
        yield();
      }
      ok = ok && !ferror(in) && durable(out);
    }
    if (in) {
      fclose(in);
    }
    if (out) {
      fclose(out);
    }
    ok = ok && digest(temp) == hash && forgetFileIdentity(to) && rename(temp.c_str(), to.c_str()) == 0;
    if (!ok) {
      remove(temp.c_str());
    } else {
      app.dirty = true;
    }
    Serial.println(ok ? "JD COPY OK" : "JD ERROR COPY");
    return;
  }
  if (input.compare(0, 6, "BEGIN ") == 0) {
    cancel();
    auto a = input.find(' ', 6), b = a == Str::npos ? a : input.find(' ', a + 1);
    if (a == Str::npos || b == Str::npos || !dev.card || dev.serving) {
      Serial.println("JD ERROR BEGIN");
      return;
    }
    Str size = input.substr(6, a - 6);
    expectedHash = input.substr(a + 1, b - a - 1);
    Str name = input.substr(b + 1);
    char* end;
    unsigned long n = strtoul(size.c_str(), &end, 10);
    if (*end || n == 0 || n > 32UL * 1024 * 1024 || expectedHash.size() != 64 ||
        !std::all_of(expectedHash.begin(), expectedHash.end(), [](char v) { return hexValue(v) >= 0; }) ||
        !fontName(name)) {
      Serial.println("JD ERROR ARGUMENT");
      return;
    }
    target = root + "/fonts/" + name;
    if (access(target.c_str(), F_OK) == 0 || !makedirs(root + "/fonts")) {
      Serial.println("JD ERROR EXISTS");
      return;
    }
    temporary = root + "/fonts/.jingdu-font.tmp";
    output = fopen(temporary.c_str(), "wb");
    expectedBytes = n;
    lastBlock = millis();
    Serial.println(output ? "JD BEGIN" : "JD ERROR WRITE");
    return;
  }
  if (input.compare(0, 5, "DATA ") == 0) {
    auto sp = input.find(' ', 5);
    char* end = nullptr;
    if (!output || sp == Str::npos) {
      Serial.println("JD ERROR OFFSET");
      return;
    }
    unsigned long offset = strtoul(input.c_str() + 5, &end, 10);
    size_t chars = input.size() - sp - 1;
    if (end != input.data() + sp || offset != received || !chars || chars % 2 || chars > 1024 ||
        chars / 2 > expectedBytes - received) {
      Serial.printf("JD OFFSET %lu\n", (unsigned long)received);
      return;
    }
    uint8_t data[512];
    for (size_t i = 0; i < chars / 2; i++) {
      int a = hexValue(input[sp + 1 + 2 * i]), b = hexValue(input[sp + 2 + 2 * i]);
      if (a < 0 || b < 0) {
        Serial.println("JD ERROR HEX");
        return;
      }
      data[i] = uint8_t((a << 4) | b);
    }
    if (fwrite(data, 1, chars / 2, output) != chars / 2) {
      cancel();
      Serial.println("JD ERROR WRITE");
      return;
    }
    received += chars / 2;
    lastBlock = millis();
    Serial.printf("JD DATA %lu\n", (unsigned long)received);
    return;
  }
  if (input == "COMMIT") {
    bool ok = output && received == expectedBytes && durable(output);
    if (output) {
      fclose(output);
      output = nullptr;
    }
    if (ok) {
      ok = digest(temporary) == expectedHash && access(target.c_str(), F_OK) != 0 &&
           forgetFileIdentity(target) && rename(temporary.c_str(), target.c_str()) == 0;
    }
    cancel();
    if (ok) {
      app.dirty = true;
    }
    Serial.println(ok ? "JD COMMIT OK" : "JD ERROR COMMIT");
    return;
  }
  if (input == "ABORT") {
    cancel();
    Serial.println("JD ABORT");
  }
}
bool UsbTools::poll(App& app, Device& dev, bool ready) {
  if (output && (!dev.card || app.sleepRequested() || millis() - lastBlock > 30000)) {
    cancel();
  }
  unsigned budget = 1400;
  while (budget-- && Serial.available()) {
    int c = Serial.read();
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      bool valid = !discarded && line.compare(0, 3, "JD ") == 0;
      if (valid) {
        command(line.substr(3), app, dev, ready);
      }
      line.clear();
      discarded = false;
      if (valid) {
        return true;
      }
    } else if (!discarded && c >= 32) {
      if (line.size() >= 1150) {
        line.clear();
        discarded = true;
      } else {
        line.push_back(char(c));
      }
    }
  }
  return false;
}
}  // namespace jd
