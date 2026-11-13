#include "FontAlloc.h"

#include <stdlib.h>

#if defined(ESP_PLATFORM)
#include "esp_heap_caps.h"

// PSRAM first, internal heap as fallback. heap_caps_malloc(MALLOC_CAP_SPIRAM)
// returns NULL when no PSRAM is fitted (or it is exhausted), so the fallback
// also covers no-PSRAM boards transparently.
static void* platformAlloc(size_t size) {
  void* p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
  if (p == NULL) p = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  return p;
}

static void* platformRealloc(void* ptr, size_t size) {
  void* p = heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM);
  if (p == NULL && size != 0) p = heap_caps_realloc(ptr, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  return p;
}

static void platformFree(void* ptr) { heap_caps_free(ptr); }

#else  // host / non-ESP build

static void* platformAlloc(size_t size) { return malloc(size); }
static void* platformRealloc(void* ptr, size_t size) { return realloc(ptr, size); }
static void platformFree(void* ptr) { free(ptr); }

#endif

#if defined(JINGDU_SPLIT_INFLATE)
#include <stddef.h>
#include <stdint.h>

// Font operations run on the foreground task. Count live payloads, including
// allocations made internally by FreeType, through its existing allocator.
typedef union { max_align_t alignment; size_t size; } FontHeader;
static size_t live, peak;
size_t fiFontPeakBytes(void) { return peak; }
size_t fiFontLiveBytes(void) { return live; }

static int fits(size_t old, size_t size) {
    if (size > 64 * 1024 || size > 96 * 1024 - (live - old)) return 0;
#ifdef ESP_PLATFORM
    if (size > old && heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) <
        size - old + sizeof(FontHeader) + 4096) return 0;
#endif
    return 1;
}
void* fiFontMalloc(size_t size) {
    if (!fits(0, size)) return NULL;
    FontHeader* h = platformAlloc(sizeof(FontHeader) + size);
    if (!h) return NULL;
    h->size = size;
    live += size;
    if (live > peak) peak = live;
    return h + 1;
}
void fiFontFree(void* ptr) {
    if (!ptr) return;
    FontHeader* h = (FontHeader*)ptr - 1;
    live -= h->size;
    platformFree(h);
}
void* fiFontRealloc(void* ptr, size_t size) {
    if (!ptr) return fiFontMalloc(size);
    if (!size) { fiFontFree(ptr); return NULL; }
    FontHeader* h = (FontHeader*)ptr - 1;
    size_t old = h->size;
    if (!fits(old, size)) return NULL;
    h = platformRealloc(h, sizeof(FontHeader) + size);
    if (!h) return NULL;
    h->size = size;
    live = live - old + size;
    if (live > peak) peak = live;
    return h + 1;
}
#else
void* fiFontMalloc(size_t size) { return platformAlloc(size); }
void* fiFontRealloc(void* ptr, size_t size) { return platformRealloc(ptr, size); }
void fiFontFree(void* ptr) { platformFree(ptr); }
#endif
