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
  struct GlyphWindow {
    Anchor edges[101]{};
    unsigned done = 0, saved = 0;
    bool ended = false;
    Str record;
    std::unique_ptr<Page> scratch;
    Cursor cursor;
    std::unique_ptr<FILE, decltype(&fclose)> stream{nullptr, fclose};
    unsigned primed = 0;
    bool laid = false;
    void dropPage() { stream.reset(); scratch.reset(); cursor = {}; primed = 0; laid = false; }
  };
  std::unique_ptr<GlyphWindow> window;
  bool windowDisabled = false;
  int speculativeChapter = -1;
  Epub::TextSnapshot retainedText;
  void restoreDecoder();
  void resetWindow();
  void saveWindow();
  void alignWindow();
  bool canPrefetch = false;
  struct CloseStream {
    void operator()(FILE* f) const {
      fclose(f);
    }
  };
  std::unique_ptr<FILE, CloseStream> preText;
  bool initialFont = false;
  int selectedToc = -1, selectedSpine = -1;
  int aheadChapter = -1;
  bool aheadFailed = false;
  void stageNextChapter();
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
  bool open(const Str& relative, const Str& typeface, Anchor a = {}, FileIdentity* prepared = nullptr,
            bool* needsCatalog = nullptr);
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
  // USB measurement only: prime one TXT page without changing the visible
  // anchor, page history, or page-image cache. Returns the next byte offset.
  bool primeTextGlyphs(uint32_t offset, uint32_t& next);
  void startChapters();
  void scanChapters();
  const char* chapterError() const { return txtChapters.error(); }
  unsigned chapterScanPercent() const { return txtChapters.percent(); }
  bool beginWindow();
  void warmSlice(bool opening = false);
  unsigned nearbyPages() const { return window ? window->done : 0; }
  bool nearbyDone(unsigned n) const { return !window || windowDisabled || window->ended || window->done >= n; }
  void checkpoint();
  bool chaptersPending() const {
    return !epub && txtChapters.pending();
  }
  unsigned chapters() const {
    return epub ? epub->tocSize() : txtChapters.size();
  }
  void jumpChapter(unsigned);
  int currentToc() const {
    return epub ? (selectedSpine == int(at.chapter) ? selectedToc : epub->tocForSpine(at.chapter))
                : txtChapters.activeIndex(at.byte);
  }
  Str currentTitle() const {
    return epub ? epub->title(at.chapter) : txtChapters.active(at.byte);
  }
  Str chapterName(unsigned c) const {
    return epub ? epub->tocName(c) : txtChapters.title(c);
  }
};
}  // namespace jd
