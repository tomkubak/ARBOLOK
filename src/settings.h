// ============================================================================
//  settings.h  –  uživatelské nastavení (ukládá se do NVS / Preferences)
//  Struktura + validace jsou čisté C++ (testovatelné na PC),
//  načtení/uložení je v settings.cpp.
// ============================================================================
#pragma once
#include <stdint.h>
#include <string.h>

#include "config.h"
#include "moisture.h"
#include "wiring.h"

struct Settings {
  char lang[3];            // "cz" | "en" | "ua"
  uint16_t webMin;         // doba běhu webu v minutách (1..30)
  uint16_t intervalMin;    // interval záznamu v minutách (0 = vypnuto, 1..INTERVAL_MAX_MIN)
  uint8_t dryPct;          // "je třeba zalévat" pod touto hodnotou (%)
  uint8_t wetPct;          // "velmi mokro" nad touto hodnotou (%)
  moist::Calib cal;        // kalibrace sondy
  bool vexcTrack;          // čítač se normalizuje na napětí baterie (ESP32 je napájeno přímo z článku; výchozí zapnuto)
  uint8_t probeMode;       // 0 = auto (adaptivní okno), 1 = pevné okno = strop pulzu (jako v3), 2 = krátký pulz (earlyUs)
  float rPullK;            // pull-up děliče v kΩ: vždy DEF_PULLUP_KOHM (10), jen pro zobrazení odporu půdy
  float vbatCorr;          // korekce napětí baterie (0.80..1.20)
  bool timeSet;            // hodiny byly nastaveny uživatelem
  uint8_t biasPin;         // GPIO pinu BIAS sondy (wiring::kPins); 0 = zatím neurčeno -> pin ze sestavení + rozpoznání při startu webu
  // --- pokročilé (pro odborníky) ---
  uint8_t retPin;          // druhá elektroda: 0 = napevno na GND (jako dosud), jinak GPIO (wiring::kRetPins) -> obrácená polarita
  uint16_t maxMs;          // strop délky jednoho pulzu (ms), PROBE_MAXMS_MIN..PROBE_MAXMS_MAX
  uint8_t epsCounts;       // tolerance ustálení (ADC čítače), 1..50
  uint16_t predischMs;     // vybití elektrod před pulzem (ms), 0..500
  uint8_t logPulses;       // pulzů na jeden záznam do logu (medián), 1..5; 1 = jeden pulz + ověření při podezření
  uint16_t confDelta;      // skok čítače proti minulému záznamu, od kterého se jeden pulz ověřuje (20..2000)
  uint8_t wJumpPct;        // detekce zalití na stránce: nárůst vlhkosti aspoň o tolik % (1..50)
  uint8_t wWinH;           // ... během nejvýš tolika hodin (1..48)
  uint16_t earlyUs;        // režim 2: délka krátkého pulzu v µs (PROBE_EARLY_MIN..MAX); hodnota = medián druhé poloviny
  uint8_t locActive;       // aktivní místo (locations.h), 0..LOC_MAX-1
  uint8_t captive;         // po připojení k Wi-Fi: 0 = nic, 1 = okno s rozcestníkem (captive portal), 2 = tiše (bez okna)
  uint8_t txLow;           // vysílač Wi-Fi: 0 = normální (15 dBm), 1 = nízký (8,5 dBm, telefon u stromu)
  // ruční zápis (přenosný logger, od v6.10)
  uint8_t manOn;           // 1 = na Přehledu je tlačítko Zapsat měření
  uint8_t manNote;         // poznámka k zápisu: 0 = žádná, 1 = text od uživatele, 2 = automatické číslo („Bod 1, 2…“)
  uint8_t manExt;          // 1 = každý zápis prodlouží stránku (aspoň 5 min zbývá), až do SESSION_MAX_MAN_SEC
  uint8_t liveSec;         // jak často se při běžícím webu obnovuje živá hodnota (s), 5..120 (od v6.11)
};

