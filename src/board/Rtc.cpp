#include "Rtc.h"

namespace jd::board {
namespace {
constexpr uint8_t kTest = 0x80, kReset = 0x01, kInvalid = 0x03;
uint8_t bcd(int value) {
  return uint8_t((value / 10) * 16 + value % 10);
}
int decimal(uint8_t value) {
  return (value & 15) <= 9 && (value >> 4) <= 9 ? (value >> 4) * 10 + (value & 15) : -1;
}
bool decode(const uint8_t* b, time_t& value) {
  tm local{};
  local.tm_sec = decimal(b[0]);
  local.tm_min = decimal(b[1]);
  local.tm_hour = decimal(b[2]);
  local.tm_mday = decimal(b[4]);
  local.tm_mon = decimal(b[5]) - 1;
  local.tm_year = decimal(b[6]) + 100;
  if (local.tm_sec < 0 || local.tm_sec > 59 || local.tm_min < 0 || local.tm_min > 59 ||
      local.tm_hour < 0 || local.tm_hour > 23 || local.tm_mday < 1 || local.tm_mday > 31 ||
      local.tm_mon < 0 || local.tm_mon > 11 || local.tm_year < 120 || local.tm_year > 199 ||
      !b[3] || (b[3] & 0x80) || (b[3] & (b[3] - 1))) {
    return false;
  }
  const tm expected = local;
  const time_t decoded = mktime(&local);
  // mktime normalizes impossible dates; reject these rather than silently changing the date.
  if (decoded == time_t(-1) || local.tm_year != expected.tm_year || local.tm_mon != expected.tm_mon ||
      local.tm_mday != expected.tm_mday || b[3] != (1u << local.tm_wday)) {
    return false;
  }
  value = decoded;
  return true;
}
}  // namespace

RtcResult readRtc(RtcBus& bus, time_t& value, uint8_t& flags) {
  uint8_t state[3], data[7];
  flags = 0xff;
  if (!bus.read(0x0d, state, sizeof state)) {
    return RtcResult::ReadError;
  }
  flags = state[1];
  if ((state[0] & kTest) || (flags & 0x02) || (state[2] & kReset)) {
    return RtcResult::Untrusted;
  }
  if (!bus.read(0x00, data, sizeof data)) {
    return RtcResult::ReadError;
  }
  time_t decoded;
  if (!decode(data, decoded)) {
    return RtcResult::InvalidTime;
  }
  value = decoded;
  return RtcResult::Ok;
}

RtcResult setRtc(RtcBus& bus, time_t value) {
  tm local{};
  if (!localtime_r(&value, &local) || local.tm_year < 120 || local.tm_year > 199) {
    return RtcResult::InvalidTime;
  }
  uint8_t state[3];
  if (!bus.read(0x0d, state, sizeof state)) {
    return RtcResult::ReadError;
  }
  // After power loss TEST/RESET may be undefined. Preserve compensation and interrupt settings.
  const uint8_t extension = state[0] & ~kTest, control = state[2] & ~kReset;
  if ((extension != state[0] && !bus.write(0x0d, &extension, 1)) ||
      (control != state[2] && !bus.write(0x0f, &control, 1))) {
    return RtcResult::WriteError;
  }
  const uint8_t data[] = {bcd(local.tm_sec), bcd(local.tm_min), bcd(local.tm_hour),
                          uint8_t(1u << local.tm_wday), bcd(local.tm_mday),
                          bcd(local.tm_mon + 1), bcd(local.tm_year - 100)};
  if (!bus.write(0x00, data, sizeof data)) {
    return RtcResult::WriteError;
  }
  uint8_t returned[7];
  time_t actual = 0;
  if (!bus.read(0x00, returned, sizeof returned)) {
    return RtcResult::ReadError;
  }
  if (!decode(returned, actual) || difftime(actual, value) < 0 || difftime(actual, value) > 2) {
    return RtcResult::VerifyError;
  }
  // Only acknowledge a previously invalid clock after the actual calendar has been checked.
  const uint8_t flags = state[1] & ~kInvalid;
  if (!bus.write(0x0e, &flags, 1)) {
    return RtcResult::WriteError;
  }
  uint8_t checked;
  const auto result = readRtc(bus, actual, checked);
  if (result != RtcResult::Ok) {
    return result;
  }
  return checked & kInvalid || difftime(actual, value) < 0 || difftime(actual, value) > 2
             ? RtcResult::VerifyError
             : RtcResult::Ok;
}

const char* rtcResultName(RtcResult result) {
  switch (result) {
    case RtcResult::Ok:
      return "ok";
    case RtcResult::ReadError:
      return "read_error";
    case RtcResult::WriteError:
      return "write_error";
    case RtcResult::InvalidTime:
      return "invalid_time";
    case RtcResult::Untrusted:
      return "untrusted";
    case RtcResult::VerifyError:
      return "verify_error";
  }
  return "unknown";
}
}  // namespace jd::board
