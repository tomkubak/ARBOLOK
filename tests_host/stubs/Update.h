#pragma once
#include "Arduino.h"
#define U_FLASH 0
#define U_SPIFFS 100
#define UPDATE_SIZE_UNKNOWN 0xFFFFFFFF
class UpdateClass {
 public:
  bool begin(size_t size = UPDATE_SIZE_UNKNOWN, int command = U_FLASH, int ledPin = -1, uint8_t ledOn = LOW, const char* label = NULL);
  size_t write(uint8_t* data, size_t len);
  bool end(bool evenIfRemaining = false);
  void abort();
  bool isRunning() { return false; }
  bool hasError() { return false; }
  void printError(Print& out);
};
extern UpdateClass Update;
