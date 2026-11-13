#include "App.h"
#ifdef ARDUINO
#include <Arduino.h>
#endif
#include <array>
#include <algorithm>
#include "ViewData.h"

namespace jd {
void App::setStage(FontStage stage) {
#ifdef ARDUINO
  if (stage != FontStage::Open) {
    Serial.printf("JINGDU_FONT_PHASE stage=%u elapsed_ms=%u\n", unsigned(fontStage),
                  dev.uptime() - phaseStarted);
  }
#endif
  phaseStarted = dev.uptime();
  fontStage = stage;
  prepPercent = stage == FontStage::Done ? 100 : 0;
  if (stage == FontStage::Open) {
    prepReady = prepPainted = false;
    dirty = true;
  }
}
void App::finishFont() {
  fontPath = pendingFont;
  pendingFont.clear();
  fontChosen = true;
  storeSettings();
  back();  // Preparation -> picker -> original layout page.
  back();
}
void App::startFont(const Str& path) {
  if (path.empty() ||
      (reader.font.active() && path == reader.font.path && !reader.font.generation().empty())) {
    reader.selectFont(path);
    fontPath = path;
    fontChosen = true;
    storeSettings();
    back();
    return;
  }
  pendingFont = path;
  warmIndex = 0;
  go(View::FontPrep);
  setStage(FontStage::Open);
  prepareFont();
}
void App::failFont(const Str& reason) {
  reader.selectFont(fontPath);
  pendingFont.clear();
  back();  // Keep the picker and its original selection accessible.
  msg(reason, Recovery::Font);
}
void App::prepareFont() {
  unsigned progress = 0;
  switch (fontStage) {
    case FontStage::Open:
      // The current file can render immediately. Persistent glyphs and page
      // images stay gated on its identity inside BodyFont and Reader.
      if (!reader.selectFont(pendingFont)) {
        forgetFileIdentity(root + pendingFont);
        failFont("字体无法打开，请选择其他字体");
        return;
      }
      if (reader.restoreCachedBody(canvas)) {
        finishFont();
        return;
      }
      setStage(FontStage::Warm);
      return;
    case FontStage::Warm:
      if (!reader.path.empty()) {
        int done = reader.preparePage(progress);
        if (done < 0) {
          failFont("当前页读取失败，请检查 TF 卡");
          return;
        }
        if (done > 0) {
          setStage(FontStage::Render);
          return;
        }
      } else {
        static constexpr char32_t sample[] =
            U"静读春天的夜里，风从窗前吹过。山水之间有一条小路。Aa0123456789";
        constexpr unsigned total = sizeof(sample) / sizeof(*sample) - 1;
        for (unsigned i = 0; i < 3 && warmIndex < total; ++i) {
          reader.font.advance(sample[warmIndex++]);
        }
        progress = warmIndex * 100 / total;
        if (warmIndex == total) {
          setStage(FontStage::Done);
          return;
        }
      }
      break;
    case FontStage::Render:
      // The progress frame has been acknowledged. Its framebuffer is free for the real page now.
      canvas.clear();
      if (!reader.paint(canvas)) {
        if (!reader.pending) {
          failFont("当前页渲染失败");
        }
        return;
      }
      setStage(FontStage::Done);
      return;
    case FontStage::Done:
      finishFont();
      return;
  }
  if (progress / 20 > prepPercent / 20 && dev.uptime() - lastFontFrame >= 700) {
    prepPercent = progress;
    prepReady = false;
    dirty = true;
  }
}
void App::fontProgress() {
  header("准备正文字体", false);
  canvas.ui(16, 69, leaf(pendingFont), 16, 210);
  const char* phase = fontStage == FontStage::Open ? "打开字体索引"
                      : fontStage == FontStage::Warm
                          ? (reader.path.empty() ? "缓存常用字形" : "排版当前阅读页")
                      : fontStage == FontStage::Render ? "生成当前页画面"
                                                       : "字体已准备好";
  canvas.ui(16, 154, phase, 16, 211);
  bool measured = fontStage == FontStage::Warm || fontStage == FontStage::Done;
  if (measured) {
    canvas.ui(178, 156, std::to_string(prepPercent) + "%", 12, 46);
    canvas.border(16, 185, 208, 13);
    canvas.rect(18, 187, 204 * prepPercent / 100, 9);
  } else {
    canvas.ui(16, 187, "正在处理…", 12);
  }
  canvas.rect(16, 326, 208, 1);
  canvas.ui(16, 345, "完成后自动返回阅读排版", 12, 215);
  prepPainted = true;
}
void App::fontRows() {
  header("选择正文字体");
  auto select = [this](const Str& path) { startFont(path); };
  option(48, "内置点阵", fontPath.empty(), [select] { select(""); });
  bool more = false;
  auto entries = dev.card ? list(root + fontDir, offset, 5, more, true) : std::vector<Entry>{};
  for (unsigned i = 0; i < entries.size(); ++i) {
    const auto& e = entries[i];
    Str path = join(fontDir, e.name);
    int y = 95 + i * 47;
    if (e.folder) {
      entry(y, e.name, "", [this, path] {
        fontDir = path;
        offset = 0;
        dirty = true;
      });
    } else {
      option(y, displayName(e.name), fontPath == path, [select, path] { select(path); });
    }
  }
  if (entries.empty()) {
    canvas.ui(15, 121, dev.card ? "此目录没有 TTF / OTF" : "请插入 TF 卡", 12, 215);
  }
  canvas.ui(10, 338, fontDir, 12, 220);
  if (offset) {
    pageControl(false, [this] {
      offset = offset >= 5 ? offset - 5 : 0;
      dirty = true;
    });
  }
  if (fontDir != "/") {
    compact(1, 380, "上级", [this] {
      fontDir = parent(fontDir);
      if (fontDir.empty()) {
        fontDir = "/";
      }
      offset = 0;
      dirty = true;
    });
  }
  if (more) {
    pageControl(true, [this] {
      offset += 5;
      dirty = true;
    });
  }
}
void App::abortReading(const Str& reason) {
  bool wasEpub = ends(reader.path, ".epub");
  invalidateMarks();
  reader.close();
  waitingResume = resolving = uncommitted = pageAck = false;
  view = fromShelf ? View::Shelf : originDir.empty() ? View::Home : View::Files;
  stack.clear();
  msg(reason, wasEpub ? Recovery::Shelf : Recovery::Back);
}
void App::reading() {
  auto reset = [this] {
    reader.selectFont(fontPath);
    storeSettings();
    dirty = true;
  };
  switch (view) {
    case View::Book: {
      if (waitingResume) {
        header("恢复阅读");
        canvas.ui(20, 115, "正在核对书籍版本", 16);
        canvas.border(20, 154, 200, 10);
        canvas.rect(22, 156, 196 * resumePercent / 100, 6);
        canvas.ui(20, 178, std::to_string(resumePercent) + "%", 12);
        return;
      }
      header(displayName(reader.path), false);
      bool ok = reader.paint(canvas);
      if (!ok) {
        if (reader.pending) {
          canvas.rect(0, 34, 240, 382, false);
          int progress = reader.chapterPreparation();
          if (progress < 0) {
            canvas.ui(17, 120, "正在准备这一页…", 16);
          } else {
            // Update at ten-percent intervals to limit e-paper refreshes.
            int step = progress / 10 * 10;
            canvas.ui(17, 116, "正在准备章节", 16);
            canvas.border(17, 153, 206, 10);
            canvas.rect(19, 155, 202 * step / 100, 6);
            canvas.ui(17, 178, std::to_string(step) + "%", 12);
          }
          dirty = true;
        } else {
          // Return to the book's origin so dismissing an error cannot reenter
          // the same failing page and overwrite the last readable position.
          Str reason = reader.error.empty() ? "无法读取本页" : reader.error;
          abortReading(reason);
        }
        return;
      }
      if (marked()) {
        canvas.rect(230, 42, 9, 14);
        for (int i = 0; i < 4; ++i) {
          canvas.rect(230, 56 + i, 4 - i, 1);
          canvas.rect(235 + i, 56 + i, 4 - i, 1);
        }
      }
      pageAck = true;
      int left = grip == 1 ? 102 : grip == 2 ? 48 : 72;
      int right = grip == 2 ? 128 : grip == 1 ? 182 : 168;
      hit(0, 36, left, reader.layout.bodyBottom() - 36, [this] {
        if (grip == 1 ? reader.forward() : reader.backward()) {
          dirty = true;
        }
      });
      hit(left, 36, right - left, reader.layout.bodyBottom() - 36, [this] { go(View::Menu); });
      hit(right, 36, 240 - right, reader.layout.bodyBottom() - 36, [this] {
        if (grip == 1 ? reader.backward() : reader.forward()) {
          dirty = true;
        }
      });
      if (reader.layout.showProgress) {
        unsigned percent = chapterPercent ? reader.chapterProgress() : reader.progress();
        Str label = std::to_string(percent) + "%";
        canvas.rect(0, 402, 240, 14, false);
        for (int x = 16; x < 192; x += 3) {
          canvas.dot(x, 409);
        }
        canvas.rect(16, 409, 176 * percent / 100, 1);
        canvas.ui(224 - Canvas::textWidth(label, 12), 402, label, 12);
      }
      break;
    }
    case View::Menu: {
      header("阅读菜单");
      canvas.ui(16, 44, displayName(reader.path), 15, 208);
      canvas.ui(16, 67, reader.currentTitle(), 12, 208);
      canvas.rect(16, 91, 208, 1);
      entry(96, "跳转进度", std::to_string(reader.progress()) + "%", [this] { go(View::Jump); });
      entry(141, "章节目录", "", [this] { go(View::Toc); });
      entry(186, "我的书签", "", [this] { go(View::Marks); });
      canvas.rect(16, 233, 208, 1);
      entry(238, "阅读排版", "", [this] { go(View::Layout); });
      entry(283, "快捷设置", "", [this] { go(View::Tools); });
      canvas.rect(16, 329, 208, 1);
      bool saved = marked();
      canvas.icon(saved ? "minus" : "plus", 16, 342);
      canvas.ui(45, 345, saved ? "取消本页书签" : "添加本页书签", 15, 160);
      hit(0, 328, 240, 44, [this, saved] { noteMark(!saved); });
      if (hasBefore) {
        entry(372, "回到跳转前", "", [this] {
          reader.jump(before);
          hasBefore = false;
          view = View::Book;
          stack.clear();
          dirty = true;
        });
      }
      break;
    }
    case View::Tools: {
      bool fromBook = !stack.empty() && (stack.back().view == View::Book || stack.back().view == View::Menu);
      header("快捷设置");
      brightnessControl();
      entry(110, "全刷间隔", std::to_string(cadence) + " 页", [this] { go(View::Refresh); });
      if (fromBook) {
        canvas.rect(16, 175, 208, 1);
        stepper(190, "翻页手势", std::array<const char*, 3>{"标准", "左手", "右手"}[grip], grip, 0, 2,
                [this](int delta) {
                  grip += delta;
                  storeSettings();
                  dirty = true;
                });
      }
      primary(16, 366, 208, "立即清屏", [this] {
        forceFull = true;
        back();
      });
      break;
    }
    case View::Layout:
      header("阅读排版");
      row(
          0, "正文字体",
          [this] {
            fontDir = folder(root + "/fonts") ? "/fonts" : "/";
            go(View::Fonts);
          },
          fontPath.empty() ? "内置点阵" : leaf(fontPath));
      canvas.rect(16, 99, 208, 1);
      stepper(105, "字号", std::to_string(reader.layout.size), reader.layout.size, Layout::minSize,
              Layout::maxSize, [this, reset](int d) {
                reader.layout.size += 2 * d;
                reset();
              });
      stepper(152, "行距", std::to_string(reader.layout.gap), reader.layout.gap, 4, 10, [this, reset](int d) {
        reader.layout.gap += 3 * d;
        reset();
      });
      stepper(199, "边距", std::to_string(reader.layout.margin), reader.layout.margin, 10, 22,
              [this, reset](int d) {
                reader.layout.margin += 6 * d;
                reset();
              });
      stepper(246, "墨色", std::array<const char*, 3>{"清淡", "标准", "浓墨"}[reader.layout.tone],
              reader.layout.tone, 0, 2, [this, reset](int d) {
                reader.layout.tone += d;
                reset();
              });
      canvas.rect(16, 297, 208, 1);
      canvas.ui(16, 319, "显示阅读进度");
      canvas.ui(158, 321, reader.layout.showProgress ? "开" : "关", 12);
      canvas.border(184, 315, 38, 22);
      canvas.rect(reader.layout.showProgress ? 204 : 187, 318, 15, 16);
      hit(0, 304, 240, 44, [this] {
        reader.showProgress(!reader.layout.showProgress);
        storeSettings();
        dirty = true;
      });
      if (reader.layout.showProgress) {
        stepper(354, "进度范围", chapterPercent ? "本章" : "全书", chapterPercent, 0, 1, [this](int delta) {
          chapterPercent = delta > 0;
          storeSettings();
          dirty = true;
        });
      }
      break;
    case View::Toc: {
      header("章节目录");
      auto n = reader.chapters();
      if (!n && reader.chaptersPending()) {
        canvas.ui(16, 65, "正在查找章节…", 15);
      } else if (!n) {
        row(0, "此 TXT 无章节目录", [this] { go(View::Jump); }, "按阅读进度跳转");
      }
      for (unsigned i = offset; i < n && i < offset + 6; i++) {
        row(i - offset, reader.chapterName(i), [this, i] {
          before = reader.at;
          hasBefore = true;
          reader.jumpChapter(i);
          view = View::Book;
          stack.clear();
          dirty = true;
        });
      }
      turnList(offset, offset + 6 < n, n);
      break;
    }
    case View::Jump:
      header("跳转进度");
      canvas.ui(16, 55, "当前 " + std::to_string(reader.progress()) + "%", 12);
      for (unsigned i = offset; i < 21 && i < offset + 6; ++i) {
        unsigned pct = i * 5;
        option(82 + (i - offset) * 45, std::to_string(pct) + "%", pct == reader.progress(), [this, pct] {
          before = reader.at;
          hasBefore = true;
          reader.percent(pct);
          view = View::Book;
          stack.clear();
          dirty = true;
        });
      }
      turnList(offset > 0, offset + 6 < 21, 21);
      break;
    case View::RemoveMark: {
      header("书签详情");
      Str p = markPath();
      JsonDocument d;
      readDoc(p, d);
      auto ar = d.as<JsonArray>();
      if (markIndex >= ar.size()) {
        back();
        return;
      }
      auto saved = ar[markIndex];
      auto target = anchor(saved);
      canvas.ui(16, 55, str(saved["name"]), 15, 208);
      canvas.ui(16, 80, std::to_string(saved["percent"] | 0) + "%", 12);
      paragraph(str(saved["text"]), 16, 109, 208, 245);
      primary(16, 366, 208, "打开书签位置", [this, target] {
        before = reader.at;
        hasBefore = true;
        reader.jump(target);
        view = View::Book;
        stack.clear();
        dirty = true;
      });
      btn(16, 275, 208, 44, "移除此书签", [this, p] {
        JsonDocument doc;
        if (!readMarks(p, doc) || markIndex >= doc.size()) {
          msg("书签读取失败");
          return;
        }
        doc.as<JsonArray>().remove(markIndex);
        invalidateMarks();
        if (doc.overflowed() || !saveRecord(p, serialize(doc))) {
          msg("保存失败");
        } else {
          back();
        }
      });
      break;
    }
    case View::Marks: {
      header("我的书签");
      JsonDocument d;
      Str p = markPath();
      readDoc(p, d);
      auto ar = d.as<JsonArray>();
      if (offset >= ar.size()) {
        offset = ar.size() ? ((ar.size() - 1) / 6) * 6 : 0;
      }
      if (!ar.size()) {
        canvas.ui(16, 78, "还没有书签");
        canvas.ui(16, 118, "正文按住再下拉可添加。", 12);
        primary(16, 366, 208, "添加本页书签", [this] { noteMark(true); });
      }
      for (unsigned i = offset; i < ar.size() && i < offset + 6; ++i) {
        Str title = str(ar[i]["name"]);
        entry(52 + (i - offset) * 47, title.empty() ? "书签 " + std::to_string(i + 1) : title,
              std::to_string(ar[i]["percent"] | 0) + "%", [this, i] {
                markIndex = i;
                go(View::RemoveMark);
              });
      }
      turnList(offset, offset + 6 < ar.size(), ar.size());
      break;
    }
    default:
      break;
  }
}
}  // namespace jd
