// ============================================================================
//  bootguard.h  –  ochrana před "zabrickováním", když se firmware nahrává JEN přes OTA
//                  (ČISTÉ C++, žádná závislost na Arduinu; testováno na PC)
//
//  Problém
//    Zařízení nemá sériový převodník. Kdyby nový firmware nastartoval a pak dokola padal
//    (nebo se zasekl při startu), Wi-Fi s OTA stránkou by se nikdy nezapnula na dost dlouho
//    a firmware by už nešel vyměnit.
//
//  Řešení
//    Malý záznam v paměti RTC (RTC_NOINIT: přežije pád, watchdog i restart, nepřežije odpojení
//    baterie) pamatuje, kolik "špatných" startů proběhlo po sobě a v jaké FÁZI program byl.
//    Při dalším startu se rozhodne, jestli se místo běžného programu spustí NOUZOVÝ REŽIM
//    (safemode.cpp): jen Wi-Fi AP + stránka /update, nic dalšího.
//
//  Kdy se spustí nouzový režim
//    1. Pád (panic / watchdog) během startu webového režimu (fáze WEB_START, prvních 45 s webu):
//       hned při dalším startu. Uživatel je u zařízení, ať rovnou nahraje opravu.
//    2. Tři pády v řadě jinde (start, záznam), bez jediného úspěšného cyklu mezi nimi.
//    3. Ručně: magnet u jazýčkového relé při PŘIPOJENÍ baterie (držet ~2 s).
//    Trvá, dokud (a) se neodpojí baterie, (b) se úspěšně nenahraje firmware, nebo (c) uživatel
//    na stránce nezvolí "Zkusit normální režim". Po 10 min bez připojeného telefonu usne
//    (probudí ho magnet) – nevybíjí baterii.
//
//  Co se NEPOČÍTÁ jako pád: podpětí (to řeší normální program: nezapne Wi-Fi), restart z webu,
//  restart po OTA, probuzení ze spánku.
//
//  Záznam si pamatuje, který OBRAZ (oddíl aplikace) ho naposledy zapsal. Po OTA, nebo když bootloader
//  nepotvrzený nový firmware vrátí zpět, běží jiný obraz než ten, který pády napočítal: jeho pády
//  a jeho nouzový režim se nepřenášejí (jinak by zdravý starý firmware skončil v nouzovém režimu
//  kvůli pádu nového).
// ============================================================================
#pragma once
#include <stdint.h>

