#include "App.h"
#ifdef ARDUINO
#include <Arduino.h>
#endif
#include <algorithm>
#include <unistd.h>
#include "ViewData.h"
#include "Bookshelf.h"

namespace jd {
unsigned App::paragraph(const Str& text, int left, int top, int width, int bottom, unsigned wanted,
                        bool study) {
  const int px = study ? 20 : reader.font.px(), right = left + width;
  auto advance = [&](uint32_t cp) {
    int n = study ? Canvas::studyAdvance(cp, px) : -1;
    if (n >= 0) {
      return n;
    }
    if (study && !studyFontTried) {
      studyFontTried = true;
      reader.font.select(fontPath, 20, 1);
    }
    return reader.font.advance(cp);
  };
  auto latin = [](uint32_t cp) { return cp < 0x370 || (cp >= 0x2000 && cp < 0x2070); };
  int x = left, y = top;
  unsigned page = 0;
  auto newline = [&] {
    x = left;
    y += px + 6;
    if (y + px > bottom) {
      ++page;
      y = top;
    }
  };
  const char* p = text.data();
  const char* end = p + text.size();
  bool gap = false;
  while (p < end) {
    const char* token = p;
    auto cp = nextChar(p, end);
    if (cp == '\n') {
      newline();
      gap = false;
      continue;
    }
    if (cp == ' ' || cp == '\t' || cp == '\r') {
      gap = true;
      continue;
    }
    int w = advance(cp);
    if (latin(cp)) {
      while (p < end) {
        const char* after = p;
        auto next = nextChar(after, end);
        if (next <= 32 || !latin(next)) {
          break;
        }
        w += advance(next);
        p = after;
      }
    }
    int space = gap && x > left ? advance(' ') : 0;
    if (x > left && x + space + w > right) {
      newline();
    } else {
      x += space;
    }
    while (token < p) {
      cp = nextChar(token, p);
      int n = advance(cp);
      if (x > left && x + n > right) {
        newline();
      }
      if (page == wanted && (!study || canvas.studyChar(x, y, cp, px) < 0)) {
        reader.font.draw(canvas, x, y, cp);
      }
      x += n;
    }
    gap = false;
  }
  return page + 1;
}
void App::releaseStudy() {
  if (studyFontTried) {
    reader.font.close();
  }
  studyFontTried = false;
  Str title = std::move(lesson.title);
  lesson = {};
  lesson.title = std::move(title);
  Str().swap(answer);
  Str().swap(word);
  Str().swap(definition);
}
void App::enterLesson(bool restore) {
  if (course.empty() || !dev.card) {
    msg("请插入课包 TF 卡", Recovery::Card);
    return;
  }
  Str expected;
  if (restore) {
    Str s;
    if (loadRecord(userDirectory() + "study/" + key(course), s)) {
      JsonDocument d;
      if (!deserializeJson(d, s)) {
        article = d["article"] | 0u;
        segment = d["segment"] | 0u;
        answer = str(d["answer"]);
        expected = str(d["id"]);
      }
    }
  }
  bool ok = fetchLesson(course, article, segment, lesson, articles, segments);
  if (restore && !expected.empty() && (!ok || lesson.id != expected)) {
    ok = false;
    unsigned count = 0, parts = 0;
    Lesson tmp;
    if (fetchLesson(course, 0, 0, tmp, count, parts)) {
      for (unsigned a = 0; a < count && !ok; ++a) {
        if (!fetchLesson(course, a, 0, tmp, count, parts)) {
          break;
        }
        for (unsigned n = 0; n < parts && !ok; ++n) {
          if (fetchLesson(course, a, n, tmp, count, parts) && tmp.id == expected) {
            article = a;
            segment = n;
            lesson = tmp;
            articles = count;
            segments = parts;
            ok = true;
          }
        }
      }
    }
    if (!ok) {
      article = segment = 0;
      answer.clear();
      ok = fetchLesson(course, 0, 0, lesson, articles, segments);
    }
  }
  if (!ok) {
    releaseStudy();
    msg("课包缺失或格式不支持");
    return;
  }
  go(restore && !answer.empty() ? View::Feedback : View::Passage);
  saveLesson();
}
void App::saveLesson() {
  if (!dev.card || lesson.id.empty()) {
    return;
  }
  JsonDocument d;
  d["article"] = article;
  d["segment"] = segment;
  d["answer"] = answer;
  d["id"] = lesson.id;
  saveRecord(userDirectory() + "study/" + key(course), serialize(d));
}
void App::choose(unsigned i) {
  if (i >= lesson.choiceIds.size()) {
    return;
  }
  answer = lesson.choiceIds[i];
  Str score = userDirectory() + "answers/" + key(course + lesson.id);
  Str old;
  if (!loadRecord(score, old)) {
    saveRecord(score, answer == lesson.correct ? "1" : "0");
  }
  if (answer != lesson.correct) {
    JsonDocument d;
    d["course"] = course;
    d["article"] = article;
    d["segment"] = segment;
    d["title"] = lesson.title;
    saveRecord(userDirectory() + "errors/" + key(course + lesson.id) + "/item", serialize(d));
  } else {
    Str p = userDirectory() + "errors/" + key(course + lesson.id);
    remove((p + "/item.0").c_str());
    remove((p + "/item.1").c_str());
    rmdir(p.c_str());
  }
  saveLesson();
  go(View::Feedback);
}
unsigned App::meaningPages(const Str& text, int top) {
  auto end = text.find('\n');
  if (!text.empty() && text.front() == '/' && end != Str::npos && end > 1 && end < 100 &&
      text[end - 1] == '/' && Canvas::textWidth(text.substr(0, end), 15, true) <= 208) {
    canvas.ui(16, top, text.substr(0, end), 15, 208, true, true);
    return paragraph(text.substr(end + 1), 16, top + 30, 208, 334, textPage, true);
  }
  return paragraph(text, 16, top, 208, 334, textPage, true);
}
void App::learning() {
  auto study = [this](unsigned a) {
    article = a;
    segment = chunk = 0;
    answer.clear();
    enterLesson(false);
  };
  if (view == View::Learn) {
    header("英语练习");
    entry(
        48, "继续练习", "", [this] { enterLesson(true); },
        lesson.title.empty() ? displayName(course) : lesson.title);
    canvas.rect(16, 106, 208, 1);
    entry(116, "选择课包", displayName(course), [this] { go(View::Courses); });
    entry(163, "选择篇目", "", [this] { go(View::Catalog); });
    canvas.rect(16, 216, 208, 1);
    entry(226, "我的词典", "", [this] { go(View::Dicts); });
    entry(273, "我的生词", "", [this] { go(View::Vocab); });
    entry(320, "错题复习", "", [this] { go(View::Review); });
    return;
  }
  if (view == View::Courses) {
    header("TF 课包");
    bool more;
    auto es = list(root + "/courses/english", offset, 6, more);
    if (es.empty()) {
      canvas.ui(16, 72, "尚未导入课包", 15);
      canvas.ui(16, 112, "TF /courses/english", 12);
    }
    for (unsigned i = 0; i < es.size(); i++) {
      auto n = es[i].name;
      if (ends(n, ".json") || ends(n, ".jdc")) {
        option(48 + 47 * i, displayName(n), leaf(course) == n, [this, n] {
          releaseStudy();
          lesson.title.clear();
          course = "/courses/english/" + n;
          article = segment = 0;
          answer.clear();
          storeSettings();
          go(View::Catalog);
        });
      }
    }
    turnList(offset, more);
    return;
  }
  if (view == View::Catalog) {
    header("阅读篇目");
    Lesson l;
    articles = 0;
    unsigned n, s;
    for (unsigned i = 0; i < 6; ++i) {
      unsigned a = offset + i;
      if (!fetchLesson(course, a, 0, l, n, s, true)) {
        break;
      }
      articles = n;
      entry(52 + i * 47, l.title, a == article ? "当前" : "", [study, a] { study(a); });
    }
    turnList(offset, offset + 6 < articles, articles);
    return;
  }
  if (view == View::Dicts) {
    header("添加与选择词典");
    auto ds = dictionaryFiles(offset, 7);
    if (ds.empty()) {
      canvas.ui(16, 72, "尚未导入词典", 15);
      canvas.ui(16, 112, "TF /dictionaries", 12);
    }
    for (unsigned i = 0; i < ds.size() && i < 6; i++) {
      auto p = ds[i];
      option(48 + i * 47, displayName(p), dict == p, [this, p] {
        dict = p;
        storeSettings();
        back();
      });
    }
    turnList(offset, ds.size() > 6);
    return;
  }
  if (view == View::Vocab) {
    header("我的生词");
    bool more;
    auto es = list(userDirectory() + "vocab", offset, 6, more);
    if (es.empty()) {
      canvas.ui(12, 90, "在英语课文中收藏词义", 16);
    }
    for (unsigned i = 0; i < es.size(); ++i) {
      JsonDocument d;
      readDoc(userDirectory() + "vocab/" + es[i].name + "/item", d);
      Str w = str(d["word"]);
      row(
          i, w,
          [this, w] {
            word = w;
            reveal = false;
            go(View::Recall);
          },
          (d["due"] | int64_t(0)) <= dev.clock() ? "待复习" : "已安排复习");
    }
    turnList(offset, more);
    return;
  }
  if (view == View::Recall) {
    header("看词忆义");
    Str p = userDirectory() + "vocab/" + key(word) + "/item";
    JsonDocument d;
    readDoc(p, d);
    canvas.ui(16, 55, word, 20, 208, true, true);
    unsigned n = 1;
    if (reveal) {
      n = meaningPages(str(d["meaning"]) + "\n" + str(d["source"]), 94);
    } else {
      btn(20, 125, 170, 46, "显示词义", [this] {
        reveal = true;
        dirty = true;
      });
    }
    if (reveal) {
      auto grade = [this, p](bool good) {
        JsonDocument d;
        readDoc(p, d);
        int stage = good ? std::min(3, (d["stage"] | 0) + 1) : 0;
        const int days[] = {0, 1, 3, 7};
        d["stage"] = stage;
        d["due"] = int64_t(dev.clock()) + days[stage] * 86400;
        if (!saveRecord(p, serialize(d))) {
          msg("保存失败");
        } else {
          back();
        }
      };
      if (studyPager(n)) {
        int top = n > 1 ? 337 : 380;
        compact(0, top, "再练", [grade] { grade(false); });
        compact(2, top, "记住了", [grade] { grade(true); }, true);
      }
    }
    return;
  }
  if (view == View::Choice) {
    header("阅读选项");
    if (detailChoice >= lesson.choices.size()) {
      back();
      return;
    }
    unsigned n = paragraph(lesson.choices[detailChoice], 16, 55, 208, 334, textPage, true);
    if (n == 1) {
      compact(0, 380, "取消", [this] { back(); });
    }
    if (studyPager(n)) {
      compact(
          2, n > 1 ? 337 : 380, "选这一项",
          [this] {
            chosen = detailChoice;
            back();
          },
          true);
    }
    return;
  }
  if (view == View::Review) {
    header("错题复习");
    bool more;
    auto es = list(userDirectory() + "errors", offset, 6, more);
    if (es.empty()) {
      canvas.ui(12, 90, "暂时没有错题", 16);
    }
    for (unsigned i = 0; i < es.size(); i++) {
      JsonDocument d;
      readDoc(userDirectory() + "errors/" + es[i].name + "/item", d);
      Str c = str(d["course"]);
      unsigned a = d["article"] | 0u, s = d["segment"] | 0u;
      row(i, str(d["title"]), [this, c, a, s] {
        course = c;
        article = a;
        segment = s;
        answer.clear();
        enterLesson(false);
      });
    }
    turnList(offset, more);
    return;
  }
  if (view == View::Words) {
    header("选择课文中的词");
    // Offsets refer to the bounded lesson record. Only the six visible words
    // need strings. On unusually diverse passages scan the prefix for overflow.
    uint16_t first[512]{};
    const Str& text = lesson.text;
    auto isLetter = [](char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == 39; };
    auto fold = [](char c) { return c >= 'A' && c <= 'Z' ? char(c + ('a' - 'A')) : c; };
    unsigned total = 0;
    for (size_t end = 0; end < text.size();) {
      if (!isLetter(text[end])) {
        ++end;
        continue;
      }
      size_t start = end;
      unsigned hash = 2166136261u;
      while (end < text.size() && isLetter(text[end])) {
        hash = (hash ^ fold(text[end++])) * 16777619u;
      }
      auto matches = [&](size_t other) {
        size_t n = end - start;
        if (other + n > start || (other + n < text.size() && isLetter(text[other + n]))) {
          return false;
        }
        for (size_t k = 0; k < n; ++k) {
          if (fold(text[start + k]) != fold(text[other + k])) {
            return false;
          }
        }
        return true;
      };
      bool seen = false;
      unsigned tries = 0, slot = hash % 512;
      while (tries < 512 && first[slot]) {
        if (matches(first[slot] - 1)) {
          seen = true;
          break;
        }
        slot = (slot + 1) % 512;
        ++tries;
      }
      if (!seen && tries == 512) {
        for (size_t i = 0; i < start && !seen;) {
          if (!isLetter(text[i])) {
            ++i;
            continue;
          }
          seen = matches(i);
          while (i < start && isLetter(text[i])) {
            ++i;
          }
        }
      }
      if (seen) {
        continue;
      }
      if (tries < 512) {
        first[slot] = uint16_t(start + 1);
      }
      if (total >= offset && total - offset < 6) {
        auto s = lower(text.substr(start, end - start));
        row(total - offset, s, [this, s] {
          word = s;
          definition = lookup(dict, s);
          go(View::Definition);
        });
      }
      ++total;
    }
    turnList(offset, offset + 6 < total, total);
    return;
  }
  if (view == View::Definition) {
    header(word);
    auto n = meaningPages(definition, 55);
    if (studyPager(n)) {
      compact(
          2, n > 1 ? 337 : 380, "收藏词义",
          [this] {
            JsonDocument d;
            d["word"] = word;
            d["meaning"] = definition;
            d["source"] = lesson.text;
            d["dict"] = dict;
            if (!saveRecord(userDirectory() + "vocab/" + key(word) + "/item", serialize(d))) {
              msg("保存失败");
            } else {
              back();
            }
          },
          true);
    }
    return;
  }
  if (view == View::Passage || view == View::Chunks) {
    bool chunks = view == View::Chunks;
    header(chunks ? "逐句阅读" : "英语阅读");
    canvas.ui(16, 48, lesson.title, 15, 208, true, true);
    canvas.ui(16, 71,
              "第 " + std::to_string(article + 1) + " 篇 · " + std::to_string(segment + 1) + "/" +
                  std::to_string(segments),
              12, 208);
    unsigned n = paragraph(chunks && chunk < lesson.chunks.size() ? lesson.chunks[chunk] : lesson.text, 16,
                           99, 208, !chunks && !lesson.chunks.empty() ? 290 : 334, textPage, true);
    bool last = studyPager(n);
    int actionY = n > 1 ? 337 : 380;
    if (!chunks) {
      if (!lesson.chunks.empty()) {
        entry(291, "逐句阅读", "", [this] {
          chunk = 0;
          go(View::Chunks);
        });
      }
      compact(0, actionY, "篇目", [this] { go(View::Catalog); });
      compact(1, actionY, "查词", [this] { go(View::Words); });
    } else if (chunk) {
      compact(0, actionY, "上一句", [this] {
        --chunk;
        textPage = 0;
        dirty = true;
      });
    }
    if (last) {
      if (chunks && chunk + 1 < lesson.chunks.size()) {
        compact(
            2, actionY, "下一句",
            [this] {
              ++chunk;
              textPage = 0;
              dirty = true;
            },
            true);
      } else {
        compact(2, actionY, "理解题", [this] { go(View::Quiz); }, true);
      }
    }
    return;
  }
  if (view == View::Quiz) {
    header("理解题");
    bool separate = lesson.choices.size() > 4;
    int top = std::min(179, 337 - 44 * int(lesson.choices.size()));
    unsigned n = paragraph(lesson.prompt, 16, 55, 208, separate ? 334 : top - 9, textPage, true);
    if (separate) {
      ++n;
    }
    if (studyPager(n)) {
      for (unsigned i = 0; i < lesson.choices.size(); ++i) {
        option(top + 44 * i, Str(1, char('A' + i)) + " " + lesson.choices[i], chosen == i, [this, i] {
          if (Canvas::textWidth(lesson.choices[i], 15, true) > 161) {
            detailChoice = i;
            go(View::Choice);
          } else {
            chosen = i;
            dirty = true;
          }
        });
      }
      int actionY = n > 1 ? 337 : 380;
      if (chosen < lesson.choices.size()) {
        compact(2, actionY, "核对答案", [this] { choose(chosen); }, true);
      } else {
        canvas.ui(16, actionY + 10, "请选择一个答案", 12);
      }
    }
    return;
  }
  header(answer == lesson.correct ? "回答正确" : "回答有误");
  Str correct;
  for (size_t i = 0; i < lesson.choices.size(); ++i) {
    if (lesson.choiceIds[i] == lesson.correct) {
      correct = lesson.choices[i];
    }
  }
  Str feedback = "答案 " + correct + "\n" + lesson.evidence + "\n" + lesson.explanation;
  unsigned n = paragraph(feedback, 16, 55, 208, 334, textPage, true);
  if (!studyPager(n)) {
    return;
  }
  int top = n > 1 ? 337 : 380;
  compact(0, top, "重读", [this] { go(View::Passage); });
  compact(
      2, top, "继续",
      [this] {
        if (segment + 1 < segments) {
          ++segment;
          answer.clear();
          enterLesson(false);
        } else {
          go(View::Catalog);
        }
      },
      true);
}
}  // namespace jd
