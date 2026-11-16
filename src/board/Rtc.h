#pragma once
#include <cstddef>
#include <cstdint>
#include <ctime>

namespace jd::board {
// RX8025T at 0x32. Fixed-size transfers; the board adapter supplies the I2C timeout.
class RtcBus {
 public:
  virtual ~RtcBus() = default;
  virtual bool read(uint8_t reg, uint8_t* data, size_t size) = 0;
  virtual bool write(uint8_t reg, const uint8_t* data, size_t size) = 0;
};
enum class RtcResult { Ok, ReadError, WriteError, InvalidTime, Untrusted, VerifyError };
// The device stores local Beijing time, matching the factory firmware.
RtcResult readRtc(RtcBus& bus, time_t& value, uint8_t& flags);
RtcResult setRtc(RtcBus& bus, time_t value);
const char* rtcResultName(RtcResult result);
}  // namespace jd::board
