#include "Reader.h"
#include "HeapRoom.h"
#include <algorithm>
#include <cstring>
#include <new>
namespace jd {
void Reader::restoreDecoder() {
  if (speculativeChapter < 0) return;
  epub->restoreText(retainedText);
  speculativeChapter = -1;
  retainedText = {};
}
void Reader::saveWindow() {
  if (!window || window->record.empty() || window->done == window->saved || !font.cacheInstance) return;
  auto& w = *window;
  // saveRecord checksums and alternates two bounded copies. The last edge is the next page start.
  uint32_t header[] = {font.cacheInstance, w.done, w.ended};
  Str body(reinterpret_cast<const char*>(header), sizeof header);
  body.append(reinterpret_cast<const char*>(w.edges), (w.done + 1) * sizeof(Anchor));
  if (saveRecord(w.record, body, 1024)) w.saved = w.done;
}
void Reader::checkpoint() {
  saveWindow();
  txtChapters.saveProgress();
}
void Reader::resetWindow() {
  restoreDecoder();
  saveWindow();
  window.reset();
  windowDisabled = false;
}
bool Reader::beginWindow() {
  if (windowDisabled || path.empty() || !font.active() || !font.cacheInstance || font.allWarmed()) return false;
  if (!window) {
    if (!byteHeapRoom(sizeof(GlyphWindow), 14000)) return false;
    window.reset(new (std::nothrow) GlyphWindow);
    if (!window) return false;
    auto& w = *window;
    w.edges[0] = at;
    if (!id.empty()) {
      w.record = root + "/.jingdu/cache/near-" + key(layoutId + ":" + std::to_string(layout.tone));
      Str data;
      uint32_t h[3];
      if (loadRecord(w.record, data, 1024) && data.size() >= sizeof h) {
        memcpy(h, data.data(), sizeof h);
        if (h[0] == font.cacheInstance && h[1] <= 100 && h[2] <= 1 &&
            data.size() == sizeof h + (h[1] + 1) * sizeof(Anchor)) {
          memcpy(w.edges, data.data() + sizeof h, data.size() - sizeof h);
          w.done = w.saved = h[1];
          w.ended = h[2];
        }
      }
    }
  }
  alignWindow();
  return true;
}
void Reader::alignWindow() {
  auto& w = *window;
  unsigned n = 0;
  while (n <= w.done && !same(w.edges[n], at)) ++n;
  if (n == 0 || (n < w.done && (w.ended || w.done - n > 10))) return;
  if (n <= w.done) {
    // A normal turn consumes the prefix. Keep the in-flight tail page intact.
    std::move(w.edges + n, w.edges + w.done + 1, w.edges);
    w.done -= n;
  } else {
    w.dropPage();
    w.edges[0] = at;
    w.done = 0;
    w.ended = false;
  }
  w.saved = 0;
}
void Reader::warmSlice(bool opening) {
  if (pending || (!opening && !pagePrepared())) return;
  if (!font.active() || !font.cacheInstance) return;
  if (!beginWindow()) return;
  if (nearbyDone(opening ? 10 : 100)) {
    if (!opening) font.warmOne();
    return;
  }
  auto& w = *window;
  const Anchor pos = w.edges[w.done];
  if (!w.stream) {
    Str source = textPath;
    if (epub) {
      if (pos.chapter >= epub->count()) { w.ended = true; saveWindow(); return; }
      const int live = aheadChapter >= 0 ? aheadChapter : currentChapter;
      if (int(pos.chapter) == live) {
        if (speculativeChapter >= 0) source = retainedText.file;
        else {
          if (!epub->complete || !epub->fileBacked()) return;
          source = epub->textPath;
        }
      } else {
        if (speculativeChapter != int(pos.chapter)) {
          if (opening || !epub->complete || !epub->fileBacked() || !byteHeapRoom(16384, 16000)) return;
          if (speculativeChapter < 0) retainedText = epub->snapshot();
          // Chapter files share bounded disk slots. Never overwrite the foreground snapshot.
          if (epub->textSlot(pos.chapter) == retainedText.file) {
            restoreDecoder();
            return;
          }
          speculativeChapter = int(pos.chapter);
          if (!epub->chapter(pos.chapter, true) || !epub->fileBacked()) {
            restoreDecoder(); windowDisabled = true; w.dropPage(); return;
          }
        }
        if (!epub->complete) {
          if (!epub->ensure(UINT32_MAX, 1)) { restoreDecoder(); windowDisabled = true; w.dropPage(); }
          return;
        }
        source = epub->textPath;
      }
    }
    if (!w.scratch) {
      if (!byteHeapRoom(sizeof(Page), 12000)) return;
      w.scratch.reset(new (std::nothrow) Page);
      if (!w.scratch) return;
    }
    w.stream.reset(fopen(source.c_str(), "rb"));
    if (!w.stream) { windowDisabled = true; w.dropPage(); return; }
  }
  auto& page = *w.scratch;
  if (!w.laid && !scanPage(w.stream.get(), enc, pos.byte, layout, font, page, w.cursor, 2, w.laid)) {
    windowDisabled = true; w.dropPage(); return;
  }
  if (!w.laid) return;
  const unsigned end = std::min(page.count, w.primed + 4);
  for (; w.primed < end; ++w.primed) {
    const auto& g = page.chars[w.primed];
    if (!(g.x & 0x8000) && !font.cacheForPage(g.cp & 0x1fffff)) {
      windowDisabled = true; w.dropPage(); return;
    }
  }
  if (w.primed < page.count) return;
  if (!font.flushPageGlyphs()) { windowDisabled = true; w.dropPage(); return; }
  Anchor next{pos.chapter, page.end};
  if (page.eof) {
    if (epub && pos.chapter + 1 < epub->count()) next = {pos.chapter + 1, 0};
    else w.ended = true;
  }
  w.edges[++w.done] = next;
  w.stream.reset();
  w.cursor = {};
  w.primed = 0;
  w.laid = false;
  if (w.ended || w.done == 100 || w.done % 10 == 0) saveWindow();
  if (w.ended || w.done == 100) w.dropPage();
}
} // namespace jd
