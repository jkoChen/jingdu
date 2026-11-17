#pragma once
#include <cstdint>

namespace jd {
constexpr int BatteryEmptyRaw = 1750;
constexpr int BatteryFullRaw = 2400;
constexpr int BatteryAdcLimit = 4095;

// Raw ADC units, not millivolts or battery capacity. Returns -1 for invalid input.
bool validBatteryFullRaw(int raw);
int batteryPercent(int raw, int fullRaw = BatteryFullRaw);

// Five fixed sample groups; the device adapter takes a median of three ADC reads per group.
struct BatteryCalibration {
  enum class Stage : uint8_t {
    Ready, Waiting, Sampling, Review, Saved, Unstable, Invalid, WriteError, ResetError
  };
  Stage stage = Stage::Ready;
  uint8_t count = 0;
  uint16_t samples[5]{};
  int candidate = 0, deviation = 0;
  uint32_t started = 0, sampled = 0;
  bool running() const { return stage == Stage::Waiting || stage == Stage::Sampling; }
  void start(uint32_t now);
  void cancel();
  bool due(uint32_t now) const;
  bool collect(uint32_t now, int raw);
  void saved(bool ok);
};
static_assert(sizeof(BatteryCalibration) <= 32, "Keep calibration state bounded on the target board");
}  // namespace jd
