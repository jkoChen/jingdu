#pragma once
#if defined(ARDUINO) || defined(JD_LAN_TEST)
#include "Canvas.h"
#ifdef JD_LAN_TEST
#include "Loopback.h"
#else
#include <WiFi.h>
#endif
namespace jd {
class ScreenReceiver {
  WiFiServer server{8080};
  WiFiClient client;
  bool listening = false;
  enum class Phase { Header, Text, Bitmap, Drain } phase = Phase::Header;
  char header[1025]{};
  unsigned headSize = 0, size = 0, used = 0;
  uint32_t started = 0, lastByte = 0;
  static constexpr unsigned blockSize = 2082;
  std::unique_ptr<uint8_t[]> blocks[6];
  std::unique_ptr<Canvas> target;
  void closeClient();
  void reply(unsigned status, const char* reason);
  void parse(const Str& key);
  bool commit();

 public:
  const Canvas* image() const {
    return target.get();
  }
  bool listen();
  void disconnect();
  void stop();
  bool poll(const Str& key);
};
}  // namespace jd
#endif
