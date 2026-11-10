#include "rtcutil.h"

#include <Arduino.h>
#include <RTClib.h>
#include <Wire.h>

#include <math.h>

#include "timeutil.h"

static RTC_DS3231 s_rtc;
static bool s_ok = false;

bool rtcInit(bool& lostPower) {
  Wire.begin();
  s_ok = s_rtc.begin();
  lostPower = false;
  if (!s_ok) return false;
  lostPower = s_rtc.lostPower();
  // Výstup 32 kHz je po zapnutí aktivní; na modulech s pull-upem zbytečně žere
  // stovky µA. Nikde se nepoužívá, takže ho vypneme.
  s_rtc.disable32K();
  return true;
}

bool rtcAvailable() { return s_ok; }

bool rtcGetParts(int& y, int& mo, int& d, int& h, int& mi, int& s) {
  if (!s_ok) return false;
  const DateTime n = s_rtc.now();
  if (!n.isValid()) return false;
  y = n.year();
  mo = n.month();
  d = n.day();
  h = n.hour();
  mi = n.minute();
  s = n.second();
  return true;
}

uint32_t rtcUnixNow() {
  if (!s_ok) return 0;
  const DateTime n = s_rtc.now();
  if (!n.isValid()) return 0;
  return n.unixtime();
}

bool rtcSetParts(int y, int mo, int d, int h, int mi, int s) {
  if (!s_ok) return false;
  if (!validDateTime(y, mo, d, h, mi, s)) return false;
  s_rtc.adjust(DateTime((uint16_t)y, (uint8_t)mo, (uint8_t)d, (uint8_t)h, (uint8_t)mi, (uint8_t)s));
  return true;
}

void rtcSetToBuildTime() {
  if (!s_ok) return;
  s_rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
}

// Vlastní čtení teploty: RTClib getTemperature() bere horní bajt jako BEZ znaménka,
// takže pod 0 °C vrací nesmysl (např. -5 °C -> 251). Registr 0x11 je dvojkový doplněk.
float rtcTemperature() {
  if (!s_ok) return NAN;
  Wire.beginTransmission(0x68);
  Wire.write(0x11);
  if (Wire.endTransmission() != 0) return NAN;
  if (Wire.requestFrom((uint8_t)0x68, (uint8_t)2) != 2) return NAN;
  const int8_t msb = (int8_t)Wire.read();
  const uint8_t lsb = (uint8_t)Wire.read();
  return (float)msb + (float)(lsb >> 6) * 0.25f;
}
