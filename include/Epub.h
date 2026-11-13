#pragma once
#include "Storage.h"
#include <FreeInkBook.h>
#include <BookCatalog.h>
#include "CatalogFile.h"
#include <epub/XmlSax.h>
#include <memory>
namespace jd {
struct Canvas;
class Epub final : private freeink::book::BookSource, private freeink::book::XmlHandler {
  FILE* zip = nullptr;
  uint64_t zipPosition = 0;
  FILE* output = nullptr;
  std::unique_ptr<uint8_t[]> metaBuf, workBuf;
  freeink::book::Arena meta, work;
  freeink::book::Book book;
  CatalogFile catalogFile;
  freeink::book::BookCatalog catalog;
  freeink::book::ZipEntry chapterEntry;
  Str chapterHref;
  freeink::book::BookStatus openCatalog(const Str&, unsigned entries);

  freeink::book::XmlSaxSession sax;
  Str dir, part, ready, stamp, token, chapterId;
  uint32_t total = 0, written = 0;
  int body = 0, hidden = 0, bold = 0, italic = 0;
  char tail = '\n';
  bool failed = false;
  std::unique_ptr<char[]> window;
  uint32_t windowStart = 0, held = 0;
  unsigned selected = 0;
  FILE* history = nullptr;
  Str historyPath;
  void useHistory(const Str&);
  void releaseHistory();
  FILE* inflateHistory() override {
    return history;
  }
  uint32_t chapterBytes = 0;
  bool acquireWork();
  void releaseWork();
  bool forceWindow = false;
  static constexpr unsigned win = 8192;
  int32_t readAt(uint64_t, void*, uint32_t) override;
  uint64_t size() const override {
    return total;
  }
  void onStartElement(const char*, const char**) override;
  void onEndElement(const char*) override;
  void onText(const char*, int) override;
  void emit(const char*, size_t);
  void newline();
  void richText(const char*, size_t);

 public:
  ~Epub();
  bool open(const Str&, const Str&);
  void verified(const Str& id) {
    token = id;
  }
  bool chapter(unsigned, bool stageForFont = false);
  unsigned decodedPercent() const {
    return chapterBytes ? unsigned(std::min<uint64_t>(99, sax.bytesConsumed() * 100 / chapterBytes)) : 0;
  }
  bool ensure(uint32_t offset, unsigned maxSteps = 4, bool measure = false);
  bool image(unsigned index, Canvas&, int y, int maxWidth);
  // Pair each page read with closeText so an incomplete chapter resumes appending.
  FILE* openText();
  bool closeText(FILE* stream);
  bool fileBacked() const {
    return !window;
  }
  uint32_t base() const {
    return window ? windowStart : 0;
  }
  uint32_t available() const {
    return complete && !window ? length(textPath) : written;
  }
  unsigned count() const {
    return catalog.isOpen() ? catalog.spineCount() : book.spineCount();
  }
  Str chapterProof(unsigned i);
  Str title(unsigned i) const;
  unsigned tocSize() const {
    unsigned n = catalog.isOpen() ? catalog.tocCount() : book.tocCount();
    return n ? n : count();
  }
  Str tocName(unsigned i) const;
  bool target(unsigned i, unsigned& spine, Str& fragment) const;
  Str wantedFragment;
  uint32_t foundAt = UINT32_MAX;
  bool complete = false;
  Str textPath, error;
};
}  // namespace jd
