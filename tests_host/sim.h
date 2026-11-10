// Jednoduchý simulátor děliče "BIAS --R-- uzel(ADC) --Rs--[Cdl]-- GND" pro host testy.
//  - R      pull-up
//  - Rs     odpor zeminy mezi elektrodami (roztok)
//  - Cdl    kapacita dvojvrstvy elektrod (polarizace), v sérii s Rs
//  - Cn     kapacita uzlu k zemi (např. kondenzátor u ADC)
//  Výstup v ADC čítačích, "naprázdno" = open (typ. 4095 nebo menší).
#pragma once
#include <math.h>
#include <stdint.h>

#include <random>

struct SimProbe {
  double R = 10e3, Rs = 10e3, Cdl = 1.0 /*F = prakticky bez polarizace*/, Cn = 0.0;
  double open = 4095.0;   // čítač při x = 1
  double noise = 0.0;     // sigma šumu (čítače)
  double spikeProb = 0.0; // pravděpodobnost rušivého vzorku
  double spikeAmp = 400.0;
  double rampPerMs = 0.0; // umělý lineární drift (čítače / ms)
  uint32_t adcTimeUs = 40;

  // stav
  double vc = 0.0;        // napětí na Cdl (normované: 1 = Vexc)
  double vn = 0.0;        // napětí uzlu
  double tNow = 0.0;      // s
  std::mt19937 rng{12345};

  void reset() { vc = 0.0; vn = 0.0; tNow = 0.0; }

  void advanceTo(double tUs) {
    const double tTarget = tUs * 1e-6;
    const double dtMax = 5e-6;
    while (tNow < tTarget) {
      double dt = tTarget - tNow;
      if (dt > dtMax) dt = dtMax;
      step(dt);
      tNow += dt;
    }
  }

  void step(double dt) {
    // node (normované Vexc = 1)
    if (Cn <= 0.0) {
      // algebraicky: vn*(1/R + 1/Rs) = 1/R + vc/Rs
      vn = (1.0 / R + vc / Rs) / (1.0 / R + 1.0 / Rs);
    } else {
      const double i_in = (1.0 - vn) / R;
      const double i_b = (vn - vc) / Rs;
      vn += dt * (i_in - i_b) / Cn;
    }
    const double i_b = (vn - vc) / Rs;
    vc += dt * i_b / Cdl;
  }

  uint16_t sample(double tUs) {
    advanceTo(tUs);
    double v = vn * open + rampPerMs * (tUs / 1000.0);
    std::normal_distribution<double> g(0.0, 1.0);
    std::uniform_real_distribution<double> u(0.0, 1.0);
    if (noise > 0.0) v += g(rng) * noise;
    if (spikeProb > 0.0 && u(rng) < spikeProb) v += (u(rng) < 0.5 ? -1.0 : 1.0) * spikeAmp;
    if (v < 0) v = 0;
    if (v > 4095) v = 4095;
    return (uint16_t)lround(v);
  }
};
