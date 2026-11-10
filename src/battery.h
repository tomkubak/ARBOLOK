// ============================================================================
//  battery.h  –  měření napětí článku LiFePO4 (dělič 1:2 + přesná reference 1.235 V)
//
//  Princip (beze změny proti v3): poměr čítačů ADC děliče a reference nezávisí na
//  přesnosti/ zesílení ADC:   Ubat = 2 * Uref * (adcDiv / adcRef) * korekce
// ============================================================================
#pragma once
#include <stdint.h>

struct BatteryReading {
  bool ok = false;        // false = reference/dělič mimo rozumný rozsah (HW závada)
  float volts = 0.0f;     // s korekcí
  float voltsRaw = 0.0f;  // bez korekce (pro kalibraci podle multimetru)
  float refCounts = 0.0f;
  float divCounts = 0.0f;
};

// Hibernace při hluboce vybitém článku (viz VBAT_RESUME_V v config.h): nový stav podle změřeného napětí.
// Pod lowV se hibernuje, ven až od resumeV výš. Neplatné měření (ok = false) stav nemění.
inline bool batteryHibernate(bool hibernating, bool ok, float volts, float lowV, float resumeV) {
  if (!ok || !(volts > 0.0f)) return hibernating;
  if (volts < lowV) return true;
  if (hibernating && volts >= resumeV) return false;
  return hibernating;
}

void batteryInit();
BatteryReading batteryMeasure(float correction);
