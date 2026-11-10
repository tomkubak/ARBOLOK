// ============================================================================
//  timeutil.h  –  pomocné časové funkce (ČISTÉ C++)
// ============================================================================
#pragma once
#include <stdint.h>

// Uplynulo od časového razítka `since` aspoň `ms` milisekund? Rozdíl je ZNAMÉNKOVÝ (a tedy odolný
// vůči přetečení millis()): razítko, které volaný kód nastavil až PO načtení `now` (pomalé
// měření, zápis do logu), je "v budoucnosti" a nesmí vyjít jako uplynulé miliardy ms – akce by
// se jinak spustila znovu hned.
inline bool elapsedAtLeast(uint32_t now, uint32_t since, uint32_t ms) {
  return (int32_t)(now - since) >= (int32_t)ms;
}

// Kolik sekund spát, aby cyklus měl průměrně přesně "interval" (probuzení,
// boot a měření se odečtou). Nikdy méně než 1 s.
inline uint32_t sleepSecondsFor(uint32_t intervalSec, uint32_t awakeMs, uint32_t bootOverheadMs = 250) {
  const uint32_t spent = (awakeMs + bootOverheadMs + 999u) / 1000u;
  if (spent + 1u >= intervalSec) return 1u;
  return intervalSec - spent;
}

inline bool isLeap(int y) { return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0); }

inline int daysInMonth(int y, int m) {
  static const int d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (m < 1 || m > 12) return 0;
  return (m == 2 && isLeap(y)) ? 29 : d[m - 1];
}

// Validace data/času zadaného z prohlížeče (ochrana RTC před nesmyslem).
inline bool validDateTime(int y, int mo, int d, int h, int mi, int s) {
  if (y < 2024 || y > 2099) return false;
  if (mo < 1 || mo > 12) return false;
  if (d < 1 || d > daysInMonth(y, mo)) return false;
  if (h < 0 || h > 23 || mi < 0 || mi > 59 || s < 0 || s > 59) return false;
  return true;
}

// "Lokální unix čas" (sekundy od 1970-01-01 00:00:00 bez časových pásem – tak, jak
// ho vrací RTClib z DS3231) -> datum a čas. Platí pro roky 1970..2105.
inline void unixToParts(uint32_t t, int& y, int& mo, int& d, int& h, int& mi, int& s) {
  const uint32_t days = t / 86400u;
  const uint32_t rem = t % 86400u;
  h = (int)(rem / 3600u);
  mi = (int)((rem % 3600u) / 60u);
  s = (int)(rem % 60u);
  // civil_from_days (H. Hinnant), pro nezáporné dny
  const int32_t z = (int32_t)days + 719468;
  const int32_t era = z / 146097;
  const uint32_t doe = (uint32_t)(z - era * 146097);
  const uint32_t yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
  const int32_t yy = (int32_t)yoe + era * 400;
  const uint32_t doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
  const uint32_t mp = (5u * doy + 2u) / 153u;
  d = (int)(doy - (153u * mp + 2u) / 5u + 1u);
  mo = (int)(mp < 10u ? mp + 3u : mp - 9u);
  y = yy + (mo <= 2 ? 1 : 0);
}

