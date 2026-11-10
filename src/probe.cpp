#include "probe.h"

#include <Arduino.h>

#include <algorithm>

#include "config.h"
#include "wiring.h"

static uint32_t s_lastPulseEndMs = 0;
static uint8_t s_bias = SONDA_BIAS_PIN;  // pin BIAS, který se právě používá (viz probeUsePin)
static uint8_t s_ret = 0;                // pin druhé elektrody (0 = napevno na GND), viz probeUseRetPin

uint8_t probeBiasPin() { return s_bias; }
uint8_t probeRetPin() { return s_ret; }

void probeInit() {
  s_bias = SONDA_BIAS_PIN;
  pinMode(s_bias, OUTPUT);
  digitalWrite(s_bias, LOW);
  pinMode(SONDA_ADC_PIN, INPUT);
}

void probeUsePin(uint8_t pin) {
  if (!wiring::validPin(pin)) pin = SONDA_BIAS_PIN;
  if (pin != s_bias) {
    digitalWrite(s_bias, LOW);
    pinMode(s_bias, INPUT);  // dosavadní pin se pustí: na této desce nic nebudí a nemá co rušit
    s_bias = pin;
  }
  pinMode(s_bias, OUTPUT);
  digitalWrite(s_bias, LOW);
}

// Druhá elektroda: mezi měřeními vysoká impedance (sonda je úplně odpojená), jako "zem" jen během měření.
void probeUseRetPin(uint8_t pin) {
  if (pin != 0 && !wiring::validRetPin(pin)) pin = 0;
  if (s_ret && s_ret != pin) pinMode(s_ret, INPUT);
  s_ret = pin;
  if (s_ret) pinMode(s_ret, INPUT);
}

void probeRelease() {
  digitalWrite(s_bias, LOW);
  if (s_ret) pinMode(s_ret, INPUT);
}

// Druhá elektroda jako zem (výstup LOW) – před každým pulzem i před testem zapojení.
static void retLow() {
  if (!s_ret) return;
  pinMode(s_ret, OUTPUT);
  digitalWrite(s_ret, LOW);
}

static moist::SettleParams paramsFor(const Settings& s) {
  moist::SettleParams p;
  p.t0Us = PROBE_T0_US;
  p.maxUs = (uint32_t)s.maxMs * 1000UL;
  p.epsCounts = (float)s.epsCounts;
  p.minSamples = PROBE_MIN_SAMPLES;
  p.stopEarly = (s.probeMode == 0);
  return p;
}

// Jeden pulz: ustálená hodnota + čítač uzlu těsně PŘED pulzem (vše dole). Rozdíl obou je odezva uzlu na buzení:
// bez odezvy BIAS dělič nebudí (špatný pin) a měření nic neříká o zemině (viz moist::isNoResponse).
struct Pulse {
  moist::SettleResult r;
  float base = 0.0f;
};

// Příprava před pulzem: BIAS (a druhá elektroda) dole, krátká pauza na vybití elektrod (polarizace z předchozího
// pulzu), "zahřátí" ADC kanálu (první čtení bývá nepřesné) a základní hodnota uzlu (medián z 5 čtení). Vrací ji.
static float prepare(uint16_t predischMs) {
  digitalWrite(s_bias, LOW);
  retLow();
  const uint32_t since = millis() - s_lastPulseEndMs;
  if (since < 3000UL && predischMs) delay(predischMs);
  for (int i = 0; i < 4; i++) (void)analogRead(SONDA_ADC_PIN);
  uint16_t b[5];
  for (int i = 0; i < 5; i++) b[i] = (uint16_t)analogRead(SONDA_ADC_PIN);
  std::sort(b, b + 5);
  return (float)b[2];
}

// reverse = fáze B: budí se druhá elektroda (BIAS zůstává dole a jeho pull-up slouží jako "zem" děliče).
static Pulse capturePulse(const Settings& s, bool reverse) {
  moist::SettleDetector det(paramsFor(s));
  Pulse out;
  out.base = prepare(s.predischMs);
  const uint8_t drive = reverse ? s_ret : s_bias;

  const uint32_t t0 = micros();
  digitalWrite(drive, HIGH);
  if (s.probeMode == 2) {
    // Krátký pulz: hodnota dřív, než se elektrody stihnou polarizovat (v polarizující zemině čítač roste celé stovky
    // ms). Čte se celou dobu, hodnotou je medián vzorků z druhé poloviny pulzu (první polovinu bere zákmit uzlu).
    const uint32_t T = s.earlyUs;
    uint16_t v[64];
    uint8_t n = 0;
    uint32_t t;
    while ((t = micros() - t0) < T) {
      const uint16_t a = (uint16_t)analogRead(SONDA_ADC_PIN);
      if (t >= T / 2 && n < 64) v[n++] = a;
    }
    if (n == 0) v[n++] = (uint16_t)analogRead(SONDA_ADC_PIN);
    digitalWrite(drive, LOW);
    s_lastPulseEndMs = millis();
    std::sort(v, v + n);
    out.r.value = (float)v[n / 2];
    out.r.tSettleUs = T;
    out.r.settled = true;
    out.r.spread = (float)(v[n - 1] - v[0]);
    out.r.n = n;
    out.r.flags = 0;
    return out;
  }
  for (;;) {
    const uint32_t t = micros() - t0;
    const uint16_t v = (uint16_t)analogRead(SONDA_ADC_PIN);
    if (det.feed(t, v)) break;
    const uint32_t gap = det.gapUs();
    if (gap > 50) delayMicroseconds(gap - 50);  // samotné čtení ADC zabere ~40+ µs
  }
  digitalWrite(drive, LOW);
  s_lastPulseEndMs = millis();
  out.r = det.result();
  return out;
}

