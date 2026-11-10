// Host test src/wiring.h: z čítačů uzlu při BIAS dole/nahoře vybrat pin, který opravdu buzí dělič sondy.
// Hlídá hlavně: žádný falešný nález (šum, přeslechy, zkrat, visící uzel), správný pin ve vzduchu i v mokré zemině,
// nerozhodnutelné případy se nesmějí tvářit jednoznačně.
// Sestavení a spuštění:  tools/run_host_tests.sh
#include <stdio.h>

#include "../src/wiring.h"

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                            \
  do {                                                         \
    if (cond) {                                                \
      g_pass++;                                                \
    } else {                                                   \
      g_fail++;                                                \
      printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                          \
  } while (0)

static wiring::Sample S(uint8_t pin, float lo, float hi) {
  wiring::Sample s;
  s.pin = pin;
  s.lo = lo;
  s.hi = hi;
  return s;
}

static void test_pins() {
  printf("[wiring] kandidáti na pin\n");
  CHECK(wiring::validPin(32) && wiring::validPin(26) && !wiring::validPin(25));  // GPIO25 od v6.1 ne
  // piny, které firmware používá k jinému účelu, nikdy nesmí být kandidát na BIAS
  const uint8_t others[] = {0, 2, 4, 12, 13, 21, 22, 27, 33, 34, 35, 36, 39, 255};
  for (uint8_t p : others) CHECK(!wiring::validPin(p));
  CHECK(wiring::kPinCount == 2 && wiring::kPrimaryCount == 2);
  CHECK(wiring::kPinCount == 2 && wiring::kPins[0] == 32 && wiring::kPins[1] == 26);  // pořadí zkoušení
}

static void test_found() {
  printf("[wiring] pravý pin: ve vzduchu, v mokré zemině, na každém kandidátovi\n");
  const uint8_t pins[] = {32, 26, 25};
  const float hiFor[] = {4095.0f, 3950.0f, 2047.0f, 372.0f, 120.0f};  // vzduch, suchá, 10 kΩ, mokrá 1 kΩ, velmi mokrá
  for (uint8_t real : pins) {
    for (float hi : hiFor) {
      wiring::Sample s[3];
      for (int i = 0; i < 3; i++) s[i] = S(pins[i], 2.0f, pins[i] == real ? hi : 4.0f);
      const wiring::Result r = wiring::choose(s, 3, 0);
      CHECK(r.verdict == wiring::V_FOUND);
      CHECK(r.pin == real);
      CHECK(r.delta > hi - 10.0f && r.delta < hi);
    }
  }
}

static void test_none() {
  printf("[wiring] nic nereaguje: zkrat, visící uzel, šum, přeslech\n");
  wiring::Sample s[3];
  // zkrat sondy: uzel je ~0 při všem
  for (int i = 0; i < 3; i++) s[i] = S(i < 2 ? wiring::kPins[i] : (uint8_t)25, 0.0f, 0.0f);
  CHECK(wiring::choose(s, 3, 0).verdict == wiring::V_NONE);
  // visící uzel: čte náhodné nízké hodnoty, nahoře i dole stejně (v obou směrech ± šum)
  s[0] = S(32, 41.0f, 62.0f);
  s[1] = S(26, 58.0f, 37.0f);
  s[2] = S(25, 50.0f, 52.0f);
  CHECK(wiring::choose(s, 3, 0).verdict == wiring::V_NONE);
  // přeslech sousedního pinu (~20 čítačů) není odezva
  s[0] = S(32, 0.0f, 20.0f);
  s[1] = S(26, 0.0f, 3.0f);
  s[2] = S(25, 0.0f, 1.0f);
  CHECK(wiring::choose(s, 3, 0).verdict == wiring::V_NONE);
  // uzel držený nahoře (4095 při všem)
  for (int i = 0; i < 3; i++) s[i] = S(i < 2 ? wiring::kPins[i] : (uint8_t)25, 4095.0f, 4095.0f);
  CHECK(wiring::choose(s, 3, 0).verdict == wiring::V_NONE);
  // bez dat
  CHECK(wiring::choose(nullptr, 3, 0).verdict == wiring::V_NONE);
  CHECK(wiring::choose(s, 0, 0).verdict == wiring::V_NONE);
  // pokles při BIAS nahoře (záporná odezva) se bere jako nula
  s[0] = S(32, 3000.0f, 100.0f);
  s[1] = S(26, 3000.0f, 100.0f);
  s[2] = S(25, 3000.0f, 100.0f);
  CHECK(wiring::choose(s, 3, 0).verdict == wiring::V_NONE);
}

static void test_threshold() {
  printf("[wiring] mez odezvy (kMinDelta) a převaha nad druhým\n");
  wiring::Sample s[2];
  s[0] = S(32, 0.0f, wiring::kMinDelta - 1.0f);
  s[1] = S(26, 0.0f, 0.0f);
  CHECK(wiring::choose(s, 2, 0).verdict == wiring::V_NONE);
  s[0] = S(32, 0.0f, wiring::kMinDelta);
  CHECK(wiring::choose(s, 2, 0).verdict == wiring::V_FOUND);
  // druhý pin reaguje ze třetiny: pořád ještě rozhodnutelné (převaha 3x), o trochu víc už ne
  s[0] = S(32, 0.0f, 900.0f);
  s[1] = S(26, 0.0f, 300.0f);
  CHECK(wiring::choose(s, 2, 0).verdict == wiring::V_FOUND && wiring::choose(s, 2, 0).pin == 32);
  s[1] = S(26, 0.0f, 301.0f);
  CHECK(wiring::choose(s, 2, 0).verdict == wiring::V_AMBIGUOUS);
}

static void test_ambiguous_and_preferred() {
  printf("[wiring] reagují oba kandidáti: rozhoduje používaný pin, jinak se nic nepřepíná\n");
  wiring::Sample s[3];
  s[0] = S(32, 0.0f, 3000.0f);
  s[1] = S(26, 0.0f, 3100.0f);
  s[2] = S(25, 0.0f, 2.0f);
  wiring::Result r = wiring::choose(s, 3, 0);  // žádný se nepoužívá
  CHECK(r.verdict == wiring::V_AMBIGUOUS && r.pin == 0);
  r = wiring::choose(s, 3, 32);  // 32 se používá a reaguje skoro stejně silně
  CHECK(r.verdict == wiring::V_FOUND && r.pin == 32);
  r = wiring::choose(s, 3, 26);
  CHECK(r.verdict == wiring::V_FOUND && r.pin == 26);
  r = wiring::choose(s, 3, 25);  // používaný pin nereaguje: nerozhodnuto
  CHECK(r.verdict == wiring::V_AMBIGUOUS);
  // používaný pin reaguje o dost slaběji (< 70 %) než nejlepší: nepřebíjí
  s[0] = S(32, 0.0f, 1000.0f);
  s[1] = S(26, 0.0f, 1900.0f);
  r = wiring::choose(s, 3, 32);
  CHECK(r.verdict == wiring::V_AMBIGUOUS);
}

static void test_scale_independent_of_order() {
  printf("[wiring] pořadí vzorků nehraje roli\n");
  wiring::Sample a[3] = {S(32, 0, 5), S(26, 1, 4000), S(25, 0, 0)};
  wiring::Sample b[3] = {S(25, 0, 0), S(26, 1, 4000), S(32, 0, 5)};
  const wiring::Result ra = wiring::choose(a, 3, 0), rb = wiring::choose(b, 3, 0);
  CHECK(ra.verdict == wiring::V_FOUND && rb.verdict == wiring::V_FOUND && ra.pin == 26 && rb.pin == 26);
}

int main() {
  test_pins();
  test_found();
  test_none();
  test_threshold();
  test_ambiguous_and_preferred();
  test_scale_independent_of_order();
  printf("\n== wiring: %d OK, %d FAIL ==\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