inline void settingsDefaults(Settings& s) {
  s = Settings();  // vynuluje i vnořenou kalibraci
  strcpy(s.lang, "cz");
  s.webMin = DEF_WEB_RUNTIME_MIN;
  s.intervalMin = DEF_INTERVAL_MIN;
  s.dryPct = 30;
  s.wetPct = 85;
  s.cal = moist::Calib();
  s.cal.gDry = DEF_CAL_G_DRY;
  s.cal.gWet = DEF_CAL_G_WET;
  s.cal.rawOpen = DEF_CAL_RAW_OPEN;
  s.cal.scale = 1;
  s.vexcTrack = DEF_VEXC_TRACK;
  s.probeMode = 2;  // pevně krátký pulz: odečte se dřív, než se elektrody polarizují (od v5.1 jen výchozí hodnoty)
  s.rPullK = DEF_PULLUP_KOHM;
  s.vbatCorr = VBAT_DEF_CORRECTION;
  s.timeSet = false;
  s.biasPin = 0;
  s.retPin = 0;
  s.maxMs = (uint16_t)(PROBE_MAX_US / 1000UL);
  s.epsCounts = (uint8_t)PROBE_EPS_COUNTS;
  s.predischMs = PROBE_PREDISCHARGE_MS;
  s.logPulses = 1;
  s.confDelta = DEF_CONFIRM_DELTA;
  s.wJumpPct = DEF_WJUMP_PCT;
  s.wWinH = DEF_WWIN_H;
  s.locActive = 0;
  s.captive = 0;  // výchozí nic: okénko přihlášení k síti (Mac, iPhone) překáží při nahrávání firmwaru
  s.earlyUs = DEF_EARLY_US;
  s.txLow = 0;
  s.manOn = 0;
  s.manNote = 2;
  s.manExt = 1;
  s.liveSec = DEF_LIVE_SEC;
}

