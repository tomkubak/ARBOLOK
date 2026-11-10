// Funkční atrapa LittleFS nad tests_host/fsmem/FS.h (viz tam).
#pragma once
#include "FS.h"

namespace fs {
class LittleFSFS : public FS {
 public:
  bool begin(bool formatOnFail = false, const char* = "/littlefs", uint8_t = 10, const char* = "spiffs") {
    MemFs& m = mem();
    if (m.mountFailsOnce) {  // poškozený FS: připojí se až po formátu
      if (!formatOnFail) return false;
      m.mountFailsOnce = false;
      m.files.clear();
    }
    return true;
  }
  size_t totalBytes() { return mem().capacity; }
  size_t usedBytes() { return mem().used(); }
  void end() {}
  bool format() {
    mem().files.clear();
    return true;
  }
};
}  // namespace fs
inline fs::LittleFSFS LittleFS;
