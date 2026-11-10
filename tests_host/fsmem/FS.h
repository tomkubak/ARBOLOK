// FUNKČNÍ atrapa souborového systému v paměti pro host testy logstore.cpp.
// Předsazuje se před tests_host/stubs (-Itests_host/fsmem), takže `#include <FS.h>` najde tuhle.
// Chování se řídí LittleFS: "w" soubor založí/zkrátí, "a" připíše, "r" vyžaduje existující soubor;
// při nedostatku místa zápis uloží jen to, co se vejde (a vrátí menší počet bajtů).
#pragma once
#include <stdint.h>
#include <string.h>

#include <map>
#include <memory>
#include <string>

#include "Arduino.h"

#define FILE_READ "r"
#define FILE_WRITE "w"
#define FILE_APPEND "a"

namespace fs {
enum SeekMode { SeekSet = 0, SeekCur = 1, SeekEnd = 2 };

struct MemFs {
  std::map<std::string, std::string> files;
  size_t capacity = 1441792;  // 1408 KB jako výchozí oddíl
  bool mountFailsOnce = false;
  size_t writes = 0;

  void reset() {
    files.clear();
    capacity = 1441792;
    mountFailsOnce = false;
    writes = 0;
  }
  // LittleFS počítá po blocích (4 kB); každý soubor zabere aspoň blok, plus režie metadat.
  size_t used() const {
    size_t u = 8192;
    for (const auto& kv : files) u += std::max<size_t>(4096, ((kv.second.size() + 4095) / 4096) * 4096);
    return u;
  }
};
inline MemFs& mem() {
  static MemFs m;
  return m;
}

class File {
 public:
  File() {}
  File(const std::string& path, bool wr, bool app) : impl_(std::make_shared<Impl>()) {
    impl_->path = path;
    impl_->wr = wr;
    impl_->app = app;
  }
  operator bool() const { return impl_ && !impl_->closed; }
  size_t size() const { return ok() ? mem().files[impl_->path].size() : 0; }
  size_t position() const { return impl_ ? impl_->pos : 0; }
  bool seek(uint32_t pos, SeekMode mode = SeekSet) {
    if (!ok()) return false;
    const size_t sz = mem().files[impl_->path].size();
    size_t np = pos;
    if (mode == SeekCur) np = impl_->pos + pos;
    else if (mode == SeekEnd) np = sz - std::min<size_t>(sz, pos);
    if (np > sz) return false;
    impl_->pos = np;
    return true;
  }
  size_t read(uint8_t* buf, size_t n) {
    if (!ok()) return 0;
    const std::string& s = mem().files[impl_->path];
    if (impl_->pos >= s.size()) return 0;
    const size_t k = std::min(n, s.size() - impl_->pos);
    memcpy(buf, s.data() + impl_->pos, k);
    impl_->pos += k;
    return k;
  }
  size_t write(const uint8_t* buf, size_t n) {
    if (!ok() || !impl_->wr) return 0;
    MemFs& m = mem();
    m.writes++;
    std::string& s = m.files[impl_->path];
    size_t take = n;
    // kolik se vejde: zkoušíme zmenšovat, dokud se celek vejde do kapacity
    const size_t before = m.used();
    const size_t cur = std::max<size_t>(4096, ((s.size() + 4095) / 4096) * 4096);
    const size_t after = std::max<size_t>(4096, ((s.size() + n + 4095) / 4096) * 4096);
    if (before - cur + after > m.capacity) {
      const size_t room0 = m.capacity > (before - cur) ? m.capacity - (before - cur) : 0;
      const size_t room = (room0 / 4096) * 4096;  // soubor zabírá celé bloky
      take = room > s.size() ? std::min(n, room - s.size()) : 0;
    }
    if (impl_->app) impl_->pos = s.size();
    if (impl_->pos > s.size()) impl_->pos = s.size();
    s.replace(impl_->pos, std::min(take, s.size() - impl_->pos), std::string((const char*)buf, take));
    impl_->pos += take;
    return take;
  }
  void close() {
    if (impl_) impl_->closed = true;
  }
  bool isDirectory() { return false; }
  const char* name() const { return impl_ ? impl_->path.c_str() + 1 : ""; }

 private:
  struct Impl {
    std::string path;
    bool wr = false, app = false, closed = false;
    size_t pos = 0;
  };
  bool ok() const { return impl_ && !impl_->closed; }
  std::shared_ptr<Impl> impl_;
};

class FS {
 public:
  virtual ~FS() {}
  File open(const char* path, const char* mode = FILE_READ, const bool create = false) {
    (void)create;
    MemFs& m = mem();
    const std::string p = path;
    const bool exists = m.files.count(p) > 0;
    if (mode[0] == 'r') return exists ? File(p, false, false) : File();
    if (mode[0] == 'w') {
      if (!exists && m.used() + 4096 > m.capacity) return File();  // ani na prázdný soubor není místo
      m.files[p].clear();
      return File(p, true, false);
    }
    if (mode[0] == 'a') {
      if (!exists) {
        if (m.used() + 4096 > m.capacity) return File();
        m.files[p];
      }
      return File(p, true, true);
    }
    return File();
  }
  File open(const String& path, const char* mode = FILE_READ, const bool create = false) { return open(path.c_str(), mode, create); }
  bool exists(const char* path) { return mem().files.count(path) > 0; }
  bool exists(const String& path) { return exists(path.c_str()); }
  bool remove(const char* path) { return mem().files.erase(path) > 0; }
  bool remove(const String& path) { return remove(path.c_str()); }
  // Jako lfs_rename: cíl, který už existuje, se nahradí.
  bool rename(const char* from, const char* to) {
    MemFs& m = mem();
    auto it = m.files.find(from);
    if (it == m.files.end()) return false;
    std::string data = it->second;
    m.files.erase(it);
    m.files[to] = data;
    return true;
  }
  bool rename(const String& from, const String& to) { return rename(from.c_str(), to.c_str()); }
};
}  // namespace fs

using fs::File;
using fs::FS;
using fs::SeekCur;
using fs::SeekEnd;
using fs::SeekMode;
using fs::SeekSet;
