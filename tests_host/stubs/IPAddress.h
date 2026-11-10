#pragma once
#include <stdint.h>
#include "Print.h"
#include "WString.h"
class IPAddress : public Printable {
 public:
  IPAddress() : a_(0) {}
  IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) : a_((uint32_t)a | (uint32_t)b << 8 | (uint32_t)c << 16 | (uint32_t)d << 24) {}
  IPAddress(uint32_t address) : a_(address) {}
  operator uint32_t() const { return a_; }
  bool operator==(const IPAddress& o) const { return a_ == o.a_; }
  String toString() const { return String(); }
  size_t printTo(Print&) const override { return 0; }
 private:
  uint32_t a_;
};
