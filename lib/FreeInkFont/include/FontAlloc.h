#pragma once

// FreeInkFont — PSRAM-preferring allocator.
//
// Font data (FreeType faces, glyph rasterization, streaming caches, glyph
// bitmap arenas) is large but latency-tolerant, so on a device with external
// PSRAM it belongs there — freeing scarce internal SRAM for the framebuffer,
// task stacks, and DMA buffers. These helpers allocate from PSRAM when it is
// present and fall back to the internal heap otherwise (a no-PSRAM device such
// as an ESP32-S3 N8, or a host build). Detection is at runtime via the ESP-IDF
// heap-caps API, so no build-time PSRAM flag is required; on non-ESP builds
// these are plain malloc/realloc/free.
//
// Pair these allocations with fiFontFree, including on the budgeted reader build.

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void* fiFontMalloc(size_t size);
void* fiFontRealloc(void* ptr, size_t size);
void fiFontFree(void* ptr);
#ifdef JINGDU_SPLIT_INFLATE
size_t fiFontLiveBytes(void);
size_t fiFontPeakBytes(void);
#endif

#ifdef __cplusplus
}
#endif
