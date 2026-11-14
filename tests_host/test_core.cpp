// Host testy čistého jádra (moisture, logfmt, battery_curve, timeutil, settings).
// Sestavení a spuštění:  tools/run_host_tests.sh
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <string>
#include <vector>

#include "../src/battery_curve.h"
#include "../src/logfmt.h"
#include "../src/moisture.h"
#include "../src/settings.h"
#include "../src/timeutil.h"
#include "sim.h"

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                          \
  do {                                                                       \
    if (cond) {                                                              \
      g_pass++;                                                              \
    } else {                                                                 \
      g_fail++;                                                              \
      printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);               \
    }                                                                        \
  } while (0)
#define CHECK_NEAR(a, b, tol)                                                                    \
  do {                                                                                           \
    double _a = (a), _b = (b);                                                                   \
    if (fabs(_a - _b) <= (tol)) {                                                                \
      g_pass++;                                                                                  \
    } else {                                                                                     \
      g_fail++;                                                                                  \
      printf("  FAIL %s:%d  %s=%.4f  %s=%.4f  (tol %.4f)\n", __FILE__, __LINE__, #a, _a, #b, _b, (double)(tol)); \
    }                                                                                            \
  } while (0)

using namespace moist;

// Spustí jedno "měření" na simulované sondě stejnou smyčkou, jakou používá firmware.
static SettleResult runCapture(SimProbe& sim, const SettleParams& p) {
  sim.reset();
  SettleDetector det(p);
  double t = 0.0;
  // 3 zahazovací čtení před sepnutím nesimulujeme; BIAS se sepne v t = 0
  long guard = 0;
  while (!det.feed((uint32_t)t, sim.sample(t))) {
    t += sim.adcTimeUs + det.gapUs();
    if (++guard > 5000000) break;
  }
  return det.result();
}

// ---------------------------------------------------------------------------
static void test_settle_fast_node() {
  printf("[settle] rychlý uzel, bez polarizace\n");
  SimProbe s;
  s.R = 10e3; s.Rs = 10e3; s.noise = 4.0;
  SettleParams p;
  auto r = runCapture(s, p);
  CHECK(r.settled);
  CHECK_NEAR(r.value, 2047.5, 6.0);
  CHECK(r.tSettleUs <= 6000);
}

static void test_settle_rc_node() {
  printf("[settle] uzel s kondenzátorem (tau ~ 10 ms) – nesmí ustálit předčasně\n");
  SimProbe s;
  s.R = 10e3; s.Rs = 10e3; s.Cn = 2e-6; s.noise = 3.0;
  SettleParams p;
  auto r = runCapture(s, p);
  CHECK(r.settled);
  CHECK_NEAR(r.value, 2047.5, 10.0);
  CHECK(r.tSettleUs >= 24000);  // ne dřív než ~2.4 tau
  printf("    value=%.1f tSettle=%u us\n", r.value, (unsigned)r.tSettleUs);
}

static void test_settle_polarization_early_plateau() {
  printf("[settle] polarizace elektrod: auto okno vs. pevné okno 300 ms\n");
  SimProbe s;
  s.R = 10e3; s.Rs = 50e3; s.Cdl = 10e-6; s.noise = 3.0;
  const double early = 4095.0 * (1.0 - (10e3 / 60e3) * exp(-0.0045 / 0.6));  // odpor roztoku
  SettleParams a;
  auto ra = runCapture(s, a);
  SettleParams f;
  f.stopEarly = false;
  auto rf = runCapture(s, f);
  printf("    auto: %.1f (t=%.1f ms)   pevné 300ms: %.1f   odhad pro čistý roztok: %.1f\n", ra.value,
         ra.tSettleUs / 1000.0, rf.value, early);
  CHECK(ra.settled);
  CHECK(ra.tSettleUs <= 12000);
  CHECK_NEAR(ra.value, early, 20.0);
  CHECK(rf.value > ra.value + 100.0);  // pevné okno je zkreslené polarizací
}

static void test_settle_noise_and_spikes() {
  printf("[settle] šum + rušivé špičky (Wi-Fi)\n");
  int ok = 0, settled = 0;
  std::vector<double> errs;
  for (int i = 0; i < 200; i++) {
    SimProbe s;
    s.rng.seed(1000 + i);
    s.R = 10e3; s.Rs = 10e3; s.noise = 8.0; s.spikeProb = 0.05; s.spikeAmp = 400.0;
    SettleParams p;
    auto r = runCapture(s, p);
    errs.push_back(fabs(r.value - 2047.5));
    if (r.settled) settled++;
    if (fabs(r.value - 2047.5) <= 8.0) ok++;
  }
  std::sort(errs.begin(), errs.end());
  printf("    settled %d/200, |chyba|<=8: %d/200, p95 chyba=%.1f, max=%.1f\n", settled, ok, errs[190], errs.back());
  CHECK(settled >= 160);
  CHECK(errs[190] <= 12.0);
}

static void test_settle_drift_never_settles() {
  printf("[settle] trvalý drift -> strop, příznak UNSETTLED\n");
  SimProbe s;
  s.R = 10e3; s.Rs = 10e3; s.rampPerMs = 8.0;
  SettleParams p;
  auto r = runCapture(s, p);
  CHECK(!r.settled);
  CHECK(r.flags & FLAG_UNSETTLED);
  CHECK(r.tSettleUs >= 192000);
}

static void test_settle_slow_adc() {
  printf("[settle] velmi pomalé ADC (400 µs / vzorek) – okna se spojí\n");
  SimProbe s;
  s.R = 10e3; s.Rs = 10e3; s.noise = 3.0; s.adcTimeUs = 400;
  SettleParams p;
  auto r = runCapture(s, p);
  CHECK(r.n >= 10);
  CHECK_NEAR(r.value, 2047.5, 12.0);
}

static void test_settle_fixed_mode_runs_to_cap() {
  printf("[settle] stopEarly=false měří až do stropu\n");
  SimProbe s;
  s.R = 10e3; s.Rs = 10e3; s.noise = 3.0;
  SettleParams p;
  p.stopEarly = false;
  auto r = runCapture(s, p);
  CHECK(r.tSettleUs >= 192000);
  CHECK(r.settled);  // poslední dvě okna se shodují
  CHECK_NEAR(r.value, 2047.5, 6.0);
}

static void test_settle_degenerate() {
  printf("[settle] degenerované vstupy\n");
  {
    SettleDetector d;
    d.finish();
    CHECK(d.done());
    CHECK(d.result().flags & FLAG_UNSETTLED);
  }
  {
    SettleDetector d;
    for (int i = 0; i < 4; i++) d.feed(2000 + i * 100, 1234);
    d.finish();
    CHECK(d.done());
    CHECK_NEAR(d.result().value, 1234.0, 0.1);
  }
  {
    // vzorky před t0 se ignorují
    SettleDetector d;
    CHECK(!d.feed(100, 1));
    CHECK(!d.feed(1000, 1));
  }
}

// ---------------------------------------------------------------------------
static void test_conversion() {
  printf("[conv] kalibrace a převod na procenta\n");
  Calib c;  // výchozí: gDry 0.05, gWet 3.0, rawOpen 4095, log
  CHECK(calibValid(c));
  CHECK_NEAR(percentFromRaw(3900.0f, c), 0.0, 0.5);
  CHECK_NEAR(percentFromRaw(1023.75f, c), 100.0, 0.5);
  CHECK(percentFromRaw(4095.0f, c) == 0.0f);   // naprázdno -> 0 %
  CHECK(percentFromRaw(5.0f, c) == 100.0f);    // zkrat -> 100 %
  float prev = 101.0f;
  bool mono = true;
  for (int raw = 15; raw <= 4090; raw += 5) {
    float p = percentFromRaw((float)raw, c);
    if (p > prev + 1e-3f) mono = false;
    prev = p;
  }
  CHECK(mono);
  // uprostřed log škály
  const float gMid = sqrtf(c.gDry * c.gWet);
  const float rawMid = c.rawOpen / (1.0f + gMid);
  CHECK_NEAR(percentFromRaw(rawMid, c), 50.0, 0.5);

  // vlastní kalibrace: suché pole (raw 3300) .. mokré (raw 1500)
  Calib f;
  f.rawOpen = 3480;
  f.gDry = conductance(3300.0f, 3480.0f);
  f.gWet = conductance(1500.0f, 3480.0f);
  CHECK(calibValid(f));
  CHECK_NEAR(percentFromRaw(3300.0f, f), 0.0, 0.01);
  CHECK_NEAR(percentFromRaw(1500.0f, f), 100.0, 0.01);
  CHECK(percentFromRaw(2400.0f, f) > 0.0f && percentFromRaw(2400.0f, f) < 100.0f);

  // legacy
  Calib l;
  l.scale = 0;
  CHECK_NEAR(percentFromRaw(4095.0f, l), 0.0, 1e-3);
  CHECK_NEAR(percentFromRaw(2047.5f, l), 50.0, 1e-3);
  CHECK_NEAR(percentFromRaw(0.0f, l), 100.0, 1e-3);

  // poškozená kalibrace -> výchozí
  Calib bad;
  bad.gDry = -1; bad.gWet = 0;
  CHECK(!calibValid(bad));
  CHECK_NEAR(percentFromRaw(3900.0f, bad), 0.0, 0.5);

  // normalizace buzení
  CHECK_NEAR(normalizeRaw(2000.0f, 3.30f, 3.30f), 2000.0, 1e-3);
  CHECK_NEAR(normalizeRaw(2000.0f, 3.00f, 3.30f), 2200.0, 1e-2);
  CHECK_NEAR(normalizeRaw(2000.0f, 0.0f, 3.30f), 2000.0, 1e-3);   // ochrana
  CHECK_NEAR(normalizeRaw(2000.0f, 9.0f, 3.30f), 2000.0, 1e-3);   // ochrana
  CHECK_NEAR(normalizeRaw(2000.0f, NAN, 3.30f), 2000.0, 1e-3);    // NaN z měření baterie nesmí zničit čítač
  CHECK_NEAR(normalizeRaw(2000.0f, 3.60f, 3.30f), 2000.0 * 3.30 / 3.60, 1e-2);  // plný článek: čítač se zmenší
  // plně nasycený čítač (uzel nad rozsahem ADC) se při plném článku NESMÍ zmenšit: sonda ve vzduchu zůstane "sucho"
  CHECK_NEAR(normalizeRaw(4095.0f, 3.50f, 3.30f), 4095.0, 1e-3);
  CHECK_NEAR(normalizeRaw(4085.0f, 3.50f, 3.30f), 4085.0, 1e-3);
  CHECK(percentFromRaw(normalizeRaw(4095.0f, 3.50f, 3.30f), Calib()) == 0.0f);
  // slabší článek: čítač se zvětší, ale nikdy nad 4095 (jinak by záporná vodivost -> neplatná hodnota)
  CHECK_NEAR(normalizeRaw(3900.0f, 3.00f, 3.30f), 4095.0, 1e-3);
  CHECK(normalizeRaw(4079.0f, 3.00f, 3.30f) <= 4095.0f);
  // odstup od sytosti: normalizace dává při různém napětí článku stejné procento (lineární část ADC)
  {
    const Calib c;  // výchozí
    const float rho = 0.5f;  // uzel = polovina buzení
    float pcts[3];
    const float vb[3] = {3.00f, 3.30f, 3.45f};
    for (int i = 0; i < 3; i++) {
      const float raw = 4095.0f * (vb[i] / 3.30f) * rho;  // ADC: čítač ~ napětí na uzlu (pevná reference)
      pcts[i] = percentFromRaw(normalizeRaw(raw, vb[i], 3.30f), c);
    }
    CHECK_NEAR(pcts[0], pcts[1], 0.05);
    CHECK_NEAR(pcts[2], pcts[1], 0.05);
  }

  // kalibrační body
  {
    Calib c;
    CHECK(setCalPoint(c, false, 0.20f) && c.gDry == 0.20f && c.gWet == 3.0f);     // běžné "sucho"
    CHECK(setCalPoint(c, true, 5.0f) && c.gWet == 5.0f);                           // běžné "mokro"
    CHECK(setCalPoint(c, false, 0.001f) && c.gDry == kDryAnchorMin);               // nasycený ADC: spodní mez
    CHECK(setCalPoint(c, false, 0.0f) == false && c.gDry == kDryAnchorMin);        // nesmysl: beze změny
    CHECK(setCalPoint(c, false, NAN) == false);
    CHECK(setCalPoint(c, false, -1.0f) == false);
    const Calib before = c;
    CHECK(setCalPoint(c, true, 0.04f) == false);                                    // mokro méně vodivé než sucho
    CHECK(c.gDry == before.gDry && c.gWet == before.gWet);                          // neplatné se nezapíše
    CHECK(setCalPoint(c, false, 6.0f) == false);                                    // sucho vodivější než mokro
    CHECK(c.gDry == before.gDry && c.gWet == before.gWet);
    CHECK(setCalPoint(c, true, 5000.0f) == false);                                  // mimo meze
    // s dolní mezí zůstává škála rozumná: g=0,3 (zemina) vychází hluboko nad suchem, ne na 90 %
    Calib d;
    setCalPoint(d, false, 0.001f);
    const float raw = 4095.0f / (1.0f + 0.3f);  // g = 0,3
    CHECK(percentFromRaw(raw, d) < 60.0f);
  }

  CHECK(isOpenCircuit(4090.0f, Calib()));
  CHECK(!isOpenCircuit(3000.0f, Calib()));
  CHECK(isShort(0.0f));
  CHECK(!isShort(1.0f) && !isShort(3.0f) && !isShort(16.0f) && !isShort(300.0f));  // pár čítačů = velmi mokro, ne zkrat
  // odezva na BIAS: velmi mokrá zem (plató 16 nad nulou) reaguje; uzel visící na 200/60 bez nárůstu ne
  CHECK(!isNoResponse(16.0f, 0.0f) && !isNoResponse(4.0f, 0.0f) && !isNoResponse(300.0f, 2.0f));
  CHECK(isNoResponse(200.0f, 198.0f) && isNoResponse(60.0f, 60.0f) && isNoResponse(16.0f, 12.0f));
  CHECK(isNoResponse(0.0f, 0.0f) && isNoResponse(NAN, 0.0f) && isNoResponse(30.0f, NAN));
}

static void test_confirm() {
  printf("[confirm] ověřování podezřelých měření\n");
  CHECK(!needsConfirm(0, 2870.0f, 2865.0f, 150.0f));          // stabilní -> bez ověřování
  CHECK(needsConfirm(0, 2870.0f, 2600.0f, 150.0f));           // skok +270 -> ověřit
  CHECK(needsConfirm(0, 2400.0f, 2600.0f, 150.0f));           // skok -200 -> ověřit
  CHECK(!needsConfirm(0, 2700.0f, 2600.0f, 150.0f));          // změna 100 < 150
  CHECK(needsConfirm(FLAG_NOISY, 2870.0f, 2870.0f, 150.0f));  // rušení -> ověřit
  CHECK(!needsConfirm(FLAG_UNSETTLED, 2870.0f, 2870.0f, 150.0f));  // samotné neustálení nestačí
  CHECK(!needsConfirm(0, 2870.0f, 0.0f, 150.0f));             // žádný předchozí záznam
  CHECK(!needsConfirm(FLAG_SAT_HIGH, 4095.0f, 4095.0f, 150.0f));
}

static void test_profile() {
  printf("[profile] logaritmické koše\n");
  std::vector<uint32_t> t;
  std::vector<uint16_t> v;
  for (uint32_t us = 200; us < 300000; us += (us < 4000 ? 40 : us / 16)) {
    t.push_back(us);
    v.push_back((uint16_t)(1000 + (us > 50000 ? 100 : 0)));
  }
  float ot[64], ov[64];
  size_t n = buildProfile(t.data(), v.data(), t.size(), ot, ov, 64);
  CHECK(n >= 15 && n <= 40);
  bool inc = true;
  for (size_t i = 1; i < n; i++) if (ot[i] <= ot[i - 1]) inc = false;
  CHECK(inc);
  CHECK_NEAR(ov[0], 1000.0, 1e-6);
  CHECK_NEAR(ov[n - 1], 1100.0, 1e-6);
}

// ---------------------------------------------------------------------------
static void test_battery_curve() {
  printf("[bat] křivka LiFePO4\n");
  CHECK(lifepo4Percent(NAN) == -1);
  CHECK(lifepo4Percent(2.40f) == 0);
  CHECK(lifepo4Percent(3.60f) == 100);
  CHECK(lifepo4Percent(3.30f) == 70);
  CHECK(lifepo4Percent(3.00f) == 10);
  CHECK(lifepo4Percent(3.10f) >= 14 && lifepo4Percent(3.10f) <= 16);
  int prev = -1;
  bool mono = true;
  for (float v = 2.4f; v <= 3.5f; v += 0.005f) {
    int p = lifepo4Percent(v);
    if (p < prev) mono = false;
    prev = p;
  }
  CHECK(mono);
}

static void test_logfmt() {
  printf("[log] formát řádku\n");
  logfmt::Record r;
  r.year = 2026; r.month = 11; r.day = 2; r.hour = 17; r.minute = 3;
  r.hum = 45; r.temp = 21.3f; r.vbat = 3.254f; r.raw = 2873; r.settleMs = 12; r.settled = true;
  char b[128];
  int n = logfmt::formatLine(b, sizeof b, r);
  CHECK(n > 0);
  CHECK(std::string(b) == "2611021703 45 21 325 s12\r\n");

  r.settled = false; r.timeOk = false; r.lowBat = true; r.temp = -5.25f;
  n = logfmt::formatLine(b, sizeof b, r);
  CHECK(std::string(b) == "2611021703 45 -5 325 s12u ? !\r\n");
  // krátký pulz (1 ms, ustálený) se nezapisuje, místo 1 také ne: nejkratší běžný řádek
  r.settled = true; r.timeOk = true; r.lowBat = false; r.temp = 21.3f; r.settleMs = 1; r.loc = 1;
  n = logfmt::formatLine(b, sizeof b, r);
  CHECK(std::string(b) == "2611021703 45 21 325\r\n" && n == 22);
  r.temp = 21.6f;
  n = logfmt::formatLine(b, sizeof b, r);
  CHECK(std::string(b) == "2611021703 45 22 325\r\n");  // zaokrouhlení na celé °C
  // přelitá půda (v7.0): vlhkost neznámá, značka F
  r.hum = -1; r.flood = true;
  n = logfmt::formatLine(b, sizeof b, r);
  CHECK(std::string(b) == "2611021703 - 22 325 F\r\n");
  r.hum = 45; r.flood = false;
  CHECK(moist::isFlood(moist::FLAG_SAT_LOW, true));
  CHECK(!moist::isFlood(moist::FLAG_SAT_LOW, false));  // neověřené zapojení: jen chyba sondy
  CHECK(!moist::isFlood(moist::FLAG_SAT_LOW | moist::FLAG_NO_RESP, true));
  CHECK(!moist::isFlood(moist::FLAG_SAT_HIGH, true));
  r.temp = 21.3f;
  r.settleMs = 12; r.loc = -1;

  logfmt::Record e;
  e.year = 2026; e.month = 1; e.day = 5; e.hour = 0; e.minute = 0;
  n = logfmt::formatLine(b, sizeof b, e);  // všechno neznámé
  CHECK(std::string(b) == "2601050000 - - -\r\n");

  char small[20];
  CHECK(logfmt::formatLine(small, sizeof small, r) == -1);

  // zpětná kompatibilita: v3 sloupce na stejných místech (0..4)
  std::string line = "2026-11-02 17:03 45% 21.3C 3.25V r2873 s12\r\n";
  int tokens = 0;
  for (size_t i = 0; i < line.size(); i++) if (line[i] == ' ') tokens++;
  CHECK(tokens >= 4);
}

// Zdroj/ cíl pro compact() nad pamětí
struct MemReader {
  const std::string& s;
  size_t pos = 0;
  explicit MemReader(const std::string& str) : s(str) {}
  bool readLine(char* buf, size_t cap, size_t& rawLen) {
    if (pos >= s.size()) return false;
    size_t start = pos, len = 0;
    while (pos < s.size()) {
      char c = s[pos++];
      if (len + 1 < cap) buf[len++] = c;
      if (c == '\n') break;
    }
    buf[len] = 0;
    rawLen = pos - start;
    return true;
  }
};
struct MemWriter {
  std::string out;
  bool writeLine(const char* b, size_t len) { out.append(b, len); return true; }
};

static std::string makeLog(int lines) {
  std::string s;
  char b[128];
  for (int i = 0; i < lines; i++) {
    logfmt::Record r;
    r.year = 2026; r.month = 3 + (i / 3000); r.day = 1 + (i / 100) % 28; r.hour = (i / 4) % 24; r.minute = (i % 4) * 15;
    r.hum = i % 100; r.temp = 20.0f; r.vbat = 3.25f; r.raw = 2000 + i % 500; r.settleMs = 12;
    int n = logfmt::formatLine(b, sizeof b, r);
    s.append(b, (size_t)n);
  }
  return s;
}

// Přečíslování míst v logu (v6.7): smazání místa nesmí přesunout záznamy k jinému místu.
static std::string remapOne(const std::string& line, const uint8_t* map, uint8_t nOld) {
  char o[200];
  const int n = logfmt::remapPlaceLine(line.c_str(), line.size(), o, sizeof o, map, nOld);
  return n < 0 ? std::string("<ERR>") : std::string(o, (size_t)n);
}
static void test_place_remap() {
  printf("[log] přečíslování míst po smazání a přeuspořádání\n");
  uint8_t m[9];
  logfmt::placeMapDelete(m, 3, 1);  // 3 místa, smaže se druhé
  CHECK(m[1] == 1 && m[2] == 0 && m[3] == 2);
  // záznam bez p = místo 1: zůstane beze změny
  CHECK(remapOne("2026-11-02 17:00 45% 21.3C 3.25V r2873 s12\r\n", m, 3) == "2026-11-02 17:00 45% 21.3C 3.25V r2873 s12\r\n");
  // smazané místo -> p0, další o jedno níž, příznaky za p zůstanou
  CHECK(remapOne("2026-11-02 17:00 45% 21.3C 3.25V r2873 s12 p2 ? !\r\n", m, 3) == "2026-11-02 17:00 45% 21.3C 3.25V r2873 s12 p0 ? !\r\n");
  CHECK(remapOne("2026-11-02 17:00 45% 21.3C 3.25V r2873 s12 p3\r\n", m, 3) == "2026-11-02 17:00 45% 21.3C 3.25V r2873 s12 p2\r\n");
  CHECK(remapOne("2026-11-02 17:00 45% 21.3C 3.25V r2873 s12 p3\n", m, 3) == "2026-11-02 17:00 45% 21.3C 3.25V r2873 s12 p2\n");
  // smaže se první místo: záznam bez p (= místo 1) dostane p0, místo 2 se stane místem 1
  logfmt::placeMapDelete(m, 2, 0);
  CHECK(m[1] == 0 && m[2] == 1);
  CHECK(remapOne("2026-11-02 17:00 45% 21.3C 3.25V\r\n", m, 2) == "2026-11-02 17:00 45% 21.3C 3.25V p0\r\n");
  CHECK(remapOne("2026-11-02 17:00 --% 21.3C 2.68V p2 ! H\r\n", m, 2) == "2026-11-02 17:00 --% 21.3C 2.68V p1 ! H\r\n");
  // už smazané (p0), číslo mimo rozsah a řádky, které nejsou záznam, se nemění
  CHECK(remapOne("2026-11-02 17:00 45% 21.3C 3.25V p0\r\n", m, 2) == "2026-11-02 17:00 45% 21.3C 3.25V p0\r\n");
  CHECK(remapOne("2026-11-02 17:00 45% 21.3C 3.25V p7\r\n", m, 2) == "2026-11-02 17:00 45% 21.3C 3.25V p7\r\n");
  CHECK(remapOne("# hlavicka p2\r\n", m, 2) == "# hlavicka p2\r\n");
  // "p" uvnitř jiného tokenu se nebere (jen samostatné " pN")
  CHECK(remapOne("2026-11-02 17:00 45% 21.3C 3.25V xp2\r\n", m, 2) == "2026-11-02 17:00 45% 21.3C 3.25V p0 xp2\r\n" ||
        remapOne("2026-11-02 17:00 45% 21.3C 3.25V xp2\r\n", m, 2) == "2026-11-02 17:00 45% 21.3C 3.25V xp2 p0\r\n");
  // přeuspořádání (Servis: tabulka): místa 1,2,3 -> nové pořadí 3,1 (místo 2 smazané)
  uint8_t r[9] = {0, 2, 0, 1};
  CHECK(remapOne("2026-11-02 17:00 45% 21.3C 3.25V\r\n", r, 3) == "2026-11-02 17:00 45% 21.3C 3.25V p2\r\n");
  CHECK(remapOne("2026-11-02 17:00 45% 21.3C 3.25V p3\r\n", r, 3) == "2026-11-02 17:00 45% 21.3C 3.25V p1\r\n");
  // malý výstupní buffer: chyba, ne přetečení
  char tiny[10];
  CHECK(logfmt::remapPlaceLine("2026-11-02 17:00 45% p3\r\n", 25, tiny, sizeof tiny, r, 3) == -1);
  // celý log: počet řádků stejný, změněné řádky spočítané, opakované přečíslování identitou nic nemění
  std::string log;
  char b[128];
  for (int i = 0; i < 300; i++) {
    logfmt::Record rec;
    rec.year = 2026; rec.month = 5; rec.day = 1 + i / 48; rec.hour = (i / 2) % 24; rec.minute = (i % 2) * 30;
    rec.hum = 40; rec.temp = 20.0f; rec.vbat = 3.25f; rec.raw = 2000; rec.settleMs = 12; rec.loc = 1 + i % 3;
    log.append(b, (size_t)logfmt::formatLine(b, sizeof b, rec));
  }
  logfmt::placeMapDelete(m, 3, 1);
  MemReader rd(log);
  MemWriter wr;
  size_t ch = 0;
  CHECK(logfmt::remapPlaces(rd, wr, m, 3, &ch));
  CHECK(ch == 200);  // místa 2 a 3 se změnila, místo 1 ne
  CHECK(std::count(wr.out.begin(), wr.out.end(), '\n') == 300);
  CHECK(wr.out.find(" p3") == std::string::npos && wr.out.find(" p0") != std::string::npos);
  uint8_t id[9] = {0, 1, 2};
  MemReader rd2(wr.out);
  MemWriter wr2;
  CHECK(logfmt::remapPlaces(rd2, wr2, id, 2, &ch) && ch == 0 && wr2.out == wr.out);
}

// Ruční zápis (v6.10): značka M a poznámka na konci řádku; zředění je nechává všechny; přečíslování míst
// poznámku nečte ani nemění.
static void test_manual_records() {
  printf("[log] ruční zápis: formát, zředění, přečíslování\n");
  char b[200];
  logfmt::Record r;
  r.year = 2026; r.month = 11; r.day = 9; r.hour = 13; r.minute = 5;
  r.hum = 41; r.temp = 12.5f; r.vbat = 3.21f; r.raw = 2500; r.settleMs = 1; r.loc = 2; r.manual = true; r.note = "Lípa p3 # 4";
  int n = logfmt::formatLine(b, sizeof b, r);
  CHECK(n > 0 && std::string(b, (size_t)n) == "2611091305 41 13 321 p2 M # Lípa p3 # 4\r\n");
  CHECK(logfmt::isManualLine(b, (size_t)n));
  r.note = nullptr;
  n = logfmt::formatLine(b, sizeof b, r);
  CHECK(n > 0 && std::string(b, (size_t)n) == "2611091305 41 13 321 p2 M\r\n" && logfmt::isManualLine(b, (size_t)n));
  // přečíslování krátkého řádku: místo 1 bez p dostane p0 před poznámkou, p2 -> p1
  {
    uint8_t mm[9];
    logfmt::placeMapDelete(mm, 2, 0);
    char o2[200];
    const std::string s1 = "2611091305 41 12.5 321 M # Bod 1\r\n", s2 = "2611091305 41 12.5 321 p2\r\n";
    int k = logfmt::remapPlaceLine(s1.c_str(), s1.size(), o2, sizeof o2, mm, 2);
    CHECK(k > 0 && std::string(o2, (size_t)k) == "2611091305 41 12.5 321 M p0 # Bod 1\r\n");
    k = logfmt::remapPlaceLine(s2.c_str(), s2.size(), o2, sizeof o2, mm, 2);
    CHECK(k > 0 && std::string(o2, (size_t)k) == "2611091305 41 12.5 321 p1\r\n");
  }
  r.manual = false;
  n = logfmt::formatLine(b, sizeof b, r);
  CHECK(!logfmt::isManualLine(b, (size_t)n));
  // "M" jen v poznámce není ruční zápis
  const char* fake = "2026-11-09 13:05 41% 12.5C 3.21V # A M B\r\n";
  CHECK(!logfmt::isManualLine(fake, strlen(fake)));
  // přečíslování: " p3" v poznámce se nemění, p2 -> p1; bez p -> p0 vloženo PŘED poznámku
  uint8_t m[9];
  logfmt::placeMapDelete(m, 3, 0);  // smaže se místo 1: 2 -> 1, 3 -> 2
  const std::string l1 = "2026-11-09 13:05 41% 12.5C 3.21V p2 M # Lípa p3\r\n";
  char o[200];
  n = logfmt::remapPlaceLine(l1.c_str(), l1.size(), o, sizeof o, m, 3);
  CHECK(n > 0 && std::string(o, (size_t)n) == "2026-11-09 13:05 41% 12.5C 3.21V p1 M # Lípa p3\r\n");
  const std::string l2 = "2026-11-09 13:05 41% 12.5C 3.21V M # bod p2\r\n";
  n = logfmt::remapPlaceLine(l2.c_str(), l2.size(), o, sizeof o, m, 3);
  CHECK(n > 0 && std::string(o, (size_t)n) == "2026-11-09 13:05 41% 12.5C 3.21V M p0 # bod p2\r\n");
  // zředění: z 1000 řádků (každý 10. ruční) zůstanou všechny ruční
  std::string log;
  int manIn = 0;
  for (int i = 0; i < 1000; i++) {
    logfmt::Record q;
    q.year = 2026; q.month = 3; q.day = 1 + i / 96; q.hour = (i / 4) % 24; q.minute = (i % 4) * 15;
    q.hum = 50; q.temp = 20.0f; q.vbat = 3.25f; q.raw = 2000; q.settleMs = 12;
    q.manual = (i % 10 == 3);
    q.note = q.manual ? "Bod" : nullptr;
    if (q.manual) manIn++;
    const int k = logfmt::formatLine(b, sizeof b, q);
    log.append(b, (size_t)k);
  }
  MemReader rd(log);
  MemWriter wr;
  size_t a = 0, z = 0;
  CHECK(logfmt::compact(rd, wr, log.size(), &a, &z));
  int manOut = 0;
  size_t p = 0;
  while ((p = wr.out.find(" M # Bod", p)) != std::string::npos) { manOut++; p++; }
  CHECK(manOut == manIn && z < a);
}

static void test_log_compact() {
  printf("[log] zředění starší poloviny\n");
  std::string log = makeLog(1000);
  MemReader rd(log);
  MemWriter wr;
  size_t nin = 0, nout = 0;
  bool ok = logfmt::compact(rd, wr, log.size(), &nin, &nout);
  CHECK(ok);
  CHECK(nin == 1000);
  // starší polovina (~500 řádků) -> 250, novější polovina 500 zůstane
  CHECK(nout >= 745 && nout <= 755);
  CHECK(wr.out.size() < log.size() * 0.8);
  // poslední řádek zůstává
  size_t p1 = log.rfind("\r\n", log.size() - 3);
  std::string lastIn = log.substr(p1 + 2);
  CHECK(wr.out.size() >= lastIn.size() && wr.out.compare(wr.out.size() - lastIn.size(), lastIn.size(), lastIn) == 0);
  // pořadí zachováno (vzestupně podle hum sloupce dvojic): zkontroluj, že výstup je podsekvence vstupu
  size_t pos = 0;
  size_t cnt = 0;
  size_t i = 0;
  bool subseq = true;
  while (i < wr.out.size()) {
    size_t e = wr.out.find('\n', i);
    std::string ln = wr.out.substr(i, e - i + 1);
    size_t f = log.find(ln, pos);
    if (f == std::string::npos) { subseq = false; break; }
    pos = f + ln.size();
    i = e + 1;
    cnt++;
  }
  CHECK(subseq);
  CHECK(cnt == nout);

  // opakovaná aplikace stále zmenšuje
  MemReader rd2(wr.out);
  MemWriter wr2;
  CHECK(logfmt::compact(rd2, wr2, wr.out.size()));
  CHECK(wr2.out.size() < wr.out.size());

  // zkomolené / prázdné řádky se zahazují
  std::string junk = "\r\n\r\nabc\r\n" + makeLog(10) + "\n\n";
  MemReader rd3(junk);
  MemWriter wr3;
  size_t a = 0, b = 0;
  CHECK(logfmt::compact(rd3, wr3, junk.size(), &a, &b));
  CHECK(a == 10);

  // velmi dlouhý řádek nerozbije buffer
  std::string longline(1000, 'x');
  longline += "\r\n";
  std::string big = makeLog(4) + longline + makeLog(4);
  MemReader rd4(big);
  MemWriter wr4;
  CHECK(logfmt::compact(rd4, wr4, big.size()));
  // ... a zkrácený řádek si nechá konec řádku: nic se neslepí, ostatní záznamy zůstanou celé
  {
    size_t lines = 0;
    for (char ch : wr4.out) if (ch == '\n') lines++;
    CHECK(!wr4.out.empty() && wr4.out.back() == '\n');
    CHECK(wr4.out.find("xx2") == std::string::npos && wr4.out.find("x\r\n") != std::string::npos);
    CHECK(lines >= 5);  // novější polovina (4 záznamy) + zkrácený řádek, nic slepeného
  }
  // poslední řádek bez CR/LF (nahraný log) dostane konec, další zápis se k němu nepřilepí
  {
    std::string tail = makeLog(20);
    tail.resize(tail.size() - 2);
    MemReader rd5(tail);
    MemWriter wr5;
    CHECK(logfmt::compact(rd5, wr5, tail.size()));
    CHECK(wr5.out.size() >= 2 && wr5.out.compare(wr5.out.size() - 2, 2, "\r\n") == 0);
  }
  // terminateLine: přidá CR/LF, plný buffer přepíše poslední dva znaky, ukončený řádek nechá
  {
    char t1[16] = "abc";
    CHECK(logfmt::terminateLine(t1, 3, sizeof t1) == 5 && !strcmp(t1, "abc\r\n"));
    CHECK(logfmt::terminateLine(t1, 5, sizeof t1) == 5 && !strcmp(t1, "abc\r\n"));
    char t2[8] = "abcdefg";
    CHECK(logfmt::terminateLine(t2, 7, sizeof t2) == 7 && !strcmp(t2, "abcde\r\n"));
  }
  // přečíslování míst: dlouhý řádek se také neslepí s dalším záznamem
  {
    uint8_t m[3] = {0, 2, 1};
    MemReader rd6(big);
    MemWriter wr6;
    size_t ch = 0;
    CHECK(logfmt::remapPlaces(rd6, wr6, m, 2, &ch) && ch == 8);
    size_t lines = 0;
    for (char c2 : wr6.out) if (c2 == '\n') lines++;
    CHECK(lines == 9);
  }

  CHECK(logfmt::firstFullLineOffset("abc\ndef\n", 8) == 4);
  CHECK(logfmt::firstFullLineOffset("abcdef", 6) == 6);
}

static void test_timeutil_settings() {
  printf("[time/settings] validace a hranice\n");
  CHECK(sleepSecondsFor(600, 900) == 598);
  CHECK(sleepSecondsFor(60, 900) == 58);
  CHECK(sleepSecondsFor(2, 5000) == 1);
  CHECK(sleepSecondsFor(1, 0) == 1);
  CHECK(sleepSecondsFor(3600, 0, 0) == 3600);

  CHECK(validDateTime(2026, 2, 28, 12, 0, 0));
  CHECK(!validDateTime(2026, 2, 29, 12, 0, 0));
  CHECK(validDateTime(2028, 2, 29, 23, 59, 59));
  CHECK(!validDateTime(0, 0, 0, 0, 0, 0));
  CHECK(!validDateTime(2026, 13, 1, 0, 0, 0));
  CHECK(!validDateTime(2026, 4, 31, 0, 0, 0));
  CHECK(!validDateTime(2026, 4, 30, 24, 0, 0));
  CHECK(!validDateTime(2100, 1, 1, 0, 0, 0));
  CHECK(!validDateTime(1999, 1, 1, 0, 0, 0));

  Settings s;
  settingsDefaults(s);
  settingsSanitize(s);
  CHECK(s.webMin == DEF_WEB_RUNTIME_MIN && s.intervalMin == DEF_INTERVAL_MIN);
  CHECK(moist::calibValid(s.cal));
  CHECK(s.rPullK == 10.0f);        // pull-up sondy je 10 kΩ
  CHECK(s.vexcTrack);              // ESP32 jede přímo z LiFePO4 -> normalizace čítače je ve výchozím stavu zapnutá

  s.webMin = 9999; s.intervalMin = 60000; s.dryPct = 90; s.wetPct = 10; s.probeMode = 7;
  s.rPullK = NAN; s.vbatCorr = 5.0f; s.cal.gDry = -3; s.cal.gWet = NAN; strcpy(s.lang, "xx");
  settingsSanitize(s);
  CHECK(s.webMin == 30);
  CHECK(s.intervalMin == INTERVAL_MAX_MIN);
  CHECK(s.dryPct < s.wetPct);
  CHECK(s.probeMode == 2);  // neplatný režim -> výchozí krátký pulz
  CHECK(s.rPullK == DEF_PULLUP_KOHM);  // neplatná hodnota (NaN) -> hardwarový pull-up
  CHECK_NEAR(s.vbatCorr, VBAT_DEF_CORRECTION, 1e-6);
  CHECK(moist::calibValid(s.cal));
  CHECK(strcmp(s.lang, "cz") == 0);

  s.webMin = 0;
  s.dryPct = 99; s.wetPct = 99;
  settingsSanitize(s);
  CHECK(s.webMin == 1);
  CHECK(s.dryPct < s.wetPct && s.wetPct <= 100);
}

static void test_combine_dual() {
  printf("[dual] obrácená polarita: výběr fáze a kontrola zapojení\n");
  // obě fáze v okně: přesný poměr, nezávislý na zesílení (stejná zemina při jiném napětí dá stejné g)
  moist::DualResult d = moist::combineDual(2000.0f, 1000.0f, 0.0f, 1000.0f, 4095.0f);
  CHECK(d.phase == moist::PH_RATIO && !d.retBad);
  CHECK_NEAR(d.g, 0.5, 1e-6);
  d = moist::combineDual(1800.0f, 900.0f, 0.0f, 950.0f, 4095.0f);
  CHECK_NEAR(d.g, 0.5, 1e-6);
  // suchá: a u stropu, b v okně -> z b a rawOpen
  d = moist::combineDual(3900.0f, 195.0f, 0.0f, 195.0f, 4095.0f);
  CHECK(d.phase == moist::PH_B);
  CHECK_NEAR(d.g, 195.0 / 3900.0, 1e-4);
  // mokrá: b u stropu -> fáze A
  d = moist::combineDual(372.0f, 4000.0f, 0.0f, 4000.0f, 4095.0f);
  CHECK(d.phase == moist::PH_A && !d.retBad);
  // druhá elektroda nebudí (b ~ základ), a ukazuje proud -> retBad
  d = moist::combineDual(2047.0f, 3.0f, 2.0f, 3.0f, 4095.0f);
  CHECK(d.retBad && d.phase == moist::PH_A);
  // vzduch: a plné, b nula -> žádná chyba, fáze A
  d = moist::combineDual(4095.0f, 0.0f, 0.0f, 0.0f, 4095.0f);
  CHECK(!d.retBad && d.phase == moist::PH_A);
  // NaN
  d = moist::combineDual(NAN, 100.0f, 0.0f, 100.0f, 4095.0f);
  CHECK(d.phase == moist::PH_A && !d.retBad);
  // rawFromG je inverze conductance
  for (float g : {0.01f, 0.3f, 1.0f, 5.0f}) CHECK_NEAR(moist::conductance(moist::rawFromG(g, 4095.0f), 4095.0f), g, g * 1e-3 + 1e-4);
  CHECK(moist::rawFromG(NAN, 4095.0f) > 4000.0f);
}

static void test_log_loc() {
  printf("[logfmt] číslo místa v řádku\n");
  logfmt::Record r;
  r.year = 2026; r.month = 11; r.day = 7; r.hour = 9; r.minute = 30;
  r.hum = 41; r.temp = 12.5f; r.vbat = 3.28f; r.raw = 2100; r.settleMs = 1; r.loc = 3; r.timeOk = false;
  char line[128];
  CHECK(logfmt::formatLine(line, sizeof(line), r) > 0);
  CHECK(strstr(line, " 328 p3 ?") != nullptr && strstr(line, " r") == nullptr && strstr(line, " s1") == nullptr);
  r.loc = -1;
  CHECK(logfmt::formatLine(line, sizeof(line), r) > 0 && strstr(line, " p") == nullptr);
  r.loc = 0;
  CHECK(logfmt::formatLine(line, sizeof(line), r) > 0 && strstr(line, " p") == nullptr);
  CHECK(strstr(line, " H") == nullptr);
  // záznam o hibernaci: "--%", napětí, "!", "H" na konci
  logfmt::Record h;
  h.year = 2026; h.month = 11; h.day = 7; h.hour = 20; h.minute = 45;
  h.hum = -1; h.temp = 9.5f; h.vbat = 2.68f; h.loc = 2; h.lowBat = true; h.hibernate = true;
  CHECK(logfmt::formatLine(line, sizeof(line), h) > 0);
  CHECK(strcmp(line, "2611072045 - 10 268 p2 ! H\r\n") == 0);
}

int main() {
  test_log_loc();
  test_combine_dual();
  test_settle_fast_node();
  test_settle_rc_node();
  test_settle_polarization_early_plateau();
  test_settle_noise_and_spikes();
  test_settle_drift_never_settles();
  test_settle_slow_adc();
  test_settle_fixed_mode_runs_to_cap();
  test_settle_degenerate();
  test_conversion();
  test_confirm();
  test_profile();
  test_battery_curve();
  test_logfmt();
  test_log_compact();
  test_place_remap();
  test_manual_records();
  test_timeutil_settings();
  printf("\n== %d OK, %d FAIL ==\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
