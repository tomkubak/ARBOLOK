#pragma once
#include "Print.h"
class Stream : public Print {
 public:
  virtual int available() = 0;
  virtual int read() = 0;
  virtual int peek() = 0;
  void setTimeout(unsigned long) {}
  virtual size_t readBytes(char* buffer, size_t length) {
    size_t n = 0;
    while (n < length) { int c = read(); if (c < 0) break; buffer[n++] = (char)c; }
    return n;
  }
  virtual size_t readBytes(uint8_t* buffer, size_t length) { return readBytes((char*)buffer, length); }
  String readString() { return String(); }
};
