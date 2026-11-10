// Host testy: plánovač záznamů, převod času, JSON, parsování parametrů, jména souborů.
// Spouští se přes tools/run_host_tests.sh (JSON se navíc validuje v Pythonu).
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <random>
#include <string>
#include <vector>

#include "../src/jsonbuf.h"
#include "../src/settings.h"
#include "../src/timeutil.h"
#include "../src/webjson.h"
#include "../src/webparse.h"

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

// ---------------------------------------------------------------------------
static void test_unix_to_parts() {
  printf("[time] unixToParts proti gmtime()\n");
  std::mt19937 rng(7);
  std::uniform_int_distribution<uint32_t> dist(0u, 4102444799u);  // do 2099-12-31
  int bad = 0;
  for (int i = 0; i < 200000; i++) {
    const uint32_t t = (i < 1000) ? (uint32_t)i * 86399u : dist(rng);
    int y, mo, d, h, mi, s;
    unixToParts(t, y, mo, d, h, mi, s);
    time_t tt = (time_t)t;
    struct tm g;
    gmtime_r(&tt, &g);
    if (y != g.tm_year + 1900 || mo != g.tm_mon + 1 || d != g.tm_mday || h != g.tm_hour || mi != g.tm_min ||
        s != g.tm_sec)
      bad++;
  }
  CHECK(bad == 0);
  int y, mo, d, h, mi, s;
  unixToParts(1782993600u, y, mo, d, h, mi, s);  // 2026-07-02 12:00:00
  CHECK(y == 2026 && mo == 7 && d == 2 && h == 12 && mi == 0 && s == 0);
  unixToParts(951782400u, y, mo, d, h, mi, s);   // 2000-02-29 00:00:00 (přestupný)
  CHECK(y == 2000 && mo == 2 && d == 29);
}

// ---------------------------------------------------------------------------
// Simulace zařízení s chybou časovače RC (od v6.5 jedno probuzení na záznam + učení chyby časovače, jako main.cpp):
// zkontroluje, že záznamy vznikají právě jednou na každou hranici, s malou odchylkou a bez probuzení navíc.
struct SchedSim {
  double now;           // skutečný čas (s)
  double drift;         // skutečná délka spánku / zadaná časovači (základ)
  double wobble = 0.0;  // denní kolísání chyby (teplota): drift * (1 + wobble * sin(2π t / den))
  uint32_t last = 0;
  int logs = 0;
  double maxEarly = 0, maxLate = 0;
  double maxDevAfter = 0;  // největší |odchylka| po zaučení (od 11. záznamu)
  int wakes = 0;
  std::vector<uint32_t> stamps;
  float k = 0.0f;           // RtcState::driftK
  uint32_t sleepAt = 0, sleepTimer = 0;
};

static double simDrift(const SchedSim& sm, double t) {
  return sm.drift * (1.0 + sm.wobble * std::sin(2.0 * M_PI * t / 86400.0));
}

// Usnutí jako sleepPlanned(): plán na skutečné sekundy, převod na časovač podle naučeného k. Vrací čas probuzení.
static double simSleep(SchedSim& sm, uint32_t period) {
  const uint32_t nowU = (uint32_t)sm.now;
  const SchedPlan s2 = scheduleStep(nowU, period, sm.last, 3u, 2u);
  const uint32_t timer = timerSecFor(s2.sleepSec, sm.k);
  sm.sleepAt = nowU;
  sm.sleepTimer = timer;
  return sm.now + timer * simDrift(sm, sm.now) + 0.35;  // + boot
}

static void simLog(SchedSim& sm, const SchedPlan& q, double at) {
  sm.logs++;
  const double dev = at - (double)q.boundary;
  if (dev < sm.maxEarly) sm.maxEarly = dev;
  if (dev > sm.maxLate) sm.maxLate = dev;
  if (sm.logs > 10 && std::fabs(dev) > sm.maxDevAfter) sm.maxDevAfter = std::fabs(dev);
  sm.stamps.push_back(q.boundary);
  sm.last = q.boundary;
}

static void sched_loop(SchedSim& sm, uint32_t period, double wakeAt, double endT);

static void run_sched(uint32_t period, double drift, double startT, int days, SchedSim& sm, double wobble = 0.0) {
  sm.now = startT;
  sm.drift = drift;
  sm.wobble = wobble;
  // start: jako po zapnutí – aktuální hranice se považuje za vyřízenou
  sm.last = ((uint32_t)sm.now / period) * period;
  const double endT = startT + days * 86400.0;
  sched_loop(sm, period, simSleep(sm, period), endT);  // první naplánování po (hypotetickém) webu
}

