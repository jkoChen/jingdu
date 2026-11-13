#include "App.h"
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
using namespace jd;
struct ScreenBoard : Device {
  Canvas target;
  unsigned resets = 0;
  void cast(bool on) override {
    casting = on;
    screenReady = on && connected;
  }
  const Canvas* screenImage() const override {
    return &target;
  }
  bool renewScreenKey() override {
    ++resets;
    return true;
  }
};
int main(int argc, char** argv) {
  assert(argc == 2);
  root = Str(argv[1]) + "/ui-regression";
  std::filesystem::create_directories(root + "/books");
  std::ofstream(root + "/books/only.txt") << "Text in books";
  std::ofstream(root + "/root.txt") << "Text in root";
  ScreenBoard device;
  for (bool shelf : {false, true}) {
    App app(device);
    device.card = false;
    app.begin();
    app.paint();
    auto click = [&](int x, int y) {
      app.tap(x, y, x, y);
      app.paint();
    };
    if (shelf) {
      click(70, 201);
      click(80, 382);
    } else {
      click(80, 129);
    }
    assert(app.view == View::Files);
    device.card = true;
    app.paint();
    click(60, 94);
    assert(app.view == View::File);
    click(60, 115);
    assert(app.reader.path == "/books/only.txt");
  }
  App app(device);
  device.card = device.connected = false;
  device.target.clear();
  device.target.dot(0, 0);
  app.begin();
  app.paint();
  auto click = [&](int x, int y) {
    app.tap(x, y, x, y);
    app.paint();
  };
  click(70, 371);
  click(70, 147);
  assert(app.view == View::ScreenConnect && device.casting && !app.idleMinutes());
  device.connected = device.screenReady = true;
  app.networkChanged();
  app.paint();
  assert(app.view == View::ScreenCanvas &&
         !memcmp(device.target.bits, app.canvas.bits, sizeof app.canvas.bits));
  click(120, 208);
  assert(app.view == View::ScreenInfo);
  click(90, 274);
  click(13, 16);
  assert(device.resets == 0 && app.view == View::ScreenInfo);
  click(90, 274);
  click(120, 383);
  assert(device.resets == 1 && app.view == View::ScreenInfo);
  device.target.dot(23, 69);
  app.removed();
  click(13, 16);
  assert(app.view == View::ScreenCanvas &&
         !memcmp(device.target.bits, app.canvas.bits, sizeof app.canvas.bits));
  app.tap(105, 9, 110, 72);
  app.paint();
  assert(app.view == View::Tools);
  app.sleep();
  app.paint();
  assert(app.view == View::Lock && !device.casting);
  app.single();
  app.paint();
  assert(app.view == View::Settings && app.idleMinutes());
  click(70, 147);
  click(120, 208);
  click(100, 383);
  assert(app.view == View::Settings && !device.casting);
}
