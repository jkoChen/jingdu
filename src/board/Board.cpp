#include "Board.h"
#include <algorithm>
#include <sys/stat.h>
#include <dirent.h>
#include <Arduino.h>
#include <WiFi.h>
#include <SD.h>
#include <Wire.h>
#include <Preferences.h>
#include <esp_sleep.h>
#include <esp_sntp.h>
#include <atomic>
#include <sys/time.h>
#include <driver/rtc_io.h>
#include "App.h"
#include "UsbTools.h"
#include "Button.h"
#include "Lan.h"
#include "ScreenReceiver.h"
#include <ESPmDNS.h>
#include "Il0324StockLuts.h"
// FreeType hinting and EPUB decoding share the Arduino task on the classic ESP32.
SET_LOOP_TASK_STACK_SIZE(24 * 1024)
using namespace jd;
namespace {
SPIClass panel(HSPI), cardBus(VSPI);
Preferences prefs;
constexpr int CS = 5, DC = 19, RST = 16, PanelBusy = 17;
RTC_DATA_ATTR bool asleep = false, haveSample = false;
RTC_DATA_ATTR float lastT = 0, lastRH = 0;
RTC_DATA_ATTR unsigned minutes = 0;
RTC_DATA_ATTR time_t clockRetryAt = 0;
void cmd(uint8_t c) {
  panel.beginTransaction(SPISettings(15000000, MSBFIRST, SPI_MODE0));
  digitalWrite(DC, LOW);
  digitalWrite(CS, LOW);
  panel.transfer(c);
  digitalWrite(CS, HIGH);
  digitalWrite(DC, HIGH);
  panel.endTransaction();
}
void data(const uint8_t* p, size_t n) {
  panel.beginTransaction(SPISettings(15000000, MSBFIRST, SPI_MODE0));
  digitalWrite(DC, HIGH);
  digitalWrite(CS, LOW);
  panel.writeBytes(p, n);
  digitalWrite(CS, HIGH);
  panel.endTransaction();
}
void send(uint8_t c, std::initializer_list<uint8_t> b) {
  cmd(c);
  data(b.begin(), b.size());
}
void table(uint8_t c, const uint8_t* a, size_t n) {
  cmd(c);
  data(a, n);
}
bool working = false, first = true, alt = false, full = false;
unsigned partial = 0;
uint32_t submitted = 0;
void panelBegin() {
  pinMode(CS, OUTPUT);
  digitalWrite(CS, HIGH);
  pinMode(DC, OUTPUT);
  pinMode(RST, OUTPUT);
  pinMode(PanelBusy, INPUT);
  panel.begin(18, -1, 23, CS);
  digitalWrite(RST, HIGH);
  delay(10);
  digitalWrite(RST, LOW);
  delay(10);
  digitalWrite(RST, HIGH);
  delay(10);
  send(0, {255, 1});
  send(1, {3, 16, 63, 63, 3});
  send(6, {23, 55, 61});
  send(0x30, {9});
  send(0x82, {7});
  send(0x61, {240, 1, 160});
  send(0x50, {215});
  first = true;
}
void submit(const Canvas& c, bool force, unsigned interval) {
  full = force || first || partial + 1 >= interval;
  if (full) {
    uint8_t white[120];
    memset(white, 255, sizeof white);
    cmd(0x10);
    for (unsigned i = 0; i < sizeof c.bits / sizeof white; i++) {
      data(white, sizeof white);
    }
    partial = 0;
  } else {
    cmd(0x91);
    send(0x90, {0, 239, 0, 0, 1, 159, 1});
    ++partial;
  }
  table(0x13, c.bits, sizeof c.bits);
  table(0x20, full ? LUT20 : FAST_LUT20, sizeof LUT20);
  table(0x21, full ? LUT21 : FAST_LUT21, sizeof LUT21);
  table(0x24, full ? LUT24 : FAST_LUT24, sizeof LUT24);
  table(0x22, full ? (alt ? LUT22_ALT : LUT22) : (alt ? FAST_LUT22_ALT : FAST_LUT22), sizeof LUT22);
  table(0x23, full ? (alt ? LUT23_ALT : LUT23) : (alt ? FAST_LUT23_ALT : FAST_LUT23), sizeof LUT23);
  send(0x17, {165});
  submitted = millis();
  working = true;
  alt = !alt;
}
bool iread(uint8_t addr, uint8_t reg, uint8_t* b, unsigned n) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom(addr, (uint8_t)n, true) != n) {
    return false;
  }
  for (unsigned i = 0; i < n; i++) {
    b[i] = Wire.read();
  }
  return true;
}
static uint8_t crc8(const uint8_t* p, int n) {
  uint8_t crc = 255;
  while (n--) {
    crc ^= *p++;
    for (int i = 0; i < 8; i++) {
      crc = crc & 128 ? (crc << 1) ^ 0x31 : crc << 1;
    }
  }
  return crc;
}
bool sensor(float& t, float& rh) {
  uint8_t b[7];
  for (uint8_t a : {0x44, 0x45, 0x38}) {
    Wire.beginTransmission(a);
    if (a == 0x38) {
      Wire.write(0xac);
      Wire.write(0x33);
      Wire.write(0);
    } else {
      Wire.write(0x24);
      Wire.write(0);
    }
    if (Wire.endTransmission() != 0) {
      continue;
    }
    delay(a == 0x38 ? 82 : 18);
    int n = a == 0x38 ? 7 : 6;
    if (Wire.requestFrom(a, (uint8_t)n, true) != n) {
      continue;
    }
    for (int i = 0; i < n; i++) {
      b[i] = Wire.read();
    }
    if (a == 0x38) {
      if ((b[0] & 128) || crc8(b, 6) != b[6]) {
        continue;
      }
      rh = ((uint32_t(b[1]) << 12) | (b[2] << 4) | (b[3] >> 4)) * 100.f / 1048576;
      t = (((uint32_t(b[3]) & 15) << 16) | (b[4] << 8) | b[5]) * 200.f / 1048576 - 50;
    } else {
      if (crc8(b, 2) != b[2] || crc8(b + 3, 2) != b[5]) {
        continue;
      }
      t = -45 + 175.f * ((b[0] << 8) | b[1]) / 65535;
      rh = 100.f * ((b[3] << 8) | b[4]) / 65535;
    }
    return true;
  }
  return false;
}
int bcd(uint8_t b) {
  return 10 * (b >> 4) + (b & 15);
}
void rtcTime() {
  uint8_t b[7], ctl[2];
  if (!iread(0x32, 0xe0, ctl, 2) || !iread(0x32, 0, b, 7)) {
    return;
  }
  int hour = bcd(b[2] & 0x3f);
  if (!(ctl[0] & 0x20)) {
    hour = bcd(b[2] & 0x1f) % 12 + ((b[2] & 0x20) ? 12 : 0);
  }
  struct tm local{};
  local.tm_year = 100 + bcd(b[6]);
  local.tm_mon = bcd(b[5] & 31) - 1;
  local.tm_mday = bcd(b[4] & 63);
  local.tm_hour = hour;
  local.tm_min = bcd(b[1] & 127);
  local.tm_sec = bcd(b[0] & 127);
  if (local.tm_year < 120 || local.tm_mon < 0 || local.tm_mon > 11 || !local.tm_mday || local.tm_mday > 31 ||
      hour > 23 || local.tm_min > 59 || local.tm_sec > 59) {
    return;
  }
  timeval tv{mktime(&local), 0};
  settimeofday(&tv, nullptr);
}
std::atomic<bool> gotNetworkTime{false};
bool writeRtc(time_t t) {
  uint8_t control;
  if (!iread(0x32, 0xe0, &control, 1)) {
    return false;
  }
  struct tm local{};
  localtime_r(&t, &local);
  auto bcdValue = [](int v) { return uint8_t((v / 10) * 16 + v % 10); };
  uint8_t hour = (control & 0x20) ? bcdValue(local.tm_hour)
                                  : uint8_t(bcdValue(local.tm_hour % 12 ? local.tm_hour % 12 : 12) |
                                            (local.tm_hour >= 12 ? 0x20 : 0));
  uint8_t data[] = {0,
                    bcdValue(local.tm_sec),
                    bcdValue(local.tm_min),
                    hour,
                    uint8_t(local.tm_wday),
                    bcdValue(local.tm_mday),
                    bcdValue(local.tm_mon + 1),
                    bcdValue(local.tm_year - 100)};
  Wire.beginTransmission(0x32);
  Wire.write(data, sizeof data);
  return Wire.endTransmission() == 0;
}
UsbTools usb;
class Board final : public Device {
 public:
  Lan lan;
  ScreenReceiver screen;
  bool screenOwnsWifi = false, mdns = false;
  uint32_t reconnectAt = 0;
  const Canvas* screenImage() const override {
    return screen.image();
  }
  bool renewScreenKey() override {
    static constexpr char chars[] = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";
    static_assert(sizeof(chars) - 1 == 32);
    Str next;
    do {
      auto bits = esp_random();
      next.clear();
      for (int i = 0; i < 4; ++i) {
        next += chars[bits & 31];
        bits >>= 5;
      }
    } while (next == screenKey);
    if (prefs.putString("screenKey", next.c_str()) != next.size()) {
      return false;
    }
    screenKey = next;
    // Discard any request authenticated before the rotation.
    if (screenReady) {
      screen.disconnect();
      screenReady = screen.listen();
    }
    return true;
  }
  void closeScreenNetwork() {
    screen.disconnect();
    if (mdns) {
      MDNS.end();
    }
    mdns = screenReady = false;
    screenHost.clear();
  }
  void cast(bool on) override {
    if (!on) {
      closeScreenNetwork();
      screen.stop();
      casting = false;
      screenError.clear();
      bool turnOff = screenOwnsWifi && !serving && !autoSync;
      if (screenOwnsWifi && autoSync && !serving) {
        temporaryWifi = true;
      }
      screenOwnsWifi = false;
      if (turnOff) {
        radio(false);
      }
      return;
    }
    casting = true;
    screenOwnsWifi = !wifi || temporaryWifi;
    temporaryWifi = false;
    if (!wifi) {
      radio(true);
    }
    trySaved = false;
    if (!retained("screenKey", screenKey) || screenKey.size() != 4) {
      if (!renewScreenKey()) {
        screenKey.clear();
        screenError = "密钥保存失败，请重试";
        return;
      }
    }
    reconnectAt = millis();
    updateScreenNetwork();
  }
  void updateScreenNetwork() {
    if (!casting || screenKey.empty()) {
      return;
    }
    if (!connected) {
      if (screenReady || mdns) {
        closeScreenNetwork();
      }
      if (!wifi || since || scanning || int32_t(millis() - reconnectAt) < 0) {
        return;
      }
      Str ss, pass;
      reconnectAt = millis() + 20000;
      if (retained("ssid", ss)) {
        retained("pass", pass);
        beginConnection(ss, pass);
        screenError = "正在连接已保存的 Wi-Fi";
      } else {
        screenError = "请先连接 Wi-Fi";
      }
      return;
    }
    if (screenReady) {
      return;
    }
    screenReady = screen.listen();
    if (!screenReady) {
      screenError = "服务启动失败";
      return;
    }
    uint8_t mac[6]{};
    WiFi.macAddress(mac);
    char name[16];
    snprintf(name, sizeof name, "jingdu-%02x%02x", mac[4], mac[5]);
    mdns = MDNS.begin(name);
    if (mdns) {
      screenHost = name;
      MDNS.addService("jingdu", "tcp", 8080);
    }
    screenError.clear();
    Serial.printf("JD_SCREEN ready heap=%u largest=%u file_service=%d\n", ESP.getFreeHeap(),
                  ESP.getMaxAllocHeap(), serving);
  }
  uint32_t since = 0;
  void brightness(int v) override {
    ledcWrite(33, (v * v * 255 + 50) / 100);
  }
  void mount() override {
    if (card) {
      if (SD.cardType() != CARD_NONE) {
        return;
      }
      SD.end();
    }
    card = SD.begin(4, cardBus, 15000000, "/sd", 12, false);
    if (card) {
      uint64_t cap = SD.totalBytes();
      setCacheBudget(cap, cap - SD.usedBytes());
    } else {
      setCacheBudget(0, 0);
    }
  }
  void retain(const Str& k, const Str& v) override {
    if (!prefs.isKey(k.c_str()) || prefs.getString(k.c_str(), "") != v.c_str()) {
      prefs.putString(k.c_str(), v.c_str());
    }
  }
  bool retained(const Str& k, Str& v) override {
    if (!prefs.isKey(k.c_str())) {
      return false;
    }
    auto s = prefs.getString(k.c_str(), "");
    if (!s.length()) {
      return false;
    }
    v = s.c_str();
    return true;
  }
  uint32_t scanAt = 0, syncAt = 0;
  bool scanIssued = false, trySaved = false, syncWaiting = false;
  bool autoSync = false, temporaryWifi = false;
  uint32_t autoStarted = 0;
  unsigned retries = 0;
  void stopTimeRequest() {
    if (syncWaiting) {
      esp_sntp_stop();
    }
    syncWaiting = false;
    gotNetworkTime.store(false);
  }
  void radio(bool on) override {
    temporaryWifi = false;
    if (on) {
      WiFi.persistent(false);
      wifi = WiFi.mode(WIFI_STA);
      error = wifi ? "" : "无线启动失败";
      trySaved = wifi;
      Serial.printf("JD_RADIO on=%d free=%u largest=%u\n", wifi, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    } else {
      autoSync = false;
      stopTimeRequest();
      service(false);
      WiFi.disconnect(false, false);
      WiFi.mode(WIFI_OFF);
      if (scanning || scanIssued) {
        WiFi.scanDelete();
      }
      wifi = connected = scanning = scanIssued = trySaved = syncWaiting = false;
      std::vector<Str>().swap(networks);
      draftS.clear();
      draftP.clear();
      since = 0;
      scanInfo.clear();
      error.clear();
    }
  }
  void scan() override {
    temporaryWifi = false;
    if (!wifi || scanning) {
      return;
    }
    scanning = true;
    scanIssued = false;
    retries = 0;
    scanAt = millis() + 250;
    scanInfo = "正在扫描 2.4 GHz 网络";
  }
  void scanFailed() {
    scanIssued = false;
    if (++retries < 3) {
      scanAt = millis() + 650;
      return;
    }
    scanning = false;
    scanInfo = "扫描失败，点刷新重试";
  }
  bool pollScan() {
    if (!scanning) {
      return false;
    }
    if (!scanIssued) {
      if (int32_t(millis() - scanAt) < 0) {
        return false;
      }
      int n = WiFi.scanNetworks(true, false, false, 180);
      Serial.printf("JD_SCAN start=%d try=%u\n", n, retries + 1);
      if (n == WIFI_SCAN_FAILED) {
        scanFailed();
        return !scanning;
      }
      scanIssued = true;
    }
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) {
      return false;
    }
    if (n < 0) {
      scanFailed();
      return !scanning;
    }
    networks.clear();
    for (int i = 0; i < n && networks.size() < 32; i++) {
      Str name = WiFi.SSID(i).c_str();
      if (!name.empty() && std::find(networks.begin(), networks.end(), name) == networks.end()) {
        networks.push_back(std::move(name));
      }
    }
    WiFi.scanDelete();
    scanning = scanIssued = false;
    scanInfo =
        networks.empty() ? "没有找到网络，点刷新重试" : "找到 " + std::to_string(networks.size()) + " 个网络";
    Serial.printf("JD_SCAN complete=%d visible=%u\n", n, unsigned(networks.size()));
    if (trySaved) {
      trySaved = false;
      Str ss, ps;
      if (retained("ssid", ss)) {
        retained("pass", ps);
        connect(ss, ps);
      }
    }
    return true;
  }
  bool setTime(time_t t) override {
    struct tm local{};
    localtime_r(&t, &local);
    if (local.tm_year < 120 || local.tm_year > 199) {
      clockInfo = "时间无效";
      return false;
    }
    timeval tv{t, 0};
    if (settimeofday(&tv, nullptr)) {
      clockInfo = "校时失败";
      return false;
    }
    clockInfo = writeRtc(t) ? "已校准系统与硬件时钟" : "系统时间已校准";
    return true;
  }
  void requestTime() override {
    if (!connected) {
      clockInfo = "请先连接 Wi-Fi";
      return;
    }
    syncAt = millis();
    syncWaiting = true;
    clockInfo = "正在联网校时";
    esp_sntp_set_time_sync_notification_cb([](timeval*) { gotNetworkTime.store(true); });
    configTzTime("CST-8", "ntp.aliyun.com", "cn.pool.ntp.org", "time.cloudflare.com");
  }
  Str draftS, draftP;
  void connect(const Str& s, const Str& p) override {
    temporaryWifi = false;
    beginConnection(s, p);
  }
  void beginConnection(const Str& s, const Str& p) {
    service(false);
    error = "正在连接";
    draftS = s;
    draftP = p;
    WiFi.begin(s.c_str(), p.c_str());
    since = millis();
  }
  void service(bool on) override {
    if (on) {
      temporaryWifi = false;
      screenOwnsWifi = false;
    }
    if (on && card && connected && ESP.getFreeHeap() > 32768) {
      lan.start();
    } else {
      lan.stop();
    }
    serving = lan.enabled;
    pin = lan.pairing();
  }
  void finishAutoSync() {
    autoSync = false;
    if (temporaryWifi && !casting) {
      radio(false);
    }
  }
  bool scheduleClock(bool allowStart) {
    if (autoSync) {
      if (autoClockDays && millis() - autoStarted < 50000) {
        return false;
      }
      stopTimeRequest();
      clockInfo = autoClockDays ? "联网校时超时，稍后重试" : "";
      finishAutoSync();
      return true;
    }
    if (!allowStart || !autoClockDays || since || syncWaiting || scanning || transferring) {
      return false;
    }
    const time_t now = time(nullptr);
    if (!clockDue(now, clockRetryAt)) {
      return false;
    }
    if (!connected && ESP.getFreeHeap() < 90000) {
      return false;
    }
    clockRetryAt = now + 3600;
    Str name, password;
    if (!connected && !retained("ssid", name)) {
      clockInfo = "请先连接一次 Wi-Fi";
      return true;
    }
    autoStarted = millis();
    autoSync = true;
    if (connected) {
      requestTime();
    } else {
      retained("pass", password);
      if (!wifi) {
        radio(true);
        if (!wifi) {
          autoSync = false;
          clockInfo = "无线启动失败，稍后重试";
          return true;
        }
        temporaryWifi = true;
      }
      trySaved = false;
      beginConnection(name, password);
      clockInfo = "正在连接已保存的 Wi-Fi";
    }
    Serial.printf("JD_AUTO_TIME start days=%d temporary_wifi=%d\n", autoClockDays, temporaryWifi);
    return true;
  }
  bool tick(bool allowClockStart) {
    bool changed = scheduleClock(allowClockStart);
    if (wifi) {
      changed |= pollScan();
      bool up = WiFi.status() == WL_CONNECTED;
      if (up != connected) {
        connected = up;
        changed = true;
        if (up) {
          network = WiFi.SSID().c_str();
          ip = WiFi.localIP().toString().c_str();
          requestTime();
          error.clear();
          if (network == draftS) {
            retain("ssid", draftS);
            retain("pass", draftP);
            draftP.clear();
            draftS.clear();
          }
        } else {
          service(false);
          error = "网络已断开";
        }
      }
      if (up) {
        since = 0;
      }
      if (!up && since && millis() - since > 18000) {
        since = 0;
        draftP.clear();
        error = "连接失败，请检查密码";
        if (autoSync) {
          clockInfo = "Wi-Fi 连接失败，稍后重试";
          finishAutoSync();
        }
        changed = true;
      }
    }
    if (gotNetworkTime.exchange(false) && syncWaiting) {
      lastNetworkSync = time(nullptr);
      const bool rtc = writeRtc(lastNetworkSync);
      clockInfo = rtc ? "网络与硬件时钟已校准" : "系统已校准，硬件时钟未写入";
      prefs.putLong64("lastSync", lastNetworkSync);
      clockRetryAt = 0;
      stopTimeRequest();
      Serial.printf("JD_NETWORK_TIME success rtc=%d automatic=%d\n", rtc, autoSync);
      finishAutoSync();
      changed = true;
    } else if (syncWaiting && millis() - syncAt > 30000) {
      stopTimeRequest();
      clockInfo = "网络校时超时，可重试";
      finishAutoSync();
      changed = true;
    }
    if (casting) {
      const auto before = screenError;
      const bool ready = screenReady;
      updateScreenNetwork();
      if (!connected && !since && !error.empty()) {
        screenError = error;
      }
      changed |= ready != screenReady || before != screenError;
    }
    lan.poll();
    bool transfer = lan.busy(), expired = serving && lan.expired();
    if (transfer != transferring || lan.paired() != browserPaired || expired != pairingExpired ||
        transferName != lan.transferName || transferResult != lan.result) {
      transferring = transfer;
      browserPaired = lan.paired();
      pairingExpired = expired;
      transferName = lan.transferName;
      transferResult = lan.result;
      changed = true;
    }
    return changed;
  }
  bool powerBusy() const override {
    return usb.busy() || lan.busy();
  }
  void enterPowerMode(PowerAction action) {
    service(false);
    WiFi.mode(WIFI_OFF);
    brightness(0);
    if (card && action != PowerAction::ClockSleep) {
      SD.end();
      card = false;
    }
    if (action == PowerAction::Restart) {
      cardBus.end();
      pinMode(4, OUTPUT);
      digitalWrite(4, HIGH);
      pinMode(0, INPUT_PULLUP);
      Serial.println("JD_POWER restart");
      Serial.flush();
      esp_restart();
    }
    cmd(2);
    uint32_t start = millis();
    delay(2);
    while (!digitalRead(PanelBusy) && millis() - start < 2000) {
      delay(2);
    }
    send(7, {165});
    asleep = action == PowerAction::ClockSleep;
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    esp_sleep_enable_ext1_wakeup(UINT64_C(1) << 39, ESP_EXT1_WAKEUP_ANY_HIGH);
    if (asleep) {
      time_t t = time(nullptr);
      esp_sleep_enable_timer_wakeup(uint64_t(60 - (t > 0 ? t % 60 : 0)) * 1000000);
    }
    Serial.println(asleep ? "JD_POWER clock_sleep timer=1" : "JD_POWER long_sleep timer=0");
    Serial.flush();
    esp_deep_sleep_start();
  }
} device;
App app(device);
Button button;
struct Input {
  int kind, x, y, rx, ry;
  unsigned heldStillMs;
};
Input queue[8];
uint8_t head = 0, count = 0;
uint32_t lastInput = 0;
void enqueue(Input e) {
  if (app.preparingFont()) {
    return;
  }
  lastInput = millis();
  if (count == 8) {
    if (e.kind != 2) {
      return;
    }
    head = count = 0;
  }
  queue[(head + count++) % 8] = e;
}
void sample() {
  auto now = millis();
  auto evt = button.poll(digitalRead(39) == HIGH, now);
  if (evt != ButtonEvent::None) {
    enqueue({evt == ButtonEvent::Sleep ? 2 : 1});
  }
  static uint32_t tick = 0;
  static bool contact = false;
  static int x, y, rx, ry;
  static uint32_t downAt, heldStillMs;
  static bool moved;
  if (now - tick < 20) {
    return;
  }
  tick = now;
  uint8_t data[16];
  if (!iread(0x40, 0, data, 16)) {
    return;
  }
  if (data[2] & 0x0f) {
    int nx = ((data[3] & 15) << 8) | data[4], ny = ((data[5] & 15) << 8) | data[6];
    if (nx >= 240 || ny >= 416) {
      return;
    }
    lastInput = now;
    if (!contact) {
      x = nx;
      y = ny;
      downAt = now;
      heldStillMs = 0;
      moved = false;
    } else if (!moved) {
      if (abs(nx - x) > 9 || abs(ny - y) > 9) {
        moved = true;
      }
      heldStillMs = now - downAt;
    }
    rx = nx;
    ry = ny;
    contact = true;
  } else if (contact) {
    contact = false;
    enqueue({0, x, y, rx, ry, heldStillMs});
  }
}
void battery() {
  static const int raw[] = {1750, 1827, 1888, 1950, 2020, 2078, 2130, 2192, 2254, 2310},
                   pct[] = {0, 5, 10, 20, 30, 45, 60, 75, 90, 100};
  int a[3] = {analogRead(36), analogRead(36), analogRead(36)};
  std::sort(a, a + 3);
  int n = a[1];
  device.battery = n <= raw[0] ? 0 : 100;
  for (int i = 1; i < 10; i++) {
    if (n >= raw[i - 1] && n < raw[i]) {
      device.battery = pct[i - 1] + (n - raw[i - 1]) * (pct[i] - pct[i - 1]) / (raw[i] - raw[i - 1]);
      break;
    }
  }
}
bool timed = false;
}  // namespace
void jd::board::setup() {
  Serial.setRxBufferSize(2048);
  Serial.begin(115200);
  setenv("TZ", "CST-8", 1);
  tzset();
  auto wake = esp_sleep_get_wakeup_cause();
  timed = asleep && wake == ESP_SLEEP_WAKEUP_TIMER;
  bool resume = asleep && wake == ESP_SLEEP_WAKEUP_EXT1;
  asleep = false;
  pinMode(25, OUTPUT);
  digitalWrite(25, HIGH);
  if (timed || wake == ESP_SLEEP_WAKEUP_EXT1) {
    rtc_gpio_deinit(GPIO_NUM_39);
  }
  pinMode(39, INPUT);
  pinMode(36, INPUT);
  pinMode(13, INPUT);
  pinMode(12, OUTPUT);
  digitalWrite(12, HIGH);
  Wire.begin(21, 22, 200000);
  Wire.setTimeOut(40);
  delay(50);
  uint8_t firstTouch[21];
  iread(0x40, 0, firstTouch, 21);
  ledcAttach(33, 1000, 8);
  battery();
  if (time(nullptr) < 1577836800) {
    rtcTime();
  }
  if (!timed || minutes++ % 5 == 0) {
    haveSample = sensor(lastT, lastRH);
  }
  device.temp = lastT;
  device.rh = lastRH;
  device.measured = haveSample;
  digitalWrite(25, LOW);
  delay(100);
  if (!timed) {
    cardBus.begin(15, 0, 2, 4);
    device.mount();
  }
  panelBegin();
  prefs.begin("jingdu", false);
  if (prefs.isKey("lastSync")) {
    device.lastNetworkSync = prefs.getLong64("lastSync");
  }
  if (!timed) {
    if (prefs.isKey("recent")) {
      prefs.remove("recent");
    }
    if (prefs.isKey("resume")) {
      prefs.remove("resumeState");
      prefs.remove("resume");
    }
  }
  button.wake(digitalRead(39) == HIGH, millis());
  device.lan.protectedPath = [](const Str& p) { return app.inUse(p); };
  app.begin(timed, resume);
  lastInput = millis();
  Serial.printf("JINGDU_BOOT esp32 240x416 card=%d heap=%lu largest=%lu wake=%d reset=%d\n", device.card,
                (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getMaxAllocHeap(), int(wake),
                int(esp_reset_reason()));
}
void jd::board::loop() {
  if (usb.poll(app, device, !first && !working && !app.dirty)) {
    lastInput = millis();
  }
  sample();
  app.background(!working && !count && millis() - lastInput > 140 && !device.transferring &&
                 ESP.getFreeHeap() > 23552);
  if (device.tick(!app.sleepRequested() &&
                 (timed || app.view == View::Home || app.view == View::Clock || app.view == View::Book))) {
    app.networkChanged();
  }
  if (device.screenReady && device.screen.poll(device.screenKey) && app.view == View::ScreenCanvas) {
    app.dirty = true;
  }
  auto now = millis();
  static uint32_t checked = 0;
  if (now - checked >= 3000 && !timed && !app.sleepRequested()) {
    checked = now;
    if (device.card) {
      DIR* d = opendir("/sd");
      bool present = false;
      if (d) {
        errno = 0;
        readdir(d);
        present = errno == 0;
        closedir(d);
      }
      if (!present) {
        SD.end();
        device.card = false;
        app.removed();
      }
    } else {
      invalidateFileIdentities();
      device.mount();
      if (device.card) {
        app.dirty = true;
      }
    }
  }
  if (working && now - submitted >= 10 && (digitalRead(PanelBusy) == HIGH || now - submitted > 4500)) {
    bool ok = digitalRead(PanelBusy) == HIGH;
    if (ok) {
      if (!full) {
        cmd(0x92);
      }
      table(0x10, app.canvas.bits, sizeof app.canvas.bits);
      first = false;
      app.displayed();
      Serial.printf("JINGDU_FRAME full=%d busy_ms=%lu free=%lu minimum=%lu largest=%lu stack=%u\n", full,
                    (unsigned long)(now - submitted), (unsigned long)ESP.getFreeHeap(),
                    (unsigned long)ESP.getMinFreeHeap(), (unsigned long)ESP.getMaxAllocHeap(),
                    unsigned(uxTaskGetStackHighWaterMark(nullptr)));
    } else {
      Serial.println("JINGDU_DISPLAY_TIMEOUT");
      first = true;
      panelBegin();
      app.dirty = app.forceFull = true;
    }
    working = false;
  }
  // The panel latched the submitted frame. Only the separate next-page buffer
  // may be written while BUSY; the current canvas will be sent to plane 0x10.
  if (working && !timed && !count && !device.transferring && !app.dirty && !app.sleepRequested() &&
      app.view == View::Book && ESP.getFreeHeap() > 23552 && digitalRead(PanelBusy) == LOW) {
    app.reader.prefetch(3);
    // A continuous stream of taps can leave no idle time between refreshes.
    // Advance the digest without touching the canvas held by the panel.
    app.reader.background();
  }
  if (!working) {
    // EXT1 immediately wakes a regular interactive session if the user presses during timed sync.
    if (timed && device.autoSync && digitalRead(39)) {
      device.enterPowerMode(PowerAction::ClockSleep);
    }
    if ((timed || app.sleepRequested()) && !app.dirty && (!device.autoSync || app.sleepRequested()) &&
        !digitalRead(39)) {
      device.enterPowerMode(timed ? PowerAction::ClockSleep : app.powerAction);
    }
    if (count) {
      auto e = queue[head];
      head = (head + 1) % 8;
      --count;
      if (e.kind == 2) {
        app.sleep();
      } else if (e.kind == 1) {
        app.single();
      } else {
        app.tap(e.x, e.y, e.rx, e.ry, e.heldStillMs);
      }
    }
    if (!timed && app.idleMinutes() && !device.transferring && !usb.busy() &&
        now - lastInput > app.idleMinutes() * 60000u) {
      app.sleep();
    }
    if (app.dirty) {
      static uint32_t lastFrame = 0;
      app.paint();
      const auto frame = checksum(app.canvas.bits, sizeof app.canvas.bits);
      if (app.forceFull || first || lastFrame != frame) {
        lastFrame = frame;
        submit(app.canvas, app.forceFull, app.fullEvery());
      } else {
        app.displayed();
      }
      app.forceFull = false;
    }
  }
  static uint32_t env = 0;
  if (!timed && now - env >= 300000) {
    env = now;
    battery();
    device.measured = sensor(device.temp, device.rh);
  }
  static uint32_t lastHotCheck = 0;
  if (millis() - lastHotCheck >= 500 && !working && !count) {
    bool reading = app.view == View::Book && !app.reader.pending && !device.wifi;
    app.reader.font.sizeHotCache(reading && !app.dirty && !app.sleepRequested());
    lastHotCheck = millis();
  }
  static uint32_t lastWarm = 0;
  if (app.view == View::Book && app.reader.pagePrepared() && device.card && !device.serving &&
      !device.scanning && !device.transferring && !usb.busy() && !working && !count && !app.dirty &&
      !app.sleepRequested() && millis() - lastInput > 1500 && millis() - lastWarm >= 100 &&
      ESP.getFreeHeap() > 32768) {
    app.reader.font.warmOne();
    lastWarm = millis();
  }
  delay(2);
}
