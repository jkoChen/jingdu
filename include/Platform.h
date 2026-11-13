#pragma once
#include "Canvas.h"
#include <ctime>
#include <chrono>
namespace jd {
struct Device {
  bool card = false, wifi = false, connected = false, serving = false, transferring = false;
  bool browserPaired = false, pairingExpired = false;
  Str transferName, transferResult;
  bool casting = false, screenReady = false;
  Str screenKey, screenHost, screenError;
  virtual void cast(bool on) {
    casting = on;
  }
  virtual bool renewScreenKey() {
    return false;
  }
  virtual const Canvas* screenImage() const {
    return nullptr;
  }
  int battery = -1;
  float temp = 0, rh = 0;
  bool measured = false;
  Str ip, pin, network, error, scanInfo, clockInfo;
  bool scanning = false;
  int autoClockDays = 0;
  time_t lastNetworkSync = 0;
  bool clockDue(time_t now, time_t retryAt) const {
    if (autoClockDays != 1 && autoClockDays != 7) {
      return false;
    }
    if (retryAt > now && retryAt - now <= 3600) {
      return false;
    }
    return !lastNetworkSync || now < lastNetworkSync || now - lastNetworkSync >= autoClockDays * 86400;
  }
  virtual void requestTime() {}
  virtual bool setTime(time_t) {
    return false;
  }
  std::vector<Str> networks;
  virtual ~Device() = default;
  virtual void brightness(int) {}
  virtual void radio(bool) {}
  virtual void scan() {}
  virtual void connect(const Str&, const Str&) {}
  virtual void service(bool) {}
  virtual void mount() {}
  virtual bool retained(const Str&, Str&) {
    return false;
  }
  virtual void retain(const Str&, const Str&) {}
  virtual uint32_t uptime() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
  }
  virtual time_t clock() {
    return time(nullptr);
  }
  virtual bool powerBusy() const {
    return transferring;
  }
};
}  // namespace jd
