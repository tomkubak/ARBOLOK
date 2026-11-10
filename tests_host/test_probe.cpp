// Host test src/probe.cpp: skutečný kód měřicí smyčky běží nad virtuálními hodinami a simulovanou
// sondou (tests_host/sim.h). Hlídá hlavně: BIAS se po pulzu vždy vypne a nikdy nesvítí déle než
// strop, medián pulzů, pevný režim (jako v3), vybití elektrod před pulzem, příznaky zkratu a vzduchu.
// Sestavení a spuštění:  tools/run_host_tests.sh
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <vector>

#include <Arduino.h>

#include "../src/config.h"
#include "../src/probe.h"
#include "sim.h"

// ---------------------------------------------------------------------------
//  Virtuální Arduino: čas běží jen voláním delay*/analogRead
// ---------------------------------------------------------------------------
static uint64_t g_us = 10ULL * 1000000ULL;  // start po 10 s (ať první pulz nepovažuje "předchozí" za čerstvý)
static bool g_biasHigh = false;
static uint64_t g_biasOnUs = 0;
static uint64_t g_maxHighUs = 0;
static uint64_t g_lastLowUs = 0;
static int g_pulses = 0;
static SimProbe g_sim;
static std::vector<double> g_rsPerPulse;  // odpor zeminy pro 1., 2., 3. pulz (prázdné = beze změny)
static std::vector<uint64_t> g_onTimes;
// Deska: který pin OPRAVDU buzí dělič (g_realBias), které piny firmware zapnul / nastavil, a "visící" uzel.
static uint8_t g_realBias = SONDA_BIAS_PIN;
static bool g_pinHigh[64];
static uint8_t g_pinModeSet[64];  // poslední pinMode (0 = nikdy nenastaveno)
static int g_floating = -1;       // >= 0: uzel visí a čte tuto hodnotu bez ohledu na BIAS
// Druhá elektroda: 0 = napevno na GND (jako dosud), jinak GPIO, na který je OPRAVDU přepojená.
static uint8_t g_realRet = 0;
static bool g_bothHigh = false;   // někdy byly BIAS i druhá elektroda nahoře současně (nesmí)