namespace bootguard {

const uint32_t kMagic = 0xB007C0DEu;
const uint8_t kCrashLimit = 3;  // pády v řadě bez úspěšného cyklu -> nouzový režim
const uint8_t kGiveUp = 6;      // ani nouzový režim neustojí -> Wi-Fi nezapínat, spát (ochrana baterie)

const uint32_t kSafeIdleMs = 10UL * 60UL * 1000UL;  // nikdo není připojen -> spánek
const uint32_t kSafeMaxMs = 60UL * 60UL * 1000UL;   // absolutní strop délky nouzového režimu
const uint32_t kSafeOtaMs = 15UL * 60UL * 1000UL;   // nejdéle takto dlouho se čeká na dokončení jedné aktualizace

// Příčina startu (zjednodušeno z esp_reset_reason() a příčiny probuzení).
enum Cause : uint8_t {
  C_POWER = 0,   // zapnutí napájení / reset tlačítkem EN: čistý start
  C_SW = 1,      // esp_restart(): po OTA, tlačítko Restart ve webu
  C_WAKE = 2,    // probuzení z deep sleep (časovač, magnet)
  C_CRASH = 3,   // panic / watchdog
  C_BROWNOUT = 4,
  C_OTHER = 5
};

// Fáze programu v okamžiku zápisu; po případném pádu se podle ní váží pády.
enum Phase : uint8_t {
  P_IDLE = 0,        // poslední cyklus úspěšně skončil (nebo se ještě nic nespustilo)
  P_BOOT = 1,        // start / krátké probuzení / záznam
  P_WEB_START = 2,   // webový režim běží méně než WEB_STABLE_MS
  P_WEB_STABLE = 3,  // webový režim běží dost dlouho
  P_SAFE = 4         // nouzový režim
};

// POZOR: bez inicializátorů členů! Proměnná leží v RTC_NOINIT paměti a nesmí se při startu přepsat
// (inicializátor by vygeneroval konstruktor, který by ji vynuloval).
struct State {
  uint32_t magic;
  uint8_t streak;   // počet "špatných" startů po sobě
  uint8_t phase;    // Phase při předchozím běhu
  uint8_t safe;     // 1 = nouzový režim trvá (do restartu napájení / úspěšného OTA / ručního návratu)
  uint8_t spare;
  uint32_t part;    // adresa oddílu aplikace (obrazu), který záznam naposledy zapsal; 0 = neznámý
  uint32_t check;
};

inline uint32_t checksum(const State& s) {
  const uint8_t b[12] = {(uint8_t)(s.magic), (uint8_t)(s.magic >> 8), (uint8_t)(s.magic >> 16),
                         (uint8_t)(s.magic >> 24), s.streak, s.phase, s.safe, s.spare,
                         (uint8_t)(s.part), (uint8_t)(s.part >> 8), (uint8_t)(s.part >> 16),
                         (uint8_t)(s.part >> 24)};
  uint32_t h = 2166136261u;  // FNV-1a
  for (int i = 0; i < 12; i++) {
    h ^= b[i];
    h *= 16777619u;
  }
  return h;
}

inline void seal(State& s) {
  s.magic = kMagic;
  s.check = checksum(s);
}

inline bool valid(const State& s) { return s.magic == kMagic && s.check == checksum(s); }

// Čistý štít (zapnutí napájení, úspěšné OTA, ruční návrat do normálního režimu).
inline void clear(State& s) {
  s.streak = 0;
  s.phase = P_IDLE;
  s.safe = 0;
  s.spare = 0;
  s.part = 0;
  seal(s);
}

struct Decision {
  bool safe;     // spustit nouzový režim
  bool forced;   // důvodem je magnet při zapnutí (ne pády)
  bool giveUp;   // nouzový režim padá dokola: Wi-Fi nezapínat, spát jen na magnet
};

// Volá se jako PRVNÍ věc po startu. reedHeld = relé bylo sepnuté po celých REED_SAFE_HOLD_MS
// (zjišťuje se jen po zapnutí napájení). part = adresa oddílu právě běžícího obrazu (0 = neznámá).
inline Decision onBoot(State& s, Cause cause, bool reedHeld, uint32_t part = 0) {
  if (!valid(s)) clear(s);  // po zapnutí napájení je paměť RTC náhodná; poškozený záznam = čistý štít

  // Záznam napsal jiný obraz (po OTA, nebo po návratu bootloaderu na předchozí firmware): jeho pády i nouzový
  // režim nejsou naše, začínáme načisto a pád, po kterém jsme se probudili, se nepočítá.
  const bool otherImage = part != 0 && s.part != 0 && s.part != part;
  if (otherImage) clear(s);

  if (cause == C_POWER) {
    clear(s);
  } else if (cause == C_CRASH && !otherImage) {
    // Pád při startu webu = nejhorší případ (uživatel nemá jak se k zařízení dostat): hned nouzový režim.
    const unsigned add = (s.phase == P_WEB_START) ? kCrashLimit : 1u;
    unsigned v = (unsigned)s.streak + add;
    if (v > 250u) v = 250u;
    s.streak = (uint8_t)v;
  }

  Decision d = {false, false, false};
  if (s.streak >= kGiveUp) d.giveUp = true;
  if (s.streak >= kCrashLimit) s.safe = 1;  // trvale (do restartu napájení / úspěšného OTA)
  if (s.safe) {
    d.safe = true;
  } else if (cause == C_POWER && reedHeld) {
    d.safe = true;  // jednorázově: nezapisuje se do s.safe
    d.forced = true;
  }
  s.phase = d.safe ? (uint8_t)P_SAFE : (uint8_t)P_BOOT;
  if (part != 0) s.part = part;  // až po případných clear(): clear() ho nuluje
  seal(s);
  return d;
}

inline void setPhase(State& s, Phase p) {
  if (!valid(s)) clear(s);
  s.phase = (uint8_t)p;
  seal(s);
}

// Cyklus úspěšně dokončen (těsně před uspáním): pády se přestanou počítat.
inline void healthy(State& s) {
  if (!valid(s)) clear(s);
  s.streak = 0;
  s.phase = P_IDLE;
  seal(s);
}

// Web běží dost dlouho: start je za námi, případný pád už nespustí nouzový režim hned.
inline void webStable(State& s) {
  if (!valid(s)) clear(s);
  s.streak = 0;
  s.phase = P_WEB_STABLE;
  seal(s);
}

// ---------------------------------------------------------------------------
//  Nouzový režim: časovač spánku a LED
// ---------------------------------------------------------------------------
// Rozdíly časů jsou v uint32_t aritmetice (přetečení millis() po 49 dnech nevadí).
inline bool safeExpired(uint32_t now, uint32_t startMs, uint32_t lastBusyMs, bool otaActive, uint32_t otaStartMs) {
  if ((uint32_t)(now - startMs) > kSafeMaxMs) return true;  // tvrdý strop platí vždy
  if (otaActive && (uint32_t)(now - otaStartMs) <= kSafeOtaMs) return false;  // aktualizace běží
  return (uint32_t)(now - lastBusyMs) > kSafeIdleMs;
}

// Dvojité bliknutí každé 2 s (v běžném webu je to jeden krátký záblesk po 5 s).
inline bool safeLedOn(uint32_t nowMs) {
  const uint32_t t = nowMs % 2000u;
  return t < 120u || (t >= 300u && t < 420u);
}

}  // namespace bootguard
