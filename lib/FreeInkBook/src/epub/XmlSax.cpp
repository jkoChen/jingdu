// FreeInkBook — expat-backed streaming XML parse of ZIP entries.

#include "epub/XmlSax.h"

#include <expat.h>
#ifdef ARDUINO
#include <Arduino.h>
#endif

#include <new>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <cstddef>

#include "text/EntityFilter.h"

namespace freeink {
namespace book {

namespace {

constexpr uint32_t kParseChunk = 2048;

#ifdef JINGDU_SPLIT_INFLATE
// One shared quota for XML readers on the firmware's foreground task.
union XmlBlock { std::max_align_t alignment; size_t bytes; };
size_t xmlLive = 0;
void* xmlResize(void* ptr, size_t bytes) {
  auto* old = ptr ? static_cast<XmlBlock*>(ptr) - 1 : nullptr;
  size_t oldSize = old ? old->bytes : 0;
  if (!bytes) {
    xmlLive -= oldSize;
    std::free(old);
    return nullptr;
  }
  if (bytes > 24 * 1024 || xmlLive - oldSize + bytes > 48 * 1024) return nullptr;
  auto* block = static_cast<XmlBlock*>(std::realloc(old, sizeof(XmlBlock) + bytes));
  if (!block) return nullptr;
  block->bytes = bytes;
  xmlLive = xmlLive - oldSize + bytes;
  return block + 1;
}
void* xmlAlloc(size_t size) { return xmlResize(nullptr, size); }
void xmlFree(void* ptr) { xmlResize(ptr, 0); }
#endif

void XMLCALL startElement(void* userData, const XML_Char* name, const XML_Char** atts) {
  static_cast<XmlHandler*>(userData)->onStartElement(name, atts);
}

void XMLCALL endElement(void* userData, const XML_Char* name) {
  static_cast<XmlHandler*>(userData)->onEndElement(name);
}

void XMLCALL characterData(void* userData, const XML_Char* text, int len) {
  static_cast<XmlHandler*>(userData)->onText(text, len);
}

}  // namespace

BookStatus XmlSaxSession::open(BookSource& source, const ZipEntry& entry, Arena& scratch,
                               XmlHandler& handler, bool filterHtmlEntities) {
  close();
  lex_ = Lex::Text; quote_ = 0;
  handler_ = &handler;
  bytesConsumed_ = 0;
  finished_ = false;

  BookStatus status = reader_.open(source, entry, scratch);
  if (status != BookStatus::Ok) return status;

  buf_ = static_cast<char*>(scratch.alloc(kParseChunk, 1));
  filter_ = nullptr;
  if (filterHtmlEntities) {
    filter_ = static_cast<EntityFilter*>(scratch.alloc(sizeof(EntityFilter), alignof(EntityFilter)));
    if (filter_ != nullptr) {
      filter_ = new (filter_) EntityFilter();
      filter_->reset();
    }
  }
  if (buf_ == nullptr || (filterHtmlEntities && filter_ == nullptr)) {
    return BookStatus::OutOfMemory;
  }

#ifdef JINGDU_SPLIT_INFLATE
  const XML_Memory_Handling_Suite memory{xmlAlloc, xmlResize, xmlFree};
  XML_Parser parser = XML_ParserCreate_MM(nullptr, &memory, nullptr);
#else
  XML_Parser parser = XML_ParserCreate(nullptr);
#endif
  if (parser == nullptr) return BookStatus::OutOfMemory;
  XML_SetUserData(parser, &handler);
  XML_SetElementHandler(parser, startElement, endElement);
  XML_SetCharacterDataHandler(parser, characterData);
  parser_ = parser;
  return BookStatus::Ok;
}

bool XmlSaxSession::parseChunk(const char* text, int n) {
  auto parser = static_cast<XML_Parser>(parser_);
  if (!n) return XML_Parse(parser, "", 0, true) != XML_STATUS_ERROR;
  if (!bytesConsumed_ && (std::memchr(text, 0, std::min(4,n)) ||
      static_cast<unsigned char>(text[0]) == 0xff || static_cast<unsigned char>(text[0]) == 0xfe))
    lex_ = Lex::Passthrough;
  int start = 0;
  for (int i = 0; i < n && lex_ != Lex::Passthrough; ++i) {
    char c = text[i];
    switch (lex_) {
      case Lex::Text:
        if (c == '<') { lex_ = Lex::Prefix; prefix_[0] = '<'; prefixSize_ = 1; }
        break;
      case Lex::Prefix:
        prefix_[prefixSize_++] = c;
        if (prefixSize_ == 2 && c == '?') { lex_ = Lex::Instruction; delimiter_ = 0; break; }
        if (!memcmp(prefix_, "<!--", std::min(4u,prefixSize_)) && prefixSize_ <= 4) {
          if (prefixSize_ == 4) { lex_ = Lex::Comment; delimiter_ = commentRun_ = 0; }
          break;
        }
        if (!memcmp(prefix_, "<![CDATA[", prefixSize_)) {
          if (prefixSize_ == 9) { lex_ = Lex::Cdata; delimiter_ = 0; }
          break;
        }
        // Pass declarations (including DTD internal subsets) intact.
        if (prefix_[1] == '!') { lex_ = Lex::Passthrough; break; }
        lex_ = c == '>' ? Lex::Text : Lex::Tag;
        break;
      case Lex::Tag:
        if (quote_) { if (c == quote_) quote_ = 0; }
        else if (c == 34 || c == 39) quote_ = c;
        else if (c == '>') lex_ = Lex::Text;
        break;
      case Lex::Instruction:
        if (c == '>' && delimiter_) lex_ = Lex::Text;
        delimiter_ = c == '?';
        break;
      case Lex::Cdata:
        if (c == '>' && delimiter_ == 2) lex_ = Lex::Text;
        delimiter_ = c == ']' ? std::min(delimiter_+1, 2u) : 0;
        break;
      case Lex::Comment: {
        if (delimiter_ == 2) {
          // Expat validates the original closing delimiter or reports its error.
          lex_ = c == '>' ? Lex::Text : Lex::Passthrough;
          break;
        }
        delimiter_ = c == '-' ? delimiter_ + 1 : 0;
        ++commentRun_;
        bool boundary = static_cast<unsigned char>(c) < 128 ||
            (i+1 < n && (static_cast<unsigned char>(text[i+1]) & 0xc0) != 0x80);
        if (!delimiter_ && boundary && commentRun_ >= kParseChunk) {
          if (XML_Parse(parser,text+start,i+1-start,false) == XML_STATUS_ERROR ||
              XML_Parse(parser,"--><!--",7,false) == XML_STATUS_ERROR) return false;
          start = i+1;
          commentRun_ = 0;
        }
        break;
      }
      case Lex::Passthrough: break;
    }
  }
  return XML_Parse(parser,text+start,n-start,false) != XML_STATUS_ERROR;
}

BookStatus XmlSaxSession::feedChunk(bool* atEnd) {
  if (atEnd != nullptr) *atEnd = finished_;
  if (parser_ == nullptr || finished_) return BookStatus::Unsupported;

  const int32_t n = filter_ != nullptr ? filter_->read(reader_, buf_, kParseChunk)
                                       : reader_.read(buf_, kParseChunk);
  if (n < 0) return BookStatus::IoError;
  if (!parseChunk(buf_, n)) {
    const auto error = XML_GetErrorCode(static_cast<XML_Parser>(parser_));
#ifdef ARDUINO
    Serial.printf("JD_EPUB_XML error=%u offset=%ld free=%u largest=%u\n", unsigned(error),
                  long(XML_GetCurrentByteIndex(static_cast<XML_Parser>(parser_))),
                  ESP.getFreeHeap(), ESP.getMaxAllocHeap());
#endif
    return error == XML_ERROR_NO_MEMORY ? BookStatus::OutOfMemory : BookStatus::ParseError;
  }
  bytesConsumed_ += static_cast<uint64_t>(n);
  if (n == 0) {
    finished_ = true;
    if (atEnd != nullptr) *atEnd = true;
  }
  return BookStatus::Ok;
}

void XmlSaxSession::close() {
#ifdef JINGDU_SPLIT_INFLATE
  reader_.releaseWindow();
#endif
  if (parser_ != nullptr) {
    XML_ParserFree(static_cast<XML_Parser>(parser_));
    parser_ = nullptr;
  }
  // buf_/filter_ are arena allocations — reclaimed by the arena owner.
  buf_ = nullptr;
  filter_ = nullptr;
  handler_ = nullptr;
  finished_ = false;
}

BookStatus XmlSax::parseEntry(BookSource& source, const ZipEntry& entry, Arena& scratch,
                              XmlHandler& handler, bool filterHtmlEntities) {
  // One-shot = the session pumped to completion; a single code path keeps
  // stepped and blocking parses behaviorally identical.
  const size_t marked = scratch.mark();
  XmlSaxSession session;
  BookStatus status = session.open(source, entry, scratch, handler, filterHtmlEntities);
  if (status == BookStatus::Ok) {
    bool atEnd = false;
    while (!atEnd && !handler.stopParse) {
      status = session.feedChunk(&atEnd);
      if (status != BookStatus::Ok) break;
    }
  }
  session.close();
  scratch.release(marked);
  return status;
}

}  // namespace book
}  // namespace freeink
