#pragma once
#include <cstdint>
namespace jd {
enum class ButtonEvent { None, Next, Sleep };
class Button {
  bool sample = false, stable = false, consume = false, waiting = false, second = false;
  uint32_t edge = 0, release = 0;

 public:
  void wake(bool held, uint32_t now) {
    consume = held;
    sample = stable = held;
    edge = now;
    waiting = second = false;
  }
  ButtonEvent poll(bool pressed, uint32_t now) {
    if (pressed != sample) {
      sample = pressed;
      edge = now;
    }
    if (sample != stable && now - edge >= 22) {
      stable = sample;
      if (consume) {
        if (!stable) {
          consume = false;
        }
        return ButtonEvent::None;
      }
      if (stable) {
        if (waiting && edge - release <= 280) {
          waiting = false;
          second = true;
        }
      } else if (second) {
        second = false;
        return ButtonEvent::Sleep;
      } else {
        waiting = true;
        release = now;
      }
    }
    if (waiting && now - release >= 280 && (!sample || edge - release > 280)) {
      waiting = false;
      return ButtonEvent::Next;
    }
    return ButtonEvent::None;
  }
};
}  // namespace jd
