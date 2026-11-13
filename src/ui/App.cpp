#include "App.h"
#ifdef ARDUINO
#include <Arduino.h>
#endif
#include "Lunar.h"
#include <algorithm>
#include <cmath>
#include "ViewData.h"
#include "Bookshelf.h"

namespace jd {
void App::begin(bool sleeping, bool resume) {
  Str s;
  if (dev.retained("settings", s) || loadRecord(userDirectory() + "settings", s)) {
    JsonDocument d;
    if (!deserializeJson(d, s)) {
      light = std::clamp(d["light"] | 4, 0, 10);
      int saved = d["full"] | 10;
      auto interval = std::lower_bound(std::begin(kRefreshIntervals), std::end(kRefreshIntervals), saved);
      cadence = interval == std::end(kRefreshIntervals) ? 100 : *interval;
      grip = std::clamp(d["grip"] | 0, 0, 2);
      idleMins = d["idle"] | 10;
      const int days = d["autoClockDays"] | 0;
      dev.autoClockDays = days == 1 || days == 7 ? days : 0;
      int oldFooter = d["footer"] | 0;
      reader.layout.showProgress = d["showProgress"] | (oldFooter != 2);
      chapterPercent = d["chapterPercent"] | (oldFooter == 1);
      reader.layout.size = std::clamp(d["size"] | 20, Layout::minSize, Layout::maxSize) & ~1;
      reader.layout.gap = std::clamp(d["gap"] | 7, 4, 10);
      reader.layout.margin = std::clamp(d["margin"] | 16, 10, 22);
      reader.layout.tone = std::clamp(d["tone"] | 1, 0, 2);
      fontPath = str(d["font"]);
      fontChosen = d["fontChosen"] | !fontPath.empty();
      dict = str(d["dict"]);
      course = str(d["course"]);
    }
  }
  if (!fontChosen) {
    for (auto name : {"/fonts/方正屏显雅宋.TTF", "/fonts/霞鹜臻楷.ttf"}) {
      if (length(root + name)) {
        fontPath = name;
        break;
      }
    }
  }
  if (course.empty()) {
    course = "/courses/english/everyday-starter.json";
  }
  dev.brightness(sleeping ? 0 : light);
  if (sleeping) {
    view = View::Lock;
    return;
  }
  if (resume) {
    Str ticket, saved;
    dev.retained("resumeState", ticket);
    JsonDocument state;
    if (ticket.compare(0, 5, "card:") == 0) {
      if (dev.card && loadRecord(userDirectory() + "wake", saved) && ticket.substr(5) == key(saved)) {
        deserializeJson(state, saved);
      }
    } else if (ticket.compare(0, 6, "local:") == 0) {
      saved = ticket.substr(6);
      deserializeJson(state, saved);
    }
    Str mode = str(state["mode"]);
    if (mode == "book") {
      lastBook = currentRecent(dev.card);
    }
    if (mode == "book" && !lastBook.empty() && lastBook == str(state["book"])) {
      openBook(lastBook);
    } else if (mode == "learn" && dev.card) {
      enterLesson(true);
    }
    if (!state.isNull()) {
      int n = state["view"] | 0;
      if ((n > int(View::Lock) && n <= int(View::ConfirmRestart)) || n == int(View::About)) {
        auto target = View(n);
        bool contextOk = (mode == "book"    ? !reader.path.empty()
                          : mode == "learn" ? !lesson.id.empty()
                                            : true);
        if (contextOk) {
          switch (target) {
            case View::ConfirmLongSleep:
            case View::ConfirmRestart:
              target = View::Power;
              break;
            case View::ClockEdit:
              target = View::Clock;
              break;
            case View::FontPrep:
              target = View::Fonts;
              break;
            case View::Password:
              target = View::Wifi;
              break;
            case View::RemoveMark:
              target = View::Marks;
              break;
            case View::Choice:
              target = View::Quiz;
              break;
            case View::Definition:
            case View::Recall:
              target = View::Learn;
              break;
            case View::Message:
              target = View::Home;
              break;
            default:
              break;
          }
          view = target;
          dir = str(state["dir"]);
          if (!userPath(dir, true)) {
            dir = "/";
          }
          originDir = str(state["origin"]);
          selected = str(state["selected"]);
          fromShelf = state["shelf"] | false;
          pickBook = state["pick"] | false;
          offset = state["offset"] | 0u;
          textPage = state["text"] | 0u;
          chunk = state["chunk"] | 0u;
          directoryPages.clear();
          for (auto saved : state["dirs"].as<JsonArrayConst>()) {
            if (directoryPages.size() >= 64) {
              break;
            }
            directoryPages.push_back(saved.as<unsigned>());
          }
          chosen = state["chosen"] | UINT_MAX;
          stack.clear();
          for (auto v : state["back"].as<JsonArrayConst>()) {
            int b = v.as<int>();
            if (((b > int(View::Lock) && b <= int(View::Power)) || b == int(View::About)) &&
                stack.size() < 12) {
              auto page = state["pages"][stack.size()];
              stack.push_back({View(b), page[0] | 0u, page[1] | 0u});
            }
          }
        }
      }
    }
  }
}
void App::storeSettings() {
  JsonDocument d;
  d["light"] = light;
  d["full"] = cadence;
  d["grip"] = grip;
  d["idle"] = idleMins;
  d["autoClockDays"] = dev.autoClockDays;
  d["showProgress"] = reader.layout.showProgress;
  d["chapterPercent"] = chapterPercent;
  d["size"] = reader.layout.size;
  d["gap"] = reader.layout.gap;
  d["margin"] = reader.layout.margin;
  d["tone"] = reader.layout.tone;
  d["font"] = fontPath;
  d["fontChosen"] = fontChosen;
  d["dict"] = dict;
  d["course"] = course;
  auto s = serialize(d);
  dev.retain("settings", s);
  if (dev.card) {
    saveRecord(userDirectory() + "settings", s);
  }
}
void App::go(View v) {
  if (v == View::Quiz) {
    chosen = UINT_MAX;
  }
  if (v == View::Files && dir == "/") {
    directoryPages.clear();
  }
  if (view != v) {
    if (stack.size() == 12) {
      stack.erase(stack.begin());
    }
    stack.push_back({view, offset, textPage});
  }
  view = v;
  textPage = 0;
  offset = v == View::Jump ? (reader.progress() / 30) * 6 : 0;
  if (v == View::File) {
    fileAdded = onShelf(selected);
  }
  dirty = true;
}
void App::back() {
  if (view == View::ScreenConnect) {
    endScreen();
    return;
  }
  bool leavingStudy = studying();
  if (view == View::Files) {
    pendingBookDirectory = false;
  }
  bool fromTools = view == View::Tools;
  offset = textPage = 0;
  if (view == View::Service) {
    confirmStop = false;
  }
  if (view == View::Password) {
    password.clear();
    showPass = false;
    joining = false;
  }
  if (view == View::Book) {
    position(true);
    invalidateMarks();
    reader.close();
    view = fromShelf ? View::Shelf : originDir.empty() ? View::Home : View::Files;
    stack.clear();
  } else if (view == View::Files && dir != "/") {
    dir = parent(dir);
    if (!directoryPages.empty()) {
      offset = directoryPages.back();
      directoryPages.pop_back();
    }
    if (dir.empty()) {
      dir = "/";
    }
  } else if (!stack.empty()) {
    view = stack.back().view;
    offset = stack.back().offset;
    textPage = stack.back().text;
    stack.pop_back();
  } else {
    view = View::Home;
  }

  dirty = true;
  if (leavingStudy && !studying()) {
    releaseStudy();
  }
  if (fromTools && view == View::Password && joining && dev.connected && dev.network == ssid) {
    back();
  }
}
void App::msg(const Str& s, Recovery action) {
  recovery = action;
  notice = s;
  go(View::Message);
}
void App::tap(int x, int y, int rx, int ry, unsigned heldStillMs) {
  if (preparingFont() || sleepRequested()) {
    return;
  }
  if (view != View::Lock && view != View::Tools && y < 32 && ry - y >= 44 && ry - y > abs(rx - x)) {
    if (view == View::Refresh && !stack.empty() && stack.back().view == View::Tools) {
      back();
    } else {
      go(View::Tools);
    }
    return;
  }
  if (view == View::Book && !waitingResume && !reader.pending && y >= 34 && y < reader.layout.bodyBottom() &&
      heldStillMs >= 400 && ry - y >= 48 && abs(rx - x) <= (ry - y) / 2) {
    noteMark(!marked());
    return;
  }
  if (abs(rx - x) > 28 || abs(ry - y) > 28) {
    return;
  }
  for (auto it = hits.rbegin(); it != hits.rend(); ++it) {
    if (rx >= it->x && rx < it->x + it->w && ry >= it->y && ry < it->y + it->h) {
      auto fn = it->run;
      fn();
      return;
    }
  }
}
bool App::jumpFromReader(Anchor target) {
  if (view != View::Book || reader.path.empty() || waitingResume || reader.pending) {
    return false;
  }
  before = reader.at;
  hasBefore = true;
  reader.jump(target);
  dirty = true;
  return true;
}
void App::single() {
  if (view == View::Lock) {
    if (powerAction == PowerAction::LongSleep || powerAction == PowerAction::Restart) {
      return;
    }
    powerAction = PowerAction::None;
    view = previous;
    dirty = true;
    dev.brightness(light);
    return;
  }
  if (view == View::Book) {
    if (reader.forward()) {
      dirty = true;
    }
    return;
  }
  if (studying() &&
      (view == View::Passage || view == View::Chunks || view == View::Feedback || view == View::Choice ||
       view == View::Quiz || view == View::Definition || view == View::Recall)) {
    if (textPage + 1 < studyPageCount) {
      ++textPage;
      dirty = true;
    } else if (view == View::Chunks && chunk + 1 < lesson.chunks.size()) {
      ++chunk;
      textPage = 0;
      dirty = true;
    }
    return;
  }
  if (view == View::Files || view == View::Shelf || view == View::Catalog || view == View::Toc ||
      view == View::Courses || view == View::Marks || view == View::Jump || view == View::Fonts ||
      view == View::Wifi || view == View::Words || view == View::Dicts || view == View::Vocab ||
      view == View::Review) {
    if (nextListPage) {
      auto next = nextListPage;
      next();
    }
  }
}
void App::sleep() {
  if (view == View::Lock || preparingFont() || dev.powerBusy()) {
    return;
  }
  if (dev.casting) {
    endScreen();
  }
  position(true);
  saveLesson();
  JsonDocument snapshot;
  snapshot["view"] = int(view);
  if (dev.card) {
    snapshot["dir"] = dir;
    snapshot["origin"] = originDir;
    snapshot["selected"] = selected;
  }
  snapshot["shelf"] = fromShelf;
  snapshot["pick"] = pickBook;
  snapshot["offset"] = offset;
  snapshot["text"] = textPage;
  snapshot["chunk"] = chunk;
  snapshot["chosen"] = chosen;
  auto dirs = snapshot["dirs"].to<JsonArray>();
  for (auto index : directoryPages) {
    dirs.add(index);
  }
  auto backStack = snapshot["back"].to<JsonArray>();
  auto pages = snapshot["pages"].to<JsonArray>();
  for (const auto& route : stack) {
    backStack.add(int(route.view));
    auto page = pages.add<JsonArray>();
    page.add(route.offset);
    page.add(route.text);
  }
  snapshot["mode"] =
      !reader.path.empty() ? "book"
      : (view >= View::Learn && view <= View::Review) || view == View::Recall || view == View::Choice ||
              (!stack.empty() && (stack.back().view == View::Passage || stack.back().view == View::Chunks))
          ? "learn"
          : "home";
  if (dev.card) {
    if (!reader.path.empty()) {
      snapshot["book"] = reader.path;
    }
    snapshot["time"] = int64_t(dev.clock());
    snapshot["uptime"] = dev.uptime();
    auto saved = serialize(snapshot);
    dev.retain("resumeState", saveRecord(userDirectory() + "wake", saved) ? "card:" + key(saved) : "");
  } else {
    dev.retain("resumeState", "local:" + serialize(snapshot));
  }
  password.clear();
  dev.service(false);
  dev.brightness(0);
  previous = view;
  view = View::Lock;
  powerAction = PowerAction::ClockSleep;
  dirty = forceFull = true;
}
void App::confirmPower(PowerAction action) {
  if (dev.powerBusy()) {
    msg("文件正在传输，请稍后再试");
    return;
  }
  dev.service(false);
  position(true);
  saveLesson();
  shelfCheck.reset();
  invalidateMarks();
  reader.close();
  dev.brightness(0);
  powerAction = action;
  view = View::Lock;
  dirty = forceFull = true;
}
bool App::inUse(const Str& p) const {
  auto target = lower(p);
  for (const auto* s : {&reader.path, &reader.font.path, &pendingFont, &course, &dict}) {
    auto actual = lower(*s);
    if (!actual.empty() && (actual == target || actual.compare(0, target.size() + 1, target + "/") == 0)) {
      return true;
    }
  }
  if (!dict.empty()) {
    auto base = lower(dict.substr(0, dict.size() - 4));
    for (const char* ext : {".idx", ".dict", ".syn", ".jdx"}) {
      if (target == base + ext) {
        return true;
      }
    }
  }
  return false;
}
void App::removed() {
  releaseStudy();
  lesson.title.clear();
  lastBook.clear();
  dir = "/";
  selected.clear();
  originDir.clear();
  directoryPages.clear();
  pendingFont.clear();
  shelfCheck.reset();
  checkingShelf.clear();
  visiblePending.clear();
  invalidateFileIdentities();
  waitingResume = uncommitted = false;
  invalidateMarks();
  reader.close();
  dev.service(false);
  if (view == View::About || view == View::CardInfo) {
    dirty = true;
    return;
  }
  if (view != View::Lock && !dev.casting) {
    stack.clear();
    view = View::Home;
    msg("TF 卡已断开，插回后可继续", Recovery::Card);
  }
}
void App::background(bool idle) {
  if (sleepRequested()) {
    return;
  }
  if (preparingFont()) {
    if (prepReady && !dirty) {
      prepareFont();
    }
    return;
  }
  checkShelf(idle);
  if (idle || waitingResume || resolving) {
    reader.background(idle && view == View::Book && !dirty && !waitingResume);
  }
  if (idle && view == View::Toc && !dirty && reader.chaptersPending()) {
    auto count = reader.chapters();
    reader.scanChapters();
    if (!reader.chaptersPending() || (count <= offset + 6 && count != reader.chapters())) {
      dirty = true;
    }
  }
  if (waitingResume && view == View::Book && reader.validating()) {
    unsigned progress = reader.verificationPercent() / 20 * 20;
    if (progress != resumePercent) {
      resumePercent = progress;
      dirty = true;
    }
  }
  if (resolving && !reader.path.empty() && !reader.validating()) {
    identityReady();
  }
  if (uncommitted && !reader.id.empty()) {
    position();
  }
}
void App::checkShelf(bool idle) {
  const Str* path = nullptr;
  if (view == View::File && fileAdded) {
    path = &selected;
  }
  if (view == View::Shelf && !visiblePending.empty()) {
    path = &visiblePending;
  }
  if (!path || !dev.card || !reader.path.empty()) {
    if (shelfCheck.running()) {
      shelfCheck.reset();
      checkingShelf.clear();
    }
    return;
  }
  if (!idle || dev.serving) {
    return;
  }
  if (checkingShelf != *path) {
    checkingShelf = *path;
    if (!shelfCheck.begin(root + *path)) {
      return;
    }
  }
  if (shelfCheck.running() && !shelfCheck.step()) {
    return;
  }
  if (!shelfCheck.value().empty()) {
    bool changed = resolveShelf(*path, shelfCheck.value());
    shelfCheck.reset();
    if (changed && view == View::Shelf) {
      dirty = true;
    }
  }
}
void App::startBookPicker(bool adding) {
  directoryPages.clear();
  dir = "/";
  pendingBookDirectory = true;
  pickBook = adding;
  go(View::Files);
}
void App::fileRows() {
  header(pickBook ? "添加书籍" : "文件管理");
  if (!dev.card) {
    row(0, "重新检测 TF 卡", [this] {
      dev.mount();
      dirty = true;
    });
    return;
  }
  if (pendingBookDirectory) {
    dir = folder(root + "/books") ? "/books" : "/";
    pendingBookDirectory = false;
  }
  canvas.ui(16, 44, dir, 12, 208);
  bool more = false;
  unsigned total = 0;
  auto entries = list(root + dir, offset, 6, more, false, &total);
  if (entries.empty()) {
    canvas.ui(16, 88, "此目录为空", 15);
  }
  for (unsigned i = 0; i < entries.size(); i++) {
    auto e = entries[i];
    Str p = join(dir, e.name);
    entry(73 + i * 45, e.name, e.folder ? "目录" : "", [this, e, p] {
      if (e.folder) {
        if (directoryPages.size() >= 64) {
          directoryPages.erase(directoryPages.begin());
        }
        directoryPages.push_back(offset);
        dir = p;
        offset = 0;
        dirty = true;
        return;
      }
      selected = p;
      go(View::File);
    });
  }
  turnList(offset, more, total);
}
void App::lock() {
  header("静读", false);
  if (powerAction == PowerAction::LongSleep || powerAction == PowerAction::Restart) {
    const bool sleeping = powerAction == PowerAction::LongSleep;
    const Str title = sleeping ? "已进入长期休眠" : "正在重启";
    canvas.ui((240 - Canvas::textWidth(title, 20)) / 2, 171, title, 20);
    const Str hint = sleeping ? "按一下侧键回到首页" : "稍后将回到首页";
    canvas.ui((240 - Canvas::textWidth(hint, 12)) / 2, 219, hint, 12);
    return;
  }
  auto t = dev.clock();
  struct tm local{};
  localtime_r(&t, &local);
  bool known = local.tm_year >= 120;
  const char* days[] = {"日", "一", "二", "三", "四", "五", "六"};
  char clockText[8] = "--:--";
  if (known) {
    strftime(clockText, sizeof clockText, "%H:%M", &local);
  }
  canvas.number(known ? 14 : 26, 148, clockText, true);
  auto centered = [this](const Str& text, int y, int px) {
    canvas.ui((240 - Canvas::textWidth(text, px)) / 2, y, text, px, 230);
  };
  if (known) {
    centered(std::to_string(local.tm_mon + 1) + " 月 " + std::to_string(local.tm_mday) + " 日  星期" +
                 days[local.tm_wday],
             184, 15);
    Str calendar = std::to_string(local.tm_year + 1900) + " 年";
    Str lunar = lunarDate(local);
    if (!lunar.empty()) {
      calendar += "  ·  " + lunar;
    }
    centered(calendar, 214, 12);
  } else {
    centered("时间待校准", 184, 15);
  }

  canvas.rect(18, 263, 204, 1);
  canvas.icon("thermo", 18, 282);
  canvas.ui(42, 285, "温度", 12);
  canvas.icon("drop", 135, 282);
  canvas.ui(159, 285, "湿度", 12);
  char temp[12] = "--", humidity[12] = "--";
  if (dev.measured && std::isfinite(dev.temp) && std::isfinite(dev.rh)) {
    snprintf(temp, sizeof temp, "%.1f", dev.temp);
    snprintf(humidity, sizeof humidity, "%.0f", dev.rh);
  }
  auto tx = canvas.number(18, 340, temp);
  auto hx = canvas.number(135, 340, humidity);
  canvas.ui(tx + 2, 325, "°C", 15);
  canvas.ui(hx + 2, 325, "%", 15);
  centered("单击侧键唤醒", 388, 12);
}
void App::paint() {
  dirty = false;
  pageAck = false;
  hits.clear();
  nextListPage = {};
  canvas.clear();
  switch (view) {
    case View::Lock:
      lock();
      break;
    case View::Home: {
      lastBook = currentRecent(dev.card);
      header("静读", false);
      canvas.ui(16, 53, "最近阅读", 12);
      canvas.ui(16, 78, lastBook.empty() ? "尚未打开书籍" : displayName(lastBook), 15, 156);
      if (!lastBook.empty() && dev.card) {
        Str id;
        JsonDocument saved;
        if (loadRecord(userDirectory() + "locator/" + key(lastBook), id)) {
          readDoc(userDirectory() + "positions/" + id, saved);
          if (saved["percent"].is<int>()) {
            canvas.ui(178, 82, std::to_string(saved["percent"].as<int>()) + "%", 12);
          }
        }
      }
      primary(16, 111, 208, lastBook.empty() ? "从 TF 卡选书" : "继续阅读", [this] {
        lastBook = currentRecent(dev.card);
        originDir.clear();
        fromShelf = false;
        if (lastBook.empty()) {
          startBookPicker(false);
        } else {
          openBook(lastBook);
        }
      });
      canvas.rect(16, 168, 208, 1);
      const char* labels[] = {"书架", "英语练习", "文件管理", "设置"};
      for (int n = 0; n < 4; n++) {
        int y = 178 + 55 * n;
        canvas.homeIcon(n, 16, y + 13);
        canvas.ui(46, y + 14, labels[n], 15, 151);
        canvas.icon("next", 208, y + 11);
        hit(0, y, 240, 44, [this, n] {
          if (n == 0) {
            go(View::Shelf);
          }
          if (n == 1) {
            go(View::Learn);
          }
          if (n == 2) {
            fromShelf = pickBook = false;
            dir = originDir.empty() ? "/" : originDir;
            go(View::Files);
          }
          if (n == 3) {
            go(View::Settings);
          }
        });
      }
      break;
    }
    case View::Shelf: {
      header("书架");
      visiblePending.clear();
      bool more;
      unsigned total = 0;
      auto es = list(userDirectory() + "shelf", offset, 6, more, false, &total);
      for (unsigned i = 0; i < es.size(); i++) {
        Str p;
        loadRecord(userDirectory() + "shelf/" + es[i].name + "/ref", p);
        JsonDocument d;
        if (es[i].name.compare(0, 2, "p-") == 0) {
          if (visiblePending.empty()) {
            visiblePending = p;
          }
        } else {
          readDoc(userDirectory() + "positions/" + es[i].name, d);
        }
        entry(52 + i * 47, displayName(p), std::to_string(d["percent"] | 0) + "%", [this, p] {
          originDir.clear();
          fromShelf = true;
          openBook(p);
        });
      }
      auto add = [this] { startBookPicker(true); };
      if (offset || more) {
        compact(2, 337, "添加", add);
      } else {
        primary(16, 366, 208, "从 TF 卡添加", add);
      }
      turnList(offset, more, total);
      break;
    }
    case View::Files:
      fileRows();
      break;
    case View::FontPrep:
      fontProgress();
      break;
    case View::Fonts:
      fontRows();
      break;
    case View::File:
      header("文件详情");
      canvas.ui(12, 50, leaf(selected), 16);
      if (ends(selected, ".txt") || ends(selected, ".epub")) {
        row(1, "打开阅读", [this] {
          originDir = dir;
          openBook(selected);
        });
        if (fileAdded) {
          canvas.ui(16, 157, "已加入书架", 15);
          canvas.icon("tick", 206, 151);
        } else {
          row(2, "加入书架", [this] { addBook(); });
        }
      } else {
        canvas.ui(12, 100, "可通过文件服务管理", 16);
      }
      break;
    case View::Book:
    case View::Menu:
    case View::Tools:
    case View::Layout:
    case View::Toc:
    case View::Marks:
    case View::RemoveMark:
    case View::Jump:
      reading();
      break;
    case View::ScreenConnect:
    case View::ScreenCanvas:
    case View::ScreenInfo:
    case View::ScreenKey:
      screenPage();
      break;
    case View::Clock:
    case View::AutoClock:
    case View::ClockEdit:
      clockPage();
      break;
    case View::About:
    case View::CardInfo:
    case View::AutoLock:
    case View::Refresh:
    case View::Settings:
    case View::Power:
    case View::ConfirmLongSleep:
    case View::ConfirmRestart:
    case View::Wifi:
    case View::Password:
    case View::Service:
      settings();
      break;
    case View::Message:
      if (recovery == Recovery::Changed && versionChoice) {
        header("文件已更新", false);
        canvas.ui(16, 76, "旧版本的进度和书签已保留", 14, 208);
        auto choose = [this](bool restart) {
          versionChoice = false;
          resumeId.clear();
          if (restart) {
            reader.jump({});
          }
          stack.clear();
          view = View::Book;
          dirty = true;
        };
        row(2, "从此处继续", [choose] { choose(false); });
        row(3, "从头阅读", [choose] { choose(true); });
        break;
      }
      header("静读");
      paragraph(notice, 12, 67, 216, 315, 0);
      compact(1, 380,
              recovery == Recovery::Card    ? "重新检测"
              : recovery == Recovery::Font  ? "重新选字体"
              : recovery == Recovery::Shelf ? "回到书架"
                                            : "返回",
              [this] {
                if (recovery == Recovery::Card) {
                  dev.mount();
                  if (!dev.card) {
                    notice = "请检查 TF 卡";
                    dirty = true;
                    return;
                  }
                }
                if (recovery == Recovery::Shelf && (stack.empty() || stack.back().view != View::Shelf)) {
                  stack.clear();
                  view = View::Home;
                  go(View::Shelf);
                } else {
                  back();
                }
              });
      break;
    default:
      learning();
      break;
  }
}
}  // namespace jd
