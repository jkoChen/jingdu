#pragma once
#include "Epub.h"
#include "Text.h"
#include "Chapters.h"
namespace jd {
struct Anchor {
  uint32_t chapter = 0, byte = 0;
};
inline bool same(Anchor a, Anchor b) {
  return a.chapter == b.chapter && a.byte == b.byte;
}
class Reader {
  std::unique_ptr<Epub> epub;
  Chapters txtChapters;
  FileIdentity verification;
  Str textPath, layoutId, indexPath, cacheBasis, suspendedFont;
  void identifyLayout();
  void closeBook();
  bool layoutReady = false;
  Cursor warmCursor;
  EncodingInfo enc{Encoding::Utf8, 0};
  int currentChapter = -1;
  Anchor last[16]{};
  unsigned history = 0, historyEnd = 0;
  bool seekChapterEnd = false;
  int requestedFraction = -1;
  uint32_t seekByte = UINT32_MAX, chapterLength = 0;
  bool skipTowardsEnd();
  uint32_t preAt = 0;
  std::unique_ptr<Canvas> nextFrame;
  unsigned drawn = 0;
  bool scanned = false, started = false, prefetchCompleteInk = true;
  Str nextExcerpt;
  bool drawChars(Canvas&, unsigned begin, unsigned end, Str&);
  void cancelAhead();
  Cursor preCursor;
  bool canPrefetch = false;
  struct CloseStream {
    void operator()(FILE* f) const {
      fclose(f);
    }
  };
  std::unique_ptr<FILE, CloseStream> preText;
  bool eof = false;
  uint32_t nextByte = 0;
  Page page{};
  bool prepare(uint32_t need);
  bool awaitingText(uint32_t need) const;
  void saveIndex(uint32_t a, uint32_t b);
  bool cached(Canvas&, bool write);
  uint32_t align(uint32_t pos);

 public:
  BodyFont font;
  Layout layout;
  Anchor at;
  Str path, id, error, excerpt;
  bool open(const Str& relative, const Str& typeface, Anchor a = {}, FileIdentity* prepared = nullptr);
  void commitIdentity();
  bool sampledIdentity() const {
    return verification.sampled();
  }
  Str locationProof(Anchor location);
  void showProgress(bool visible);
  bool covers(Anchor a) const {
    return a.chapter == at.chapter && (a.byte == at.byte || (a.byte > at.byte && a.byte < nextByte));
  }
  void close();
  bool selectFont(const Str& fontPath);
  void jump(Anchor a);
  bool forward();
  bool backward();
  void percent(unsigned pct);
  unsigned chapterProgress() const;
  unsigned progress() const;
  bool paint(Canvas& c);
  bool restoreCachedBody(Canvas& canvas);
  bool pending = false;
  int chapterPreparation() const;
  int preparePage(unsigned& percent);
  void background(bool idle = false);
  void prefetch(unsigned quota = 8);
  bool pagePrepared() const {
    return !pending && !validating() && (!canPrefetch || (scanned && drawn == page.count));
  }
  unsigned verificationPercent() const {
    return verification.percent();
  }
  bool validating() const {
    return verification.running();
  }
  void scanChapters();
  bool chaptersPending() const {
    return !epub && txtChapters.pending();
  }
  unsigned chapters() const {
    return epub ? epub->tocSize() : txtChapters.size();
  }
  void jumpChapter(unsigned);
  Str currentTitle() const {
    return epub ? epub->title(at.chapter) : txtChapters.active(at.byte);
  }
  Str chapterName(unsigned c) const {
    return epub ? epub->tocName(c) : txtChapters.title(c);
  }
};
}  // namespace jd