unsigned long millis() { return (unsigned long)(g_us / 1000ULL); }
unsigned long micros() { return (unsigned long)(g_us & 0xFFFFFFFFULL); }
void delay(uint32_t ms) { g_us += (uint64_t)ms * 1000ULL; }
void delayMicroseconds(uint32_t us) { g_us += us; }
void pinMode(uint8_t pin, uint8_t mode) {
  if (pin < 64) g_pinModeSet[pin] = mode;
}
void digitalWrite(uint8_t pin, uint8_t val) {
  if (pin < 64) g_pinHigh[pin] = (val == HIGH);
  if (g_realRet && g_pinHigh[g_realRet] && g_pinHigh[g_realBias]) g_bothHigh = true;
  if (pin != g_realBias) return;
  if (val == HIGH && !g_biasHigh) {
    g_biasHigh = true;
    g_biasOnUs = g_us;
    g_onTimes.push_back(g_us);
    g_sim.reset();
    if ((size_t)g_pulses < g_rsPerPulse.size()) g_sim.Rs = g_rsPerPulse[g_pulses];
    g_pulses++;
  } else if (val == LOW && g_biasHigh) {
    g_biasHigh = false;
    const uint64_t d = g_us - g_biasOnUs;
    if (d > g_maxHighUs) g_maxHighUs = d;
    g_lastLowUs = g_us;
  }
}
int analogRead(uint8_t) {
  g_us += g_sim.adcTimeUs;
  if (g_floating >= 0) return g_floating;
  if (g_realRet) {
    // Druhá elektroda na pinu: proud teče jen když je pin výstup. Fáze A: pin dole; fáze B: pin nahoře, BIAS dole.
    const bool retOut = g_pinModeSet[g_realRet] == OUTPUT;
    if (g_biasHigh) {
      if (!retOut || g_pinHigh[g_realRet]) return (int)g_sim.open;  // není kudy téct -> uzel naprázdno
      return (int)g_sim.sample((double)(g_us - g_biasOnUs));
    }
    if (retOut && g_pinHigh[g_realRet]) return (int)lround(g_sim.open * g_sim.R / (g_sim.R + g_sim.Rs));
    return 0;
  }
  return g_biasHigh ? (int)g_sim.sample((double)(g_us - g_biasOnUs)) : 0;
}

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
#define CHECK_NEAR(a, b, tol)                                                                         \
  do {                                                                                                \
    double _a = (a), _b = (b);                                                                        \
    if (_a - _b <= (tol) && _b - _a <= (tol)) {                                                       \
      g_pass++;                                                                                       \
    } else {                                                                                          \
      g_fail++;                                                                                       \
      printf("  FAIL %s:%d  %s=%.3f  %s=%.3f  (tol %.3f)\n", __FILE__, __LINE__, #a, _a, #b, _b, (double)(tol)); \
    }                                                                                                 \
  } while (0)

static void fresh(double R, double Rs) {
  probeUsePin(SONDA_BIAS_PIN);  // předchozí test mohl přepnout pin
  probeUseRetPin(0);
  g_realBias = SONDA_BIAS_PIN;
  g_realRet = 0;
  g_bothHigh = false;
  g_floating = -1;
  memset(g_pinHigh, 0, sizeof(g_pinHigh));
  memset(g_pinModeSet, 0, sizeof(g_pinModeSet));
  g_sim = SimProbe();
  g_sim.R = R;
  g_sim.Rs = Rs;
  g_biasHigh = false;
  g_maxHighUs = 0;
  g_pulses = 0;
  g_rsPerPulse.clear();
  g_onTimes.clear();
  g_us += 20ULL * 1000000ULL;  // dlouhá pauza: dávno po předchozím pulzu
}

static Settings defaults() {
  Settings s;
  settingsDefaults(s);
  return s;
}

// ---------------------------------------------------------------------------
static void test_single_pulse() {
  printf("[probe] jeden pulz: hodnota, ustálení, BIAS vypnut\n");
  fresh(10e3, 10e3);  // dělič 1:1 -> 2047,5 čítačů
  g_sim.noise = 3.0;
  ProbeMeasurement m;
  CHECK(probeMeasureOnce(defaults(), 3.3f, m));
  CHECK(m.ok);
  CHECK_NEAR(m.rawRaw, 2047.5, 12.0);
  CHECK(m.settled);
  CHECK(m.settleMs < 20);
  CHECK(m.flags == 0);
  CHECK(!g_biasHigh);
  CHECK(g_pulses == 1);
  CHECK(g_maxHighUs < 30000ULL);                       // adaptivní měření trvá desítky ms nanejvýš
  CHECK_NEAR(m.g, (4095.0 - m.raw) / m.raw, 1e-3);     // index vodivosti sedí s čítačem
  CHECK(m.percent >= 0.0f && m.percent <= 100.0f);
}

static void test_fixed_mode_like_v3() {
  printf("[probe] pevný režim (jako v3): celé okno do stropu\n");
  fresh(10e3, 10e3);
  Settings s = defaults();
  s.probeMode = 1;
  ProbeMeasurement m;
  CHECK(probeMeasureOnce(s, 3.3f, m));
  CHECK(g_maxHighUs >= (uint64_t)(PROBE_MAX_US - 10000) && g_maxHighUs <= (uint64_t)PROBE_MAX_US + 2000);
  CHECK_NEAR(m.rawRaw, 2047.5, 8.0);
  CHECK(!g_biasHigh);
}

static void test_never_stuck_high() {
  printf("[probe] BIAS nikdy nezůstane zapnutý a nesvítí déle než strop (i u driftující sondy)\n");
  fresh(10e3, 10e3);
  g_sim.Cdl = 20e-6;  // pomalá polarizace: napětí se plazí přes celý strop
  ProbeMeasurement m;
  CHECK(probeMeasureOnce(defaults(), 3.3f, m));
  CHECK(!g_biasHigh);
  CHECK(g_maxHighUs <= (uint64_t)PROBE_MAX_US + 2000);
  float t[48], v[48];
  fresh(10e3, 10e3);
  g_sim.Cdl = 20e-6;
  const size_t n = probeCaptureProfile(t, v, 48);
  CHECK(!g_biasHigh);
  CHECK(g_maxHighUs <= (uint64_t)PROBE_MAX_US + 2000);
  CHECK(n >= 10 && n <= 48);
  bool inc = true;
  for (size_t i = 1; i < n; i++)
    if (!(t[i] > t[i - 1])) inc = false;
  CHECK(inc);
  CHECK(n > 0 && t[n - 1] <= 301.0f && t[0] >= 0.0f);
  for (size_t i = 0; i < n; i++) CHECK(v[i] >= 0.0f && v[i] <= 4095.0f);
}

static void test_median_of_pulses() {
  printf("[probe] medián ze tří pulzů a pauzy mezi nimi\n");
  fresh(10e3, 10e3);
  g_rsPerPulse = {10e3, 40e3, 4e3};  // 2047,5 | 3276 | 1170 -> medián je ten první
  ProbeMeasurement m;
  CHECK(probeMeasureMedian(defaults(), 3.3f, 3, m));
  CHECK(g_pulses == 3);
  CHECK_NEAR(m.rawRaw, 2047.5, 12.0);
  CHECK(m.settled);
  CHECK(g_onTimes.size() == 3);
  if (g_onTimes.size() == 3) {
    CHECK(g_onTimes[1] - g_onTimes[0] >= (uint64_t)PROBE_REPEAT_GAP_MS * 1000ULL);
    CHECK(g_onTimes[2] - g_onTimes[1] >= (uint64_t)PROBE_REPEAT_GAP_MS * 1000ULL);
  }
  CHECK(!g_biasHigh);

  // počet pulzů se ořeže na 1..5
  fresh(10e3, 10e3);
  CHECK(probeMeasureMedian(defaults(), 3.3f, 0, m));
  CHECK(g_pulses == 1);
  fresh(10e3, 10e3);
  CHECK(probeMeasureMedian(defaults(), 3.3f, 99, m));
  CHECK(g_pulses == 5);

  // sudý počet: vybere se celé měření nejblíž hornímu ze dvou prostředních (žádná smyšlená hodnota)
  fresh(10e3, 10e3);
  g_rsPerPulse = {10e3, 10e3, 30e3, 30e3};  // 2047,5 2047,5 3071 3071
  CHECK(probeMeasureMedian(defaults(), 3.3f, 4, m));
  CHECK_NEAR(m.rawRaw, 3071.25, 15.0);
}

static void test_predischarge() {
  printf("[probe] před pulzem se elektrody vybijí, když předchozí pulz byl nedávno\n");
  fresh(10e3, 10e3);
  ProbeMeasurement m;
  probeMeasureOnce(defaults(), 3.3f, m);  // první pulz
  const uint64_t end1 = g_lastLowUs;
  probeMeasureOnce(defaults(), 3.3f, m);  // hned další
  CHECK(g_onTimes.size() == 2);
  if (g_onTimes.size() == 2) CHECK(g_onTimes[1] - end1 >= (uint64_t)PROBE_PREDISCHARGE_MS * 1000ULL);
  // po dlouhé pauze se vybíjení nezdržuje
  g_us += 10ULL * 1000000ULL;
  const uint64_t before = g_us;
  probeMeasureOnce(defaults(), 3.3f, m);
  CHECK(g_onTimes.size() == 3);
  if (g_onTimes.size() == 3) CHECK(g_onTimes[2] - before < (uint64_t)PROBE_PREDISCHARGE_MS * 1000ULL);
}

static void test_flags() {
  printf("[probe] příznaky: zkrat a sonda ve vzduchu\n");
  ProbeMeasurement m;
  fresh(10e3, 1.0);  // zkrat
  CHECK(probeMeasureOnce(defaults(), 3.3f, m));
  CHECK(m.flags & moist::FLAG_SAT_LOW);
  CHECK(!g_biasHigh);
  fresh(10e3, 1e12);  // vzduch
  CHECK(probeMeasureOnce(defaults(), 3.3f, m));
  CHECK(m.flags & moist::FLAG_SAT_HIGH);
  CHECK(!g_biasHigh);
}

static void test_vexc_normalization() {
  printf("[probe] normalizace na napětí buzení je volitelná\n");
  fresh(10e3, 10e3);
  Settings s = defaults();
  ProbeMeasurement a, b;
  s.vexcTrack = false;
  probeMeasureOnce(s, 2.97f, a);
  fresh(10e3, 10e3);
  s.vexcTrack = true;
  probeMeasureOnce(s, 2.97f, b);
  CHECK_NEAR(a.raw, a.rawRaw, 1e-3);        // bez korekce = surová hodnota
  CHECK(b.raw > b.rawRaw);                  // buzení 2,97 V < 3,3 V -> čítač se přepočte nahoru
  CHECK_NEAR(b.raw / b.rawRaw, 3.3 / 2.97, 0.01);
}


static bool anyPinHigh() {
  for (int p = 0; p < 64; p++)
    if (g_pinHigh[p]) return true;
  return false;
}

static void test_wiring_finds_pin() {
  printf("[probe] test zapojení najde pin, který opravdu buzí dělič (vzduch, zemina, každý kandidát)\n");
  struct Env {
    double Rs;
    float minHi;
  };
  const Env envs[] = {{1e12, 4000.0f}, {300e3, 3800.0f}, {10e3, 1900.0f}, {1e3, 300.0f}};  // vzduch, suchá, střední, mokrá
  const uint8_t pins[] = {32, 26};
  for (const Env& e : envs) {
    for (uint8_t real : pins) {
      fresh(10e3, e.Rs);
      g_realBias = real;
      wiring::Sample out[3];
      const uint64_t t0 = g_us;
      const size_t n = probeWiringTest(out, 3, false, 0);
      CHECK(n == 2);
      const wiring::Result r = wiring::choose(out, n, 0);
      CHECK(r.verdict == wiring::V_FOUND);
      CHECK(r.pin == real);
      for (size_t i = 0; i < n; i++)
        if (out[i].pin == real) CHECK(out[i].hi >= e.minHi && out[i].lo < 10.0f);
      CHECK(g_us - t0 < 400000ULL);    // celý test trvá desítky ms, ne vteřiny
      CHECK(!anyPinHigh());            // po testu nesvítí žádný pin
      CHECK(!g_biasHigh);
    }
  }
}

static void test_wiring_lazy_and_cleanup() {
  printf("[probe] test zapojení: jen GPIO32 a GPIO26 (GPIO25 se nikdy nebudí), po testu se kandidáti pustí\n");
  wiring::Sample out[3];
  for (uint8_t real : {(uint8_t)32, (uint8_t)26}) {
    fresh(10e3, 1e12);
    g_realBias = real;
    const size_t n = probeWiringTest(out, 3, true, 0);
    CHECK(n == 2);                                    // pravý pin je jasný z prvních dvou kandidátů
    CHECK(wiring::choose(out, n, 0).pin == real);
    CHECK(g_pinModeSet[25] == 0);                     // na GPIO25 se vůbec nesáhlo
  }
  fresh(10e3, 1e12);
  g_realBias = 25;
  size_t n = probeWiringTest(out, 3, true, 0);
  CHECK(n == 2 && wiring::choose(out, n, 0).verdict == wiring::V_NONE);  // GPIO25 se už nezkouší
  CHECK(g_pinModeSet[25] == 0);

  // úklid: používaný pin (výchozí 32) zůstane výstupem v LOW, ostatní kandidáti jsou vstupy
  fresh(10e3, 1e12);
  g_realBias = 26;
  n = probeWiringTest(out, 3, false, 0);
  CHECK(g_pinModeSet[SONDA_BIAS_PIN] == OUTPUT && !g_pinHigh[SONDA_BIAS_PIN]);
  for (uint8_t p : {(uint8_t)32, (uint8_t)26})
    if (p != SONDA_BIAS_PIN) CHECK(g_pinModeSet[p] == INPUT);
  CHECK(g_pinModeSet[25] == 0);

  // nic nereaguje (zkrat, jiný pin ADC...): žádný nález, ale taky nic nezůstane zapnuté
  fresh(10e3, 1.0);
  n = probeWiringTest(out, 3, false, 0);
  CHECK(n == 2 && wiring::choose(out, n, 0).verdict == wiring::V_NONE);
  CHECK(!anyPinHigh());
  fresh(10e3, 1e12);
  g_realBias = 7;  // BIAS je na pinu, který firmware nezná
  n = probeWiringTest(out, 3, false, 0);
  CHECK(n == 2 && wiring::choose(out, n, 0).verdict == wiring::V_NONE);
  fresh(10e3, 1e12);
  g_floating = 60;  // uzel visí
  n = probeWiringTest(out, 3, false, 0);
  CHECK(n == 2 && wiring::choose(out, n, 0).verdict == wiring::V_NONE);

  // neplatné vstupy
  CHECK(probeWiringTest(nullptr, 3, false, 0) == 0);
  CHECK(probeWiringTest(out, 0, false, 0) == 0);
  fresh(10e3, 1e12);
  g_realBias = 26;
  CHECK(probeWiringTest(out, 1, false, 0) == 1);  // jen první kandidát (32)
}

static void test_use_pin() {
  printf("[probe] přepnutí pinu BIAS: měření buzí nový pin, starý se pustí, neplatný pin se nepřijme\n");
  fresh(10e3, 10e3);
  probeUsePin(26);
  g_realBias = 26;
  CHECK(probeBiasPin() == 26);
  CHECK(g_pinModeSet[26] == OUTPUT && g_pinModeSet[32] == INPUT);
  ProbeMeasurement m;
  CHECK(probeMeasureOnce(defaults(), 3.3f, m));
  CHECK(g_pulses == 1);
  CHECK_NEAR(m.rawRaw, 2047.5, 12.0);
  CHECK(m.flags == 0 && !isnan(m.percent) && !moist::isFault(m.flags));
  CHECK(!g_pinHigh[26] && !g_pinHigh[32]);
  float t[48], v[48];
  fresh(10e3, 10e3);
  probeUsePin(26);
  g_realBias = 26;
  CHECK(probeCaptureProfile(t, v, 48) >= 10);  // i profil buzí nový pin
  CHECK(!g_pinHigh[26]);
  // neplatný pin (LED) se nepřijme, použije se pin ze sestavení a na LED se nesáhne
  fresh(10e3, 10e3);
  probeUsePin(13);
  CHECK(probeBiasPin() == SONDA_BIAS_PIN && g_pinModeSet[13] == 0);
  probeUsePin(0);
  CHECK(probeBiasPin() == SONDA_BIAS_PIN);
}

static void test_fault_flags() {
  printf("[probe] zkrat / uzel nereaguje na BIAS = chyba bez procent; vzduch a mokrá zemina chyba není\n");
  ProbeMeasurement m;
  // 1) špatný pin: BIAS nic nebudí, uzel visí na nenulové hodnotě (přesně to, co se stalo na desce s BIAS na GPIO26)
  fresh(10e3, 10e3);
  g_realBias = 7;
  g_floating = 60;
  CHECK(probeMeasureOnce(defaults(), 3.3f, m));
  CHECK((m.flags & moist::FLAG_NO_RESP) && !(m.flags & moist::FLAG_SAT_LOW));
  CHECK(moist::isFault(m.flags) && isnan(m.percent));
  CHECK_NEAR(m.base, 60.0, 0.5);
  // 2) totéž, uzel visí na ~0: hlásí se jako 0 V
  fresh(10e3, 10e3);
  g_realBias = 7;
  g_floating = 0;
  CHECK(probeMeasureOnce(defaults(), 3.3f, m));
  CHECK((m.flags & moist::FLAG_SAT_LOW) && moist::isFault(m.flags) && isnan(m.percent));
  // 3) uzel držený nahoře
  fresh(10e3, 10e3);
  g_realBias = 7;
  g_floating = 4095;
  CHECK(probeMeasureOnce(defaults(), 3.3f, m));
  CHECK((m.flags & moist::FLAG_NO_RESP) && moist::isFault(m.flags) && isnan(m.percent));
  // 4) skutečný zkrat při správném pinu
  fresh(10e3, 1.0);
  CHECK(probeMeasureOnce(defaults(), 3.3f, m));
  CHECK((m.flags & moist::FLAG_SAT_LOW) && moist::isFault(m.flags) && isnan(m.percent));
  // 4b) extrémně zalitá zem (čítač ~16, jako květináč hned po zalití): NENÍ zkrat, ukáže se vlhkost
  for (double rs : {40.0, 60.0, 120.0}) {
    fresh(10e3, rs);
    CHECK(probeMeasureOnce(defaults(), 3.3f, m));
    CHECK(m.rawRaw > 0.5f && m.rawRaw < 60.0f);
    CHECK(!moist::isFault(m.flags) && !isnan(m.percent) && m.percent > 90.0f);
  }
  // 5) sonda ve vzduchu při správném pinu: NENÍ porucha, 0 %
  fresh(10e3, 1e12);
  CHECK(probeMeasureOnce(defaults(), 3.3f, m));
  CHECK((m.flags & moist::FLAG_SAT_HIGH) && !moist::isFault(m.flags));
  CHECK(!isnan(m.percent) && m.percent < 1.0f);
  // 6) mokrá zemina (1 kΩ): žádná porucha, skoro 100 %
  fresh(10e3, 1e3);
  CHECK(probeMeasureOnce(defaults(), 3.3f, m));
  CHECK(!moist::isFault(m.flags) && !isnan(m.percent) && m.percent > 90.0f);
  // 7) medián ze tří pulzů nad nereagujícím uzlem
  fresh(10e3, 10e3);
  g_realBias = 7;
  g_floating = 60;
  CHECK(probeMeasureMedian(defaults(), 3.3f, 3, m));
  CHECK((m.flags & moist::FLAG_NO_RESP) && isnan(m.percent));
  // 8) zdravá sonda: základ uzlu před pulzem je ~0
  fresh(10e3, 10e3);
  CHECK(probeMeasureOnce(defaults(), 3.3f, m));
  CHECK(m.base < 5.0f && !moist::isFault(m.flags));
  // 9) velmi mokrá zemina těsně nad mezí (Rs 200 Ω): ještě zdravé měření (odezva ~80 čítačů)
  fresh(10e3, 200.0);
  CHECK(probeMeasureOnce(defaults(), 3.3f, m));
  CHECK(!moist::isFault(m.flags) && !isnan(m.percent) && m.percent > 99.0f);
}

static Settings dualSet(uint8_t ret) {
  Settings s = defaults();
  s.retPin = ret;
  return s;
}

static void test_dual_polarity() {
  printf("[probe] obrácená polarita: poměr obou fází, suchá zemina z fáze B, nepřepojená elektroda, odpojení\n");
  ProbeMeasurement m;
  // 1) střední zemina (10 kΩ): obě fáze v lineárním okně -> g = b / a = R / Rs = 1
  fresh(10e3, 10e3);
  g_realRet = 14;
  probeUseRetPin(14);
  CHECK(probeMeasureOnce(dualSet(14), 3.3f, m));
  CHECK(m.phase == moist::PH_RATIO);
  CHECK_NEAR(m.g, 1.0, 0.02);
  CHECK_NEAR(m.rawB, 2047.5, 3.0);
  CHECK_NEAR(m.raw, 4095.0 / 2.0, 25.0);
  CHECK(!(m.flags & moist::FLAG_RET_BAD) && !moist::isFault(m.flags));
  CHECK(!g_bothHigh && !g_biasHigh && !g_pinHigh[14]);
  CHECK(g_pinModeSet[14] == INPUT);  // po měření je sonda odpojená (vysoká impedance)
  // 2) suchá zemina (200 kΩ): fáze A je nad lineárním oknem, fáze B uprostřed -> g z B, R/Rs = 0,05
  fresh(10e3, 200e3);
  g_realRet = 14;
  probeUseRetPin(14);
  CHECK(probeMeasureOnce(dualSet(14), 3.3f, m));
  CHECK(m.phase == moist::PH_B);
  CHECK_NEAR(m.g, 0.05, 0.004);
  CHECK(!(m.flags & moist::FLAG_SAT_HIGH));  // suchá, ale ne "naprázdno"
  // 3) velmi suchá (2 MΩ): b ~ 20 pod oknem -> zůstává fáze A, bez chyby
  fresh(10e3, 2e6);
  g_realRet = 14;
  probeUseRetPin(14);
  CHECK(probeMeasureOnce(dualSet(14), 3.3f, m));
  CHECK(m.phase == moist::PH_A && !(m.flags & moist::FLAG_RET_BAD) && !moist::isFault(m.flags));
  // 4) mokrá (1 kΩ): fáze B u stropu -> dosavadní výpočet z A
  fresh(10e3, 1e3);
  g_realRet = 14;
  probeUseRetPin(14);
  CHECK(probeMeasureOnce(dualSet(14), 3.3f, m));
  CHECK(m.phase == moist::PH_A && m.percent > 90.0f && !(m.flags & moist::FLAG_RET_BAD));
  // 5) režim zapnutý, ale elektroda pořád na GND: fáze B nic -> příznak, hodnota z A platí
  fresh(10e3, 10e3);
  g_realRet = 0;  // elektroda zůstala na GND
  probeUseRetPin(14);
  CHECK(probeMeasureOnce(dualSet(14), 3.3f, m));
  CHECK((m.flags & moist::FLAG_RET_BAD) && m.phase == moist::PH_A);
  CHECK(!moist::isFault(m.flags) && !isnan(m.percent));
  CHECK_NEAR(m.rawRaw, 2047.5, 12.0);
  // 6) sonda ve vzduchu v režimu obrácené polarity: naprázdno, žádný falešný příznak
  fresh(10e3, 1e12);
  g_realRet = 14;
  probeUseRetPin(14);
  CHECK(probeMeasureOnce(dualSet(14), 3.3f, m));
  CHECK((m.flags & moist::FLAG_SAT_HIGH) && !(m.flags & moist::FLAG_RET_BAD) && m.percent < 1.0f);
  // 7) medián ze tří měření v režimu obrácené polarity + test zapojení nechá druhou elektrodu odpojenou
  fresh(10e3, 10e3);
  g_realRet = 14;
  probeUseRetPin(14);
  CHECK(probeMeasureMedian(dualSet(14), 3.3f, 3, m));
  CHECK(m.phase == moist::PH_RATIO && g_pulses == 3);
  wiring::Sample smp[3];
  const size_t n = probeWiringTest(smp, 3, true, SONDA_BIAS_PIN);
  CHECK(wiring::choose(smp, n, SONDA_BIAS_PIN).verdict == wiring::V_FOUND);  // s pinem jako "zemí" se BIAS najde
  CHECK(g_pinModeSet[14] == INPUT && !g_bothHigh);
  // 8) neplatný pin druhé elektrody se nikdy nebudí
  fresh(10e3, 10e3);
  probeUseRetPin(25);
  CHECK(probeRetPin() == 0);
  probeUseRetPin(0);
}

static void test_settings_params() {
  printf("[probe] parametry z nastavení: strop pulzu, vybití\n");
  ProbeMeasurement m;
  fresh(10e3, 10e3);
  Settings s = defaults();
  s.probeMode = 1;  // pevné okno = celý strop
  s.maxMs = 50;
  CHECK(probeMeasureOnce(s, 3.3f, m));
  CHECK(g_maxHighUs <= 52000ULL && g_maxHighUs >= 45000ULL);
  fresh(10e3, 10e3);
  g_sim.Cdl = 20e-6;  // plazí se: adaptivní měření skončí na stropu z nastavení, ne na 300 ms
  s.probeMode = 0;
  CHECK(probeMeasureOnce(s, 3.3f, m));
  CHECK(g_maxHighUs <= 52000ULL);
  fresh(10e3, 10e3);
  s = defaults();
  s.predischMs = 200;
  probeMeasureOnce(s, 3.3f, m);
  const uint64_t end1 = g_lastLowUs;
  probeMeasureOnce(s, 3.3f, m);
  CHECK(g_onTimes.size() == 2 && g_onTimes[1] - end1 >= 200000ULL);
}

static void test_short_pulse() {
  printf("[probe] krátký pulz: hodnota před polarizací, délka pulzu z nastavení\n");
  ProbeMeasurement m;
  // polarizující zemina (jako květináč u uživatele: 560 čítačů hned, přes 1100 po 300 ms)
  fresh(10e3, 1.6e3);
  g_sim.Cdl = 20e-6;
  Settings s = defaults();
  CHECK(s.probeMode == 2);  // výchozí (a od v5.1 jediný nastavitelný) režim je krátký pulz
  s.probeMode = 0;
  CHECK(probeMeasureOnce(s, 3.3f, m));
  const float adaptive = m.rawRaw;
  fresh(10e3, 1.6e3);
  g_sim.Cdl = 20e-6;
  s.probeMode = 2;
  s.earlyUs = 1000;
  CHECK(probeMeasureOnce(s, 3.3f, m));
  const double ohmic = 4095.0 * 1.6e3 / 11.6e3;  // ~565
  CHECK(m.settled && m.settleMs == 1);
  CHECK(fabs(m.rawRaw - ohmic) < 0.1 * ohmic);
  CHECK(adaptive > m.rawRaw + 20.0f);              // adaptivní měření je polarizací posunuté výš
  CHECK(g_maxHighUs <= 1300ULL && g_maxHighUs >= 950ULL);
  CHECK(!g_biasHigh);
  // krátký pulz i s obrácenou polaritou: obě fáze krátké, poměr ~ R / Rs
  fresh(10e3, 10e3);
  g_realRet = 14;
  probeUseRetPin(14);
  s.retPin = 14;
  CHECK(probeMeasureOnce(s, 3.3f, m));
  CHECK(m.phase == moist::PH_RATIO);
  CHECK_NEAR(m.g, 1.0, 0.05);
  CHECK(g_maxHighUs <= 1300ULL);
}

int main() {
  test_single_pulse();
  test_fixed_mode_like_v3();
  test_never_stuck_high();
  test_median_of_pulses();
  test_predischarge();
  test_flags();
  test_vexc_normalization();
  test_wiring_finds_pin();
  test_wiring_lazy_and_cleanup();
  test_use_pin();
  test_fault_flags();
  test_dual_polarity();
  test_settings_params();
  test_short_pulse();
  printf("\n== probe: %d OK, %d FAIL ==\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
