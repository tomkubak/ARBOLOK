// ============================================================================
//  wiring.h  –  který pin buzí sondu (BIAS)? (ČISTÉ C++, testováno na PC)
//
//  Stará deska má BIAS sondy na GPIO32, nová na GPIO26 (to je DAC2 procesoru). Sestavení pro "špatnou" desku by BIAS
//  nikdy nebudilo: uzel dělicího odporu by visel ve vzduchu, ADC by četl náhodné nízké hodnoty a měření by hlásilo
//  zkrat nebo 100 % vlhkosti. Proto se pin pozná za běhu: pro každého kandidáta se změří čítač uzlu při BIAS = LOW
//  a při HIGH. Pin, který uzel opravdu hýbe, je ten pravý (ve vzduchu skočí čítač z ~0 na ~4095, v mokré zemině
//  aspoň o stovky čítačů).
// ============================================================================
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace wiring {

// Kandidáti v pořadí zkoušení: 32 a 26 jsou jediné používané varianty desky (GPIO25 od v6.1 vypuštěn, nepoužívá se).
const uint8_t kPins[] = {32, 26};
const size_t kPinCount = sizeof(kPins) / sizeof(kPins[0]);
const size_t kPrimaryCount = 2;

inline bool validPin(uint8_t p) {
  for (size_t i = 0; i < kPinCount; i++)
    if (kPins[i] == p) return true;
  return false;
}

// Pin, na který jde přepojit DRUHOU elektrodu (místo GND) pro obrácenou polaritu: jen GPIO14 (od v6.1; dřívější
// 18/19/23 se nepoužívají a uložená hodnota se vrátí na GND).
const uint8_t kRetPins[] = {14};
inline bool validRetPin(uint8_t p) {
  for (size_t i = 0; i < sizeof(kRetPins); i++)
    if (kRetPins[i] == p) return true;
  return false;
}

// Jedno zkoušené místo: čítač uzlu při BIAS = LOW (lo) a při BIAS = HIGH (hi).
struct Sample {
  uint8_t pin = 0;
  float lo = 0.0f;
  float hi = 0.0f;
};

enum Verdict : uint8_t {
  V_NONE = 0,       // uzel nereaguje na žádného kandidáta (zkrat sondy, chybí pull-up, jiný pin ADC...)
  V_FOUND = 1,      // jednoznačně reaguje jediný pin
  V_AMBIGUOUS = 2   // reagují dva a víc podobně (nerozhodnutelné)
};

struct Result {
  Verdict verdict = V_NONE;
  uint8_t pin = 0;     // jen při V_FOUND
  float delta = 0.0f;  // změna čítače vybraného pinu (nebo nejlepšího kandidáta)
};

// Odezva uzlu na pin v čítačích ADC (záporná = šum, bere se jako 0).
const float kMinDelta = 80.0f;   // ~65 mV: menší změna uzlu není odezva (šum a přeslechy jsou do ~20 čítačů)
const float kDominance = 3.0f;   // nejlepší pin musí reagovat aspoň 3x silněji než druhý
const float kNoiseFloor = 10.0f; // druhý se do poměru bere aspoň s touto hodnotou

inline float deltaOf(const Sample& s) {
  const float d = s.hi - s.lo;
  return d > 0.0f ? d : 0.0f;
}

// Vybere pin, který řídí dělič. preferred = pin, který se právě používá (0 = žádný): rozhodne, když reagují dva
// podobně silně (např. oba kandidáti jsou na desce spojeni), aby se zbytečně nepřepínalo.
inline Result choose(const Sample* s, size_t n, uint8_t preferred) {
  Result r;
  if (!s || n == 0) return r;
  size_t best = 0;
  for (size_t i = 1; i < n; i++)
    if (deltaOf(s[i]) > deltaOf(s[best])) best = i;
  const float bd = deltaOf(s[best]);
  r.delta = bd;
  if (bd < kMinDelta) return r;

  float second = 0.0f;
  for (size_t i = 0; i < n; i++)
    if (i != best && deltaOf(s[i]) > second) second = deltaOf(s[i]);
  const float ref = second > kNoiseFloor ? second : kNoiseFloor;
  if (bd >= kDominance * ref) {
    r.verdict = V_FOUND;
    r.pin = s[best].pin;
    return r;
  }
  for (size_t i = 0; i < n; i++) {
    if (s[i].pin == preferred && deltaOf(s[i]) >= kMinDelta && deltaOf(s[i]) >= 0.7f * bd) {
      r.verdict = V_FOUND;
      r.pin = preferred;
      r.delta = deltaOf(s[i]);
      return r;
    }
  }
  r.verdict = V_AMBIGUOUS;
  return r;
}

}  // namespace wiring
