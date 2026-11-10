// ============================================================================
//  probe.h  –  hardwarové měření vlhkostní sondy (GPIO + ADC)
// ============================================================================
#pragma once
#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "moisture.h"
#include "settings.h"
#include "wiring.h"

struct ProbeMeasurement {
  bool ok = false;
  float base = 0;       // čítač uzlu těsně před pulzem (BIAS dole); plató - base = odezva uzlu na BIAS
  float rawRaw = 0;     // surový medián okna (ADC čítače)
  float raw = 0;        // po normalizaci buzení (= rawRaw, když vexcTrack je vypnuto)
  float g = 0;          // index vodivosti
  float percent = 0;    // 0..100, okamžitá hodnota; NAN = zkrat / uzel nereaguje na BIAS (moist::isFault(flags))
  uint16_t settleMs = 0;
  bool settled = false;
  float spread = 0;
  uint8_t flags = 0;    // moist::FLAG_*
  float rawB = NAN;     // surové plató fáze B (obrácená polarita); NAN = fáze B neproběhla
  uint8_t phase = 0;    // moist::PH_A / PH_B / PH_RATIO: odkud je g (a raw, percent)
};

// Nastaví výchozí pin BIAS ze sestavení (config.h) jako výstup v LOW.
void probeInit();
// Přepne na pin BIAS podle uloženého nastavení (settingsBiasPin). Neplatný pin = pin ze sestavení.
void probeUsePin(uint8_t pin);
uint8_t probeBiasPin();  // pin BIAS, který se právě používá
// Druhá elektroda: 0 = napevno na GND (jako dosud), jinak GPIO z wiring::kRetPins -> měření i s obrácenou polaritou.
// Mezi měřeními je pin vstup (vysoká impedance), takže sonda je úplně odpojená.
void probeUseRetPin(uint8_t pin);
uint8_t probeRetPin();
// Sonda do klidu (BIAS dole, druhá elektroda vysoká impedance). Volá se po každém měření a před spánkem.
void probeRelease();

// Jeden pulz BIAS (adaptivní nebo pevné okno podle s.probeMode).
// vexc = napětí buzení v V (typicky napětí baterie), použije se jen když s.vexcTrack.
bool probeMeasureOnce(const Settings& s, float vexc, ProbeMeasurement& out);

// Medián z několika měření (kalibrace, záznam s logPulses > 1) – mezi nimi je pauza na vybití elektrod.
bool probeMeasureMedian(const Settings& s, float vexc, uint8_t pulses, ProbeMeasurement& out);

// Diagnostika: zachytí průběh napětí ~300 ms po sepnutí BIAS, vrací počet bodů.
size_t probeCaptureProfile(float* tMs, float* raw, size_t maxOut);

// Test zapojení: pro každého kandidáta na pin BIAS (wiring::kPins) změří čítač uzlu při BIAS dole a nahoře
// (~50 ms na kandidáta). Vyplní out (nejvýš maxN vzorků), vrací počet. lazy = když je pravý pin jasný už z prvních
// dvou kandidátů, GPIO25 se nezkouší. preferred = pin, který se právě používá (rozhoduje při shodě).
// Používaný pin BIAS zůstane po testu výstupem v LOW, ostatní kandidáti se pustí (vstup).
size_t probeWiringTest(wiring::Sample* out, size_t maxN, bool lazy, uint8_t preferred);
