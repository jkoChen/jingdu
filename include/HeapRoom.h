#pragma once
#include <stddef.h>
#ifdef ARDUINO
#include <esp_heap_caps.h>
#endif
namespace jd {
// IRAM can contain aligned words but cannot hold a byte buffer or a C++ string.
inline bool byteHeapRoom(size_t bytes, size_t reserve) {
#ifdef ARDUINO
  constexpr auto caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  const size_t free = heap_caps_get_free_size(caps);
  return bytes <= free && reserve <= free - bytes && heap_caps_get_largest_free_block(caps) >= bytes;
#else
  (void)bytes;
  (void)reserve;
  return true;
#endif
}
}  // namespace jd
