#include "battery.h"

#include <Arduino.h>

#include <algorithm>

#include "config.h"

void batteryInit() {
  pinMode(VREF_ON_PIN, OUTPUT);
  pinMode(VDIV_ON_PIN, OUTPUT);
  digitalWrite(VREF_ON_PIN, LOW);
  digitalWrite(VDIV_ON_PIN, LOW);
  pinMode(VREF_ADC_PIN, INPUT);
  pinMode(VDIV_ADC_PIN, INPUT);
}

// Ořezaný průměr: zahodí čtvrtinu nejnižších a čtvrtinu nejvyšších vzorků.
static float trimmedMean(uint16_t* a, int n) {
  std::sort(a, a + n);
  const int cut = n / 4;
  double sum = 0.0;
  int cnt = 0;
  for (int i = cut; i < n - cut; i++) {
    sum += a[i];
    cnt++;
  }
  return cnt ? (float)(sum / cnt) : 0.0f;
}

BatteryReading batteryMeasure(float correction) {
  BatteryReading r;

  digitalWrite(VREF_ON_PIN, HIGH);
  digitalWrite(VDIV_ON_PIN, HIGH);
  delay(VBAT_SETTLE_MS);  // ustálení reference a děliče

  (void)analogRead(VREF_ADC_PIN);  // první čtení kanálu zahodit
  (void)analogRead(VDIV_ADC_PIN);

  uint16_t refS[VBAT_SAMPLES], divS[VBAT_SAMPLES];
  for (int i = 0; i < VBAT_SAMPLES; i++) {
    refS[i] = (uint16_t)analogRead(VREF_ADC_PIN);
    divS[i] = (uint16_t)analogRead(VDIV_ADC_PIN);
    delay(2);
  }

  digitalWrite(VREF_ON_PIN, LOW);
  digitalWrite(VDIV_ON_PIN, LOW);

  r.refCounts = trimmedMean(refS, VBAT_SAMPLES);
  r.divCounts = trimmedMean(divS, VBAT_SAMPLES);

  // Ochrana proti dělení nulou a nesmyslům (odpojená reference / dělič)
  if (r.refCounts < VBAT_REF_MIN_COUNTS || r.refCounts > VBAT_REF_MAX_COUNTS || r.divCounts < 50.0f) {
    r.ok = false;
    r.volts = r.voltsRaw = 0.0f;
    return r;
  }
  r.voltsRaw = 2.0f * REF_VOLTAGE * (r.divCounts / r.refCounts);
  r.volts = r.voltsRaw * correction;
  r.ok = true;
  return r;
}
