// ============================================================================
//  rtcutil.h  –  DS3231 (hodiny + teplota)
//  Smí se volat JEN z hlavní smyčky (I²C sdílené, handlery na něj nesahají).
// ============================================================================
#pragma once
#include <stdint.h>

bool rtcInit(bool& lostPower);   // false = RTC neodpovídá na I²C
bool rtcAvailable();
bool rtcGetParts(int& y, int& mo, int& d, int& h, int& mi, int& s);  // false = chyba / neplatné
uint32_t rtcUnixNow();           // "lokální" unix čas (0 = chyba)
bool rtcSetParts(int y, int mo, int d, int h, int mi, int s);        // validuje; false = odmítnuto
void rtcSetToBuildTime();        // nouzově po ztrátě napájení RTC
float rtcTemperature();          // °C, NAN při chybě (správně zpracuje záporné teploty)
