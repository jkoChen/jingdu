#pragma once
#include "Storage.h"
#include <ArduinoJson.h>
namespace jd {
struct Lesson {
  Str id, title, text, prompt, correct, evidence, explanation;
  std::vector<Str> choices, choiceIds, chunks;
};
bool fetchLesson(const Str& relative, unsigned article, unsigned segment, Lesson& out, unsigned& articles,
                 unsigned& segments, bool titleOnly = false);
std::vector<Str> dictionaryFiles(unsigned skip, unsigned count);
Str lookup(const Str& ifo, const Str& word);
}  // namespace jd
