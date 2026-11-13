#include <FtFont.h>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

using freeink::font::FtFont;
struct alignas(max_align_t) Allocation {
  size_t bytes;
};
static size_t used = 0, peak = 0, largest = 0;
static bool failAllocation = false;
static void* allocate(void*, size_t n) {
  if (failAllocation) return nullptr;
  auto* p = static_cast<Allocation*>(malloc(sizeof(Allocation) + n));
  if (!p) return nullptr;
  p->bytes = n;
  used += n;
  if (used > peak) peak = used;
  if (n > largest) largest = n;
  return p + 1;
}
static void release(void*, void* block) {
  if (!block) return;
  auto* p = static_cast<Allocation*>(block) - 1;
  used -= p->bytes;
  free(p);
}
static void* reallocate(void* ctx, void* block, size_t, size_t n) {
  if (!n) {
    release(ctx, block);
    return nullptr;
  }
  void* next = allocate(ctx, n);
  if (!next) return nullptr;
  if (block) {
    auto* p = static_cast<Allocation*>(block) - 1;
    memcpy(next, block, p->bytes < n ? p->bytes : n);
    release(ctx, block);
  }
  return next;
}
struct Source {
  FILE* file;
  bool fail = false;
  size_t maxRead = 0;
};
static unsigned long readFont(void* ctx, unsigned long offset, unsigned char* buffer, unsigned long count) {
  auto& source = *static_cast<Source*>(ctx);
  if (source.fail || fseek(source.file, offset, SEEK_SET) != 0) return 0;
  if (count > source.maxRead) source.maxRead = count;
  return fread(buffer, 1, count, source.file);
}

int main(int argc, char** argv) {
  const FtFont::MemoryCallbacks callbacks{nullptr, allocate, release, reallocate};
  assert(FtFont::configureMemory(&callbacks));
  for (int fileIndex = 1; fileIndex < argc; ++fileIndex) {
    std::ifstream file(argv[fileIndex], std::ios::binary);
    assert(file);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
    Source source{fopen(argv[fileIndex], "rb")};
    assert(source.file);
    const size_t baseline = used;
    peak = used;
    largest = 0;
    for (int style = 0; style < 4; ++style) {
      FtFont streamed;
      assert(streamed.initStream(readFont, &source, bytes.size(), 28, style & 1 ? 700 : 400, style & 2));
      const size_t streamLive = used;
      FtFont resident;
      assert(resident.init(bytes.data(), bytes.size(), 28, style & 1 ? 700 : 400, style & 2));
      FtFont::RenderOptions options;
      options.hinting = FtFont::HintingMode::Native;
      assert(streamed.setRenderOptions(options) && resident.setRenderOptions(options));
      const uint32_t samples[] = {'A', 'g', '0', 0x4e2d, 0x6587, 0x9605, 0x8bfb, 0x5b57, 0x4f53, 0x3002};
      for (uint32_t cp : samples) {
        const auto gid = streamed.glyphId(cp);
        assert(gid && gid == resident.glyphId(cp));
        const auto* a = streamed.rasterizeGlyph26_6(gid, 29 * 64 + 11);
        const auto* b = resident.rasterizeGlyph26_6(gid, 29 * 64 + 11);
        assert(a && b && a->width == b->width && a->height == b->height);
        assert(a->xoff == b->xoff && a->yoff == b->yoff && a->advance == b->advance);
        if (a->width && a->height) assert(memcmp(a->pixels, b->pixels, size_t(a->width) * a->height) == 0);
      }
      resident.deinit();
      source.fail = true;
      assert(!streamed.rasterize('Z', 29));
      source.fail = false;
      assert(streamed.rasterize('Z', 29));
      printf("FONT_STREAM style=%d live=%zu peak=%zu largest=%zu %s\n", style, streamLive, peak, largest,
             argv[fileIndex]);
    }
    assert(largest < 64 * 1024);  // Neither full CJK loca table may become resident.
    // Refused stream reads and OOM must fail cleanly, preserving all live faces.
    source.fail = true;
    FtFont refused;
    assert(!refused.initStream(readFont, &source, bytes.size(), 28));
    source.fail = false;
    failAllocation = true;
    assert(!refused.initStream(readFont, &source, bytes.size(), 28));
    failAllocation = false;
    fclose(source.file);
    // The shared library remains process-owned; later font files must not leak faces.
    if (fileIndex > 1) assert(used == baseline);
  }
}
