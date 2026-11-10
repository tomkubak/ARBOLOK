#pragma once
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "WString.h"

class Printable;
class Print {
 public:
  virtual ~Print() {}
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t* buffer, size_t size) {
    size_t n = 0;
    while (size--) n += write(*buffer++);
    return n;
  }
  size_t write(const char* s) { return s ? write((const uint8_t*)s, strlen(s)) : 0; }
  size_t write(const char* buffer, size_t size) { return write((const uint8_t*)buffer, size); }
  virtual int availableForWrite() { return 0; }
  virtual void flush() {}
  size_t print(const String& s) { return write(s.c_str()); }
  size_t print(const char* s) { return write(s); }
  size_t print(char c) { return write((uint8_t)c); }
  size_t print(int v) { return print(String(v)); }
  size_t print(unsigned int v) { return print(String(v)); }
  size_t print(long v) { return print(String(v)); }
  size_t print(unsigned long v) { return print(String(v)); }
  size_t print(double v, int dec = 2) { return print(String(v, (unsigned char)dec)); }
  size_t println(const String& s) { return print(s) + write("\r\n"); }
  size_t println(const char* s) { return print(s) + write("\r\n"); }
  size_t println() { return write("\r\n"); }
  size_t println(int v) { return print(v) + write("\r\n"); }
  size_t println(unsigned int v) { return print(v) + write("\r\n"); }
  size_t println(double v, int dec = 2) { return print(v, dec) + write("\r\n"); }
  size_t printf(const char* format, ...) __attribute__((format(printf, 2, 3))) {
    char b[256];
    va_list ap;
    va_start(ap, format);
    int n = vsnprintf(b, sizeof b, format, ap);
    va_end(ap);
    return n > 0 ? write((const uint8_t*)b, (size_t)n) : 0;
  }
};
class Printable {
 public:
  virtual ~Printable() {}
  virtual size_t printTo(Print& p) const = 0;
};
