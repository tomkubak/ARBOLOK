// ============================================================================
//  battery_curve.h  –  napětí -> % kapacity pro jeden článek LiFePO4 (čisté C++)
//
//  LiFePO4 má velmi plochou vybíjecí křivku (3.2–3.3 V po většinu kapacity),
//  takže lineární mapování 3.10–3.30 V -> 0–100 % (v3) výrazně zkresluje.
//  Tabulka je typická křivka naprázdno / při malém zatížení. Je to ODHAD:
//  v ploché části křivky je nejistota stovky mV ADC proti desítkám mV mezi stupni.
// ============================================================================
#pragma once
#include <math.h>

inline int lifepo4Percent(float v) {
  if (isnan(v)) return -1;
  struct P {
    float v;
    float p;
  };
  static const P T[] = {{2.50f, 0},  {2.90f, 3},  {3.00f, 10}, {3.20f, 20}, {3.22f, 30}, {3.25f, 40},
                        {3.26f, 50}, {3.27f, 60}, {3.30f, 70}, {3.32f, 80}, {3.35f, 90}, {3.40f, 100}};
  const int N = (int)(sizeof(T) / sizeof(T[0]));
  if (v <= T[0].v) return 0;
  if (v >= T[N - 1].v) return 100;
  for (int i = 1; i < N; i++) {
    if (v <= T[i].v) {
      const float f = (v - T[i - 1].v) / (T[i].v - T[i - 1].v);
      return (int)lroundf(T[i - 1].p + f * (T[i].p - T[i - 1].p));
    }
  }
  return 100;
}
