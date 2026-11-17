#include "App.h"
#include <array>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>

using namespace jd;
using Stage = BatteryCalibration::Stage;
static_assert(sizeof(BatteryCalibration) <= 40, "Calibration must remain a bounded, small state machine");
struct Board : Device {
  uint32_t now = 0;
  std::array<int, 5> values{2418, 2422, 2419, 2417, 2418};
  unsigned reads = 0, writes = 0, resets = 0;
  bool failWrite = false, failReset = false;
  std::map<Str, Str> saved;
  uint32_t uptime() override { return now; }
  int sampleBattery() override {
    batteryRaw = values[std::min<unsigned>(reads++, 4)];
    battery = batteryPercent(batteryRaw, batteryFull);
    return batteryRaw;
  }
  bool saveBatteryFull(int full) override {
    ++writes;
    if (failWrite) return false;
    assert(validBatteryFullRaw(full));
    batteryFull = full;
    batteryCalibrated = true;
    battery = batteryPercent(batteryRaw, batteryFull);
    return true;
  }
  bool resetBatteryFull() override {
    ++resets;
    if (failReset) return false;
    batteryFull = BatteryFullRaw;
    batteryCalibrated = false;
    return true;
  }
  void retain(const Str& key, const Str& value) override { saved[key] = value; }
  bool retained(const Str& key, Str& value) override {
    auto it = saved.find(key);
    if (it == saved.end()) return false;
    value = it->second;
    return true;
  }
};
Str output;
void image(App& app, const Str& name) {
  app.paint();
  if (output.empty()) return;
  std::ofstream f(output + "/" + name + ".pbm", std::ios::binary);
  f << "P4\n240 416\n";
  for (uint8_t byte : app.canvas.bits) f.put(char(byte ^ 255));
}
void click(App& app, int x, int y) { app.paint(); app.tap(x, y, x, y); }
void complete(App& app, Board& device) {
  device.now += 30000;
  app.background(true);
  for (int i = 1; i < 5; ++i) {
    device.now += 10000;
    app.background(true);
  }
}
int main(int argc, char** argv) {
  if (argc > 1) root = argv[1];
  if (argc > 2) { output = argv[2]; std::filesystem::create_directories(output); }
  BatteryCalibration cal;
  cal.start(100);
  assert(!cal.collect(30099, 2418) && !cal.count);
  assert(cal.collect(30100, 2418) && cal.count == 1);
  assert(!cal.collect(40099, 2418));
  assert(cal.collect(100000, 2418) && cal.count == 2);
  assert(!cal.collect(100001, 2418));  // Delayed ticks never fabricate catch-up samples.
  for (uint32_t now : {110000, 120000, 130000}) assert(cal.collect(now, 2418));
  assert(cal.stage == Stage::Review && cal.count == 5 && cal.candidate == 2418);
  assert(!cal.collect(200000, 2418));
  cal.saved(false);
  assert(cal.stage == Stage::WriteError && cal.candidate == 2418);
  cal.saved(true);
  assert(cal.stage == Stage::Saved);
  for (int invalid : {0, -1, 1750, 4095, 65535}) {
    cal.start(0);
    assert(cal.collect(30000, invalid) && cal.stage == Stage::Invalid);
  }
  for (int limit : {25, 26}) {
    cal.start(UINT32_MAX - 10000);
    uint32_t now = cal.started + 30000;
    for (int sample : {2400 - limit, 2400, 2400 + limit, 2400, 2400}) {
      assert(cal.collect(now, sample));
      now += 10000;
    }
    assert(cal.deviation == limit);
    assert(cal.stage == (limit == 25 ? Stage::Review : Stage::Unstable));
  }
  cal.cancel();
  assert(cal.stage == Stage::Ready && !cal.count && !cal.candidate);

  Board device;
  device.battery = 100;
  App app(device);
  app.view = View::Power;
  image(app, "power");
  click(app, 70, 130);
  assert(app.view == View::BatteryCalibration);
  image(app, "ready");
  click(app, 120, 385);
  assert(app.calibratingBattery() && app.idleMinutes() == 0);
  app.tap(10, 0, 10, 90);
  assert(app.view == View::BatteryCalibration);  // No unrelated tools during sampling.
  device.now = 10000;
  app.background(true);
  image(app, "waiting");
  click(app, 120, 385);
  assert(app.view == View::Power && !device.writes && !device.reads);
  click(app, 70, 130);
  click(app, 120, 385);
  device.now += 30000;
  app.background(false);
  assert(!device.reads);
  app.background(true);
  assert(device.reads == 1);
  for (int i = 1; i < 5; ++i) {
    device.now += 10000;
    app.background(true);
    if (i == 2) image(app, "sampling");
  }
  assert(app.batteryCalibrationStatus().stage == Stage::Review && !device.writes);
  assert(app.batteryCalibrationStatus().candidate == 2418);
  image(app, "review");
  app.tap(10, 0, 10, 90);
  assert(app.view == View::Tools);
  click(app, 10, 15);
  assert(app.view == View::BatteryCalibration && app.batteryCalibrationStatus().stage == Stage::Review);
  device.failWrite = true;
  click(app, 120, 385);
  assert(app.batteryCalibrationStatus().stage == Stage::WriteError && device.batteryFull == 2400);
  image(app, "write-error");
  device.failWrite = false;
  click(app, 120, 385);
  assert(app.batteryCalibrationStatus().stage == Stage::Saved && device.batteryFull == 2418);
  app.tap(120, 385, 120, 385);  // A stale double tap must not save twice.
  assert(device.writes == 2);
  image(app, "saved");
  click(app, 120, 385);
  image(app, "power-custom");
  click(app, 70, 130);
  image(app, "ready-custom");
  device.failReset = true;
  click(app, 120, 330);
  assert(app.batteryCalibrationStatus().stage == Stage::ResetError && device.batteryFull == 2418);
  image(app, "reset-error");
  device.failReset = false;
  click(app, 120, 385);
  assert(app.batteryCalibrationStatus().stage == Stage::Ready && device.batteryFull == 2400);
  assert(!device.batteryCalibrated);
  image(app, "restored");

  for (bool invalid : {false, true}) {
    Board bad;
    bad.battery = 100;
    bad.values = invalid ? std::array<int, 5>{0, 0, 0, 0, 0}
                         : std::array<int, 5>{2400, 2401, 2402, 2444, 2404};
    App failed(bad);
    failed.view = View::BatteryCalibration;
    click(failed, 120, 385);
    complete(failed, bad);
    assert(failed.batteryCalibrationStatus().stage == (invalid ? Stage::Invalid : Stage::Unstable));
    assert(!bad.writes && bad.batteryFull == 2400);
    image(failed, invalid ? "invalid" : "unstable");
    click(failed, 120, 385);
    assert(failed.calibratingBattery());
    failed.sleep();
    assert(failed.view == View::Lock && failed.batteryCalibrationStatus().stage == Stage::Ready);
    App resumed(bad);
    resumed.begin(false, true);
    assert(resumed.view == View::Power && !resumed.calibratingBattery());
  }
  app.view = View::BatteryCalibration;
  click(app, 120, 385);
  device.card = true;
  app.removed();
  assert(app.calibratingBattery());
  std::cout << "Battery calibration: timing, rollover, median, stability, cancellation, retries, "
               "sleep and UI passed; "
            << sizeof(BatteryCalibration) << " bytes of fixed state\n";
}
