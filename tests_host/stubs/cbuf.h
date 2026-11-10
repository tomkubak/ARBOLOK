#pragma once
#include <stddef.h>
#include <stdint.h>
class cbuf {
 public:
  cbuf(size_t size) { (void)size; }
  size_t available() const { return 0; }
  size_t room() const { return 0; }
  size_t write(const char* src, size_t len) { (void)src; return len; }
  size_t read(char* dst, size_t size) { (void)dst; return size; }
  size_t size() const { return 0; }
};
