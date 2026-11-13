#include "App.h"
#ifdef ARDUINO
#include <Arduino.h>
#endif
#include <algorithm>
#include "ViewData.h"
#include "Bookshelf.h"

namespace jd {
void App::position(bool force) {
  if (reader.path.empty() || waitingResume || versionChoice || visiblePath != reader.path ||
      (!force && !pageAck && !uncommitted) || !dev.card) {
    return;
  }
  if (!force && committed && dev.uptime() - lastCommit < 15000) {
    return;
  }
  const bool draft = reader.id.empty() || resolving;
  if (draft && draftToken.empty()) {
    return;
  }
  JsonDocument doc;
  doc["chapter"] = visible.chapter;
  doc["byte"] = visible.byte;
  doc["percent"] = reader.progress();
  doc["book"] = reader.path;
  doc["proof"] = reader.locationProof(visible);
  Str base = userDirectory() + "draft/" + key(reader.path);
  if (draft) {
    doc["stamp"] = draftToken;
    doc["previous"] = resumeId;
  }
  if (!saveRecord(draft ? base : userDirectory() + "positions/" + reader.id, serialize(doc))) {
    return;
  }
  if (!draft) {
    if (!committed || !draftToken.empty()) {
      resolveShelf(reader.path, reader.id);
    }
    if (!saveRecord(userDirectory() + "locator/" + key(reader.path), reader.id)) {
      return;
    }
    if (!draftToken.empty() && mergeDraftMarks()) {
      if (saveRecord(base, "")) {
        draftToken.clear();
      }
    }
  }
  committed = true;
  lastCommit = dev.uptime();
  uncommitted = false;
  if (lastBook != reader.path && saveRecent(reader.path)) {
    lastBook = reader.path;
  }
}
Str App::markPath() const {
  return reader.id.empty() || resolving || versionChoice ? userDirectory() + "draftmarks/" + draftToken
                                                         : userDirectory() + "marks/" + reader.id;
}
bool App::mergeDraftMarks() {
  Str source = userDirectory() + "draftmarks/" + draftToken;
  JsonDocument pending;
  if (!readMarks(source, pending)) {
    return false;
  }
  if (!pending.is<JsonArray>() || pending.size() == 0) {
    return true;
  }
  Str target = userDirectory() + "marks/" + reader.id;
  JsonDocument saved;
  if (!readMarks(target, saved)) {
    return false;
  }
  auto list = saved.is<JsonArray>() ? saved.as<JsonArray>() : saved.to<JsonArray>();
  for (auto mark : pending.as<JsonArrayConst>()) {
    bool exists = false;
    for (auto old : list) {
      if (same(anchor(old), anchor(mark))) {
        exists = true;
        break;
      }
    }
    if (!exists) {
      list.add(mark);
    }
  }
  invalidateMarks();
  return !saved.overflowed() && saveRecord(target, serialize(saved)) && saveRecord(source, "");
}
void App::displayed() {
  if (preparingFont() && prepPainted) {
    prepReady = true;
    prepPainted = false;
    lastFontFrame = dev.uptime();
#ifdef ARDUINO
    Serial.printf("JINGDU_FONT_FRAME stage=%u percent=%u time=%u\n", unsigned(fontStage), prepPercent,
                  dev.uptime());
#endif
  }
  if (pageAck) {
    visible = reader.at;
    visiblePath = reader.path;
    uncommitted = true;
    position();
    pageAck = false;
  }
}
void App::openBook(const Str& path, bool allowBuild) {
#ifdef ARDUINO
  const uint32_t start = millis();
#endif
  invalidateMarks();
  if (!dev.card) {
    msg("请插入存有本书的 TF 卡", Recovery::Card);
    return;
  }
  if (checkingShelf != path) {
    shelfCheck.reset();
  }
  bool needsCatalog = false;
  if (!reader.open(path, fontPath, {}, &shelfCheck, allowBuild ? nullptr : &needsCatalog)) {
    if (needsCatalog) {
      indexingBook = path;
      go(View::FontPrep);
      setStage(FontStage::Catalog);
      prepPainted = prepReady = false;
      return;
    }
    shelfCheck.reset();
    checkingShelf.clear();
    msg(reader.error.empty() ? "书籍无法打开" : reader.error,
        ends(path, ".epub") ? Recovery::Shelf : Recovery::Back);
    return;
  }
  shelfCheck.reset();
  checkingShelf.clear();
#ifdef ARDUINO
  const uint32_t opened = millis();
#endif
  visiblePath.clear();
  draftToken.clear();
  resumeId.clear();
  uncommitted = committed = versionChoice = false;
  JsonDocument saved, draft;
  Str previous;
  loadRecord(userDirectory() + "locator/" + key(path), previous);
  if (!previous.empty()) {
    readDoc(userDirectory() + "positions/" + previous, saved);
  }
  readDoc(userDirectory() + "draft/" + key(path), draft);
  // A sampled hit still has to match the saved place in this particular book.
  if (reader.sampledIdentity() && reader.id == previous && !saved.isNull()) {
    auto proof = str(saved["proof"]);
    if (proof.empty() || reader.locationProof(anchor(saved.as<JsonVariantConst>())) != proof) {
      if (!forgetFileIdentity(root + path) || !reader.open(path, fontPath)) {
        invalidateMarks();
        reader.close();
        msg("文件校验失败");
        return;
      }
    } else {
      acceptFileSample(root + path, reader.id);
    }
  }
  if (!draft.isNull() || reader.validating()) {
    draftToken = fileStamp(root + path);
  }
  bool resumeDraft = !draftToken.empty() && draftToken == str(draft["stamp"]) &&
                     !str(draft["proof"]).empty() &&
                     reader.locationProof(anchor(draft.as<JsonVariantConst>())) == str(draft["proof"]);
  waitingResume = false;
  resumePercent = 0;
  if (resumeDraft) {
    reader.jump(anchor(draft.as<JsonVariantConst>()));
    resumeId = str(draft["previous"]);
  } else if (!reader.id.empty() && !previous.empty() && reader.id != previous &&
             !str(saved["proof"]).empty() &&
             reader.locationProof(anchor(saved.as<JsonVariantConst>())) == str(saved["proof"])) {
    reader.jump(anchor(saved.as<JsonVariantConst>()));
    resumeId = previous;
  } else if (!reader.id.empty()) {
    JsonDocument current;
    readDoc(userDirectory() + "positions/" + reader.id, current);
    reader.jump(anchor(current.as<JsonVariantConst>()));
  } else if (!previous.empty()) {
    auto target = anchor(saved.as<JsonVariantConst>());
    if (!str(saved["proof"]).empty() && str(saved["proof"]) == reader.locationProof(target)) {
      reader.jump(target);
      resumeId = previous;
    } else {
      waitingResume = true;
    }
  }
  resolving = reader.validating() || resumeDraft || !resumeId.empty();
  hasBefore = false;
  reader.startChapters();
  if (reader.chaptersPending() || reader.chapterError()) {
    go(View::FontPrep);
    setStage(reader.validating() ? FontStage::BookIdentity : FontStage::Chapters);
    prepPercent = reader.chapterScanPercent();
    prepPainted = prepReady = false;
  } else {
    enterPreparedBook();
  }
#ifdef ARDUINO
  Serial.printf("JD_OPEN_PART reader_ms=%u restore_ms=%u\n", opened - start, millis() - opened);
#endif
}
void App::enterPreparedBook() {
  onBookOpen = true;
  go(View::Book);
  if (resolving && !reader.validating()) {
    identityReady();
  }
}
void App::identityReady() {
  resolving = false;
  if (reader.id.empty()) {
    abortReading("文件校验失败");
    return;
  }
  if (waitingResume) {
    JsonDocument saved;
    readDoc(userDirectory() + "positions/" + reader.id, saved);
    reader.jump(anchor(saved.as<JsonVariantConst>()));
    waitingResume = false;
    if (view == View::Book) {
      dirty = true;
    }
  }
  if (!resumeId.empty() && resumeId != reader.id) {
    resumeId.clear();
    versionChoice = true;
    msg("文件已更新，选择阅读位置", Recovery::Changed);
  }
  committed = false;
  uncommitted = true;
}
void App::addBook() {
  FILE* file = fopen((root + selected).c_str(), "rb");
  if (!file || fclose(file) || !saveRecord(pendingShelf(selected) + "/ref", selected)) {
    msg("加入书架失败");
    return;
  }
  fileAdded = true;
  if (lastBook == selected) {
    saveRecent(selected);
  }
  checkingShelf.clear();
  dirty = true;
}
void App::invalidateMarks() {
  markIndexPath.clear();
  markAnchors.reset();
  markCount = 0;
}
bool App::marked() {
  if (reader.id.empty() && draftToken.empty()) {
    return false;
  }
  const Str path = markPath();
  if (markIndexPath == path) {
    for (unsigned i = 0; i < markCount; ++i) {
      if (reader.covers(markAnchors[i])) {
        return true;
      }
    }
    return false;
  }
  invalidateMarks();
  bool present;
  FILE* f = openRecord(path, 16384, present);
  if (!f) {
    if (!present) {
      markIndexPath = path;
    }
    return false;
  }
  auto next = [&]() {
    int c;
    do {
      c = fgetc(f);
    } while (c == ' ' || c == '\n' || c == '\t' || c == '\r');
    return c;
  };
  int c = next();
  bool ok = c == '[' || (c == EOF && !ferror(f));
  const bool tombstone = c == EOF && !ferror(f);
  if (c == '[') {
    c = next();
  }
  constexpr unsigned capacity = 128;
  bool fits = true, found = false;
  JsonDocument filter;
  filter["chapter"] = true;
  filter["byte"] = true;
  RecordReader input{f};
  while (ok && c == '{') {
    ungetc(c, f);
    JsonDocument item;
    if (deserializeJson(item, input, DeserializationOption::Filter(filter))) {
      ok = false;
      break;
    }
    Anchor at = anchor(item);
    found |= reader.covers(at);
    if (!markCount && fits) {
      markAnchors.reset(new (std::nothrow) Anchor[capacity]);
    }
    if (markAnchors && markCount < capacity) {
      markAnchors[markCount++] = at;
    } else {
      fits = false;
    }
    c = next();
    if (c == ']') {
      break;
    }
    if (c != ',' || (c = next()) != '{') {
      ok = false;
    }
  }
  ok = ok && (tombstone || (c == ']' && next() == EOF)) && !ferror(f);
  fclose(f);
  if (ok && fits) {
    markIndexPath = path;
  } else {
    invalidateMarks();
  }
  return ok && found;
}
void App::noteMark(bool add) {
  if (reader.id.empty() && draftToken.empty()) {
    msg("书籍校验中，请稍后添加");
    return;
  }
  Str p = markPath();
  JsonDocument d;
  if (!readMarks(p, d)) {
    msg("书签读取失败");
    return;
  }
  auto ar = d.is<JsonArray>() ? d.as<JsonArray>() : d.to<JsonArray>();
  if (add) {
    for (auto v : ar) {
      if (reader.covers(anchor(v))) {
        return;
      }
    }
    auto v = ar.add<JsonObject>();
    v["chapter"] = reader.at.chapter;
    v["byte"] = reader.at.byte;
    v["percent"] = reader.progress();
    v["name"] = reader.currentTitle();
    v["text"] = reader.excerpt;
  } else {
    // Layout changes can place multiple saved anchors on this page.
    for (size_t i = ar.size(); i > 0; --i) {
      if (reader.covers(anchor(ar[i - 1]))) {
        ar.remove(i - 1);
      }
    }
  }
  if (d.overflowed() || !saveRecord(p, serialize(d))) {
    msg("书签保存失败");
    return;
  }
  invalidateMarks();
  view = View::Book;
  stack.clear();
  dirty = true;
}
}  // namespace jd
