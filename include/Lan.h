#pragma once
#include "Storage.h"
#if defined(ARDUINO) || defined(JD_LAN_TEST)
#ifdef JD_LAN_TEST
#include "Loopback.h"
#else
#include <WiFi.h>
#endif
namespace jd {
class Lan {
  WiFiServer server{80};
  WiFiClient client;
  Str header, pin, token, tmp, finalPath;
  FileIdentity uploadIdentity;
  FILE* file = nullptr;
  enum class Phase { Head, Upload, Download, Page, Drain };
  Phase phase = Phase::Head;
  uint32_t left = 0, stamp = 0, pairStarted = 0;
  unsigned misses = 0;
  void finish();
  void reply(int, const Str&, const char* type = "application/json");
  void dispatch();

 public:
  Str transferName, result;
  bool paired() const {
    return !token.empty();
  }
  bool expired() const {
    return misses >= 8 || millis() - pairStarted > 300000;
  }
  bool enabled = false;
  bool busy() const {
    return file != nullptr;
  }
  Str pairing() const {
    return pin;
  }
  void start();
  void stop();
  void poll();
  bool (*protectedPath)(const Str&) = nullptr;
};
}  // namespace jd
#endif
