#if defined(ARDUINO) || defined(JD_LAN_TEST)
#include "Lan.h"
#include <ArduinoJson.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef ARDUINO
#include <esp_random.h>
#endif
namespace jd {
static const char webpage[] =
#include "../generated/lan_page.inc"
    ;
static Str param(const Str& url, const Str& k) {
  auto i = url.find('?');
  if (i == Str::npos) {
    return {};
  }
  i++;
  while (i < url.size()) {
    auto j = url.find('&', i);
    Str item = url.substr(i, j - i);
    if (item.compare(0, k.size() + 1, k + "=") == 0) {
      Str s = item.substr(k.size() + 1), r;
      for (size_t p = 0; p < s.size(); p++) {
        if (s[p] == '%') {
          if (p + 2 >= s.size() || !isxdigit(s[p + 1]) || !isxdigit(s[p + 2])) {
            return {};
          }
          r += char(strtol(s.substr(p + 1, 2).c_str(), nullptr, 16));
          p += 2;
        } else {
          r += s[p] == '+' ? ' ' : s[p];
        }
      }
      return r;
    }
    if (j == Str::npos) {
      break;
    }
    i = j + 1;
  }
  return {};
}
void Lan::finish() {
  uploadIdentity.reset();
  if (phase == Phase::Upload && !tmp.empty() && file) {
    result = "传输中断";
  }
  if (file) {
    fclose(file);
    file = nullptr;
  }
  if (!tmp.empty()) {
    remove(tmp.c_str());
    tmp.clear();
  }
  client.stop();
  header.clear();
  phase = Phase::Head;
  left = 0;
}
void Lan::reply(int status, const Str& body, const char* type) {
  client.printf(
      "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %u\r\nCache-Control: "
      "no-store\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n\r\n",
      status, status == 200 ? "OK" : "Error", type, unsigned(body.size()));
  client.write(reinterpret_cast<const uint8_t*>(body.data()), body.size());
  // Closing a socket with unread PUT data can reset the TCP connection and
  // hide the conflict reason from the browser. Drain in small poll slices.
  if (status >= 400 && left && (phase == Phase::Head || phase == Phase::Upload)) {
    phase = Phase::Drain;
    stamp = millis();
    return;
  }
  finish();
}
void Lan::start() {
  stop();
  char p[8];
  snprintf(p, sizeof p, "%06u", unsigned(esp_random() % 1000000));
  pin = p;
  misses = 0;
  pairStarted = millis();
  server.begin();
  enabled = true;
}
void Lan::stop() {
  finish();
  server.end();
  transferName.clear();
  result.clear();
  pin.clear();
  token.clear();
  enabled = false;
}
static Str field(const Str& h, const Str& name) {
  size_t p = h.find("\r\n");
  while (p != Str::npos) {
    p += 2;
    size_t e = h.find("\r\n", p);
    if (e == Str::npos || p == e) {
      break;
    }
    auto line = h.substr(p, e - p);
    auto sep = line.find(':');
    if (sep != Str::npos && lower(line.substr(0, sep)) == name) {
      auto v = line.substr(sep + 1);
      auto i = v.find_first_not_of(" \t");
      return i == Str::npos ? "" : v.substr(i);
    }
    p = e;
  }
  return {};
}
// A single byte range lets the browser resume TF downloads and allows ZIP
// tools to read the central directory without transferring an entire book.
static bool byteRange(const Str& spec, uint32_t size, uint32_t& first, uint32_t& last) {
  if (spec.compare(0, 6, "bytes=") != 0 || !size) {
    return false;
  }
  size_t dash = spec.find('-', 6);
  if (dash == Str::npos) {
    return false;
  }
  auto number = [&](size_t from, size_t end, uint32_t& value) {
    if (from == end) {
      return false;
    }
    value = 0;
    for (size_t i = from; i < end; ++i) {
      if (spec[i] < '0' || spec[i] > '9') {
        return false;
      }
      uint32_t digit = spec[i] - '0';
      if (value > (UINT32_MAX - digit) / 10) {
        return false;
      }
      value = 10 * value + digit;
    }
    return true;
  };
  if (dash == 6) {
    uint32_t suffix;
    if (!number(dash + 1, spec.size(), suffix) || !suffix) {
      return false;
    }
    first = size - std::min(size, suffix);
    last = size - 1;
    return true;
  }
  if (!number(6, dash, first) || first >= size) {
    return false;
  }
  last = size - 1;
  if (dash + 1 < spec.size()) {
    if (!number(dash + 1, spec.size(), last) || last < first) {
      return false;
    }
    last = std::min(last, size - 1);
  }
  return true;
}
void Lan::dispatch() {
  auto a = header.find(' '), b = header.find(' ', a + 1);
  if (a == Str::npos || b == Str::npos) {
    reply(400, "请求无效");
    return;
  }
  auto method = header.substr(0, a), url = header.substr(a + 1, b - a - 1), op = url.substr(0, url.find('?'));
  if (url == "/" && method == "GET") {
    left = sizeof(webpage) - 1;
    client.printf(
        "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: %u\r\n"
        "Cache-Control: no-store\r\nConnection: close\r\n\r\n",
        unsigned(left));
    phase = Phase::Page;
    return;
  }
  auto len = field(header, "content-length");
  char* end = nullptr;
  uint64_t size = strtoull(len.c_str(), &end, 10);
  if ((!len.empty() && (*end || len.front() == '-')) || size > 32u * 1024 * 1024 ||
      !field(header, "transfer-encoding").empty()) {
    reply(400, "长度无效");
    return;
  }
  if (op == "/put" && method == "PUT") {
    left = size;
  }
  if (op == "/pair" && method == "POST") {
    if (size != 6 || misses >= 8 || millis() - pairStarted > 300000) {
      reply(403, "请重新开启服务");
      return;
    }
    if (client.available() < 6) {
      return;
    }
    char value[7]{};
    client.read(reinterpret_cast<uint8_t*>(value), 6);
    if (pin != value) {
      ++misses;
      reply(403, "配对码不符");
      return;
    }
    char buf[33];
    for (int i = 0; i < 4; i++) {
      snprintf(buf + i * 8, 9, "%08lx", (unsigned long)esp_random());
    }
    token = buf;
    reply(200, "{\"token\":\"" + token + "\"}");
    return;
  }
  if (token.empty() || field(header, "x-jingdu-token") != token) {
    reply(403, "请在设备上配对");
    return;
  }
  Str p = param(url, "p"), full = root + p;
  if (!userPath(p, op == "/list")) {
    reply(400, "路径无效");
    return;
  }
  if (op == "/list" && method == "GET") {
    if (!folder(full)) {
      reply(404, "目录不存在");
      return;
    }
    unsigned skip = strtoul(param(url, "skip").c_str(), nullptr, 10);
    bool more;
    JsonDocument d;
    auto fs = d["files"].to<JsonArray>();
    for (auto& f : list(full, skip, 16, more)) {
      auto e = fs.add<JsonObject>();
      e["name"] = f.name;
      e["dir"] = f.folder;
    }
    d["more"] = more;
    Str s;
    serializeJson(d, s);
    reply(200, s);
    return;
  }
  if (op == "/get" && method == "GET") {
    file = fopen(full.c_str(), "rb");
    if (!file || folder(full)) {
      reply(404, "文件不存在");
      return;
    }
    uint32_t total = length(full);
    Str range = field(header, "range");
    if (range.empty()) {
      left = total;
      client.printf("HTTP/1.1 200 OK\r\n");
    } else {
      uint32_t first, last;
      if (!byteRange(range, total, first, last)) {
        client.printf(
            "HTTP/1.1 416 Range Not Satisfiable\r\nContent-Range: bytes */%u\r\n"
            "Content-Length: 0\r\nConnection: close\r\n\r\n",
            total);
        finish();
        return;
      }
      if (fseek(file, first, SEEK_SET)) {
        reply(500, "无法定位文件");
        return;
      }
      left = last - first + 1;
      client.printf("HTTP/1.1 206 Partial Content\r\nContent-Range: bytes %u-%u/%u\r\n", first, last, total);
    }
    client.printf(
        "Content-Type: application/octet-stream\r\nAccept-Ranges: bytes\r\n"
        "Content-Length: %u\r\nConnection: close\r\n\r\n",
        unsigned(left));
    phase = Phase::Download;
    return;
  }
  if (protectedPath && protectedPath(p)) {
    reply(409, "文件使用中");
    return;
  }
  if (op == "/put" && method == "PUT" && !len.empty()) {
    struct stat st{};
    if (!stat(full.c_str(), &st)) {
      reply(409, "同名文件已存在");
      return;
    }
    auto dir = parent(p);
    if (dir == "/books" || dir == "/fonts" || dir == "/courses/english") {
      makedirs(root + dir);
    }
    finalPath = full;
    tmp = parent(full) + "/.jingdu-upload";
    if (!forgetFileIdentity(full)) {
      reply(507, "无法更新卡索引");
      return;
    }
    if (ends(full, ".epub") || ends(full, ".txt") || ends(full, ".ttf") || ends(full, ".otf")) {
      uploadIdentity.startUpload(size);
    }
    file = fopen(tmp.c_str(), "wb");
    if (!file) {
      reply(507, "无法写入");
      return;
    }
    left = size;
    transferName = leaf(p);
    result.clear();
    phase = Phase::Upload;
    return;
  }
  bool ok = false;
  if (method == "POST" && op == "/mkdir") {
    ok = !mkdir(full.c_str(), 0755);
  } else if (method == "POST" && op == "/delete") {
    bool tree = folder(full);
    ok = forgetFileIdentity(full, tree) && (tree ? !rmdir(full.c_str()) : !remove(full.c_str()));
  } else if (method == "POST" && op == "/move") {
    auto t = param(url, "to");
    struct stat st{};
    ok = userPath(t) && (!protectedPath || !protectedPath(t)) && stat((root + t).c_str(), &st) != 0 &&
         forgetFileIdentity(full, folder(full)) && forgetFileIdentity(root + t, folder(full)) &&
         !rename(full.c_str(), (root + t).c_str());
  } else {
    reply(405, "操作无效");
    return;
  }
  reply(ok ? 200 : 409, ok ? "{}" : "文件或目录冲突");
}
void Lan::poll() {
  if (!enabled) {
    return;
  }
  if (!client || !client.connected()) {
    finish();
    client = server.accept();
    if (!client) {
      return;
    }
    stamp = millis();
  }
  if (millis() - stamp > 15000) {
    finish();
    return;
  }
  uint8_t buf[1024];
  if (phase == Phase::Head) {
    if (header.size() < 4 || header.compare(header.size() - 4, 4, "\r\n\r\n") != 0) {
      for (int i = 0; i < 512 && client.available(); ++i) {
        int c = client.read();
        if (c < 0) {
          break;
        }
        header += char(c);
        stamp = millis();
        if (header.size() > 2500) {
          reply(400, "请求过长");
          return;
        }
        if (header.size() >= 4 && header.compare(header.size() - 4, 4, "\r\n\r\n") == 0) {
          break;
        }
      }
    }
    if (header.size() >= 4 && header.compare(header.size() - 4, 4, "\r\n\r\n") == 0) {
      dispatch();
    }
    return;
  }
  if (phase == Phase::Drain) {
    int n = std::min<uint32_t>(left, std::min<int>(sizeof buf, client.available()));
    if (n > 0) {
      int got = client.read(buf, n);
      if (got > 0) {
        left -= got;
        stamp = millis();
      }
    }
    if (!left) {
      finish();
    }
    return;
  }
  if (phase == Phase::Upload) {
    int n = std::min<int>(client.available(), std::min<uint32_t>(left, sizeof buf));
    if (n > 0) {
      int got = client.read(buf, n);
      if (got <= 0) {
        return;
      }
      left -= got;
      if (fwrite(buf, 1, got, file) != unsigned(got)) {
        reply(507, "卡满或写入失败");
        return;
      }
      uploadIdentity.append(buf, got);
      stamp = millis();
    }
    if (!left) {
      bool ok = durable(file);
      ok = fclose(file) == 0 && ok;
      file = nullptr;
      if (ok && rename(tmp.c_str(), finalPath.c_str()) == 0) {
        tmp.clear();
        result = "已存入";
        uploadIdentity.published(finalPath);
        reply(200, "{}");
      } else {
        result = "写入失败";
        reply(507, "提交失败");
      }
    }
    return;
  }
  if (!left) {
    finish();
    return;
  }
  if (phase == Phase::Page) {
    size_t offset = sizeof(webpage) - 1 - left;
    size_t n =
        client.write(reinterpret_cast<const uint8_t*>(webpage + offset), std::min<uint32_t>(left, 1024));
    left -= n;
    if (n) {
      stamp = millis();
    }
    return;
  }
  auto n = fread(buf, 1, std::min<uint32_t>(left, sizeof buf), file);
  if (!n) {
    finish();
    return;
  }
  auto sent = client.write(buf, n);
  if (sent < n && fseek(file, long(sent) - long(n), SEEK_CUR)) {
    finish();
    return;
  }
  left -= sent;
  if (sent) {
    stamp = millis();
  }
}
}  // namespace jd
#endif