// Probuzení časovačem od okamžiku wakeAt do endT (sm.last = poslední zapsaná hranice).
static void sched_loop(SchedSim& sm, uint32_t period, double wakeAt, double endT) {
  while (wakeAt < endT) {
    sm.now = wakeAt;
    sm.wakes++;
    const uint32_t nowU = (uint32_t)sm.now;  // DS3231 vrací celé sekundy
    // driftLearn()
    if (sm.sleepAt && sm.sleepTimer && nowU > sm.sleepAt) sm.k = driftUpdate(sm.k, sm.sleepTimer, nowU - sm.sleepAt);
    sm.sleepAt = 0;
    // loggingWake(true, true)
    const SchedPlan q = scheduleStep(nowU, period, sm.last, wakeWindowSec(period));
    if (q.logNow) {
      simLog(sm, q, sm.now);
      sm.now += 0.5;  // měření trvá
    }
    wakeAt = simSleep(sm, period);
  }
}

// Konec webové relace v čase tEnd (reálné sekundy). Web zapisuje záznamy v tikách jednou za vteřinu
// (webBoundaryTick, tiky v čase celé sekundy + phase), po skončení relace se zařízení uspí:
//   newFlow = true : přes loggingWake() (znovu vyhodnotí plán, zapíše záznam, který je na řadě)
//   newFlow = false: přímo podle sleepPlanned() (v3/v4.0 – plán se vyhodnotí jen kvůli délce spánku)
static void run_session_end(uint32_t P, double tEnd, double phase, bool newFlow, SchedSim& sm) {
  sm.drift = 1.0;
  sm.last = (((uint32_t)tEnd - 20u + 3u) / P) * P;  // hranice vyřízené před relací
  // tiky relace: posledních ~20 s před koncem
  for (double t = std::floor(tEnd) - 20.0 + phase; t <= tEnd; t += 1.0) {
    const SchedPlan q = scheduleStep((uint32_t)t, P, sm.last);
    if (q.logNow) simLog(sm, q, t);
  }
  sm.now = tEnd;
  if (newFlow) {  // loggingWake(false): bez okna před hranicí
    const SchedPlan q = scheduleStep((uint32_t)sm.now, P, sm.last);
    if (q.logNow) {
      simLog(sm, q, sm.now);
      sm.now += 0.5;
    }
  }
  sched_loop(sm, P, simSleep(sm, P), tEnd + 4.0 * P);
}

static void test_session_end_logging() {
  printf("[sched] konec webové relace těsně před hranicí nezpozdí ani neztratí záznam\n");
  const uint32_t periods[] = {60u, 600u, 3600u};
  for (uint32_t P : periods) {
    const double B = 1782993600.0 - std::fmod(1782993600.0, (double)P) + P;  // nějaká hranice
    double worstNew = 0, worstOld = 0;
    bool seqNew = true, seqOld = true;
    for (double tEnd = B - 8.0; tEnd <= B + 2.0; tEnd += 0.05) {
      for (double phase : {0.0, 0.37, 0.93}) {
        SchedSim a, b;
        run_session_end(P, tEnd, phase, true, a);
        run_session_end(P, tEnd, phase, false, b);
        worstNew = std::max(worstNew, a.maxLate);
        worstOld = std::max(worstOld, b.maxLate);
        for (size_t i = 1; i < a.stamps.size(); i++)
          if (a.stamps[i] - a.stamps[i - 1] != P) seqNew = false;
        bool hasB = false;
        for (uint32_t st : b.stamps) if (st == (uint32_t)B) hasB = true;
        if (tEnd < B && !hasB) seqOld = false;  // hranice B během relace ani po ní nezapsaná
      }
    }
    CHECK(worstNew <= 10.0);    // záznam vždy do pár sekund od své hranice
    CHECK(seqNew);              // žádná hranice vynechaná ani zdvojená
    // zkouška testu: stará cesta (rovnou spát) záznam o hranici někdy zpozdí o skoro celou periodu, nebo (od v6.5,
    // jedno probuzení na záznam) hranici vynechá
    CHECK(worstOld > 0.5 * P || !seqOld);
  }
}

static void test_elapsed() {
  printf("[time] elapsedAtLeast: znaménkový rozdíl časových razítek\n");
  CHECK(elapsedAtLeast(1000u, 0u, 1000u));
  CHECK(!elapsedAtLeast(999u, 0u, 1000u));
  CHECK(elapsedAtLeast(5u, 0xFFFFFFF0u, 20u));   // přetečení millis()
  CHECK(!elapsedAtLeast(5u, 0xFFFFFFF0u, 22u));
  // razítko nastavené volaným kódem až po načtení `now` (je "v budoucnosti"): NESMÍ vyjít jako uplynulo
  CHECK(!elapsedAtLeast(1000u, 1400u, 5000u));
  CHECK(!elapsedAtLeast(1000u, 1400u, 0u));
  // starý unsigned zápis (now - since >= ms) by v tomto případě vyšel jako "uplynulo"
  CHECK((uint32_t)(1000u - 1400u) >= 5000u);
}

