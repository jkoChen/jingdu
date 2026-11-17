#include "Battery.h"
#include <cassert>
#include <iostream>
#include <climits>

using namespace jd;

int main() {
  const int rawKnots[] = {1750, 1839, 1910, 1982, 2063, 2130, 2191, 2263, 2335, 2400};
  const int percent[] = {0, 5, 10, 20, 30, 45, 60, 75, 90, 100};
  for (unsigned i = 0; i < sizeof percent / sizeof percent[0]; ++i) {
    assert(batteryPercent(rawKnots[i]) == percent[i]);
  }
  // The whole curve moves with the chosen full point; interpolation remains 1% granular.
  assert(BatteryFullRaw == 2400);
  assert(batteryPercent(2310) == 84);
  assert(batteryPercent(2399) == 99);
  assert(batteryPercent(2400) == 100);
  assert(batteryPercent(2421) == 100);
  assert(batteryPercent(2448) == 100);
  assert(batteryPercent(2000) == 22);
  assert(batteryPercent(2007) == 23);
  assert(batteryPercent(1) == 0);
  assert(batteryPercent(4094) == 100);

  int previous = 0;
  for (int raw = 1; raw < BatteryAdcLimit; ++raw) {
    const int result = batteryPercent(raw);
    assert(result >= previous && result <= 100);
    previous = result;
  }
  for (int bad : {INT_MIN, -1, 0, 4095, 65535, INT_MAX}) {
    assert(batteryPercent(bad) == -1);
  }
  for (int full : {1751, 1800, 2400, 2418, 2448, 4094}) {
    assert(validBatteryFullRaw(full));
    previous = 0;
    for (int raw = 1; raw < BatteryAdcLimit; ++raw) {
      const int result = batteryPercent(raw, full);
      assert(result >= previous && result <= 100);
      previous = result;
    }
    assert(batteryPercent(full, full) == 100);
  }
  for (int bad : {INT_MIN, 0, 1750, 4095, INT_MAX}) {
    assert(!validBatteryFullRaw(bad));
    assert(batteryPercent(2200, bad) == -1);
  }
  std::cout << "Fixed battery curve, full threshold, interpolation and invalid ADC checks passed\n";
}
