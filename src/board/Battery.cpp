#include "Battery.h"
#include <algorithm>
#include <cstdlib>
#include <iterator>

namespace jd {
bool validBatteryFullRaw(int raw) {
  return raw > BatteryEmptyRaw && raw < BatteryAdcLimit;
}
int batteryPercent(int raw, int fullRaw) {
  if (raw <= 0 || raw >= BatteryAdcLimit || !validBatteryFullRaw(fullRaw)) {
    return -1;
  }
  if (raw <= BatteryEmptyRaw) {
    return 0;
  }
  if (raw >= fullRaw) {
    return 100;
  }

  // Retain the factory knot spacing. The default full point produces the approved 2400 curve.
  static constexpr int knots[] = {1750, 1827, 1888, 1950, 2020, 2078, 2130, 2192, 2254, 2310};
  static constexpr int percent[] = {0, 5, 10, 20, 30, 45, 60, 75, 90, 100};
  int previous = BatteryEmptyRaw;
  for (unsigned i = 1; i < sizeof knots / sizeof knots[0]; ++i) {
    const int current = BatteryEmptyRaw + (knots[i] - BatteryEmptyRaw) * (fullRaw - BatteryEmptyRaw) / 560;
    if (raw < current) {
      return percent[i - 1] + (raw - previous) * (percent[i] - percent[i - 1]) / (current - previous);
    }
    previous = current;
  }
  return 100;
}
void BatteryCalibration::start(uint32_t now) {
  *this = {};
  started = now;
  stage = Stage::Waiting;
}
void BatteryCalibration::cancel() { *this = {}; }
bool BatteryCalibration::due(uint32_t now) const {
  return (stage == Stage::Waiting && uint32_t(now - started) >= 30000) ||
         (stage == Stage::Sampling && uint32_t(now - sampled) >= 10000);
}
bool BatteryCalibration::collect(uint32_t now, int raw) {
  if (!due(now)) return false;
  if (!validBatteryFullRaw(raw)) {
    stage = Stage::Invalid;
    return true;
  }
  sampled = now;
  samples[count++] = uint16_t(raw);
  stage = Stage::Sampling;
  if (count == 5) {
    uint16_t sorted[5];
    std::copy(std::begin(samples), std::end(samples), sorted);
    std::sort(std::begin(sorted), std::end(sorted));
    candidate = sorted[2];
    for (auto sample : samples) deviation = std::max(deviation, std::abs(int(sample) - candidate));
    stage = deviation <= 25 ? Stage::Review : Stage::Unstable;
  }
  return true;
}
void BatteryCalibration::saved(bool ok) {
  if (stage == Stage::Review || stage == Stage::WriteError) {
    stage = ok ? Stage::Saved : Stage::WriteError;
  }
}
}  // namespace jd