static void test_schedule() {
  printf("[sched] jedno probuzení na záznam, zarovnání na hodiny při chybě RC časovače\n");
  const uint32_t periods[] = {60u, 300u, 900u, 1800u, 3600u, 7200u, 14400u};
  const double drifts[] = {0.86, 0.9, 0.95, 1.0, 1.05, 1.1, 1.14};
  const double wobbles[] = {0.0, 0.03};  // bez kolísání / ±3 % během dne (teplota)
  for (uint32_t P : periods) {
    for (double dr : drifts) {
      for (double wb : wobbles) {
        SchedSim sm;
        const double start = 1782993600.0 + 1234.0;  // nějaký nezarovnaný okamžik
        const int days = P >= 7200u ? 20 : 3;
        run_sched(P, dr, start, days, sm, wb);
        const double expected = (days * 86400.0) / P;
        // každá hranice právě jednou (±1 na okrajích)
        CHECK(fabs(sm.logs - expected) <= 1.5);
        // hranice jsou po sobě jdoucí násobky P (žádná se nezdvojí, žádná nevynechá)
        bool seq = true;
        for (size_t i = 1; i < sm.stamps.size(); i++)
          if (sm.stamps[i] - sm.stamps[i - 1] != P) seq = false;
        CHECK(seq);
        // ENERGIE: probuzení navíc (bez záznamu) jen při zaučování, ne v provozu
        CHECK(sm.wakes <= (int)sm.stamps.size() + 3);
        // odchylka: nanejvýš okno před hranicí, nanejvýš ~chyba RC po ní (první cyklus)
        CHECK(sm.maxEarly >= -(double)wakeWindowSec(P) - 1.0);
        CHECK(sm.maxLate <= 0.15 * P + 5.0);
        // po zaučení: malá odchylka (bez kolísání pod 1 % intervalu, s denním kolísáním ±3 % pod 4 %; + rozlišení
        // DS3231 a boot)
        const double lim = (wb > 0 ? 0.04 : 0.01) * P + 10.0;
        CHECK(sm.maxDevAfter <= lim);
        if (!(sm.maxDevAfter <= lim) || !(sm.wakes <= (int)sm.stamps.size() + 3))
          printf("    P=%u drift=%.2f wobble=%.2f: wakes=%d logs=%zu devAfter=%.1f early=%.1f late=%.1f\n", P, dr, wb,
                 sm.wakes, sm.stamps.size(), sm.maxDevAfter, sm.maxEarly, sm.maxLate);
      }
    }
  }
  // zarovnání: záznamy na celé hodiny pro P = 3600
  {
    SchedSim sm;
    run_sched(3600u, 1.07, 1782993600.0 + 77.0, 2, sm);
    bool aligned = !sm.stamps.empty();
    for (uint32_t t : sm.stamps) if (t % 3600u != 0) aligned = false;
    CHECK(aligned);
  }
  // RTC skočí zpět (uživatel nastavil čas) -> zapíše se (nová hranice)
  {
    SchedPlan p = scheduleStep(1000000u, 600u, 2000000u);
    CHECK(p.logNow);
  }
  // už zapsaná hranice se nezapíše dvakrát
  {
    const uint32_t cur = (1782993600u / 600u) * 600u;
    SchedPlan p = scheduleStep(cur + 1u, 600u, cur);
    CHECK(!p.logNow);
    CHECK(p.sleepSec == 599u + 2u);  // rovnou do další hranice (+2 s), žádné etapy
  }
  // interval 0 = bez plánu
  {
    SchedPlan p = scheduleStep(1782993600u, 0u, 0u);
    CHECK(!p.logNow && p.sleepSec == 0u);
  }
  // blízko hranice: kratičký spánek, ne 0
  {
    const uint32_t nxt = ((1782993600u / 3600u) + 1u) * 3600u;
    SchedPlan p = scheduleStep(nxt - 10u, 3600u, nxt - 3600u);
    CHECK(p.sleepSec >= 1u && p.sleepSec <= 12u);
    CHECK(!p.logNow);
    p = scheduleStep(nxt - 2u, 3600u, nxt - 3600u);  // 3 s předstih -> už je čas
    CHECK(p.logNow && p.boundary == nxt);
  }
  // probuzení časovačem 5 min před hodinou (okno 7,5 min): záznam hned, patří k celé hodině, pak spánek do další
  {
    const uint32_t nxt = ((1782993600u / 3600u) + 1u) * 3600u;
    SchedPlan p = scheduleStep(nxt - 300u, 3600u, nxt - 3600u, wakeWindowSec(3600u));
    CHECK(p.logNow && p.boundary == nxt);
    CHECK(p.sleepSec == 3600u + 300u + 2u);
    // mimo okno (10 min před): ještě ne
    p = scheduleStep(nxt - 600u, 3600u, nxt - 3600u, wakeWindowSec(3600u));
    CHECK(!p.logNow);
  }
  // okno: desetina... osmina intervalu, 3 s až 15 min
  CHECK(wakeWindowSec(60u) == 7u && wakeWindowSec(1800u) == 225u && wakeWindowSec(14400u) == 900u && wakeWindowSec(8u) == 3u);
}