// ---------------------------------------------------------------------------
//  Plán záznamů zarovnaných na hodiny DS3231
//
//  Záznamy patří k "hranicím" (násobkům intervalu na RTC, např. 17:00, 17:30). Časovač deep sleep ale běží
//  z vnitřního RC oscilátoru ESP32 (chyba až ±10 %, závisí na teplotě). Do v6.4 se proto spalo po etapách
//  (7/8 zbytku a znovu), tedy 3–4 probuzení na jeden záznam.
//
//  Od v6.5 JEDNO probuzení na záznam:
//   * spí se rovnou do další hranice (+ SCHED_AIM_AFTER_SEC),
//   * chybu časovače se firmware učí: po každém probuzení časovačem porovná, kolik skutečně uběhlo podle DS3231,
//     s tím, kolik časovači zadal (driftUpdate), a příští spánek o to opraví (timerSecFor),
//   * probuzení trochu PŘED hranicí (v okně wakeWindowSec) se počítá k té hranici – záznam se zapíše hned
//     a nespí se kvůli pár sekundám znovu.
//  Po naučení (2–3 cykly) je odchylka záznamu od hranice řádově sekundy; v nejhorším (první cyklus po zapnutí,
//  chyba RC 10 %) je záznam o desetinu intervalu dřív nebo později. Na přesnosti nezáleží, na baterii ano.
// ---------------------------------------------------------------------------
struct SchedPlan {
  bool logNow = false;       // je na čase zapsat záznam
  uint32_t boundary = 0;     // hranice (unix), ke které záznam patří
  uint32_t sleepSec = 0;     // kolik SKUTEČNÝCH sekund spát do dalšího probuzení (>= 1); na časovač: timerSecFor()
};

// Okno, ve kterém se probuzení PŘED hranicí ještě počítá k ní (probuzení časovačem). Desetina intervalu pokryje
// i nenaučenou chybu RC oscilátoru; nejvýš 15 min.
inline uint32_t wakeWindowSec(uint32_t periodSec) {
  uint32_t w = periodSec / 8u;
  if (w < 3u) w = 3u;
  if (w > 900u) w = 900u;
  return w;
}

// earlySec: o kolik dřív než hranice se už zapisuje (3 s při webu a na konci relace, wakeWindowSec() po
// probuzení časovačem). aimAfterSec: cíl dalšího probuzení za hranicí.
inline SchedPlan scheduleStep(uint32_t nowUnix, uint32_t periodSec, uint32_t lastBoundary,
                              uint32_t earlySec = 3u, uint32_t aimAfterSec = 2u) {
  SchedPlan p;
  if (periodSec == 0u) return p;
  const uint32_t cur = ((nowUnix + earlySec) / periodSec) * periodSec;
  p.boundary = cur;
  // Záznam k hranici "cur" se zapsal s předstihem (probuzení časovačem v okně před hranicí) a teď je ještě před
  // ní: nic nezapisovat a spát až do hranice za ní.
  const bool aheadDone = lastBoundary == cur + periodSec;
  p.logNow = (cur != lastBoundary) && !aheadDone;  // i když RTC skočí zpět, zapíše se (nová hranice)
  const uint32_t next = (aheadDone ? lastBoundary : cur) + periodSec;
  const uint32_t remaining = next > nowUnix ? next - nowUnix : 0u;
  uint32_t sl = remaining + aimAfterSec;
  if (sl < 1u) sl = 1u;
  p.sleepSec = sl;
  return p;
}

// Chyba časovače: k = skutečné sekundy (DS3231) na jednu sekundu zadanou časovači. 1.0 = bez chyby.
const float kDriftMin = 0.80f, kDriftMax = 1.25f;
inline bool driftValid(float k) { return k >= kDriftMin && k <= kDriftMax; }

// Zpřesní odhad k po spánku, kdy časovač dostal timerSec a podle DS3231 uběhlo realSec. Krátké spánky (rozlišení
// DS3231 je 1 s) a nesmyslné poměry (mezitím se nastavoval čas) se ignorují. Klouzavý průměr tlumí šum.
inline float driftUpdate(float k, uint32_t timerSec, uint32_t realSec) {
  if (!driftValid(k)) k = 1.0f;
  if (timerSec < 30u) return k;
  const float m = (float)realSec / (float)timerSec;
  if (!driftValid(m)) return k;
  const float a = timerSec >= 300u ? 0.5f : 0.25f;  // krátký spánek = větší relativní chyba měření
  return k + a * (m - k);
}

// Kolik sekund zadat časovači, aby uběhlo realSec skutečných sekund.
inline uint32_t timerSecFor(uint32_t realSec, float k) {
  if (!driftValid(k)) k = 1.0f;
  const float t = (float)realSec / k + 0.5f;
  const uint32_t u = t < 1.0f ? 1u : (uint32_t)t;
  return u;
}

