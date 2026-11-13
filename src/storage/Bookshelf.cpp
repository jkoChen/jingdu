#include "Bookshelf.h"
#include <unistd.h>

namespace jd {
Str userDirectory() {
  return root + "/.jingdu/user/";
}
Str pendingShelf(const Str& path) {
  return userDirectory() + "shelf/p-" + key(path);
}
static bool references(const Str& dir, const Str& path) {
  Str value;
  return loadRecord(dir + "/ref", value) && value == path;
}
bool onShelf(const Str& path) {
  if (references(pendingShelf(path), path)) {
    return true;
  }
  Str id;
  for (const char* index : {"shelfby/", "locator/"}) {
    if (loadRecord(userDirectory() + index + key(path), id) && id.size() == 64 &&
        references(userDirectory() + "shelf/" + id, path)) {
      return true;
    }
  }
  return false;
}
static Str recentRecord() {
  return userDirectory() + "shelf/.recent";
}
bool saveRecent(const Str& path) {
  return saveRecord(recentRecord(), onShelf(path) ? "+" + path : path);
}
Str currentRecent(bool mounted) {
  if (!mounted) {
    return {};
  }
  Str path;
  const Str legacy = userDirectory() + "recent";
  bool migrate = !loadRecord(recentRecord(), path, 256);
  if (migrate && !loadRecord(legacy, path, 256)) {
    return {};
  }
  bool shelved = !migrate && !path.empty() && path.front() == '+';
  if (shelved) {
    path.erase(0, 1);
  }
  if (!userPath(path) || (!ends(path, ".txt") && !ends(path, ".epub")) || !length(root + path)) {
    return {};
  }
  if (migrate) {
    Str oldIndex;
    if (loadRecord(userDirectory() + "shelfby/" + key(path), oldIndex) && !onShelf(path)) {
      return {};
    }
    if (saveRecent(path)) {
      unlink((legacy + ".0").c_str());
      unlink((legacy + ".1").c_str());
    }
  } else if (shelved && !onShelf(path)) {
    return {};
  }
  return path;
}
static void removeShelfRef(const Str& dir) {
  // Only unlink the files written by saveRecord, after the new reference exists.
  unlink((dir + "/ref.0").c_str());
  unlink((dir + "/ref.1").c_str());
  rmdir(dir.c_str());
}
bool resolveShelf(const Str& path, const Str& id) {
  if (id.empty()) {
    return false;
  }
  auto pending = pendingShelf(path), dest = userDirectory() + "shelf/" + id;
  bool registered = references(pending, path);
  Str previous;
  if (!loadRecord(userDirectory() + "shelfby/" + key(path), previous)) {
    loadRecord(userDirectory() + "locator/" + key(path), previous);
  }
  bool obsolete =
      previous.size() == 64 && previous != id && references(userDirectory() + "shelf/" + previous, path);
  if (!registered && !obsolete) {
    return false;
  }
  if (!saveRecord(dest + "/ref", path) || !saveRecord(userDirectory() + "shelfby/" + key(path), id)) {
    return false;
  }
  if (registered) {
    removeShelfRef(pending);
  }
  if (obsolete) {
    removeShelfRef(userDirectory() + "shelf/" + previous);
  }
  return true;
}
}  // namespace jd