static void test_drift() {
  printf("[sched] učení chyby časovače spánku\n");
  // nenaučeno (0) = 1.0
  CHECK(timerSecFor(1800u, 0.0f) == 1800u);
  // časovač o 10 % pomalejší (spí déle): k -> 1.1, příští spánek kratší
  float k = 0.0f;
  for (int i = 0; i < 12; i++) k = driftUpdate(k, 1800u, 1980u);
  CHECK(fabs(k - 1.1f) < 0.002f);
  CHECK(timerSecFor(1980u, k) >= 1798u && timerSecFor(1980u, k) <= 1802u);
  // nesmysly se ignorují: nastavený čas (skok o hodiny), příliš krátký spánek
  CHECK(driftUpdate(1.05f, 1800u, 9000u) == 1.05f);
  CHECK(driftUpdate(1.05f, 1800u, 100u) == 1.05f);
  CHECK(driftUpdate(1.05f, 10u, 12u) == 1.05f);
  // neplatné uložené k (poškozená RTC RAM) se bere jako 1.0
  CHECK(timerSecFor(600u, 7.0f) == 600u);
  CHECK(timerSecFor(0u, 1.0f) == 1u);
}

// ---------------------------------------------------------------------------
static void test_settings_masked() {
  printf("[settings] applySettingsMasked\n");
  Settings a, b;
  settingsDefaults(a);
  settingsDefaults(b);
  a.cal.gDry = 0.2f;
  a.cal.gWet = 5.0f;
  a.cal.rawOpen = 4000;
  strcpy(b.lang, "ua");
  b.webMin = 12;
  b.intervalMin = 30;
  b.dryPct = 20;
  b.wetPct = 70;
  b.cal.gDry = 0.9f;  // nesmí se přenést (kalibrace není v masce)
  b.vexcTrack = true;
  b.probeMode = 1;
  b.cal.scale = 0;
  b.rPullK = 47.0f;
  b.biasPin = 26;
  applySettingsMasked(a, b, SM_LANG | SM_WEB | SM_INT | SM_DRY | SM_WET | SM_PMODE | SM_VEXC | SM_SCALE | SM_RPULL);
  CHECK(a.biasPin == 0);  // SM_BIAS není v masce: pin BIAS se tím nezměnil
  CHECK(strcmp(a.lang, "ua") == 0 && a.webMin == 12 && a.intervalMin == 30);
  CHECK(a.dryPct == 20 && a.wetPct == 70 && a.probeMode == 1 && a.vexcTrack && a.cal.scale == 0);
  CHECK(a.rPullK == DEF_PULLUP_KOHM);  // pull-up se nenastavuje: vždy 10 kΩ
  CHECK(a.cal.gDry == 0.2f && a.cal.gWet == 5.0f && a.cal.rawOpen == DEF_CAL_RAW_OPEN  /* naprázdno se nekalibruje */);  // kalibrace zůstala
  // korekce napětí baterie jen s SM_VCORR (obnova zálohy)
  b.vbatCorr = 1.05f;
  const float vc0 = a.vbatCorr;
  applySettingsMasked(a, b, SM_WEB);
  CHECK(a.vbatCorr == vc0);
  applySettingsMasked(a, b, SM_VCORR);
  CHECK(a.vbatCorr == 1.05f);
  Settings c;
  settingsDefaults(c);
  applySettingsMasked(c, b, SM_WEB);  // jen jedno pole
  CHECK(c.webMin == 12 && c.intervalMin == DEF_INTERVAL_MIN && strcmp(c.lang, "cz") == 0);
  // nesmysl z webu se ošetří i přes masku
  b.webMin = 5000;
  b.dryPct = 90;
  b.wetPct = 10;
  applySettingsMasked(c, b, SM_WEB | SM_DRY | SM_WET);
  CHECK(c.webMin == 30 && c.dryPct < c.wetPct);
  // pin BIAS jde jen přes SM_BIAS a jen platný (jinak 0 = automaticky)
  Settings d;
  settingsDefaults(d);
  b.biasPin = 26;
  applySettingsMasked(d, b, SM_BIAS);
  CHECK(d.biasPin == 26 && d.webMin == DEF_WEB_RUNTIME_MIN);
  b.biasPin = 13;  // LED: nikdy
  applySettingsMasked(d, b, SM_BIAS);
  CHECK(d.biasPin == 0);
}

