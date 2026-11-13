#include "Storage.h"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <new>
#ifdef ESP_PLATFORM
#include <mbedtls/sha256.h>
#elif defined(__APPLE__)
#include <CommonCrypto/CommonDigest.h>
#else
#include <openssl/sha.h>
#define CC_SHA256_CTX SHA256_CTX
#define CC_SHA256_Init SHA256_Init
#define CC_SHA256_Update SHA256_Update
#define CC_SHA256_Final SHA256_Final
#endif
namespace jd {
Str root = "/sd";
Str join(const Str& dir, const Str& name) {
  return dir + (dir.empty() || dir.back() == '/' ? "" : "/") + name;
}
Str leaf(const Str& s) {
  return s.substr(s.find_last_of('/') + 1);
}
Str parent(const Str& s) {
  auto n = s.rfind('/');
  return n == Str::npos ? "" : s.substr(0, n);
}
Str lower(Str s) {
  for (auto& c : s) {
    if (c >= 'A' && c <= 'Z') {
      c += 32;
    }
  }
  return s;
}
bool ends(const Str& s, const char* ext) {
  auto n = strlen(ext);
  return s.size() >= n && lower(s.substr(s.size() - n)) == ext;
}
bool userPath(const Str& p, bool allowRoot) {
  if (p == "/") {
    return allowRoot;
  }
  if (p.empty() || p.size() > 220 || p[0] != '/' || p.back() == '/') {
    return false;
  }
  size_t start = 1;
  while (start < p.size()) {
    auto slash = p.find('/', start);
    auto part = p.substr(start, slash - start);
    if (part.empty() || part.front() == '.' || part.back() == '.' || part.back() == ' ' ||
        part.size() > 140) {
      return false;
    }
    auto tilde = part.find('~');
    if (tilde != Str::npos && tilde + 1 < part.size() && part[tilde + 1] >= '0' && part[tilde + 1] <= '9') {
      return false;
    }
    for (unsigned char c : part) {
      if (c < 32 || c == 127 || strchr("\\:*?\"<>|", c)) {
        return false;
      }
    }
    if (slash == Str::npos) {
      break;
    }
    start = slash + 1;
  }
  return true;
}
bool folder(const Str& p) {
  struct stat s{};
  return !stat(p.c_str(), &s) && S_ISDIR(s.st_mode);
}
bool makedirs(const Str& p) {
  if (p.empty() || folder(p)) {
    return true;
  }
  if (!makedirs(parent(p))) {
    return false;
  }
  return !mkdir(p.c_str(), 0755) || folder(p);
}
uint32_t length(const Str& p) {
  struct stat s{};
  return !stat(p.c_str(), &s) && S_ISREG(s.st_mode) && s.st_size >= 0 && uint64_t(s.st_size) <= UINT32_MAX
             ? uint32_t(s.st_size)
             : 0;
}
std::vector<Entry> list(const Str& path, unsigned skip, unsigned count, bool& more, bool fontsOnly,
                        unsigned* total) {
  if (total) {
    *total = 0;
  }
  more = false;
  std::vector<Entry> result;
  DIR* d = opendir(path.c_str());
  if (!d) {
    return result;
  }
  while (auto e = readdir(d)) {
    if (e->d_name[0] == '.') {
      continue;
    }
    bool isDir = fontsOnly && folder(join(path, e->d_name));
    if (fontsOnly && !isDir && !ends(e->d_name, ".ttf") && !ends(e->d_name, ".otf")) {
      continue;
    }
    if (total) {
      ++*total;
    }
    if (skip) {
      --skip;
      continue;
    }
    if (result.size() >= count) {
      more = true;
      if (total) {
        continue;
      }
      break;
    }
    result.push_back({e->d_name, fontsOnly ? isDir : folder(join(path, e->d_name))});
  }
  closedir(d);
  return result;
}
static uint32_t updateChecksum(uint32_t c, const void* v, size_t n) {
  auto p = static_cast<const uint8_t*>(v);
  while (n--) {
    c ^= *p++;
    for (int b = 0; b < 8; ++b) {
      c = (c >> 1) ^ ((0u - (c & 1u)) & 0xedb88320u);
    }
  }
  return c;
}
uint32_t checksum(const void* v, size_t n) {
  return ~updateChecksum(~0u, v, n);
}
class Hash {
#ifdef ESP_PLATFORM
  mbedtls_sha256_context ctx;
#else
  CC_SHA256_CTX ctx;
#endif
 public:
  Hash() {
#ifdef ESP_PLATFORM
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
#else
    CC_SHA256_Init(&ctx);
#endif
  }
  ~Hash() {
#ifdef ESP_PLATFORM
    mbedtls_sha256_free(&ctx);
#endif
  }
  void add(const void* p, size_t n) {
#ifdef ESP_PLATFORM
    mbedtls_sha256_update(&ctx, static_cast<const uint8_t*>(p), n);
#else
    CC_SHA256_Update(&ctx, p, n);
#endif
  }
  Str end() {
    uint8_t out[32];
#ifdef ESP_PLATFORM
    mbedtls_sha256_finish(&ctx, out);
#else
    CC_SHA256_Final(out, &ctx);
#endif
    static const char h[] = "0123456789abcdef";
    Str r;
    for (auto c : out) {
      r += h[c >> 4];
      r += h[c & 15];
    }
    return r;
  }
};
// The slot bounds the on-card index. A collision costs one new scan; the
// stored relative path is checked before its digest can ever be reused.
constexpr unsigned sampleBlock = 4096;
struct Slice {
  uint32_t at, bytes;
};
static unsigned samples(uint32_t size, Slice (&ranges)[3]) {
  if (size <= 3 * sampleBlock) {
    ranges[0] = {0, size};
    return 1;
  }
  ranges[0] = {0, sampleBlock};
  ranges[1] = {(size - sampleBlock) / 2, sampleBlock};
  ranges[2] = {size - sampleBlock, sampleBlock};
  return 3;
}
static Str sampleFile(int fd, uint32_t size) {
  Hash hash;
  Slice ranges[3];
  unsigned count = samples(size, ranges);
  uint8_t buf[sampleBlock];
  for (unsigned i = 0; i < count; ++i) {
    if (lseek(fd, ranges[i].at, SEEK_SET) < 0) {
      return {};
    }
    unsigned left = ranges[i].bytes;
    while (left) {
      ssize_t n = ::read(fd, buf, std::min<size_t>(left, sizeof buf));
      if (n < 0 && errno == EINTR) {
        continue;
      }
      if (n <= 0) {
        return {};
      }
      hash.add(buf, n);
      left -= n;
    }
  }
  return hash.end();
}
struct Known {
  Str path, hash;
  uint32_t size = 0;
  time_t mtime = 0;
  bool sampled = false;
};
static Known known[6];
static unsigned nextKnown = 0, identityGeneration = 0;
static const Known* liveIdentity(const Str& path, const struct stat& st) {
  if (st.st_mtime <= 315619200) {
    return nullptr;
  }
  for (const auto& item : known) {
    if (item.path == path && item.size == st.st_size && item.mtime == st.st_mtime) {
      return &item;
    }
  }
  return nullptr;
}
void acceptFileSample(const Str& path, const Str& hash) {
  for (auto& k : known) {
    if (k.path == path && k.hash == hash) {
      k.sampled = false;
    }
  }
}
void invalidateFileIdentities() {
  ++identityGeneration;
  for (auto& k : known) {
    k = {};
  }
}
static Str identityDir() {
  return root + "/.jingdu/cache/identity-v1";
}
static Str relativeKey(const Str& path) {
  return lower(path.substr(root.size()));
}
static Str indexSlot(const Str& relative) {
  return identityDir() + "/" + key(relative).substr(0, 2);
}
static Str recordPrefix(const Str& relative, const struct stat& st) {
  return relative + "\n" + std::to_string(st.st_size) + " " + std::to_string(st.st_mtime) + "\n";
}
static bool hexDigest(const Str& s) {
  return s.size() == 64 && std::all_of(s.begin(), s.end(), [](char c) {
           return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
         });
}
static bool under(const Str& p, const Str& target, bool subtree) {
  return p == target || (subtree && p.compare(0, target.size() + 1, target + "/") == 0);
}
static bool eraseRecord(const Str& slot) {
  bool ok = true;
  for (auto ext : {".0", ".1"}) {
    if (unlink((slot + ext).c_str()) && errno != ENOENT) {
      ok = false;
    }
  }
  return ok;
}
bool forgetFileIdentity(const Str& path, bool subtree) {
  ++identityGeneration;
  for (auto& k : known) {
    if (under(lower(k.path), lower(path), subtree)) {
      k = {};
    }
  }
  auto relative = relativeKey(path);
  if (!subtree) {
    Str slot = indexSlot(relative), data;
    if (loadRecord(slot, data, 512) && data.compare(0, relative.size() + 1, relative + "\n") != 0) {
      return true;
    }
    return eraseRecord(slot);
  }
  DIR* dir = opendir(identityDir().c_str());
  if (!dir) {
    return errno == ENOENT;
  }
  bool ok = true;
  while (auto entry = readdir(dir)) {
    Str name = entry->d_name;
    if (name.size() != 4 || name[2] != '.' || (name[3] != '0' && name[3] != '1')) {
      continue;
    }
    Str slot = identityDir() + "/" + name.substr(0, 2), data;
    if (loadRecord(slot, data, 512) && under(data.substr(0, data.find('\n')), relative, true)) {
      ok = eraseRecord(slot) && ok;
    }
  }
  closedir(dir);
  return ok;
}
static void traceIdentity(const char* source, const Str& path, uint32_t bytes) {
#ifdef ESP_PLATFORM
  printf("JD_IDENTITY source=%s bytes=%u path=%s\n", source, unsigned(bytes), path.c_str());
#endif
}
static void remember(const Str& path, const struct stat& st, const Str& hash, bool sampled) {
  for (auto& k : known) {
    if (k.path == path) {
      k = {};
    }
  }
  known[nextKnown++ % 6] = {path, hash, uint32_t(st.st_size), st.st_mtime, sampled};
}
// Font rendering only needs a stable cache key. A normal SFNT directory is in
// the first sample. Include any overflow, or face zero's directory in a TTC.
static Str fontDirectory(int fd, uint32_t size, uint32_t& extra) {
  uint8_t buf[512];
  auto readAt = [&](uint32_t at, size_t count) {
    if (at > size || count > size - at || lseek(fd, at, SEEK_SET) < 0) {
      return false;
    }
    size_t used = 0;
    while (used < count) {
      ssize_t n = ::read(fd, buf + used, count - used);
      if (n < 0 && errno == EINTR) {
        continue;
      }
      if (n <= 0) {
        return false;
      }
      used += size_t(n);
    }
    extra += count;
    return true;
  };
  if (!readAt(0, 16)) {
    return {};
  }
  uint32_t at = 0;
  if (memcmp(buf, "ttcf", 4) == 0) {
    at = uint32_t(buf[12]) << 24 | uint32_t(buf[13]) << 16 | uint32_t(buf[14]) << 8 | buf[15];
    if (!readAt(at, 12)) {
      return {};
    }
  }
  uint32_t directoryBytes = 12u + 16u * ((unsigned(buf[4]) << 8) | buf[5]);
  if (directoryBytes > size - at) {
    return {};
  }
  uint32_t end = at + directoryBytes;
  Hash hash;
  for (uint32_t offset = std::max<uint32_t>(at, sampleBlock); offset < end;) {
    uint32_t count = std::min<uint32_t>(sizeof buf, end - offset);
    if (!readAt(offset, count)) {
      return {};
    }
    hash.add(buf, count);
    offset += count;
  }
  return hash.end();
}
Str fontCacheIdentity(const Str& path, int fd, uint32_t size) {
  struct stat st{};
  if (stat(path.c_str(), &st) || st.st_size != size) {
    return {};
  }
  uint32_t extra = 0;
  Str directory = fontDirectory(fd, size, extra);
  Str sample = sampleFile(fd, size);
  if (sample.empty() || directory.empty()) {
    return {};
  }
  Str relative = relativeKey(path), prefix = recordPrefix(relative, st), legacy;
  traceIdentity("font-probe", path, std::min<uint32_t>(size, 3 * sampleBlock) + extra);
  // Reuse an existing whole-file identity only if both its metadata and its
  // sample match. Never write a sampled font key into the book digest index.
  if (st.st_mtime > 315619200 && loadRecord(indexSlot(relative), legacy, 512) &&
      legacy.size() == prefix.size() + 128 && legacy.compare(0, prefix.size(), prefix) == 0 &&
      legacy.compare(prefix.size() + 64, 64, sample) == 0 && hexDigest(legacy.substr(prefix.size(), 64)) &&
      extra == 16) {
    return legacy.substr(prefix.size(), 64);
  }
  return key("font-sample-v1\n" + prefix + sample + directory);
}
Str fileStamp(const Str& path) {
  struct stat st{};
  if (stat(path.c_str(), &st) || st.st_size < 0 || st.st_size > INT32_MAX) {
    return {};
  }
  int fd = ::open(path.c_str(), O_RDONLY);
  if (fd < 0) {
    return {};
  }
  Str sample = sampleFile(fd, st.st_size);
  ::close(fd);
  return sample.empty() ? Str() : key(recordPrefix(relativeKey(path), st) + sample);
}
struct FileIdentity::Job {
  int fd = -1;
  unsigned generation = 0;
  uint32_t done = 0;
  Str path;
  uint32_t size = 0;
  time_t mtime = 0;
  Hash hash, sample;
  ~Job() {
    if (fd >= 0) {
      ::close(fd);
    }
  }
};
FileIdentity::~FileIdentity() {
  reset();
}
void FileIdentity::reset() {
  delete job;
  job = nullptr;
  result.clear();
  fromSample = false;
}
bool FileIdentity::begin(const Str& path, FileIdentity* prepared) {
  reset();
  struct stat st{};
  if (stat(path.c_str(), &st) || !S_ISREG(st.st_mode) || st.st_size > INT32_MAX) {
    return false;
  }
  if (const auto* cached = liveIdentity(path, st)) {
    result = cached->hash;
    fromSample = cached->sampled;
    traceIdentity("ram", path, 0);
    return true;
  }
  if (prepared && prepared->job) {
    auto* other = prepared->job;
    if (other->path == path && other->size == st.st_size && other->mtime == st.st_mtime &&
        other->generation == identityGeneration) {
      job = other;
      prepared->job = nullptr;
      return true;
    }
  }
  job = new (std::nothrow) Job;
  if (!job) {
    return false;
  }
  job->path = path;
  job->size = st.st_size;
  job->mtime = st.st_mtime;
  job->generation = identityGeneration;
  job->fd = ::open(path.c_str(), O_RDONLY);
  if (job->fd < 0) {
    reset();
    return false;
  }
  auto relative = relativeKey(path);
  Str record, prefix = recordPrefix(relative, st);
  if (st.st_mtime > 315619200 && loadRecord(indexSlot(relative), record, 512) &&
      record.size() == prefix.size() + 128 && record.compare(0, prefix.size(), prefix) == 0) {
    Str saved = record.substr(prefix.size(), 64);
    if (hexDigest(saved) && sampleFile(job->fd, job->size) == record.substr(prefix.size() + 64)) {
      result = saved;
      fromSample = true;
      remember(path, st, result, true);
      traceIdentity("sample", path, std::min<uint32_t>(st.st_size, 3 * sampleBlock));
      delete job;
      job = nullptr;
      return true;
    }
    if (lseek(job->fd, 0, SEEK_SET) < 0) {
      reset();
      return false;
    }
  }
  return true;
}
unsigned FileIdentity::percent() const {
  if (!job) {
    return result.empty() ? 0 : 100;
  }
  return job->size ? std::min(99u, unsigned(uint64_t(job->done) * 100 / job->size)) : 0;
}
bool FileIdentity::startUpload(uint32_t bytes) {
  reset();
  job = new (std::nothrow) Job;
  if (!job) {
    return false;
  }
  job->size = bytes;
  job->generation = identityGeneration;
  return true;
}
void FileIdentity::append(const void* bytes, size_t count) {
  if (!job) {
    return;
  }
  if (count > job->size - job->done) {
    reset();
    return;
  }
  const auto* data = static_cast<const uint8_t*>(bytes);
  job->hash.add(data, count);
  Slice ranges[3];
  unsigned n = samples(job->size, ranges);
  for (unsigned i = 0; i < n; ++i) {
    auto first = std::max(job->done, ranges[i].at);
    auto end = std::min(job->done + uint32_t(count), ranges[i].at + ranges[i].bytes);
    if (first < end) {
      job->sample.add(data + (first - job->done), end - first);
    }
  }
  job->done += count;
}
void FileIdentity::published(const Str& path) {
  if (!job) {
    return;
  }
  struct stat st{};
  if (job->done != job->size || stat(path.c_str(), &st) || st.st_size != job->size ||
      (job->fd >= 0 && st.st_mtime != job->mtime)) {
    reset();
    return;
  }
  result = job->hash.end();
  traceIdentity(job->fd < 0 ? "upload" : "full", path, job->done);
  if (job->generation == identityGeneration && st.st_mtime > 315619200) {
    Str relative = relativeKey(path);
    saveRecord(indexSlot(relative), recordPrefix(relative, st) + result + job->sample.end(), 512);
    remember(path, st, result, false);
  }
  delete job;
  job = nullptr;
}
bool FileIdentity::step() {
  if (!job) {
    return !result.empty();
  }
  if (job->generation != identityGeneration) {
    Str path = job->path;
    return begin(path);
  }
  // Two contiguous multi-sector reads per slice. POSIX read bypasses the
  // embedded stdio refill buffer and leaves the UI loop between slices.
  uint8_t buf[4096];
  for (int i = 0; i < 2 && job->done < job->size; ++i) {
    auto wanted = std::min<size_t>(sizeof buf, job->size - job->done);
    ssize_t n = ::read(job->fd, buf, wanted);
    if (n < 0 && errno == EINTR) {
      return true;
    }
    if (n <= 0) {
      reset();
      return false;
    }
    append(buf, size_t(n));
  }
  if (job->done == job->size) {
    Str path = job->path;
    published(path);
    return !result.empty();
  }
  return true;
}
Str key(const Str& s) {
  Hash h;
  h.add(s.data(), s.size());
  return h.end();
}
Str digest(const Str& p) {
  FILE* f = fopen(p.c_str(), "rb");
  if (!f) {
    return {};
  }
  Hash h;
  uint8_t b[2048];
  size_t n;
  while ((n = fread(b, 1, sizeof b, f))) {
    h.add(b, n);
  }
  bool ok = !ferror(f);
  fclose(f);
  auto v = h.end();
  return ok ? v : Str();
}
static uint64_t quota = 64u << 20, room = UINT64_MAX;
static uint32_t booked = UINT32_MAX;
void setCacheBudget(uint64_t capacity, uint64_t freeBytes) {
  quota = std::min<uint64_t>(64u << 20, capacity / 20);
  room = freeBytes > (16u << 20) ? freeBytes - (16u << 20) : 0;
  booked = UINT32_MAX;
}
bool spareGlyphRoom(uint32_t growth) {
  // Speculation must never cause eviction or consume the final quarter of the cache budget.
  return booked != UINT32_MAX && room >= growth + (1u << 20) && uint64_t(booked) + growth <= 3 * quota / 4;
}
bool cacheRoom(const Str& target, uint32_t growth) {
  if (quota == 0 || room < growth) {
    return false;
  }
  Str dir = root + "/.jingdu/cache";
  if (booked == UINT32_MAX || uint64_t(booked) + growth > quota) {
    booked = 0;
    DIR* d = opendir(dir.c_str());
    if (d) {
      Str oldest;
      time_t stamp = 0;
      struct stat st{};
      while (auto e = readdir(d)) {
        if (e->d_name[0] == '.') {
          continue;
        }
        Str p = join(dir, e->d_name);
        if (stat(p.c_str(), &st) || !S_ISREG(st.st_mode)) {
          continue;
        }
        booked += std::min<uint64_t>(st.st_size, UINT32_MAX - booked);
        if (p != target &&
            (Str(e->d_name).compare(0, 5, "page-") == 0 || Str(e->d_name).compare(0, 6, "index-") == 0) &&
            (oldest.empty() || st.st_mtime < stamp)) {
          oldest = p;
          stamp = st.st_mtime;
        }
      }
      closedir(d);
      if (uint64_t(booked) + growth > quota && !oldest.empty()) {
        auto n = length(oldest);
        if (remove(oldest.c_str()) == 0) {
          booked -= std::min(n, booked);
        }
      }
    }
  }
  if (uint64_t(booked) + growth > quota) {
    return false;
  }
  booked += growth;
  room -= growth;
  return true;
}
bool durable(FILE* f) {
  return fflush(f) == 0 && fsync(fileno(f)) == 0;
}
struct Header {
  uint32_t magic, seq, size, crc;
};
// Check both generation headers, then stream the newest valid payload. Open only
// one slot at a time; the SD mount also serves the book and font handles.
static FILE* selectRecord(const Str& path, size_t limit, Header& selected, int& slot, bool& present,
                          const Str* compare = nullptr, bool* equal = nullptr) {
  Header headers[2]{};
  bool readable[2]{};
  present = false;
  for (unsigned i = 0; i < 2; ++i) {
    FILE* f = fopen((path + (i ? ".1" : ".0")).c_str(), "rb");
    if (!f) {
      if (errno == ENOENT) {
        continue;
      }
      present = true;
      return nullptr;
    }
    present = true;
    Header& h = headers[i];
    readable[i] = fread(&h, sizeof h, 1, f) == 1 && h.magic == 0x3144524a && h.size <= limit;
    bool ioError = ferror(f);
    fclose(f);
    if (ioError) {
      return nullptr;
    }
  }
  int first = readable[1] && (!readable[0] || int32_t(headers[1].seq - headers[0].seq) > 0) ? 1 : 0;
  uint8_t buf[512];
  for (int attempt = 0; attempt < 2; ++attempt) {
    int i = first ^ attempt;
    if (!readable[i]) {
      continue;
    }
    FILE* f = fopen((path + (i ? ".1" : ".0")).c_str(), "rb");
    if (!f) {
      return nullptr;
    }
    const Header& h = headers[i];
    Header current{};
    if (fread(&current, sizeof current, 1, f) != 1 || memcmp(&h, &current, sizeof h)) {
      fclose(f);
      return nullptr;
    }
    uint32_t crc = ~0u, done = 0;
    bool same = compare && compare->size() == h.size;
    while (done < h.size) {
      size_t n = std::min<size_t>(sizeof buf, h.size - done);
      if (fread(buf, 1, n, f) != n) {
        break;
      }
      crc = updateChecksum(crc, buf, n);
      if (same && memcmp(buf, compare->data() + done, n)) {
        same = false;
      }
      done += n;
    }
    if (done == h.size && ~crc == h.crc && fgetc(f) == EOF && !ferror(f) &&
        fseek(f, sizeof h, SEEK_SET) == 0) {
      slot = i;
      selected = h;
      if (equal) {
        *equal = same;
      }
      return f;
    }
    bool ioError = ferror(f);
    fclose(f);
    if (ioError) {
      return nullptr;
    }
  }
  return nullptr;
}
FILE* openRecord(const Str& path, size_t limit, bool& present) {
  Header h{};
  int slot = -1;
  return selectRecord(path, limit, h, slot, present);
}
bool loadRecord(const Str& p, Str& out, size_t limit) {
  Header h{};
  int slot = -1;
  bool present;
  FILE* f = selectRecord(p, limit, h, slot, present);
  out.clear();
  if (!f) {
    return false;
  }
  out.resize(h.size);
  bool ok = fread(out.data(), 1, h.size, f) == h.size;
  fclose(f);
  if (!ok) {
    out.clear();
  }
  return ok;
}
bool saveRecord(const Str& p, const Str& data, size_t limit) {
  if (data.size() > limit || !makedirs(parent(p))) {
    return false;
  }
  Header old{};
  int last = -1;
  bool present, same = false;
  FILE* selected = selectRecord(p, limit, old, last, present, &data, &same);
  if (!selected && present) {
    return false;
  }
  if (selected) {
    fclose(selected);
  }
  if (same) {
    return true;
  }
  uint32_t seq = old.seq;
  FILE* f = fopen((p + (last == 0 ? ".1" : ".0")).c_str(), "wb");
  if (!f) {
    return false;
  }
  Header h{0x3144524a, seq + 1, uint32_t(data.size()), checksum(data.data(), data.size())};
  bool ok =
      fwrite(&h, sizeof h, 1, f) == 1 && fwrite(data.data(), 1, data.size(), f) == data.size() && durable(f);
  return fclose(f) == 0 && ok;
}
uint32_t nextChar(const char*& p, const char* end) {
  if (p == end) {
    return 0;
  }
  uint8_t a = *p++;
  if (a < 128) {
    return a;
  }
  int n = a >= 0xf0 && a <= 0xf4 ? 3 : a >= 0xe0 && a < 0xf0 ? 2 : a >= 0xc2 && a < 0xe0 ? 1 : 0;
  if (!n || end - p < n) {
    return 0xfffd;
  }
  uint32_t v = a & ((1u << (6 - n)) - 1);
  const char* q = p;
  for (int i = 0; i < n; i++) {
    uint8_t b = *q++;
    if ((b & 0xc0) != 0x80) {
      return 0xfffd;
    }
    v = (v << 6) | (b & 63);
  }
  if (v > 0x10ffff || (v >= 0xd800 && v <= 0xdfff) || (n == 2 && v < 0x800) || (n == 3 && v < 0x10000)) {
    return 0xfffd;
  }
  p = q;
  return v;
}
}  // namespace jd
