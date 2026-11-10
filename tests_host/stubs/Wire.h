#pragma once
#include "Arduino.h"
class TwoWire : public Stream {
 public:
  bool begin(int sda = -1, int scl = -1, uint32_t frequency = 0);
  void beginTransmission(uint8_t address);
  uint8_t endTransmission(bool sendStop = true);
  size_t requestFrom(uint8_t address, size_t size, bool sendStop = true);
  size_t write(uint8_t) override;
  size_t write(const uint8_t*, size_t) override;
  int available() override;
  int read() override;
  int peek() override;
  void flush() override;
  using Print::write;
};
extern TwoWire Wire;
