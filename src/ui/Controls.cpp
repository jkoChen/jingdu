#include "App.h"
#include <algorithm>

namespace jd {
void App::hit(int x, int y, int w, int h, std::function<void()> fn) {
  hits.push_back({x, y, w, h, std::move(fn)});
}
void App::btn(int x, int y, int w, int h, const Str& s, std::function<void()> fn) {
  if (s == "上一页" || s == "下一页") {
    pageControl(s == "下一页", std::move(fn));
    return;
  }
  canvas.border(x, y, w, h);
  int inset = std::max(4, (w - Canvas::textWidth(s)) / 2);
  canvas.ui(x + inset, y + (h - 15) / 2, s, 15, w - inset - 3);
  hit(x, y, w, h, std::move(fn));
}
void App::entry(int y, const Str& label, const Str& value, std::function<void()> action, const Str& detail) {
  int valueWidth = std::min(77, Canvas::textWidth(value, 12));
  canvas.ui(16, y + (detail.empty() ? 14 : 4), label, 15, 184 - valueWidth, true, studying());
  if (!detail.empty()) {
    canvas.ui(16, y + 25, detail, 12, 186, true, studying());
  }
  if (valueWidth) {
    canvas.ui(202 - valueWidth, y + 15, value, 12, valueWidth);
  }
  canvas.icon("next", 208, y + 11);
  hit(0, y, 240, 44, std::move(action));
}
void App::row(int n, const Str& label, std::function<void()> fn, const Str& detail) {
  entry(48 + n * 47, label, "", std::move(fn), detail);
}
void App::option(int y, const Str& label, bool current, std::function<void()> action) {
  canvas.ui(16, y + 13, label, 15, 181, true, studying());
  if (current) {
    canvas.icon("tick", 207, y + 11);
  }
  hit(0, y, 240, 44, std::move(action));
}
bool App::studying() const {
  return (view >= View::Learn && view <= View::Review) || view == View::Choice || view == View::Recall ||
         view == View::Vocab;
}
void App::compact(int slot, int top, const Str& label, std::function<void()> action, bool primary) {
  int x = slot == 0 ? 10 : slot == 1 ? 86 : 162;
  if (primary) {
    canvas.rect(x, top + 9, 68, 23);
  } else {
    canvas.border(x, top + 9, 68, 23);
  }
  canvas.ui(x + (68 - Canvas::textWidth(label, 12)) / 2, top + 14, label, 12, 68, !primary);
  hit(slot * 80, top, 80, 36, std::move(action));
}
void App::pageCount(unsigned page, unsigned count) {
  Str label = std::to_string(page + 1) + " / " + std::to_string(count);
  canvas.ui(120 - Canvas::textWidth(label, 12) / 2, 394, label, 12);
}
bool App::studyPager(unsigned pages) {
  studyPageCount = pages;
  if (textPage >= pages) {
    textPage = 0;
    dirty = true;
    return false;
  }
  if (pages > 1) {
    pageCount(textPage, pages);
    if (textPage) {
      pageControl(false, [this] {
        --textPage;
        dirty = true;
      });
    }
    if (textPage + 1 < pages) {
      pageControl(true, [this] {
        ++textPage;
        dirty = true;
      });
    }
  }
  return textPage + 1 == pages;
}
void App::brightnessControl() {
  auto& c = canvas;
  int level = light;
  c.rect(0, 44, 240, 60, false);
  auto sun = [&](int cx, bool bright) {
    int r = bright ? 5 : 3, reach = bright ? 10 : 6;
    for (int y = -reach; y <= reach; y++) {
      for (int x = -reach; x <= reach; x++) {
        int d = x * x + y * y, span = std::max(std::abs(x), std::abs(y));
        if ((d <= r * r && (bright || d > (r - 1) * (r - 1))) ||
            ((x == 0 || y == 0) && span >= r + 2 && span <= reach) ||
            (bright && std::abs(x) == std::abs(y) && span >= 6 && span <= 7)) {
          c.dot(cx + x, 78 + y);
        }
      }
    }
  };
  sun(20, false);
  sun(220, true);
  c.rect(45, 78, 151, 1);
  c.rect(45, 77, 15 * level, 3);
  for (int i = 0; i <= 10; i++) {
    int half = i % 5 == 0 ? 4 : 2;
    c.rect(45 + i * 15, 78 - half, 1, 2 * half + 1);
  }
  int x = 45 + level * 15;
  c.rect(x - 2, 73, 5, 11);
  Str label = std::to_string(level);
  c.ui(x - Canvas::textWidth(label, 12) / 2, 55, label, 12);
  auto set = [this](int value) {
    value = std::clamp(value, 0, 10);
    if (value == light) {
      return;
    }
    light = value;
    dev.brightness(light);
    storeSettings();
    dirty = true;
  };
  hit(0, 44, 38, 60, [set, level] { set(level - 1); });
  for (int i = 0; i <= 10; ++i) {
    hit(38 + 15 * i, 44, 15, 60, [set, i] { set(i); });
  }
  hit(203, 44, 37, 60, [set, level] { set(level + 1); });
}
void App::menuLine(int y, const Str& label, const char* icon, std::function<void()> action) {
  canvas.ui(16, y + 14, label, 15, 190);
  canvas.icon(icon, 207, y + 12);
  if (action) {
    hit(0, y, 240, 44, std::move(action));
  }
}
void App::primary(int x, int y, int w, const Str& label, std::function<void()> action) {
  canvas.rect(x, y, w, 40);
  canvas.ui(x + std::max(4, (w - Canvas::textWidth(label)) / 2), y + 12, label, 15, w - 8, false);
  hit(x, y - 2, w, 44, std::move(action));
}
void App::stepper(int y, const Str& label, const Str& value, int current, int low, int high,
                  std::function<void(int)> change) {
  canvas.ui(16, y + 15, label, 16, 74);
  canvas.ui(151 - Canvas::textWidth(value, 12) / 2, y + 16, value, 12, 65);
  if (current > low) {
    canvas.rect(111, y + 22, 11, 2);
    hit(94, y, 44, 45, [change] { change(-1); });
  }
  if (current < high) {
    canvas.icon("plus", 203, y + 13);
    hit(188, y, 50, 45, [change] { change(1); });
  }
}
void App::header(const Str& s, bool backButton) {
  if (backButton) {
    canvas.icon("back", 7, 8);
    hit(0, 0, 40, 36, [this] { back(); });
  }
  if (view == View::Book && !backButton) {
    canvas.icon("home", 9, 7);
    hit(0, 0, 40, 36, [this] {
      position(true);
      invalidateMarks();
      reader.close();
      fromShelf = false;
      originDir.clear();
      stack.clear();
      view = View::Home;
      dirty = true;
    });
  }
  bool leading = backButton || view == View::Book;
  canvas.ui(leading ? 39 : 15, 8, s, 15, leading ? 127 : 150, true, view == View::Definition);
  Str pc = (dev.battery < 0 ? "--" : std::to_string(dev.battery)) + Str("%");
  canvas.ui(206 - Canvas::textWidth(pc, 12), 10, pc, 12, 38);
  canvas.border(212, 9, 21, 12);
  canvas.rect(233, 12, 2, 5);
  canvas.rect(214, 11, std::clamp(dev.battery, 0, 100) * 17 / 100, 8);
  canvas.rect(0, 33, 240, 1);
}
void App::pageControl(bool next, std::function<void()> action) {
  int x = next ? 162 : 10;
  canvas.border(x, 389, 68, 23);
  canvas.ui(x + (next ? 10 : 24), 394, next ? "下一页" : "上一页", 12, 40);
  canvas.icon(next ? "next" : "back", x + (next ? 51 : 5), 390);
  if (next) {
    nextListPage = action;
  }
  hit(next ? 160 : 0, 380, 80, 36, std::move(action));
}
void App::turnList(bool prev, bool next, unsigned total, bool centerFree) {
  if (!prev && !next) {
    return;
  }
  if (centerFree) {
    Str label = std::to_string(offset / 6 + 1);
    if (total) {
      label += " / " + std::to_string((total + 5) / 6);
    }
    canvas.ui(120 - Canvas::textWidth(label, 12) / 2, 394, label, 12, 48);
  }
  if (prev) {
    pageControl(false, [this] {
      offset = offset >= 6 ? offset - 6 : 0;
      dirty = true;
    });
  }
  if (next) {
    pageControl(true, [this] {
      offset += 6;
      dirty = true;
    });
  }
}
}  // namespace jd
