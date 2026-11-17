#pragma once

namespace jd::board {
void setup();
void loop();
void rtcDiagnostics(bool restore);
void batteryDiagnostics(bool sample);
}  // namespace jd::board