static void fill(const Settings& s, float vexc, const moist::SettleResult& r, float base, ProbeMeasurement& m) {
  m.ok = true;
  m.rawRaw = r.value;
  m.base = base;
  m.raw = moist::normalizeRaw(r.value, s.vexcTrack ? vexc : 0.0f, VEXC_REF_VOLTS);
  m.g = moist::conductance(m.raw, (float)s.cal.rawOpen);
  m.percent = moist::percentFromRaw(m.raw, s.cal);
  const uint32_t ms = r.tSettleUs / 1000UL;
  m.settleMs = (uint16_t)(ms > 65535UL ? 65535UL : ms);
  m.settled = r.settled;
  m.spread = r.spread;
  m.flags = r.flags;
  m.rawB = NAN;
  m.phase = moist::PH_A;
  // Podle normalizované hodnoty (stejně jako g a procenta): při článku pod ~3,1 V je sonda ve vzduchu na uzlu
  // pod rozsahem ADC (čítač ~3950), ale po přepočtu na 3,3 V je "naprázdno" jako při plném článku.
  if (moist::isOpenCircuit(m.raw, s.cal)) m.flags |= moist::FLAG_SAT_HIGH;
  if (moist::isShort(r.value)) m.flags |= moist::FLAG_SAT_LOW;
  else if (moist::isNoResponse(r.value, base)) m.flags |= moist::FLAG_NO_RESP;
  // Zkrat / nereagující uzel: o vlhkosti to nic neříká. Procento by vyšlo 100 % (g roste nade všechny meze), což by
  // vypadalo jako "mokro, nezalévat". Raději žádná hodnota (NAN -> "--" na stránce, "--%" v logu).
  if (moist::isFault(m.flags)) m.percent = NAN;
}

// Doplní fázi B (obrácená polarita) do měření z fáze A.
static void fillDual(const Settings& s, float vexc, const Pulse& b, ProbeMeasurement& m) {
  m.rawB = b.r.value;
  if (moist::isFault(m.flags)) return;
  const float bn = moist::normalizeRaw(b.r.value, s.vexcTrack ? vexc : 0.0f, VEXC_REF_VOLTS);
  const moist::DualResult d = moist::combineDual(m.rawRaw, b.r.value, b.base, bn, (float)s.cal.rawOpen);
  if (d.retBad) {
    m.flags |= moist::FLAG_RET_BAD;
    return;
  }
  if (d.phase == moist::PH_A) return;
  m.phase = d.phase;
  m.g = d.g;
  m.raw = moist::rawFromG(d.g, (float)s.cal.rawOpen);
  m.percent = moist::percentFromRaw(m.raw, s.cal);
  m.flags &= (uint8_t)~moist::FLAG_SAT_HIGH;
  if (moist::isOpenCircuit(m.raw, s.cal)) m.flags |= moist::FLAG_SAT_HIGH;
  if (!b.r.settled) m.flags |= moist::FLAG_UNSETTLED;
  m.flags |= (uint8_t)(b.r.flags & moist::FLAG_NOISY);
}

// Jedno úplné měření: fáze A, při zapnuté obrácené polaritě ještě fáze B. Sonda je pak zase odpojená.
static void measureOne(const Settings& s, float vexc, ProbeMeasurement& out) {
  const Pulse a = capturePulse(s, false);
  fill(s, vexc, a.r, a.base, out);
  if (s_ret && !moist::isFault(out.flags)) {
    const Pulse b = capturePulse(s, true);
    fillDual(s, vexc, b, out);
  }
  probeRelease();
}

bool probeMeasureOnce(const Settings& s, float vexc, ProbeMeasurement& out) {
  measureOne(s, vexc, out);
  return true;
}

