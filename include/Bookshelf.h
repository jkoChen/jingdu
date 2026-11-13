#pragma once
#include "Storage.h"

namespace jd {
Str userDirectory();
Str pendingShelf(const Str& path);
bool onShelf(const Str& path);
bool saveRecent(const Str& path);
Str currentRecent(bool mounted);
bool resolveShelf(const Str& path, const Str& id);
}  // namespace jd
