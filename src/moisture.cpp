#include "moisture.h"

#include <math.h>

#include <algorithm>

namespace moist {

// ---------------------------------------------------------------------------
//  Pomocné
// ---------------------------------------------------------------------------
float medianOfSorted(const uint16_t* a, size_t n) {
  if (n == 0) return 0.0f;
  if (n & 1) return (float)a[n / 2];
  return 0.5f * ((float)a[n / 2 - 1] + (float)a[n / 2]);
}

// ---------------------------------------------------------------------------
//  SettleDetector
// ---------------------------------------------------------------------------
SettleDetector::SettleDetector(const SettleParams& p) : p_(p) {
  if (p_.t0Us == 0) p_.t0Us = 1;
  if (p_.minSamples < 3) p_.minSamples = 3;
  reset();
}

void SettleDetector::reset() {
  n_ = 0;
  winStart_ = p_.t0Us;
  winEnd_ = p_.t0Us * 2;
  havePrev_ = false;
  prevMedian_ = 0.0f;
  done_ = false;
  res_ = SettleResult();
}

uint32_t SettleDetector::gapUs() const {
  uint32_t len = winEnd_ - winStart_;
  return len / (uint32_t)kWinMax;
}

bool SettleDetector::feed(uint32_t tUs, uint16_t raw) {
  if (done_) return true;
  if (tUs >= p_.maxUs) {  // strop délky měření
    finish();
    return true;
  }
  if (tUs < winStart_) return false;  // zákmit před t0 se ignoruje
  while (tUs >= winEnd_) {            // vzorek patří už do dalšího okna
    closeWindow(false);
    if (done_) return true;
  }
  if (n_ < kWinMax) buf_[n_++] = raw;
  return false;
}

void SettleDetector::finish() {
  if (done_) return;
  if (n_ >= p_.minSamples) {
    closeWindow(true);
  } else if (havePrev_) {
    // rozpracované okno je příliš krátké – zůstává výsledek posledního platného
    res_.flags |= res_.settled ? 0 : FLAG_UNSETTLED;
    done_ = true;
  } else if (n_ > 0) {
    // nouzově: aspoň něco (ADC je extrémně pomalé, nebo se měřilo velmi krátce)
    std::sort(buf_, buf_ + n_);
    res_.value = medianOfSorted(buf_, n_);
    res_.spread = (float)(buf_[n_ - 1] - buf_[0]);
    res_.n = n_;
    res_.tSettleUs = winStart_;
    res_.settled = false;
    res_.flags = FLAG_UNSETTLED;
    done_ = true;
  } else {
    res_ = SettleResult();
    res_.flags = FLAG_UNSETTLED;
    done_ = true;
  }
}

void SettleDetector::closeWindow(bool force) {
  if (n_ < p_.minSamples && !force) {
    // Málo vzorků (pomalé ADC v krátkém okně) – okno se prodlouží a sloučí s dalším.
    winEnd_ *= 2;
    return;
  }

  std::sort(buf_, buf_ + n_);
  const float med = medianOfSorted(buf_, n_);
  const float lo = (float)buf_[0];
  const float hi = (float)buf_[n_ - 1];
  const float q1 = (float)buf_[n_ / 4];
  const float q3 = (float)buf_[(3 * n_) / 4];
  const float sigma = (q3 - q1) / 1.349f;
  float tol = 3.0f * sigma / sqrtf((float)n_);
  if (tol < p_.epsCounts) tol = p_.epsCounts;

  const bool agree = havePrev_ && fabsf(med - prevMedian_) <= tol;

  res_.value = med;
  res_.tSettleUs = winStart_;
  res_.spread = hi - lo;
  res_.n = n_;
  res_.settled = agree;
  res_.flags = 0;
  if (!agree) res_.flags |= FLAG_UNSETTLED;
  if (res_.spread > 100.0f) res_.flags |= FLAG_NOISY;

  if (force || (agree && p_.stopEarly)) {
    done_ = true;
    return;
  }

  havePrev_ = true;
  prevMedian_ = med;
  winStart_ = winEnd_;
  winEnd_ *= 2;
  n_ = 0;
}

// ---------------------------------------------------------------------------
//  Převod na vlhkost
// ---------------------------------------------------------------------------
static const float kGMin = 1e-3f;
static const float kSatRaw = 4080.0f;  // = 4095 - 15, stejně jako isOpenCircuit()

bool calibValid(const Calib& c) {
  return c.gDry > 0.0f && c.gWet > c.gDry * 1.2f && c.rawOpen >= 1000 && c.rawOpen <= 4095 &&
         c.gWet < 1000.0f;
}

bool setCalPoint(Calib& c, bool wet, float g) {
  if (!(g > 0.0f)) return false;  // i NaN
  Calib t = c;
  if (wet) t.gWet = g;
  else t.gDry = g < kDryAnchorMin ? kDryAnchorMin : g;
  if (!calibValid(t)) return false;
  c = t;
  return true;
}

// Čítač na uzlu je úměrný napětí článku (ADC má pevnou vnitřní referenci, BIAS = napětí článku), takže
// stejná zemina dává při 3,0 V jiný čítač než při 3,4 V. Přepočet na napětí buzení vexcRefVolts to odstraní
// v lineární části ADC (do ~2,4 V na uzlu), kde se pohybuje vlhká až středně suchá zemina.
// Výjimka: plně nasycený čítač (uzel ~ napájení, nad rozsahem ADC) o napětí buzení nic neříká – kdyby se
// zmenšil (při 3,5 V článku), sonda ve vzduchu by vypadala jako lehce vlhká.
float normalizeRaw(float raw, float vexcVolts, float vexcRefVolts) {
  if (!(vexcVolts >= 2.0f && vexcVolts <= 4.0f)) return raw;  // bez měření baterie / nesmysl / NaN
  if (raw >= kSatRaw) return raw;
  const float n = raw * vexcRefVolts / vexcVolts;
  return n > 4095.0f ? 4095.0f : n;
}

float conductance(float raw, float rawOpen) {
  if (raw < 10.0f) raw = 10.0f;
  if (rawOpen < 500.0f) rawOpen = 500.0f;
  const float g = (rawOpen - raw) / raw;
  return g < kGMin ? kGMin : g;
}

float percentFromRaw(float raw, const Calib& cIn) {
  Calib c = cIn;
  if (!calibValid(c)) c = Calib();  // poškozená kalibrace -> výchozí

  float p;
  if (c.scale == 0) {
    p = 100.0f - (raw / 4095.0f) * 100.0f;  // chování verze 3
  } else {
    const float g = conductance(raw, (float)c.rawOpen);
    const float l0 = log10f(c.gDry);
    const float l1 = log10f(c.gWet);
    p = 100.0f * (log10f(g) - l0) / (l1 - l0);
  }
  if (p < 0.0f) p = 0.0f;
  if (p > 100.0f) p = 100.0f;
  return p;
}

bool isOpenCircuit(float raw, const Calib& c) { return raw >= (float)c.rawOpen - 15.0f; }
// Zkrat jen při čítači opravdu 0 (ESP32 pod ~0,1 V čte 0). I pár čítačů je skutečná, jen extrémně mokrá zem
// (po vydatném zalití klidně ~15), ne chyba.
bool isShort(float raw) { return raw < 0.5f; }
// Požadovaný nárůst: kMinResponse, u nízkého plató (velmi mokrá zem: celý dělič má jen pár desítek čítačů) aspoň
// polovina plató. Plató 16 nad nulou je tedy odezva; uzel visící na 200 s nárůstem 2 není.
bool isNoResponse(float plateau, float baseline) {
  const float need = plateau * 0.5f < kMinResponse ? plateau * 0.5f : kMinResponse;
  return !(plateau - baseline >= need) || !(need > 0.0f);  // i NaN
}

// ---------------------------------------------------------------------------
//  Obrácená polarita
// ---------------------------------------------------------------------------
DualResult combineDual(float a, float b, float baseB, float bNorm, float rawOpen) {
  DualResult r;
  if (!(a >= 0.0f) || !(b >= 0.0f)) return r;  // i NaN
  // Kontrola: teče-li ve fázi A znatelný proud (a pod ~3500 = Rp pod ~60 kΩ), musí se ve fázi B uzel zvednout
  // zhruba o (plné - a). Nezvedne-li se ani o třetinu, druhá elektroda na zvoleném pinu není.
  const float expectB = 4095.0f - a;
  const float need = expectB * 0.3f > kMinResponse ? expectB * 0.3f : kMinResponse;
  if (a < 3500.0f && !(b - baseB >= need)) {
    r.retBad = true;
    return r;
  }
  const bool inA = a >= kLinLo && a <= kLinHi;
  const bool inB = b >= kLinLo && b <= kLinHi;
  if (inA && inB) {
    r.g = b / a;
    r.phase = PH_RATIO;
  } else if (inB && a > kLinHi) {
    if (rawOpen < 500.0f) rawOpen = 500.0f;
    float d = rawOpen - bNorm;
    if (d < 1.0f) d = 1.0f;
    r.g = bNorm / d;
    r.phase = PH_B;
  }
  if (r.phase != PH_A && r.g < kGMin) r.g = kGMin;
  return r;
}

float rawFromG(float g, float rawOpen) {
  if (!(g > 0.0f)) g = kGMin;
  return rawOpen / (1.0f + g);
}

// ---------------------------------------------------------------------------
//  Ověřování podezřelých měření
// ---------------------------------------------------------------------------
bool needsConfirm(uint8_t flags, float raw, float lastRaw, float deltaCounts) {
  if (flags & FLAG_NOISY) return true;
  return lastRaw > 0.0f && fabsf(raw - lastRaw) > deltaCounts;
}

// ---------------------------------------------------------------------------
//  Profil
// ---------------------------------------------------------------------------
size_t buildProfile(const uint32_t* tUs, const uint16_t* raw, size_t n, float* outTms,
                    float* outRaw, size_t maxOut) {
  size_t out = 0;
  double lo = 300.0;  // µs
  const double ratio = 1.35;
  size_t i = 0;
  while (i < n && out < maxOut) {
    while (i < n && (double)tUs[i] < lo) i++;
    const double hi = lo * ratio;
    uint16_t tmp[64];
    size_t m = 0, j = i;
    while (j < n && (double)tUs[j] < hi) {
      if (m < 64) tmp[m++] = raw[j];
      j++;
    }
    if (m) {
      std::sort(tmp, tmp + m);
      outTms[out] = (float)(sqrt(lo * hi) / 1000.0);
      outRaw[out] = medianOfSorted(tmp, m);
      out++;
    }
    i = j;
    lo = hi;
    if (lo > 1e9) break;
  }
  return out;
}

}  // namespace moist