template <class T>
inline T clampT(T v, T lo, T hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

// Ošetří (ořízne) všechny hodnoty do povolených mezí. Volat po načtení z NVS
// i po příjmu z webu – chrání před přetečením, nesmysly a poškozenou NVS.
inline void settingsSanitize(Settings& s) {
  if (strcmp(s.lang, "cz") != 0 && strcmp(s.lang, "en") != 0 && strcmp(s.lang, "ua") != 0) strcpy(s.lang, "cz");
  s.webMin = clampT<uint16_t>(s.webMin, 1, 30);
  if (s.intervalMin > INTERVAL_MAX_MIN) s.intervalMin = INTERVAL_MAX_MIN;  // 0 = vypnuto; starší nastavení nad 120 min se ořízne
  if (s.dryPct > 100) s.dryPct = 100;
  if (s.wetPct > 100) s.wetPct = 100;
  if (s.wetPct <= s.dryPct) {  // musí platit dry < wet
    if (s.dryPct >= 99) s.dryPct = 98;
    s.wetPct = (uint8_t)(s.dryPct + 1);
  }
  if (!moist::calibValid(s.cal)) {
    const bool scaleOk = s.cal.scale <= 1;
    const uint8_t keep = scaleOk ? s.cal.scale : 1;
    s.cal = moist::Calib();
    s.cal.gDry = DEF_CAL_G_DRY;
    s.cal.gWet = DEF_CAL_G_WET;
    s.cal.rawOpen = DEF_CAL_RAW_OPEN;
    s.cal.scale = keep;
  }
  if (s.cal.scale > 1) s.cal.scale = 1;
  s.cal.rawOpen = DEF_CAL_RAW_OPEN;  // naprázdno = vždy plný rozsah (od v5.7 se nekalibruje)
  if (s.probeMode > 2) s.probeMode = 2;
  if (s.probeMode == 0) s.probeMode = 1;  // od v6.1 jen krátký (2) a dlouhý pulz 300 ms (1, starší sondy)  // neplatné -> výchozí krátký pulz
  if (s.captive > 2) s.captive = 0;
  if (s.txLow > 1) s.txLow = 0;
  if (s.manOn > 1) s.manOn = 0;
  if (s.manNote > 2) s.manNote = 2;
  if (s.manExt > 1) s.manExt = 1;
  s.liveSec = clampT<uint8_t>(s.liveSec, 5, 120);
  if (s.locActive >= 8) s.locActive = 0;
  s.earlyUs = clampT<uint16_t>(s.earlyUs, PROBE_EARLY_MIN_US, PROBE_EARLY_MAX_US);
  s.rPullK = DEF_PULLUP_KOHM;  // pull-up děliče je na desce vždy 10 kΩ (od v5.4 se nenastavuje)
  if (!(s.vbatCorr >= 0.80f && s.vbatCorr <= 1.20f)) s.vbatCorr = VBAT_DEF_CORRECTION;
  if (s.biasPin != 0 && !wiring::validPin(s.biasPin)) s.biasPin = 0;  // neznámý pin se nikdy nebudí
  if (s.retPin != 0 && !wiring::validRetPin(s.retPin)) s.retPin = 0;  // neznámý pin se nikdy nebudí
  s.maxMs = clampT<uint16_t>(s.maxMs, PROBE_MAXMS_MIN, PROBE_MAXMS_MAX);
  s.epsCounts = clampT<uint8_t>(s.epsCounts, 1, 50);
  s.predischMs = clampT<uint16_t>(s.predischMs, 0, 500);
  s.logPulses = clampT<uint8_t>(s.logPulses, 1, 5);
  s.confDelta = clampT<uint16_t>(s.confDelta, 20, 2000);
  s.wJumpPct = clampT<uint8_t>(s.wJumpPct, 1, 50);
  s.wWinH = clampT<uint8_t>(s.wWinH, 1, 48);
}

// Nejdelší doba běhu stránky: s ručním zápisem (obcházení stromů) hodina, jinak 30 min.
inline uint32_t sessionMaxSec(const Settings& s) { return s.manOn ? (uint32_t)SESSION_MAX_MAN_SEC : (uint32_t)SESSION_MAX_SEC; }

// Pin BIAS, který se skutečně používá: uložený, jinak ten ze sestavení (config.h).
inline uint8_t settingsBiasPin(const Settings& s) { return s.biasPin ? s.biasPin : (uint8_t)SONDA_BIAS_PIN; }

bool settingsLoad(Settings& s);   // true = v NVS něco bylo
bool settingsSave(const Settings& s);
void settingsFactoryReset();

// ---------------------------------------------------------------------------
//  Částečná změna nastavení z webu: uživatelské formuláře mění jen některá pole.
//  Kalibrace (měřená v hlavní smyčce) se tím nikdy nepřepíše starou kopií.
// ---------------------------------------------------------------------------
enum : uint32_t {
  SM_LANG = 1,
  SM_WEB = 2,
  SM_INT = 4,
  SM_DRY = 8,
  SM_WET = 16,
  SM_PMODE = 32,
  SM_VEXC = 64,
  SM_SCALE = 128,
  SM_RPULL = 256,
  SM_BIAS = 512,
  SM_RET = 1024,
  SM_PROBE = 2048,  // maxMs, epsCounts, predischMs, logPulses, confDelta
  SM_WDET = 4096,   // wJumpPct, wWinH
  SM_CAPT = 8192,
  SM_VCORR = 16384,  // korekce napětí baterie (obnova zálohy; jinak se nastavuje kalibrací podle multimetru)
  SM_TXP = 32768,    // vysílač Wi-Fi (normální / nízký)
  SM_MAN = 65536,    // ruční zápis: manOn, manNote, manExt (od v6.10; maska je od té doby 32bitová)
  SM_LIVE = 131072   // obnovování živé hodnoty na stránce (liveSec)
};

inline void applySettingsMasked(Settings& dst, const Settings& src, uint32_t mask) {
  if (mask & SM_LANG) strcpy(dst.lang, src.lang);
  if (mask & SM_WEB) dst.webMin = src.webMin;
  if (mask & SM_INT) dst.intervalMin = src.intervalMin;
  if (mask & SM_DRY) dst.dryPct = src.dryPct;
  if (mask & SM_WET) dst.wetPct = src.wetPct;
  if (mask & SM_PMODE) dst.probeMode = src.probeMode;
  if (mask & SM_VEXC) dst.vexcTrack = src.vexcTrack;
  if (mask & SM_SCALE) dst.cal.scale = src.cal.scale;
  if (mask & SM_RPULL) dst.rPullK = src.rPullK;
  if (mask & SM_BIAS) dst.biasPin = src.biasPin;
  if (mask & SM_RET) dst.retPin = src.retPin;
  if (mask & SM_PROBE) {
    dst.maxMs = src.maxMs;
    dst.epsCounts = src.epsCounts;
    dst.predischMs = src.predischMs;
    dst.logPulses = src.logPulses;
    dst.confDelta = src.confDelta;
    dst.earlyUs = src.earlyUs;
  }
  if (mask & SM_CAPT) dst.captive = src.captive;
  if (mask & SM_VCORR) dst.vbatCorr = src.vbatCorr;
  if (mask & SM_TXP) dst.txLow = src.txLow;
  if (mask & SM_LIVE) dst.liveSec = src.liveSec;
  if (mask & SM_MAN) {
    dst.manOn = src.manOn;
    dst.manNote = src.manNote;
    dst.manExt = src.manExt;
  }
  if (mask & SM_WDET) {
    dst.wJumpPct = src.wJumpPct;
    dst.wWinH = src.wWinH;
  }
  settingsSanitize(dst);
}

