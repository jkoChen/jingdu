#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
namespace jd {
using Str = std::string;
struct Entry {
  Str name;
  bool folder;
};
extern Str root;
Str join(const Str& dir, const Str& name);
Str leaf(const Str& path);
Str parent(const Str& path);
bool userPath(const Str& relative, bool allowRoot = false);
bool folder(const Str& path);
bool makedirs(const Str& path);
uint32_t length(const Str& path);
Str lower(Str s);
bool ends(const Str& s, const char* ext);
std::vector<Entry> list(const Str& dir, unsigned skip, unsigned count, bool& more, bool fontsOnly = false,
                        unsigned* total = nullptr);
uint32_t checksum(const void* bytes, size_t size);
class FileIdentity {
  struct Job;
  Job* job = nullptr;
  Str result;

 public:
  ~FileIdentity();
  FileIdentity() = default;
  FileIdentity(const FileIdentity&) = delete;
  void reset();
  bool begin(const Str& path, FileIdentity* prepared = nullptr);
  bool step();
  bool startUpload(uint32_t bytes);
  void append(const void* data, size_t bytes);
  void published(const Str& path);
  bool sampled() const {
    return fromSample;
  }

 private:
  bool fromSample = false;

 public:
  unsigned percent() const;
  const Str& value() const {
    return result;
  }
  bool running() const {
    return job != nullptr;
  }
};
Str fontCacheIdentity(const Str& path, int fd, uint32_t size);
void invalidateFileIdentities();
bool forgetFileIdentity(const Str& path, bool subtree = false);
Str fileStamp(const Str& path);
void acceptFileSample(const Str& path, const Str& hash);
Str digest(const Str& path);
Str key(const Str& text);
FILE* openRecord(const Str& path, size_t limit, bool& present);
bool loadRecord(const Str& path, Str& data, size_t limit = 16384);
bool saveRecord(const Str& path, const Str& data, size_t limit = 16384);
void setCacheBudget(uint64_t capacity, uint64_t freeBytes);
bool spareGlyphRoom(uint32_t growth);
bool cacheRoom(const Str& target, uint32_t growth);
bool durable(FILE* f);
uint32_t nextChar(const char*& p, const char* end);
}  // namespace jd
