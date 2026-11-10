// Atrapa jádra Arduino-ESP32 pro kontrolu překladu na PC (tools/syntax_check.sh).
// Podpisy odpovídají Arduino-ESP32 3.x; NEJDE O FUNKČNÍ EMULACI, jen o typovou kontrolu.
#pragma once
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include <algorithm>
#include <deque>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <regex>
#include <string>
#include <unordered_map>
#include <vector>

#include "WString.h"
#include "Print.h"
#include "Stream.h"
#include "IPAddress.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#ifndef ESP32
#define ESP32 1
#endif
#ifndef __unused
#define __unused __attribute__((unused))  // newlib (sys/cdefs.h) ho definuje, glibc ne
#endif

#define PROGMEM
#define PGM_P const char*
#define PSTR(s) (s)
#define pgm_read_byte(p) (*(const uint8_t*)(p))
#define IRAM_ATTR
#define ARDUINO_ISR_ATTR
#define RTC_DATA_ATTR
#define RTC_NOINIT_ATTR
#define HIGH 0x1
#define LOW 0x0
#define INPUT 0x01
#define OUTPUT 0x03
#define INPUT_PULLUP 0x05
#define INPUT_PULLDOWN 0x09

typedef bool boolean;
typedef uint8_t byte;

void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t val);
int digitalRead(uint8_t pin);
int analogRead(uint8_t pin);
void analogReadResolution(uint8_t bits);
typedef enum { ADC_0db, ADC_2_5db, ADC_6db, ADC_11db, ADC_ATTENDB_MAX } adc_attenuation_t;
void analogSetAttenuation(adc_attenuation_t attenuation);
void delay(uint32_t ms);
void delayMicroseconds(uint32_t us);
unsigned long millis();
unsigned long micros();
bool setCpuFrequencyMhz(uint32_t cpu_freq_mhz);
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))

class EspClass {
 public:
  void restart();
  uint32_t getFreeHeap();
  uint64_t getEfuseMac();
  uint32_t getFreeSketchSpace();
};
extern EspClass ESP;

class HardwareSerial : public Stream {
 public:
  void begin(unsigned long baud);
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}
  size_t write(uint8_t) override { return 1; }
  size_t write(const uint8_t*, size_t n) override { return n; }
  void setDebugOutput(bool) {}
  using Print::write;
};
extern HardwareSerial Serial;

template <class T>
const T& max_(const T& a, const T& b) { return a > b ? a : b; }
