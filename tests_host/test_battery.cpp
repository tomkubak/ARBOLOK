// Host test src/battery.cpp (měření článku přes dělič 1:2 a přesnou referenci) nad atrapou ADC.
// Hlídá: matematiku, ořezaný průměr proti odlehlým vzorkům, meze rozumných hodnot (závada HW),
// a hlavně že napájení děliče i reference je po měření VŽDY vypnuté (jinak by tekl proud z článku).
// Sestavení a spuštění:  tools/run_host_tests.sh
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <Arduino.h>

#include "../src/battery.h"
#include "../src/config.h"

static uint64_t g_us = 0;
static bool g_refOn = false, g_divOn = false;
static bool g_everBothOn = false;
static int g_refCounts = 1500, g_divCounts = 1943;
static int g_refCalls = 0, g_divCalls = 0;
static int g_outlierEvery = 0;  // každý n-tý vzorek je odlehlý
static int g_readsWhileOff = 0;

unsigned long millis() { return (unsigned long)(g_us / 1000ULL); }
unsigned long micros() { return (unsigned long)g_us; }
void delay(uint32_t ms) { g_us += (uint64_t)ms * 1000ULL; }
void delayMicroseconds(uint32_t us) { g_us += us; }
void pinMode(uint8_t, uint8_t) {}
void digitalWrite(uint8_t pin, uint8_t v) {
  if (pin == VREF_ON_PIN) g_refOn = (v == HIGH);
  if (pin == VDIV_ON_PIN) g_divOn = (v == HIGH);
  if (g_refOn && g_divOn) g_everBothOn = true;
}
int analogRead(uint8_t pin) {
  if (pin == VREF_ADC_PIN) {
    if (!g_refOn) g_readsWhileOff++;
    const int k = g_refCalls++;
    return (g_outlierEvery && k % g_outlierEvery == 3) ? 4095 : g_refCounts;
  }
  if (pin == VDIV_ADC_PIN) {
    if (!g_divOn) g_readsWhileOff++;
    const int k = g_divCalls++;
    return (g_outlierEvery && k % g_outlierEvery == 5) ? 0 : g_divCounts;
  }
  return 0;
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
#define CHECK_NEAR(a, b, tol)                                                                          \
  do {                                                                                                 \
    double _a = (a), _b = (b);                                                                         \
    if (_a - _b <= (tol) && _b - _a <= (tol)) {                                                        \
      g_pass++;                                                                                        \
    } else {                                                                                           \
      g_fail++;                                                                                        \
      printf("  FAIL %s:%d  %s=%.4f  %s=%.4f  (tol %.4f)\n", __FILE__, __LINE__, #a, _a, #b, _b, (double)(tol)); \
    }                                                                                                  \
  } while (0)

static void setup(int refC, int divC, int outlierEvery = 0) {
  g_refCounts = refC;
  g_divCounts = divC;
  g_outlierEvery = outlierEvery;
  g_refCalls = g_divCalls = 0;
  g_everBothOn = false;
  g_readsWhileOff = 0;
  g_refOn = g_divOn = false;
}

// čítač děliče pro dané napětí článku při referenci 1,235 V a čítači reference refC
static int divFor(double vbat, int refC) { return (int)(vbat / 2.0 / REF_VOLTAGE * refC + 0.5); }

static void test_math() {
  printf("[battery] napětí z poměru čítačů a korekce\n");
  batteryInit();
  CHECK(!g_refOn && !g_divOn);
  const double vs[] = {2.6, 2.9, 3.0, 3.2, 3.4, 3.65};
  for (double v : vs) {
    setup(1500, divFor(v, 1500));
    BatteryReading r = batteryMeasure(1.0f);
    CHECK(r.ok);
    CHECK_NEAR(r.volts, v, 0.01);
    CHECK_NEAR(r.voltsRaw, r.volts, 1e-6);
    BatteryReading c = batteryMeasure(0.97f);
    CHECK_NEAR(c.volts, v * 0.97, 0.012);
    CHECK_NEAR(c.voltsRaw, r.voltsRaw, 1e-3);  // surová hodnota na korekci nezávisí
  }
}

static void test_independent_of_adc_gain() {
  printf("[battery] poměr nezávisí na zesílení ADC (referenční dioda)\n");
  for (int refC : {1100, 1500, 2000}) {
    setup(refC, divFor(3.3, refC));
    BatteryReading r = batteryMeasure(1.0f);
    CHECK(r.ok);
    CHECK_NEAR(r.volts, 3.3, 0.012);
  }
}

static void test_outliers_trimmed() {
  printf("[battery] odlehlé vzorky ořezaný průměr zahodí\n");
  setup(1500, divFor(3.2, 1500), 5);  // každý pátý vzorek ref=4095 / dělič=0
  BatteryReading r = batteryMeasure(1.0f);
  CHECK(r.ok);
  CHECK_NEAR(r.volts, 3.2, 0.01);
}

static void test_hw_faults() {
  printf("[battery] závada reference nebo děliče = ok=false, žádné nesmyslné napětí\n");
  setup(100, 1000);  // reference pod dolní mezí
  BatteryReading r = batteryMeasure(1.0f);
  CHECK(!r.ok && r.volts == 0.0f && r.voltsRaw == 0.0f);
  setup(3000, 1000);  // reference nad horní mezí
  r = batteryMeasure(1.0f);
  CHECK(!r.ok);
  setup(1500, 10);  // dělič odpojený
  r = batteryMeasure(1.0f);
  CHECK(!r.ok);
  setup(0, 0);
  r = batteryMeasure(1.0f);
  CHECK(!r.ok && r.volts == 0.0f);  // žádné dělení nulou
  CHECK(!g_refOn && !g_divOn);
}

static void test_power_pins() {
  printf("[battery] napájení děliče a reference je po měření vypnuté\n");
  setup(1500, divFor(3.2, 1500));
  (void)batteryMeasure(1.0f);
  CHECK(g_everBothOn);   // během měření byly zapnuté
  CHECK(!g_refOn && !g_divOn);
  CHECK(g_readsWhileOff == 0);  // a čtení se dělo jen při zapnutém napájení
  setup(100, 100);  // i při chybě
  (void)batteryMeasure(1.0f);
  CHECK(!g_refOn && !g_divOn);
  CHECK(g_readsWhileOff == 0);
}

static void test_duration() {
  printf("[battery] měření trvá desítky ms (ustálení + 16 vzorků)\n");
  setup(1500, divFor(3.2, 1500));
  const uint64_t t0 = g_us;
  (void)batteryMeasure(1.0f);
  const uint64_t d = g_us - t0;
  CHECK(d >= (uint64_t)VBAT_SETTLE_MS * 1000ULL);
  CHECK(d < 200000ULL);
}

static void test_hibernate() {
  printf("[battery] hibernace: pod %.2f V spát, ven až od %.2f V (hystereze), chybné měření stav nemění\n", VBAT_NOLOG_V, VBAT_RESUME_V);
  const float lo = VBAT_NOLOG_V, hi = VBAT_RESUME_V;
  CHECK(!batteryHibernate(false, true, 3.20f, lo, hi));
  CHECK(!batteryHibernate(false, true, 2.80f, lo, hi));   // slabý, ale ještě ne hibernace
  CHECK(batteryHibernate(false, true, 2.69f, lo, hi));    // vstup
  CHECK(batteryHibernate(true, true, 2.85f, lo, hi));     // po odlehčení se trochu zvedl: pořád hibernace
  CHECK(batteryHibernate(true, true, 2.99f, lo, hi));
  CHECK(!batteryHibernate(true, true, 3.00f, lo, hi));    // nabito: zpět normální provoz
  CHECK(batteryHibernate(true, false, 3.30f, lo, hi));    // neplatné měření: beze změny
  CHECK(!batteryHibernate(false, false, 0.0f, lo, hi));
  CHECK(batteryHibernate(true, true, NAN, lo, hi) && !batteryHibernate(false, true, NAN, lo, hi));
  CHECK(VBAT_RESUME_V > VBAT_NOWIFI_V && VBAT_NOWIFI_V > VBAT_NOLOG_V && VBAT_HIBERNATE_SEC >= 3600UL);
}

int main() {
  test_hibernate();
  test_math();
  test_independent_of_adc_gain();
  test_outliers_trimmed();
  test_hw_faults();
  test_power_pins();
  test_duration();
  printf("\n== battery: %d OK, %d FAIL ==\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
