#pragma once
#include <stdint.h>
#include <time.h>
#include "Arduino.h"
#define FILE_READ "r"
#define FILE_WRITE "w"
#define FILE_APPEND "a"
namespace fs {
enum SeekMode { SeekSet = 0, SeekCur = 1, SeekEnd = 2 };
class File : public Stream {
 public:
  File() {}
  size_t write(uint8_t) override { return 1; }
  size_t write(const uint8_t* buf, size_t size) override { (void)buf; return size; }
  int available() override { return 0; }
  int read() override { return -1; }
  size_t read(uint8_t* buf, size_t size) { (void)buf; (void)size; return 0; }
  int peek() override { return -1; }
  void flush() override {}
  bool seek(uint32_t pos, SeekMode mode = SeekSet) { (void)pos; (void)mode; return true; }
  size_t position() const { return 0; }
  size_t size() const { return 0; }
  bool setBufferSize(size_t) { return true; }
  void close() {}
  operator bool() const { return false; }
  time_t getLastWrite() { return 0; }
  const char* path() const { return ""; }
  const char* name() const { return ""; }
  bool isDirectory(void) { return false; }
  File openNextFile(const char* mode = FILE_READ) { (void)mode; return File(); }
  void rewindDirectory(void) {}
  using Print::write;
};
class FS {
 public:
  virtual ~FS() {}
  File open(const char* path, const char* mode = FILE_READ, const bool create = false) { (void)path; (void)mode; (void)create; return File(); }
  File open(const String& path, const char* mode = FILE_READ, const bool create = false) { return open(path.c_str(), mode, create); }
  bool exists(const char* path) { (void)path; return false; }
  bool exists(const String& path) { return exists(path.c_str()); }
  bool remove(const char* path) { (void)path; return true; }
  bool remove(const String& path) { return remove(path.c_str()); }
  bool rename(const char* from, const char* to) { (void)from; (void)to; return true; }
  bool rename(const String& from, const String& to) { return rename(from.c_str(), to.c_str()); }
  bool mkdir(const char*) { return true; }
  bool rmdir(const char*) { return true; }
};
}  // namespace fs
using fs::File;
using fs::FS;
using fs::SeekCur;
using fs::SeekEnd;
using fs::SeekMode;
using fs::SeekSet;