// ---------------------------------------------------------------------------
static void test_parse() {
  printf("[parse] webparse\n");
  long l = 0;
  float f = 0;
  CHECK(webparse::parseLong("42", 0, 100, l) && l == 42);
  CHECK(webparse::parseLong("-5", -10, 10, l) && l == -5);
  CHECK(!webparse::parseLong("", 0, 100, l));
  CHECK(!webparse::parseLong(nullptr, 0, 100, l));
  CHECK(!webparse::parseLong("12abc", 0, 100, l));
  CHECK(!webparse::parseLong(" 12", 0, 100, l) || l == 12);  // úvodní mezera strtol toleruje – hlavně žádný pád
  CHECK(!webparse::parseLong("101", 0, 100, l));
  CHECK(!webparse::parseLong("99999999999999999999", 0, 100, l));
  CHECK(webparse::parseFloat("3.25", 0, 5, f) && fabsf(f - 3.25f) < 1e-6f);
  CHECK(webparse::parseFloat("3,25", 0, 5, f) && fabsf(f - 3.25f) < 1e-6f);
  CHECK(!webparse::parseFloat("nan", 0, 5, f));
  CHECK(!webparse::parseFloat("inf", 0, 5, f));
  CHECK(!webparse::parseFloat("1e999", 0, 5, f));
  CHECK(!webparse::parseFloat("6", 0, 5, f));
  CHECK(!webparse::parseFloat("3.2.5", 0, 5, f));
  CHECK(!webparse::parseFloat("3.25V", 0, 5, f));

  char out[40];
  CHECK(webparse::sanitizeUploadName("log.txt", out, sizeof(out)) && strcmp(out, "log.txt") == 0);
  CHECK(webparse::sanitizeUploadName("C:\\Users\\me\\data 1.txt", out, sizeof(out)) && strcmp(out, "data_1.txt") == 0);
  CHECK(webparse::sanitizeUploadName("../../etc/passwd", out, sizeof(out)) && strcmp(out, "passwd") == 0);
  CHECK(!webparse::sanitizeUploadName(".hidden", out, sizeof(out)));
  CHECK(!webparse::sanitizeUploadName("", out, sizeof(out)));
  CHECK(!webparse::sanitizeUploadName("dir/", out, sizeof(out)));
  CHECK(!webparse::sanitizeUploadName("___", out, sizeof(out)));
  CHECK(!webparse::sanitizeUploadName("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.txt", out, sizeof(out)));
  CHECK(webparse::sanitizeUploadName("ěščř.txt", out, sizeof(out)));  // diakritika -> '_' (nikdy neprojde nebezpečný bajt)
  for (const char* c = out; *c; ++c) CHECK((unsigned char)*c < 0x80);

  CHECK(webparse::validExistingName("log.txt"));
  CHECK(webparse::validExistingName("chart.js.gz"));
  CHECK(!webparse::validExistingName("../log.txt"));
  CHECK(!webparse::validExistingName("a/b"));
  CHECK(!webparse::validExistingName("a\\b"));
  CHECK(!webparse::validExistingName(""));
  CHECK(!webparse::validExistingName("a\nb"));

  // Origin vs. Host (POST z webu i nahrání firmwaru /update)
  CHECK(webparse::sameOrigin(nullptr, "10.10.10.10"));  // bez Origin (curl)
  CHECK(webparse::sameOrigin("http://10.10.10.10", "10.10.10.10"));
  CHECK(webparse::sameOrigin("http://Arbolok.local", "arbolok.local"));
  CHECK(webparse::sameOrigin("http://10.10.10.10:8080", "10.10.10.10:8080"));
  CHECK(!webparse::sameOrigin("http://evil.example", "10.10.10.10"));
  CHECK(!webparse::sameOrigin("http://10.10.10.10.evil.example", "10.10.10.10"));
  CHECK(!webparse::sameOrigin("http://10.10.10.1", "10.10.10.10"));
  CHECK(!webparse::sameOrigin("null", "10.10.10.10"));  // sandbox / soubor
  CHECK(!webparse::sameOrigin("http://10.10.10.10", nullptr));
  CHECK(!webparse::sameOrigin("http://10.10.10.10", ""));

  // Nahrávání: po uložení musí logger mít místo (rezerva + 75 % logu na dočasný soubor zředění)
  const size_t K = 1024, MINF = 48 * K, HEAD = 8 * K;
  CHECK(webparse::uploadFits(100 * K, 1000 * K, 50 * K, false, MINF, HEAD));
  // potřeba = 100 + 48 + 37,5 (75 % z 50) + 8 = 193,5 kB
  CHECK(webparse::uploadFits(100 * K, 194 * K, 50 * K, false, MINF, HEAD));
  CHECK(!webparse::uploadFits(100 * K, 193 * K, 50 * K, false, MINF, HEAD));
  // starý práh (jen 24 kB rezerva) by tohle pustil a logger by pak u každého zápisu zřeďoval
  CHECK(!webparse::uploadFits(100 * K, 100 * K + 24 * K + 1, 50 * K, false, MINF, HEAD));
  // .txt nahradí log: místo starého se počítá nový (potřeba = 400 + 48 + 300 + 8 = 756 kB)
  CHECK(!webparse::uploadFits(400 * K, 700 * K, 10 * K, true, MINF, HEAD));
  CHECK(webparse::uploadFits(400 * K, 757 * K, 10 * K, true, MINF, HEAD));
  CHECK(webparse::uploadFits(400 * K, 700 * K, 10 * K, false, MINF, HEAD));  // běžný soubor: jen 75 % STÁVAJÍCÍHO logu
  CHECK(webparse::uploadFits(0, 60 * K, 0, false, MINF, HEAD));              // prázdný soubor
  CHECK(!webparse::uploadFits(0, 50 * K, 0, false, MINF, HEAD));
  CHECK(!webparse::uploadFits((size_t)-1, (size_t)-1, 0, false, MINF, HEAD));  // přetečení se nesmí propsat jako "vejde se"
  CHECK(!webparse::uploadFits((size_t)-1 - 10, (size_t)-1, 4 * K, true, MINF, HEAD));
}

