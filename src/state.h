// ============================================================================
//  state.h  –  sdílený stav mezi hlavní smyčkou (loop) a webovými handlery
//
//  Pravidlo (zjednodušuje souběh): HARDWARE (ADC, I²C, NVS, RTC) používá JEN
//  hlavní smyčka. Webové handlery (běží v tasku AsyncTCP) pouze čtou kopii stavu
//  a změny posílají hlavní smyčce jako příkazy přes frontu (cmd.h).
// ============================================================================
#pragma once
#include <Arduino.h>

#include "settings.h"
#include "wiring.h"

struct LiveState {
  // --- poslední měření ---
  bool haveMeas = false;
  float vbat = NAN;
  int bpct = -1;
  float temp = NAN;
  float hum = -1.0f;        // %, okamžitá hodnota (bez filtru)
  float raw = NAN;          // normalizovaný ADC čítač
  float rawRaw = NAN;       // surový ADC čítač
  float g = NAN;            // index vodivosti
  uint16_t settleMs = 0;
  bool settled = false;
  uint8_t flags = 0;        // moist::FLAG_*
  float rawB = NAN;         // surové plató fáze B (obrácená polarita), NAN = neproběhla
  uint8_t phase = 0;        // moist::PH_* (odkud je hodnota)
  uint32_t measAtMs = 0;    // millis() posledního měření

  // --- hodiny (cache z RTC, aby handlery nesahaly na I²C) ---
  bool rtcOk = false;
  uint32_t rtcUnix = 0;     // "lokální" unix čas z RTC
  uint32_t rtcAtMs = 0;     // millis() v okamžiku čtení

  // --- stav zařízení ---
  bool busy = false;        // běží měření / kalibrace
  bool timeSet = true;
  bool logErr = false;
  bool ota = false;         // probíhá aktualizace firmwaru (nespat!)
  uint32_t fsTotal = 0;     // velikost LittleFS, použito, velikost logu (obnovuje hlavní smyčka)
  uint32_t fsUsed = 0;
  uint32_t logBytes = 0;
  uint32_t sessionStartMs = 0;
  uint32_t sessionEndMs = 0;
  uint32_t sessionTotalSec = 0;
  char msg[32] = "";        // výsledek posledního příkazu ("ok", "err:...")
  uint32_t msgSeq = 0;

  // --- test zapojení (který pin řídí BIAS): výsledek posledního testu, ať automatického (start webu) nebo ručního ---
  bool wireHave = false;
  uint8_t wireRes = 0;      // wiring::Verdict
  uint8_t wirePin = 0;      // nalezený pin (jen při wiring::V_FOUND)
  uint8_t wireN = 0;        // kolik kandidátů se zkoušelo
  uint8_t wirePins[wiring::kPinCount] = {};
  float wireLo[wiring::kPinCount] = {};   // čítač uzlu při BIAS dole
  float wireHi[wiring::kPinCount] = {};   // čítač uzlu při BIAS nahoře

  // --- spotřeba a šetřící režim (od v6.5; čítače z RTC RAM, viz main.cpp publishPower) ---
  uint32_t pwrSince = 0, pwrBoots = 0, pwrLogs = 0, pwrAwakeMs = 0, pwrWebSec = 0, pwrSleepSec = 0;
  bool saving = false;      // šetřící režim (článek pod VBAT_SAVE_V): interval záznamu je dvojnásobný
  uint16_t intEffMin = 0;   // skutečný interval záznamu (min)
  float driftK = 0.0f;      // naučená chyba časovače spánku (0 = ještě ne)

  // --- diagnostický profil ---
  bool profValid = false;
  uint8_t profN = 0;
  float profT[48];
  float profV[48];
};

extern LiveState g_live;
extern Settings g_set;
extern portMUX_TYPE g_stateMux;
extern char g_devId[12];  // sériové číslo (7 číslic), vyplní setup()

struct StateLock {
  StateLock() { portENTER_CRITICAL(&g_stateMux); }
  ~StateLock() { portEXIT_CRITICAL(&g_stateMux); }
};

inline void liveSnapshot(LiveState& dst, Settings& setDst) {
  StateLock l;
  dst = g_live;
  setDst = g_set;
}
