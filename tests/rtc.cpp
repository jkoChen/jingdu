#include "Rtc.h"
#include <array>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>

using namespace jd::board;
namespace {
struct Bus : RtcBus {
  std::array<uint8_t, 16> registers{};
  int failRead = -1, failWrite = -1, shiftSeconds = 0;
  bool ignoreTimeWrite = false;
  unsigned operations = 0;
  bool read(uint8_t reg, uint8_t* data, size_t size) override {
    ++operations;
    assert(size && size <= 7 && reg + size <= registers.size());
    if (reg == failRead) {
      return false;
    }
    memcpy(data, registers.data() + reg, size);
    return true;
  }
  bool write(uint8_t reg, const uint8_t* data, size_t size) override {
    ++operations;
    assert(size && size <= 7 && reg + size <= registers.size());
    if (reg == failWrite) {
      return false;
    }
    if (reg == 0 && ignoreTimeWrite) {
      return true;
    }
    memcpy(registers.data() + reg, data, size);
    if (reg == 0 && shiftSeconds) {
      // The midnight/year-rollover fixture is the independently specified next calendar date.
      const uint8_t next[] = {uint8_t(shiftSeconds - 1), 0, 0, 0x10, 1, 1, 0x26};
      memcpy(registers.data(), next, sizeof next);
    }
    return true;
  }
};
time_t date(int year, int month, int day, int h = 0, int m = 0, int s = 0) {
  tm local{};
  local.tm_year = year - 1900;
  local.tm_mon = month - 1;
  local.tm_mday = day;
  local.tm_hour = h;
  local.tm_min = m;
  local.tm_sec = s;
  return mktime(&local);
}
void valid(Bus& bus, time_t expected) {
  time_t value = 17;
  uint8_t flags;
  assert(readRtc(bus, value, flags) == RtcResult::Ok);
  assert(value == expected);
}
}  // namespace
int main() {
  setenv("TZ", "CST-8", 1);
  tzset();
  Bus bus;
  bus.registers[0x0d] = 0x89;
  bus.registers[0x0e] = 0x23;
  bus.registers[0x0f] = 0x41;
  const auto leap = date(2024, 2, 29, 23, 59, 58);
  assert(setRtc(bus, leap) == RtcResult::Ok);
  assert(bus.registers[3] == 0x10 && bus.registers[2] == 0x23);
  assert(bus.registers[0x0d] == 9 && bus.registers[0x0f] == 0x40 && bus.registers[0x0e] == 0x20);
  assert(bus.operations <= 8);
  valid(bus, leap);
  uint8_t flags;
  time_t output = 123;
  auto broken = bus;
  for (auto reg : {0, 1, 2, 4, 5, 6}) {
    broken = bus;
    broken.registers[reg] = 0x1a;
    assert(readRtc(broken, output, flags) == RtcResult::InvalidTime && output == 123);
  }
  for (uint8_t weekday : {0, 3, 128, 1}) {
    broken = bus;
    broken.registers[3] = weekday;
    assert(readRtc(broken, output, flags) == RtcResult::InvalidTime);
  }
  broken = bus;
  broken.registers[6] = 0x23;
  assert(readRtc(broken, output, flags) == RtcResult::InvalidTime); // Feb 29 in a non-leap year.
  for (int reg : {0x0d, 0}) {
    broken = bus;
    broken.failRead = reg;
    assert(readRtc(broken, output, flags) == RtcResult::ReadError);
  }
  for (auto state : {std::pair<int, uint8_t>{0x0d, 0x80}, {0x0e, 2}, {0x0f, 0x41}}) {
    broken = bus;
    broken.registers[state.first] = state.second;
    assert(readRtc(broken, output, flags) == RtcResult::Untrusted);
  }
  broken = bus;
  broken.registers[0x0e] = 1;
  valid(broken, leap); // VDET alone is a warning.
  for (int reg : {0, 0x0e}) {
    broken = bus;
    broken.failWrite = reg;
    assert(setRtc(broken, leap) == RtcResult::WriteError);
  }
  broken = bus;
  broken.failRead = 0;
  assert(setRtc(broken, leap) == RtcResult::ReadError);
  broken = bus;
  broken.ignoreTimeWrite = true;
  broken.registers[0x0e] = 3;
  assert(setRtc(broken, date(2026, 11, 16)) == RtcResult::VerifyError);
  assert(broken.registers[0x0e] == 3); // A rejected write cannot bless the old clock.
  for (int seconds : {1, 2, 3}) {
    broken = bus;
    broken.shiftSeconds = seconds;
    assert(setRtc(broken, date(2025, 12, 31, 23, 59, 59)) ==
           (seconds <= 2 ? RtcResult::Ok : RtcResult::VerifyError));
  }
  assert(setRtc(bus, date(2019, 12, 31)) == RtcResult::InvalidTime);
  assert(setRtc(bus, date(2100, 1, 1)) == RtcResult::InvalidTime);
  for (int day = 1; day <= 7; ++day) {
    auto value = date(2026, 11, day, 13);
    assert(setRtc(bus, value) == RtcResult::Ok);
    valid(bus, value);
  }
  std::cout << "RTC calendar, validity, readback, rollover and I2C failure checks passed\n";
}
