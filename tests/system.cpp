#include "App.h"
#include "Button.h"
#include "Lunar.h"
#include "../lib/FreeInkBook/third_party/pngle/pngle.h"
#include <cassert>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <map>
#include <utime.h>
using namespace jd;
namespace fs = std::filesystem;
struct Board : Device {
  time_t adjusted = 0;
  bool setTime(time_t t) override {
    adjusted = t;
    return true;
  }
  void requestTime() override {
    clockInfo = connected ? "正在联网校时" : "请先连接 Wi-Fi";
  }
  std::map<Str, Str> prefs;
  void retain(const Str& k, const Str& v) override {
    prefs[k] = v;
  }
  bool retained(const Str& k, Str& v) override {
    if (!prefs.count(k)) {
      return false;
    }
    v = prefs[k];
    return true;
  }
  void radio(bool v) override {
    wifi = v;
  }
  void scan() override {
    networks = {"Test Wi-Fi"};
  }
  void connect(const Str& s, const Str&) override {
    network = s;
    connected = true;
    ip = "192.0.2.5";
  }
  void service(bool on) override {
    serving = on && connected && card;
    pin = "123456";
  }
  time_t clock() override {
    return 1794369600;
  }
};
static void screen(App& a, const Str& path = "") {
  for (int i = 0; i < 6000; i++) {
    a.background();
    if (a.dirty) {
      a.paint();
      a.displayed();
    }
    if (!a.dirty && !a.reader.validating() && !a.preparingFont()) {
      break;
    }
  }
  assert(!a.dirty);
  a.displayed();
  if (!path.empty()) {
    std::ofstream f(path, std::ios::binary);
    f << "P4\n240 416\n";
    for (auto b : a.canvas.bits) {
      f.put(~b);
    }
    assert(f.good());
  }
}
static void click(App& a, int x, int y) {
  a.tap(x, y, x, y);
  screen(a);
}
static bool render(Reader& r, Canvas& c) {
  for (int i = 0; i < 2000; i++) {
    c.clear();
    if (r.paint(c)) {
      return true;
    }
    if (!r.pending) {
      return false;
    }
  }
  return false;
}
static void fileIdentityRegression() {
  const Str path = root + "/identity-case.txt";
  Str data(65536, 'a');
  auto write = [&] {
    std::ofstream out(path, std::ios::binary);
    out.write(data.data(), data.size());
  };
  write();
  auto stamp = fs::last_write_time(path);
  auto change = [&](size_t at) {
    ++data[at];
    write();
    fs::last_write_time(path, stamp);
  };
  auto finish = [](FileIdentity& f) {
    while (f.running()) {
      assert(f.step());
    }
  };
  FileIdentity a, b;
  assert(a.begin(path) && a.running() && a.step());
  assert(b.begin(path, &a) && !a.running() && b.percent() > 0);
  finish(b);
  assert(b.value() == key(data));
  assert(a.begin(path) && !a.running() && !a.sampled());
  invalidateFileIdentities();
  assert(b.begin(path) && !b.running() && b.sampled() && b.value() == key(data));
  change(32768);
  invalidateFileIdentities();
  assert(a.begin(path) && a.running());
  finish(a);
  assert(a.value() == key(data));
  change(12000);
  invalidateFileIdentities();
  assert(a.begin(path) && !a.running() && a.value() != key(data));  // accepted blind spot
  assert(forgetFileIdentity(path));
  assert(b.begin(path) && b.running());
  finish(b);
  assert(b.value() == key(data));
  assert(forgetFileIdentity(path));
  assert(a.startUpload(data.size()));
  for (size_t at = 0; at < data.size(); at += 701) {
    a.append(data.data() + at, std::min<size_t>(701, data.size() - at));
  }
  a.published(path);
  assert(a.value() == key(data));
  invalidateFileIdentities();
  assert(b.begin(path) && b.sampled() && b.value() == a.value());
  fs::remove(path);
}
static void fontResizeKeepsIdentity() {
  bool more = false;
  const auto fonts = list(root + "/fonts", 0, 1, more, true);
  assert(!fonts.empty());
  const Str path = "/resize-font.ttf";
  fs::copy_file(root + "/fonts/" + fonts.front().name, root + path);
  struct utimbuf stamp{315532800, 315532800};  // FAT epoch, no usable modification time
  assert(utime((root + path).c_str(), &stamp) == 0);
  BodyFont font;
  assert(font.select(path, 22, 1));
  assert(!font.generation().empty());
  const Str digest = font.generation();
  FileIdentity complete;
  assert(complete.begin(root + path) && complete.running());
  assert(font.select(path, 30, 1) && font.generation() == digest);
  assert(font.select(path, 30, 2) && font.generation() == digest);
  font.close();
  // Simulate a replacement that keeps its size and timestamp. The table
  // directory checksum is in the first sample and does not alter the outline.
  std::fstream changed(root + path, std::ios::in | std::ios::out | std::ios::binary);
  changed.seekg(16);
  char byte = 0;
  assert(changed.get(byte));
  changed.seekp(16);
  changed.put(byte ^ 1);
  changed.close();
  assert(utime((root + path).c_str(), &stamp) == 0);
  assert(font.select(path, 30, 2) && font.generation() != digest);
  font.close();
  assert(utime((root + path).c_str(), nullptr) == 0);
  FileIdentity legacy;
  assert(legacy.begin(root + path));
  while (legacy.running()) {
    assert(legacy.step());
  }
  assert(!legacy.value().empty());
  invalidateFileIdentities();
  assert(font.select(path, 22, 1) && font.generation() == legacy.value());
  font.close();
  fs::remove(root + path);
}
static void shelfAndResumeRegression() {
  const Str originalRoot = root;
  root += "/shelf-test";
  fs::create_directories(root);
  const Str path = "/demo.txt";
  const Str base = root + "/.jingdu/user/";
  const Str pathKey = key(path);
  Str content;
  for (unsigned i = 0; i < 60000; ++i) {
    content += "The quick brown fox jumps over the lazy dog.\n";
  }
  std::ofstream(root + path, std::ios::binary) << content;
  const Str id = key(content);
  const Str pending = base + "shelf/p-" + pathKey + "/ref";
  const Str permanent = base + "shelf/" + id + "/ref";
  Board board;
  board.card = true;
  Str value;
  Anchor marked{};
  {
    App app(board);
    app.begin();
    screen(app);
    click(app, 70, 310);
    click(app, 70, 94);
    assert(app.view == View::File);
    click(app, 70, 165);
    assert(loadRecord(pending, value) && value == path);
    assert(!loadRecord(permanent, value));
    app.background(true);
    app.tap(65, 119, 65, 119);
    assert(app.view == View::Book && app.reader.validating());
    auto drawOne = [&] {
      do {
        app.paint();
        app.displayed();
      } while (app.dirty);
    };
    drawOne();
    assert(app.jumpFromReader({0, 22500}));
    drawOne();
    marked = app.reader.at;
    assert(marked.byte == 22500);
    app.tap(90, 100, 91, 175, 500);
    drawOne();
    assert(app.reader.validating());
    click(app, 15, 14);
    assert(app.view == View::Home);
    assert(loadRecord(base + "draft/" + pathKey, value) && !value.empty());
    assert(!loadRecord(base + "positions/" + id, value));
  }
  invalidateFileIdentities();
  {
    App app(board);
    app.begin();
    screen(app);
    click(app, 70, 125);
    assert(app.view == View::Book && app.reader.id == id && same(app.reader.at, marked));
    click(app, 15, 14);
    assert(loadRecord(permanent, value) && value == path);
    assert(!loadRecord(pending, value));
    assert(loadRecord(base + "draft/" + pathKey, value) && value.empty());
    assert(loadRecord(base + "marks/" + id, value));
    JsonDocument marks;
    assert(!deserializeJson(marks, value) && marks.size() == 1 && marks[0]["byte"] == marked.byte);
    auto stamp = fs::last_write_time(root + path);
    content[marked.byte] = 'X';
    std::ofstream(root + path, std::ios::binary) << content;
    fs::last_write_time(root + path, stamp);
    invalidateFileIdentities();
    click(app, 70, 125);
    assert(app.view == View::Book && app.reader.id == key(content) && app.reader.at.byte == 0);
    assert(loadRecord(base + "marks/" + id, value));
    assert(!loadRecord(base + "marks/" + key(content), value));
  }
  invalidateFileIdentities();
  root = originalRoot;
}
static void recentFollowsCard() {
  const Str fixture = root;
  const Str a = fixture + "/recent-card-a", b = fixture + "/recent-card-b";
  const Str userDir = "/.jingdu/user/";
  const Str first = "/first.txt", second = "/second.txt";
  fs::create_directories(a);
  fs::create_directories(b);
  std::ofstream(a + first) << "Book on the first card.\n";
  std::ofstream(b + second) << "Book on the second card.\n";
  assert(saveRecord(a + userDir + "recent", first));
  assert(saveRecord(b + userDir + "recent", second));

  Board board;
  board.card = true;
  board.retain("recent", first);
  root = a;
  App readerA(board);
  readerA.begin();
  screen(readerA);
  click(readerA, 60, 125);
  assert(readerA.view == View::Book && readerA.reader.path == first);
  readerA.sleep();
  Str snapshot;
  assert(board.prefs.at("resumeState").find(first) == Str::npos);
  assert(loadRecord(a + userDir + "wake", snapshot));

  root = b;
  std::ofstream(b + first) << "Same filename, another card.\n";
  App swapped(board);
  swapped.begin(false, true);
  screen(swapped);
  assert(swapped.view == View::Home && swapped.reader.path.empty());
  click(swapped, 60, 125);
  assert(swapped.view == View::Book && swapped.reader.path == second);
  assert(board.prefs.at("recent") == first);  // No new path written to device preferences.
  board.card = false;
  swapped.removed();
  swapped.view = View::Home;
  screen(swapped);
  click(swapped, 60, 125);
  assert(swapped.view == View::Files && swapped.reader.path.empty());

  root = a;
  board.card = true;
  swapped.view = View::Home;
  swapped.dirty = true;
  screen(swapped);
  click(swapped, 60, 125);
  assert(swapped.view == View::Book && swapped.reader.path == first);
  click(swapped, 15, 14);
  fs::rename(a + first, a + "/moved.txt");
  swapped.dirty = true;
  screen(swapped);
  click(swapped, 60, 125);
  assert(swapped.view == View::Files);

  fs::rename(a + "/moved.txt", a + first);
  Str ref = a + userDir + "shelf/p-" + key(first);
  assert(saveRecord(ref + "/ref", first));
  assert(saveRecord(a + userDir + "shelf/.recent", "+" + first));
  fs::remove_all(ref);
  App cleared(board);
  cleared.begin(false, true);
  screen(cleared);
  assert(cleared.view == View::Home);
  click(cleared, 60, 125);
  assert(cleared.view == View::Files);
  fs::remove_all(a + userDir + "shelf");
  App empty(board);
  empty.begin();
  screen(empty);
  click(empty, 60, 125);
  assert(empty.view == View::Files && !loadRecord(a + userDir + "recent", snapshot));
  root = fixture;
  invalidateFileIdentities();
}
static void pngHistoryRegression() {
  auto bytes = [](const Str& name) {
    std::ifstream file(root + "/" + name, std::ios::binary);
    assert(file);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), {});
  };
  auto source = bytes("history.png"), expected = bytes("history.gray");
  std::vector<uint8_t> pixels(expected.size());
  pngle_t* decoder = pngle_new();
  assert(decoder);
  pngle_set_user_data(decoder, &pixels);
  pngle_set_draw_callback(
      decoder, [](pngle_t* decoder, uint32_t x, uint32_t y, uint32_t, uint32_t, const uint8_t rgba[4]) {
        assert(x < 241 && y < 513);
        (*static_cast<std::vector<uint8_t>*>(pngle_get_user_data(decoder)))[y * 241 + x] = rgba[0];
      });
  for (unsigned repeat = 0; repeat < 2; ++repeat) {
    std::fill(pixels.begin(), pixels.end(), 0);
    for (size_t at = 0; at < source.size();) {
      int consumed = pngle_feed(decoder, source.data() + at, std::min<size_t>(1079, source.size() - at));
      assert(consumed > 0);
      at += consumed;
    }
    assert(pixels == expected);
    pngle_reset(decoder);
  }
  pngle_destroy(decoder);
}
int main(int argc, char** argv) {
  auto lunar = [](int y, int m, int day) {
    struct tm t{};
    t.tm_year = y - 1900;
    t.tm_mon = m - 1;
    t.tm_mday = day;
    t.tm_hour = 12;
    mktime(&t);
    return lunarDate(t);
  };
  assert(lunar(2026, 2, 17) == "农历 正月初一");
  assert(lunar(2025, 7, 25) == "农历 闰六月初一");
  assert(lunar(2026, 1, 1) == "农历 十一月十三");
  assert(lunar(2033, 12, 22) == "农历 闰十一月初一");
  assert(argc == 3);
  root = argv[1];
  pngHistoryRegression();
  fileIdentityRegression();
  shelfAndResumeRegression();
  recentFollowsCard();
  fontResizeKeepsIdentity();
  Str out = argv[2];
  fs::create_directories(out);
  Canvas c;
  c.clear();
  // Original approved 1-bit Atkinson Medium specimen, 240 x 32 pixels.
  int glyphX = 12;
  for (char cp : std::string("Il1   O0   rn  m   ea")) {
    glyphX += c.studyChar(glyphX, 6, cp, 20);
  }
  assert(checksum(c.bits, 32 * 30) == 0xdba70ce0u);
  c.clear();
  assert(userPath("/books/书.txt") && !userPath("/../etc") && !userPath("/.jingdu/user") &&
         !userPath("/ok./x") && !userPath("/a//b"));
  Str f = root + "/.jingdu/user/test";
  assert(saveRecord(f, "one") && saveRecord(f, "two"));
  std::ofstream(f + ".1", std::ios::binary) << "partial";
  Str recovered;
  assert(loadRecord(f, recovered) && recovered == "one");
  Button button;
  button.wake(true, 0);
  button.poll(false, 10);
  assert(button.poll(false, 35) == ButtonEvent::None);
  auto poll = [&](bool p, int ms) { return button.poll(p, ms); };
  poll(true, 100);
  poll(true, 125);
  poll(false, 130);
  poll(false, 155);
  assert(poll(false, 435) == ButtonEvent::Next);
  poll(true, 500);
  poll(true, 525);
  poll(false, 530);
  poll(false, 555);
  poll(true, 700);
  poll(true, 725);
  poll(false, 740);
  assert(poll(false, 765) == ButtonEvent::Sleep);
  for (auto name : {"gb18030", "utf-16", "utf-32"}) {
    FILE* fp = fopen((root + "/books/" + name + ".txt").c_str(), "rb");
    assert(fp);
    auto en = detect(fp);
    fseek(fp, en.bom, SEEK_SET);
    for (uint32_t expected : {0x53e4, 0x7c4d, 0x9605, 0x8bfb, 0x20000}) {
      assert(decode(fp, en.enc) == expected);
    }
    fclose(fp);
  }
  {
    BodyFont damaged, native;
    assert(damaged.select("/blank.ttf", 20, 1));
    Canvas actual, builtin;
    actual.clear();
    builtin.clear();
    assert(!damaged.draw(actual, 12, 45, 'X'));
    native.draw(builtin, 12, 45, 'X');
    assert(!memcmp(actual.bits, builtin.bits, sizeof actual.bits));
    assert(damaged.draw(actual, 36, 45, ' '));
    assert(damaged.stats.blank > 0);
  }
  {
    BodyFont accumulating;
    auto open = [&] {
      assert(accumulating.select("/fonts/霞鹜臻楷.ttf", 28, 1));
      assert(!accumulating.generation().empty());
    };
    auto draw = [&] {
      Canvas page;
      page.clear();
      // These codepoints collide in one on-card bucket. All must survive reopening.
      int x = 0;
      for (char32_t cp : {U'一', U'幕', U'撒'}) {
        assert(accumulating.draw(page, x, 40, cp));
        x += accumulating.advance(cp);
      }
      return checksum(page.bits, sizeof page.bits);
    };
    open();
    auto expected = draw();
    assert(accumulating.stats.raster == 3);
    accumulating.close();
    open();
    assert(draw() == expected && accumulating.stats.raster == 3 && accumulating.stats.tf == 3);
  }
  {
    // Two sets share no codepoints. Start each measurement with empty RAM.
    auto sample = [](BodyFont& f, unsigned first) {
      Canvas screen;
      screen.clear();
      for (unsigned i = 0; i < 32; ++i) {
        assert(f.draw(screen, (i % 8) * 26, (i / 8) * 34, first + i));
      }
      return checksum(screen.bits, sizeof screen.bits);
    };
    BodyFont f;
    auto open = [&] { assert(f.select("/fonts/霞鹜臻楷.ttf", 24, 2)); };
    open();
    auto a = sample(f, 0x4e00);
    auto b = sample(f, 0x4e20);
    assert(f.stats.raster == 64 && f.stats.ram == 0 && f.stats.tf == 0 && a != b);
    f.close();
    open();
    auto before = f.stats;
    assert(sample(f, 0x4e20) == b && f.stats.tf - before.tf == 32 && f.stats.raster == before.raster);
    f.close();
    assert(f.select("/fonts/霞鹜臻楷.ttf", 26, 2));
    for (unsigned i = 0; i < 180; ++i) {
      f.warmOne();
    }
    assert(f.warmed >= 64);
    f.close();
    assert(f.select("/fonts/霞鹜臻楷.ttf", 26, 2));
    before = f.stats;
    sample(f, 0x4e00);
    sample(f, 0x4e20);
    assert(f.stats.tf - before.tf == 64 && f.stats.raster == before.raster && f.stats.ram == before.ram);
    std::cout << "Disjoint 32+32 glyphs: cold raster=64, after idle warm and reopen TF=64 raster=0\n";
    // 180 mapped entries were visited before closing. After a restart, 90 slices
    // should get beyond those entries via the 128-entry checkpoint. A scan
    // incorrectly restarted at the beginning would add no new glyphs here.
    unsigned previousWarm = f.warmed;
    for (unsigned i = 0; i < 90; ++i) {
      f.warmOne();
    }
    assert(f.warmed > previousWarm);
    std::cout << "Idle warm cursor survived reopen; 90 slices added " << f.warmed - previousWarm
              << " more glyphs\n";
    // Six glyphs map to one group. Global replacement can retain all six;
    // dropping the extra bank must still render the same page from the TF cache.
    auto collidingPage = [&] {
      Canvas screen;
      screen.clear();
      for (unsigned i = 0; i < 6; ++i) {
        assert(f.draw(screen, 30 * i, 10, 0x4e00 + 20 * i));
      }
      return checksum(screen.bits, sizeof screen.bits);
    };
    f.setCacheMode(BodyFont::CacheMode::Group80);
    f.sizeHotCache(true);
    auto checksumGrouped = collidingPage();
    before = f.stats;
    assert(collidingPage() == checksumGrouped && f.stats.tf - before.tf == 6);
    f.setCacheMode(BodyFont::CacheMode::Global80);
    f.sizeHotCache(true);
    assert(collidingPage() == checksumGrouped);
    before = f.stats;
    assert(collidingPage() == checksumGrouped && f.stats.ram - before.ram == 6);
    f.sizeHotCache(false);
    assert(f.hotCapacity() == 40 && collidingPage() == checksumGrouped);
    f.sizeHotCache(true);
    assert(f.hotCapacity() == 80 && collidingPage() == checksumGrouped);
  }
  Reader r;
  r.layout.size = 20;
  assert(r.open("/books/小书.txt", "/fonts/霞鹜臻楷.ttf"));
  assert(r.font.active());
  assert(render(r, c));
  for (unsigned i = 0; i < 6000 && (r.validating() || r.font.generation().empty()); ++i) {
    r.background();
  }
  assert(!r.font.generation().empty() && !r.id.empty());
  assert(render(r, c));
  auto first = checksum(c.bits, sizeof c.bits);
  for (int i = 0; i < 140; i++) {
    r.background(true);
  }
  assert(r.chapters() && r.chapterName(0).find("第1章") != Str::npos);
  assert(r.forward());
  assert(render(r, c));
  auto second = checksum(c.bits, sizeof c.bits);
  assert(second != first);
  auto a = r.at;
  assert(r.backward() && render(r, c));
  assert(checksum(c.bits, sizeof c.bits) == first);
  assert(r.open("/books/小书.txt", "/fonts/霞鹜臻楷.ttf", a) && render(r, c));
  assert(checksum(c.bits, sizeof c.bits) == second);
  // Force corrupt page records and compare the regenerated framebuffer.
  for (auto e : fs::directory_iterator(root + "/.jingdu/cache")) {
    if (e.path().filename().string().rfind("page-", 0) == 0) {
      std::ofstream(e.path(), std::ios::trunc) << "bad";
    }
  }
  assert(render(r, c));
  assert(checksum(c.bits, sizeof c.bits) == second);
  for (int pass = 0; pass < 2; ++pass) {
    Epub many;
    assert(many.open(root + "/books/多章.epub", "large-test"));
    assert(many.count() == 180 && many.chapter(179));
    // Before the chapter is committed, page reads borrow the writer.
    // Reading an early page must not overwrite its remaining text on resume.
    if (!pass) {
      assert(many.ensure(4096, 8) && !many.complete);
      auto* prefix = many.openText();
      char bytes[96];
      assert(prefix && fread(bytes, 1, sizeof bytes, prefix) == sizeof bytes);
      assert(many.closeText(prefix));
      std::ifstream expected(root + "/expected-chapter");
      char reference[sizeof bytes];
      expected.read(reference, sizeof reference);
      assert(std::equal(std::begin(bytes), std::end(bytes), std::begin(reference)));
    }
    while (!many.complete) {
      assert(many.ensure(UINT32_MAX));
    }
    std::ifstream expected(root + "/expected-chapter"), expanded(many.textPath);
    Str wanted, actual;
    expected >> wanted;
    expanded >> actual;
    assert(wanted == actual);
  }
  for (const Str book : {"千章", "千章插图"}) {
    for (int pass = 0; pass < 2; ++pass) {
      Epub indexed;
      assert(indexed.open(root + "/books/" + book + ".epub", "catalog-" + book));
      assert(indexed.count() == 1007 && indexed.tocSize() == 1007);
      for (unsigned index : {0u, 503u, 1006u}) {
        unsigned spine;
        Str fragment;
        assert(indexed.target(index, spine, fragment) && spine == index && fragment == "top");
        assert(indexed.tocName(index) == "Chapter " + std::to_string(index));
        assert(indexed.chapter(index));
        while (!indexed.complete) {
          assert(indexed.ensure(UINT32_MAX));
        }
        std::ifstream text(indexed.textPath);
        Str decoded((std::istreambuf_iterator<char>(text)), std::istreambuf_iterator<char>());
        assert(decoded.find("Reading chapter " + std::to_string(index) + ".") != Str::npos);
      }
    }
  }
  assert(r.open("/books/样书.epub", ""));
  assert(r.chapters() == 2);
  assert(render(r, c));
  assert((c.bits[42 * 30 + 120 / 8] & (128 >> (120 % 8))) == 0);
  r.jumpChapter(1);
  assert(render(r, c));
  assert(!r.chapterName(1).empty());
  assert(r.backward() && render(r, c) && r.at.chapter == 0 && r.at.byte > 0);
  assert(r.forward() && render(r, c) && r.at.chapter == 1 && r.at.byte == 0);
  r.close();
  r.background(true);
  assert(r.progress() == 0 && !r.forward() && r.excerpt.empty());
  assert(!r.validating() && !r.font.active() && r.chapters() == 0);
  setCacheBudget(0, 0);
  assert(r.open("/books/样书.epub", ""));
  assert(render(r, c));
  auto ramFirst = checksum(c.bits, sizeof c.bits);
  for (int i = 0; i < 9; i++) {
    assert(r.forward());
    assert(render(r, c));
  }
  r.jump({0, 0});
  assert(render(r, c) && checksum(c.bits, sizeof c.bits) == ramFirst);
  for (int disk = 0; disk < 2; ++disk) {
    if (disk) {
      setCacheBudget(8ull << 30, 4ull << 30);
    }
    for (auto book : {"/books/样书.epub", "/books/小书.txt", "/books/gb18030.txt"}) {
      assert(r.open(book, ""));
      Anchor previous{};
      for (unsigned pct = 0; pct <= 100; pct += 5) {
        r.percent(pct);
        assert(render(r, c) && !r.excerpt.empty());
        if (pct < 100) {
          assert(r.at.chapter > previous.chapter ||
                 (r.at.chapter == previous.chapter && r.at.byte >= previous.byte));
        }
        if (pct == 100) {
          assert(!r.forward() && r.progress() == 100);
        }
        previous = r.at;
      }
      r.close();
    }
  }
  setCacheBudget(8ull << 30, 4ull << 30);
  assert(r.open("/books/长书.txt", ""));
  for (int i = 0; i < 4700; i++) {
    assert(render(r, c));
    assert(r.forward());
  }
  assert(r.at.byte > 500000);
  unsigned pages = 4700;
  uint32_t watermark = r.at.byte;
  do {
    if (pages++ > 100000) {
      std::abort();
    }
    assert(render(r, c));
  } while (r.forward());
  assert(r.progress() >= 99);
  std::cout << "长书 " << length(root + "/books/长书.txt") << " B，连续 " << pages
            << " 页到结尾。历史失败点后原文偏移 " << watermark << "\n";
  r.close();
  Lesson l;
  unsigned as, ss;
  assert(fetchLesson("/courses/english/everyday-starter.json", 2, 0, l, as, ss) && as == 3 &&
         l.chunks.size() == 3);
  assert(fetchLesson("/courses/english/OneStopQA-Ele.jdc", 29, 0, l, as, ss) && as == 30 &&
         !l.choices.empty());
  assert(fetchLesson("/courses/english/H5P-Pronouns.jdc", 7, 0, l, as, ss) && as == 8);
  auto ds = dictionaryFiles(0, 8);
  assert(!ds.empty());
  auto def = lookup(ds.front(), "bottle");
  assert(!def.empty() && def != "未收录" && def.find("<div") == Str::npos);
  Board board;
  board.card = true;
  board.battery = 76;
  board.temp = 23.6;
  board.rh = 50;
  board.measured = true;
  board.retain("settings", R"({"fontChosen":true})");
  App app(board);
  app.begin();
  screen(app, out + "/home.pbm");
  for (int row = 0; row < 4; ++row) {
    int black = 0;
    for (int y = 191 + 55 * row; y < 209 + 55 * row; ++y) {
      for (int x = 16; x < 34; ++x) {
        if (!(app.canvas.bits[30 * y + x / 8] & (128 >> (x % 8)))) {
          ++black;
        }
      }
    }
    assert(black > 25 && black < 150);
  }
  click(app, 50, 374);
  assert(app.view == View::Settings);
  screen(app, out + "/settings.pbm");
  app.tap(115, 12, 116, 82);
  screen(app);
  assert(app.view == View::Tools);
  for (int level = 0; level <= 10; ++level) {
    click(app, 45 + 15 * level, 78);
    JsonDocument settings;
    deserializeJson(settings, board.prefs["settings"]);
    assert(settings["light"].as<int>() == level);
  }
  click(app, 114, 130);
  assert(app.view == View::Refresh);
  click(app, 12, 15);
  click(app, 12, 15);
  assert(app.view == View::Settings);
  click(app, 80, 344);
  assert(app.view == View::Power);
  screen(app, out + "/power.pbm");
  click(app, 80, 68);
  assert(app.view == View::AutoLock);
  click(app, 15, 14);
  for (int y : {130, 190}) {
    click(app, 80, y);
    assert(app.view == (y == 130 ? View::ConfirmLongSleep : View::ConfirmRestart));
    click(app, 15, 14);
    assert(app.view == View::Power && !app.sleepRequested());
  }
  click(app, 15, 14);
  for (PowerAction wanted : {PowerAction::Restart, PowerAction::LongSleep}) {
    Board device;
    App session(device);
    session.begin();
    screen(session);
    click(session, 70, 371);
    click(session, 80, 344);
    click(session, 70, wanted == PowerAction::Restart ? 190 : 130);
    device.transferring = true;
    click(session, 110, 383);
    assert(session.view == View::Message && !session.sleepRequested());
    device.transferring = false;
    click(session, 110, 389);
    click(session, 110, 383);
    assert(session.view == View::Lock && session.powerAction == wanted);
    session.single();
    assert(session.powerAction == wanted);
    if (wanted == PowerAction::LongSleep) {
      screen(session, out + "/long-sleep.pbm");
    }
  }
  for (int i = 0; i < 5; ++i) {
    click(app, 120, 244);
    assert(app.view == View::Refresh);
    if (i == 0) {
      screen(app, out + "/refresh.pbm");
    }
    click(app, 50, 69 + i * 48);
    const unsigned expected[] = {5, 10, 20, 50, 100};
    assert(app.fullEvery() == expected[i] && app.view == View::Settings);
  }
  click(app, 100, 299);
  assert(app.view == View::Clock);
  const time_t initialSync = 1794000000;
  assert(board.autoClockDays == 0 && !board.clockDue(initialSync, 0));
  for (int days : {1, 7}) {
    click(app, 76, 243);
    assert(app.view == View::AutoClock);
    click(app, 76, days == 1 ? 116 : 166);
    assert(app.view == View::Clock && board.autoClockDays == days);
    board.lastNetworkSync = initialSync;
    time_t due = initialSync + days * 86400;
    assert(!board.clockDue(due - 1, 0) && board.clockDue(due, 0));
    assert(!board.clockDue(due, due + 3600) && board.clockDue(due + 3600, due + 3600));
    assert(board.clockDue(2, 0));  // Cold start without a valid hardware time.
    Board restarted;
    restarted.prefs = board.prefs;
    App restored(restarted);
    restored.begin();
    assert(restarted.autoClockDays == days);
  }
  click(app, 76, 243);
  click(app, 76, 70);
  assert(board.autoClockDays == 0 && !board.clockDue(initialSync + 8 * 86400, 0));
  click(app, 114, 380);
  assert(board.clockInfo == "请先连接 Wi-Fi");
  click(app, 114, 193);
  assert(app.view == View::ClockEdit);
  click(app, 208, 232);
  click(app, 150, 305);
  click(app, 100, 379);
  assert(board.adjusted > 0 && app.view == View::Clock);
  click(app, 15, 15);
  click(app, 80, 389);
  assert(app.view == View::About);
  screen(app, out + "/about.pbm");
  click(app, 70, 358);
  assert(app.view == View::CardInfo);
  board.card = false;
  app.removed();
  screen(app);
  assert(app.view == View::CardInfo);
  click(app, 15, 15);
  assert(app.view == View::About);
  board.card = true;
  click(app, 15, 15);
  assert(app.view == View::Settings);
  app.single();
  screen(app);
  assert(app.view == View::Settings);
  std::ofstream(root + "/books/排版测试.txt") << "单屏设置的排版测试。新书首次更换字体。";
  assert(saveRecord(root + "/.jingdu/user/recent", "/books/排版测试.txt"));
  click(app, 15, 15);
  click(app, 60, 128);
  assert(app.view == View::Book);
  click(app, 115, 180);
  click(app, 80, 258);
  assert(app.view == View::Layout);
  click(app, 50, 65);
  assert(app.view == View::Fonts);
  std::ofstream(root + "/fonts/not-a-font.txt") << "other";
  bool hasMore;
  auto listed = list(root + "/fonts", 0, 12, hasMore, true);
  assert(listed.size() == 1 && ends(listed[0].name, ".ttf") && ends("/fonts/A.TTF", ".ttf"));
  assert(forgetFileIdentity(root + "/fonts/" + listed.front().name));
  app.tap(52, 113, 52, 113);
  assert(app.preparingFont());
  app.background();
  app.paint();
  app.tap(15, 15, 15, 15);
  app.single();
  app.sleep();
  assert(app.preparingFont() && app.reader.font.active());
  app.displayed();
  screen(app);
  assert(app.view == View::Layout && app.reader.font.active());
  click(app, 15, 15);
  assert(app.view == View::Menu);
  click(app, 15, 15);
  assert(app.view == View::Book);
  click(app, 15, 15);
  click(app, 50, 374);
  assert(app.view == View::Settings);
  click(app, 50, 65);
  assert(app.view == View::Wifi);
  click(app, 50, 65);
  click(app, 50, 130);
  assert(app.view == View::Password);
  screen(app, out + "/wifi.pbm");
  click(app, 20, 144);
  click(app, 160, 385);
  assert(board.connected && app.view == View::Wifi);
  click(app, 15, 15);
  click(app, 55, 112);
  click(app, 110, 385);
  assert(board.serving);
  screen(app, out + "/lan.pbm");
  app.sleep();
  assert(!board.serving && app.view == View::Lock);
  screen(app, out + "/lock.pbm");
  app.single();
  screen(app);
  Board b2;
  b2.card = true;
  b2.battery = 70;
  b2.retain("recent", "/old-card.txt");
  // Recreate a card that only has the legacy recent record.
  for (auto suffix : {".0", ".1"}) {
    fs::remove(root + "/.jingdu/user/shelf/.recent" + suffix);
  }
  assert(saveRecord(root + "/.jingdu/user/recent", "/books/小书.txt"));
  App book(b2);
  book.begin();
  screen(book);
  click(book, 60, 110);
  assert(book.view == View::Book);
  screen(book, out + "/reader.pbm");
  book.single();
  screen(book);
  auto anchor = book.reader.at;
  assert(anchor.byte > 0);
  click(book, 116, 409);
  assert(book.view == View::Book && same(anchor, book.reader.at));
  click(book, 116, 190);
  click(book, 116, 159);
  assert(book.view == View::Toc);
  for (int i = 0; i < 1000 && book.reader.chaptersPending(); i++) {
    book.background(true);
    if (book.dirty) {
      screen(book);
    }
  }
  assert(book.reader.chapters() && !book.reader.chaptersPending());
  click(book, 14, 12);
  click(book, 116, 211);
  assert(book.view == View::Marks);
  click(book, 14, 12);
  click(book, 116, 119);
  assert(book.view == View::Jump);
  click(book, 14, 12);
  click(book, 14, 12);
  auto bookmarkCount = [&] {
    Str data;
    JsonDocument doc;
    if (loadRecord(root + "/.jingdu/user/marks/" + book.reader.id, data)) {
      deserializeJson(doc, data);
    }
    return doc.size();
  };
  book.tap(115, 110, 119, 200, 90);
  assert(book.view == View::Book && bookmarkCount() == 0);
  book.tap(115, 110, 119, 200, 450);
  screen(book);
  assert(book.view == View::Book && same(anchor, book.reader.at) && bookmarkCount() == 1);
  auto ink = [&](int x, int y) { return !(book.canvas.bits[y * 30 + x / 8] & (128 >> (x % 8))); };
  assert(ink(230, 42) && ink(238, 42) && ink(230, 59) && !ink(234, 59));
  assert(ink(16, 409) && ink(190, 409) && !ink(191, 409));
  screen(book, out + "/marked.pbm");
  book.tap(115, 110, 119, 200, 450);
  screen(book);
  assert(book.view == View::Book && same(anchor, book.reader.at) && bookmarkCount() == 0);
  // A valid storage slot with unreadable bookmark JSON must survive a mutation.
  const Str pathMarks = root + "/.jingdu/user/marks/" + book.reader.id;
  const Str incomplete = "[{";
  assert(saveRecord(pathMarks, incomplete));
  book.tap(115, 110, 119, 200, 450);
  Str preserved;
  assert(book.view == View::Message && loadRecord(pathMarks, preserved) && preserved == incomplete);
  book.paint();
  click(book, 14, 12);
  assert(saveRecord(pathMarks, "[]"));
  book.tap(100, 14, 100, 92);
  screen(book);
  assert(book.view == View::Tools);
  screen(book, out + "/tools.pbm");
  click(book, 14, 12);
  click(book, 115, 190);
  assert(book.view == View::Menu);
  screen(book, out + "/menu.pbm");
  click(book, 48, 260);
  assert(book.view == View::Layout && book.reader.layout.showProgress);
  screen(book, out + "/layout.pbm");
  click(book, 208, 330);
  assert(!book.reader.layout.showProgress && book.reader.layout.bodyBottom() == 416);
  const int previousSize = book.reader.layout.size;
  for (int px = previousSize + 2; px <= 30; px += 2) {
    click(book, 208, 125);
    assert(book.reader.layout.size == px);
  }
  click(book, 208, 125);
  assert(book.reader.layout.size == 30);
  App hidden(b2);
  hidden.begin();
  assert(hidden.reader.layout.size == 30);
  for (int px = 30; px > previousSize; px -= 2) {
    click(book, 115, 125);
  }
  assert(book.reader.layout.size == previousSize);
  assert(!hidden.reader.layout.showProgress);
  click(book, 14, 12);
  click(book, 14, 12);
  assert(book.view == View::Book && same(anchor, book.reader.at) && !ink(16, 409));
  click(book, 116, 410);
  assert(book.view == View::Menu);
  click(book, 48, 260);
  click(book, 208, 330);
  assert(book.reader.layout.showProgress);

  click(book, 48, 60);
  click(book, 48, 60);  // Choose the built-in face while retaining the same book position.
  click(book, 48, 60);
  book.tap(48, 113, 48, 113);
  assert(book.preparingFont() || book.view == View::Layout);
  screen(book);
  assert(book.view == View::Layout && book.reader.font.active() && same(anchor, book.reader.at));
  Str goodFont = book.reader.font.path;
  std::ofstream(root + "/fonts/Damaged.ttf") << "not a font";
  click(book, 48, 60);
  auto rows = list(root + "/fonts", 0, 5, hasMore, true);
  auto damaged =
      std::find_if(rows.begin(), rows.end(), [](const Entry& e) { return e.name == "Damaged.ttf"; });
  assert(damaged != rows.end());
  click(book, 48, 110 + 47 * int(damaged - rows.begin()));
  assert(book.view == View::Message && book.reader.font.active() && book.reader.font.path == goodFont);
  click(book, 111, 380);  // Error -> picker -> layout -> reading menu.
  click(book, 14, 12);
  click(book, 14, 12);
  assert(book.view == View::Menu && same(anchor, book.reader.at));
  click(book, 38, 352);
  assert(book.view == View::Book);
  book.sleep();
  screen(book);
  App resume(b2);
  resume.begin(false, true);
  screen(resume);
  assert(resume.view == View::Book && same(anchor, resume.reader.at));
  click(resume, 18, 14);
  assert(resume.view == View::Home && resume.reader.path.empty());
  click(resume, 120, 130);
  assert(resume.view == View::Book && same(anchor, resume.reader.at));
  App eng(b2);
  eng.begin();
  screen(eng);
  click(eng, 60, 260);
  click(eng, 60, 65);
  assert(eng.view == View::Passage);
  screen(eng, out + "/learning.pbm");
  click(eng, 175, 386);
  assert(eng.view == View::Quiz);
  click(eng, 70, 205);
  assert(eng.view == View::Quiz);
  click(eng, 202, 395);
  assert(eng.view == View::Feedback);
  screen(eng, out + "/feedback.pbm");
  std::cout << "通过：记录双代、按键、编码、真实 TTF、缓存损坏、EPUB、长书、实际课包与词典、Wi-Fi "
               "页面、休眠续读。\n";
}