bool probeMeasureMedian(const Settings& s, float vexc, uint8_t pulses, ProbeMeasurement& out) {
  if (pulses < 1) pulses = 1;
  if (pulses > 5) pulses = 5;
  ProbeMeasurement ms[5];
  float vals[5];
  for (uint8_t i = 0; i < pulses; i++) {
    if (i) delay(PROBE_REPEAT_GAP_MS);
    measureOne(s, vexc, ms[i]);
    vals[i] = ms[i].raw;
  }
  // Vybere se měření nejblíž mediánu (celé, i s příznaky a fází). Chybová měření (zkrat) vyhrají jen většinou.
  float sorted[5];
  for (uint8_t i = 0; i < pulses; i++) sorted[i] = vals[i];
  std::sort(sorted, sorted + pulses);
  const float med = sorted[pulses / 2];
  uint8_t best = 0;
  for (uint8_t i = 1; i < pulses; i++)
    if (fabsf(vals[i] - med) < fabsf(vals[best] - med)) best = i;
  out = ms[best];
  return true;
}

size_t probeCaptureProfile(float* tMs, float* raw, size_t maxOut) {
  static uint32_t ts[300];
  static uint16_t vs[300];
  size_t n = 0;
  (void)prepare(PROBE_PREDISCHARGE_MS);
  const uint32_t t0 = micros();
  digitalWrite(s_bias, HIGH);
  while (n < 300) {
    const uint32_t t = micros() - t0;
    if (t >= PROBE_MAX_US) break;
    const uint16_t v = (uint16_t)analogRead(SONDA_ADC_PIN);
    ts[n] = t;
    vs[n] = v;
    n++;
    const uint32_t gap = (t < 4000UL) ? 0UL : (t / 16UL);  // hustě na začátku, pak řídce
    if (gap > 50) delayMicroseconds(gap - 50);
  }
  digitalWrite(s_bias, LOW);
  s_lastPulseEndMs = millis();
  probeRelease();
  return moist::buildProfile(ts, vs, n, tMs, raw, maxOut);
}

// Medián z n čtení uzlu (n <= 15).
static float readMedian(uint8_t n) {
  uint16_t v[15];
  if (n > 15) n = 15;
  if (n < 1) n = 1;
  for (uint8_t i = 0; i < n; i++) v[i] = (uint16_t)analogRead(SONDA_ADC_PIN);
  std::sort(v, v + n);
  return moist::medianOfSorted(v, n);
}

size_t probeWiringTest(wiring::Sample* out, size_t maxN, bool lazy, uint8_t preferred) {
  if (!out || maxN == 0) return 0;
  const size_t cand = maxN < wiring::kPinCount ? maxN : wiring::kPinCount;

  retLow();  // druhá elektroda (je-li na pinu) musí být "zem", jinak by dělič nevedl
  // Dokumentované piny dolů (jinak by hi/lo jednoho kandidáta ovlivnil druhý); GPIO25 se nastaví až když na něj přijde řada.
  for (size_t i = 0; i < wiring::kPrimaryCount; i++) {
    pinMode(wiring::kPins[i], OUTPUT);
    digitalWrite(wiring::kPins[i], LOW);
  }
  delay(PROBE_PREDISCHARGE_MS);                          // vybití elektrod a uzlu
  for (int i = 0; i < 4; i++) (void)analogRead(SONDA_ADC_PIN);  // zahřátí ADC

  size_t n = 0;
  for (size_t i = 0; i < cand; i++) {
    const uint8_t pin = wiring::kPins[i];
    if (i >= wiring::kPrimaryCount) {
      pinMode(pin, OUTPUT);
      digitalWrite(pin, LOW);
      delay(WIRING_GAP_MS);
    }
    out[n].pin = pin;
    out[n].lo = readMedian(WIRING_READS);
    digitalWrite(pin, HIGH);
    delay(WIRING_PULSE_MS);  // uzel se ustálí i s kapacitou kabelů a elektrod
    out[n].hi = readMedian(WIRING_READS);
    digitalWrite(pin, LOW);
    delay(WIRING_GAP_MS);    // vybití před dalším kandidátem
    n++;
    if (lazy && n == wiring::kPrimaryCount && n < cand &&
        wiring::choose(out, n, preferred).verdict == wiring::V_FOUND)
      break;  // pravý pin je jasný, GPIO25 se nechá na pokoji
  }

  // Úklid: kandidáti, na které se sáhlo, se pustí (vstup), používaný pin BIAS zůstane výstupem v LOW.
  const size_t touched = n > wiring::kPrimaryCount ? n : wiring::kPrimaryCount;
  for (size_t i = 0; i < touched; i++) {
    if (wiring::kPins[i] != s_bias) pinMode(wiring::kPins[i], INPUT);
  }
  pinMode(s_bias, OUTPUT);
  digitalWrite(s_bias, LOW);
  probeRelease();
  return n;
}