// ---------------------------------------------------------------------------
static void writeFile(const char* dir, const char* name, const char* data) {
  if (!dir) return;
  std::string p = std::string(dir) + "/" + name;
  FILE* f = fopen(p.c_str(), "wb");
  if (!f) return;
  fputs(data, f);
  fclose(f);
}

static void test_json() {
  printf("[json] JsonBuf + buildStatus\n");
  const char* outDir = getenv("ARBOLOG_JSON_OUT");

  char buf[1536];
  webjson::StatusView s;
  s.fw = "v4.0";
  s.id = "1234567";
  int n = webjson::buildStatus(buf, sizeof(buf), s);
  CHECK(n > 0 && (size_t)n == strlen(buf));
  CHECK(n < 1100);  // status se posílá často – má zůstat malý (i s 8 místy viz test_json_locs)
  CHECK(strstr(buf, "\"wire\":{\"have\":false,\"res\":0,\"pin\":0,\"n\":0,\"p\":[],\"lo\":[],\"hi\":[]}") != nullptr);
  writeFile(outDir, "status_default.json", buf);

  s.clockOk = true;
  s.clockUnix = 1782993600u;
  s.clockSet = true;
  s.batV = 3.254f;
  s.batPct = 55;
  s.mHave = true;
  s.mPct = 45.26f;
  s.raw = 2873.4f;
  s.rawRaw = 2873.4f;
  s.g = 0.4254f;
  s.ms = 12;
  s.flags = 3;
  s.temp = -4.5f;
  s.busy = true;
  s.msg = "ok:dry";
  s.seq = 7;
  s.sessLeft = 200;
  s.sessTotal = 240;
  s.sessMax = 1800;
  s.fsTotal = 1441792;
  s.fsUsed = 300000;
  s.logBytes = 123456;
  s.profValid = true;
  s.profN = 30;
  s.bias = 26;
  s.biasUsed = 26;
  s.wireHave = true;
  s.wireRes = 1;
  s.wirePin = 26;
  s.wireN = 3;
  s.wireP[0] = 32; s.wireP[1] = 26; s.wireP[2] = 25;
  s.wireLo[0] = 3.0f; s.wireLo[1] = 2.0f; s.wireLo[2] = 0.0f;
  s.wireHi[0] = 5.0f; s.wireHi[1] = 4090.0f; s.wireHi[2] = 1.0f;
  n = webjson::buildStatus(buf, sizeof(buf), s);
  CHECK(n > 0);
  CHECK(strstr(buf, "\"bias\":26,\"biasUsed\":26") != nullptr);
  CHECK(strstr(buf, "\"wire\":{\"have\":true,\"res\":1,\"pin\":26,\"n\":3,"
                    "\"p\":[32,26,25],\"lo\":[3,2,0],\"hi\":[5,4090,1]}") != nullptr);
  CHECK(strstr(buf, "\"temp\":-4.5") != nullptr);
  CHECK(strstr(buf, "\"msg\":\"ok:dry\"") != nullptr);
  CHECK(strstr(buf, "NaN") == nullptr && strstr(buf, "nan") == nullptr && strstr(buf, "inf") == nullptr);
  writeFile(outDir, "status_full.json", buf);

  // NaN / ∞ musí skončit jako null (jinak by JSON.parse v prohlížeči selhal)
  s.batV = NAN;
  s.batPct = -1;
  s.raw = INFINITY;
  s.temp = NAN;
  s.g = -INFINITY;
  n = webjson::buildStatus(buf, sizeof(buf), s);
  CHECK(n > 0);
  CHECK(strstr(buf, "null") != nullptr);
  CHECK(strstr(buf, "inf") == nullptr && strstr(buf, "nan") == nullptr);
  writeFile(outDir, "status_nan.json", buf);

  // zlé znaky ve stringu
  s.msg = "a\"b\\c\n\t\x01 č";
  n = webjson::buildStatus(buf, sizeof(buf), s);
  CHECK(n > 0);
  writeFile(outDir, "status_escape.json", buf);

  // přetečení bufferu -> -1, žádný zápis mimo buffer
  char small[64];
  memset(small, 'X', sizeof(small));
  n = webjson::buildStatus(small, sizeof(small), s);
  CHECK(n == -1);
  CHECK(memchr(small, '\0', sizeof(small)) != nullptr);  // řetězec je vždy ukončený
  for (size_t cap = 0; cap < 40; cap++) {  // nikdy se nezapíše za zadanou velikost
    char t[64];
    memset(t, 'Y', sizeof(t));
    const int r = webjson::buildStatus(t, cap, s);
    CHECK(r == -1);  // celý status se do tak malého bufferu nevejde
    bool untouched = true;
    for (size_t i = cap; i < sizeof(t); i++)
      if (t[i] != 'Y') untouched = false;
    CHECK(untouched);
    if (cap > 0) CHECK(memchr(t, '\0', cap) != nullptr);  // řetězec je ukončený uvnitř bufferu
  }

  // profil
  float t[5] = {0.3f, 1.5f, 10.0f, 100.0f, 299.0f};
  float v[5] = {100.0f, 2000.5f, 2400.0f, 2500.0f, 2510.0f};
  n = webjson::buildProfile(buf, sizeof(buf), t, v, 5);
  CHECK(n > 0);
  writeFile(outDir, "profile.json", buf);
  n = webjson::buildProfile(buf, sizeof(buf), t, v, 0);
  CHECK(n > 0 && strstr(buf, "\"n\":0") != nullptr);
  writeFile(outDir, "profile_empty.json", buf);

  // samotný JsonBuf: vnoření, pole objektů, čárky
  char b2[256];
  JsonBuf j(b2, sizeof(b2));
  j.arr();
  for (int i = 0; i < 3; i++) j.obj().kvStr("n", "f").kvInt("s", i).key("a").arr().integer(1).integer(2).end().end();
  j.end();
  CHECK(j.ok());
  CHECK(strcmp(b2, "[{\"n\":\"f\",\"s\":0,\"a\":[1,2]},{\"n\":\"f\",\"s\":1,\"a\":[1,2]},{\"n\":\"f\",\"s\":2,\"a\":[1,2]}]") == 0);
  writeFile(outDir, "files_sample.json", b2);
  // hloubka
  char b3[64];
  JsonBuf d(b3, sizeof(b3));
  for (int i = 0; i < 20; i++) d.arr();
  CHECK(!d.ok());
}

