#pragma once
#include "Storage.h"
#include <BookStorage.h>
namespace jd {
// One catalog file per book path. BookCatalog verifies the ZIP fingerprint on open.
class CatalogFile final : public freeink::book::CacheStorage {
  FILE* writer = nullptr;
  mutable FILE* reader = nullptr;
  Str path, temp, backup;

 public:
  void setPath(const Str& name) {
    path = name;
    temp = path + ".part";
    backup = path + ".old";
    if (length(backup)) {
      if (length(path)) {
        std::remove(backup.c_str());
      } else {
        std::rename(backup.c_str(), path.c_str());
      }
    }
  }
  ~CatalogFile() {
    if (reader) {
      fclose(reader);
    }
    if (writer) {
      fclose(writer);
      std::remove(temp.c_str());
    }
  }
  bool exists(const char*) override {
    return length(path) > 0;
  }
  bool remove(const char*) override {
    return std::remove(path.c_str()) == 0;
  }
  int64_t fileSize(const char*) override {
    return length(path);
  }
  int32_t readAt(const char*, uint32_t off, void* dst, uint32_t bytes) override {
    if (!reader) {
      reader = fopen(path.c_str(), "rb");
    }
    if (!reader || fseek(reader, off, SEEK_SET)) {
      return -1;
    }
    return fread(dst, 1, bytes, reader);
  }
  bool beginWrite(const char*) override {
    if (reader) {
      fclose(reader);
      reader = nullptr;
    }
    writer = fopen(temp.c_str(), "wb");
    return writer != nullptr;
  }
  bool write(const void* data, uint32_t bytes) override {
    return writer && fwrite(data, 1, bytes, writer) == bytes;
  }
  void abortWrite(const char*) override {
    if (writer) {
      fclose(writer);
    }
    writer = nullptr;
    std::remove(temp.c_str());
  }
  bool endWrite() override {
    if (!writer) {
      return false;
    }
    bool ok = durable(writer);
    ok = fclose(writer) == 0 && ok;
    writer = nullptr;
    if (ok) {
      // FAT may refuse to rename over an existing catalog.
      if (std::rename(temp.c_str(), path.c_str()) != 0) {
        ok = length(path) && std::rename(path.c_str(), backup.c_str()) == 0;
        if (ok) {
          ok = std::rename(temp.c_str(), path.c_str()) == 0;
          if (ok) {
            std::remove(backup.c_str());
          } else {
            std::rename(backup.c_str(), path.c_str());
          }
        }
      }
    }
    if (!ok) {
      std::remove(temp.c_str());
    }
    return ok;
  }
};
}  // namespace jd
