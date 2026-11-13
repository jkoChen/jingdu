#include "Reader.h"
#include "Learning.h"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <iostream>

using namespace jd;
namespace fs = std::filesystem;

int main(int argc, char** argv) {
  assert(argc == 2);
  root = argv[1];
  bool openedSample = false;
  for (const auto& family : fs::directory_iterator(root + "/fonts")) {
    auto font = family.path().filename().string();
    if (!ends(font, ".ttf")) {
      continue;
    }
    for (const auto& entry : fs::directory_iterator(root + "/books")) {
      auto name = entry.path().filename().string();
      if (!ends(name, ".epub") && !ends(name, ".txt")) {
        continue;
      }
      Reader book;
      assert(book.open("/books/" + name, "/fonts/" + font));
      Canvas canvas;
      unsigned pages = 0;
      do {
        bool painted = false;
        for (int attempt = 0; attempt < 2000; ++attempt) {
          canvas.clear();
          if ((painted = book.paint(canvas))) {
            break;
          }
          assert(book.pending);
        }
        assert(painted && book.font.active() && book.font.path == "/fonts/" + font && ++pages < 2000);
      } while (book.forward());
      openedSample |= font == "霞鹜臻楷.ttf" && name == "四季小记.epub" && pages > 0;
      std::cout << font << " / " << name << " — " << pages << " 页\n";
    }
  }
  assert(openedSample);
  unsigned total = 0;
  for (const auto& item : fs::directory_iterator(root + "/courses/english")) {
    Str path = "/courses/english/" + item.path().filename().string();
    Lesson lesson;
    unsigned articles = 0, passages = 0, count = 0;
    assert(fetchLesson(path, 0, 0, lesson, articles, passages));
    for (unsigned a = 0; a < articles; ++a) {
      assert(fetchLesson(path, a, 0, lesson, articles, passages));
      for (unsigned s = 0; s < passages; ++s) {
        assert(fetchLesson(path, a, s, lesson, articles, passages));
        assert(!lesson.id.empty() && !lesson.text.empty());
        if (!lesson.choices.empty()) {
          assert(std::count(lesson.choiceIds.begin(), lesson.choiceIds.end(), lesson.correct) == 1);
        }
        ++count;
      }
    }
    total += count;
    std::cout << item.path().filename().string() << " — " << articles << " 篇，" << count << " 条\n";
  }
  assert(total > 1460);
}