static void test_captive_host() {
  printf("[web] captive portal: které adresy se přesměrují na stránku\n");
  CHECK(webparse::isForeignHost("captive.apple.com"));
  CHECK(webparse::isForeignHost("connectivitycheck.gstatic.com"));
  CHECK(webparse::isForeignHost("www.msftconnecttest.com"));
  CHECK(webparse::isForeignHost("detectportal.firefox.com:80"));
  CHECK(webparse::isForeignHost("10.10.10.1"));
  CHECK(webparse::isForeignHost("10.10.10.100"));
  CHECK(!webparse::isForeignHost("10.10.10.10"));
  CHECK(!webparse::isForeignHost("10.10.10.10:80"));
  CHECK(!webparse::isForeignHost("arbolok.local"));
  CHECK(!webparse::isForeignHost("ARBOLOK.local"));
  CHECK(!webparse::isForeignHost("arbolok"));
  CHECK(!webparse::isForeignHost(""));
  CHECK(!webparse::isForeignHost(nullptr));
  printf("[web] po připojení: testy internetu a co s nimi podle režimu\n");
  using namespace webparse;
  CHECK(probeKind("/hotspot-detect.html") == PR_APPLE && probeKind("/library/test/success.html") == PR_APPLE);
  CHECK(probeKind("/generate_204") == PR_204 && probeKind("/gen_204") == PR_204);
  CHECK(probeKind("/connecttest.txt") == PR_MSFT && probeKind("/ncsi.txt") == PR_NCSI);
  CHECK(probeKind("/canonical.html") == PR_FF_CANON && probeKind("/success.txt") == PR_FF_OK);
  CHECK(probeKind("/") == PR_NONE && probeKind("/api/status") == PR_NONE && probeKind(nullptr) == PR_NONE);
  // režim 0: nic se nemění, ani u cizí adresy
  CHECK(captiveAction(0, false, "captive.apple.com", "/hotspot-detect.html") == CA_NONE);
  CHECK(captiveAction(0, true, "www.google.com", "/") == CA_NONE);
  // vlastní adresa se nikdy nepřesměruje
  for (uint8_t m = 0; m < 3; m++) {
    CHECK(captiveAction(m, false, "10.10.10.10", "/") == CA_NONE);
    CHECK(captiveAction(m, false, "arbolok.local", "/generate_204") == CA_NONE);
  }
  // režim 1: do potvrzení rozcestník (i pro test internetu), po "Hotovo" test = je internet, jinak stránka
  CHECK(captiveAction(1, false, "captive.apple.com", "/hotspot-detect.html") == CA_PORTAL);
  CHECK(captiveAction(1, false, "connectivitycheck.gstatic.com", "/generate_204") == CA_PORTAL);
  CHECK(captiveAction(1, false, "example.com", "/neco") == CA_PORTAL);
  CHECK(captiveAction(1, true, "captive.apple.com", "/hotspot-detect.html") == CA_SUCCESS);
  CHECK(captiveAction(1, true, "connectivitycheck.gstatic.com", "/generate_204") == CA_SUCCESS);
  CHECK(captiveAction(1, true, "arbolok", "/") == CA_NONE);
  CHECK(captiveAction(1, true, "example.com", "/") == CA_HOME);
  // režim 2: testy hned "je internet" (žádné okno), cizí adresa v prohlížeči -> stránka
  CHECK(captiveAction(2, false, "captive.apple.com", "/hotspot-detect.html") == CA_SUCCESS);
  CHECK(captiveAction(2, false, "www.msftconnecttest.com", "/connecttest.txt") == CA_SUCCESS);
  CHECK(captiveAction(2, false, "detectportal.firefox.com", "/success.txt") == CA_SUCCESS);
  CHECK(captiveAction(2, false, "www.seznam.cz", "/") == CA_HOME);
}

static void test_json_locs() {
  printf("[json] místa ve stavu\n");
  char buf[3072];
  webjson::StatusView s;
  s.fw = "v5.0";
  s.id = "1";
  s.locN = 2;
  s.loc = 1;
  strcpy(s.locName[0], "Lípa u školy");
  strcpy(s.locName[1], "Záhon / jih");
  s.locDry[0] = 30; s.locWet[0] = 85; s.locGd[0] = 0.05f; s.locGw[0] = 3.0f;
  s.locDry[1] = 25; s.locWet[1] = 80; s.locGd[1] = 0.1234f; s.locGw[1] = 4.5f;
  int n = webjson::buildStatus(buf, sizeof(buf), s);
  CHECK(n > 0);
  CHECK(strstr(buf, "\"loc\":1,\"locs\":[\"Lípa u školy\",\"Záhon / jih\"]") != nullptr);
  CHECK(strstr(buf, "\"locd\":[[30,85,0.0500,3.0000],[25,80,0.1234,4.5000]]") != nullptr);
  s.locN = 8;
  for (int k = 0; k < 8; k++) {
    strcpy(s.locName[k], "žžžžžžžžžžžž");  // 24 bajtů, nejdelší možné
    s.locDry[k] = 99; s.locWet[k] = 100; s.locGd[k] = 123.4567f; s.locGw[k] = 999.1234f;
  }
  n = webjson::buildStatus(buf, sizeof(buf), s);
  CHECK(n > 0 && n < 1900);  // i s 8 místy a kalibrací daleko pod bufferem 3072
  const char* outDir = getenv("ARBOLOG_JSON_OUT");
  if (outDir) {
    char p[512];
    snprintf(p, sizeof(p), "%s/status_locs.json", outDir);
    FILE* f = fopen(p, "w");
    if (f) { fputs(buf, f); fclose(f); }
  }
}

int main() {
  test_json_locs();
  test_captive_host();
  test_unix_to_parts();
  test_schedule();
  test_drift();
  test_session_end_logging();
  test_elapsed();
  test_settings_masked();
  test_parse();
  test_json();
  printf("\n== web: %d OK, %d FAIL ==\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
