// ============================================================================
//  ARBOLOK / Arbolog  –  hlavní program
//
//  Tři režimy:
//   * ZÁZNAM  (probuzení časovačem): ~0,2 s – změř, zapiš do logu, spi dál. Žádné Wi-Fi.
//   * WEB     (probuzení magnetem / zapnutí): AP "ARBOLOK-<číslo>", stránka na
//     http://10.10.10.10 (nebo http://arbolok.local), živé měření, kalibrace, nastavení.
//   * NOUZOVÝ (safemode.cpp): jen AP "ARBOLOK-SAFE-<číslo>" + OTA stránka /update. Spustí se po
//     opakovaných pádech nebo magnetem při připojení baterie (bootguard.h) – aby se zařízení, které
//     se nahrává jen přes OTA, nedalo zabrickovat rozbitým firmwarem.
//
//  Kdo smí na hardware: JEN tahle hlavní smyčka. Webové handlery (jiný task) čtou kopii
//  stavu (state.h) a změny posílají příkazem přes frontu (cmd.h).
//
//  Kódy LED:
//    1 delší svit při probuzení   = start
//    krátký záblesk každých 5 s   = web běží
//    10 rychlých bliknutí         = baterie je příliš slabá pro Wi-Fi (web se nespustí)
//    3 bliknutí po startu         = restart kvůli podpětí (slabá baterie), web přeskočen
//    2 bliknutí po startu         = restart po pádu programu, web přeskočen
//    dvojité bliknutí každé 2 s   = NOUZOVÝ režim (OTA)
//    svítí ~2 s po připojení baterie = relé je sepnuté; po 2 s následuje nouzový režim
//    6 rychlých bliknutí a spánek = nouzový režim padá dokola, Wi-Fi se nezapíná (ochrana baterie)
//
//  Zaseknutí programu hlídá časovač (guardAlive, config.h DEADMAN_*): visí-li hlavní smyčka nebo start
//  déle než 60–90 s, zařízení se shodí a restartuje a pád se započítá do ochrany (bootguard.h).
// ============================================================================
#include <Arduino.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <esp_system.h>

#include <math.h>
#include <string.h>

#include "battery.h"
#include "battery_curve.h"
#include "bootguard.h"
#include "cmd.h"
#include "config.h"
#include "guard.h"
#include "logfmt.h"
#include "locations.h"
#include "logstore.h"
#include "moisture.h"
#include "power.h"
#include "probe.h"
#include "rtcutil.h"
#include "safemode.h"
#include "settings.h"
#include "state.h"
#include "timeutil.h"
#include "webui.h"

#if ARBOLOG_DEBUG
#define DBG(...) Serial.printf(__VA_ARGS__)
#else
#define DBG(...) ((void)0)
#endif

// ---------------------------------------------------------------------------
//  Sdílený stav (deklarováno v state.h)
// ---------------------------------------------------------------------------
LiveState g_live;
Settings g_set;
portMUX_TYPE g_stateMux = portMUX_INITIALIZER_UNLOCKED;
char g_devId[12] = "0000000";

namespace {

// ---------------------------------------------------------------------------
//  Stav, který přežije deep sleep (RTC RAM). Po zapnutí napájení je vynulovaný,
//  proto "magic": neplatný obsah se inicializuje.
// ---------------------------------------------------------------------------
struct RtcState {
  uint32_t magic;
  uint32_t lastBoundary;  // poslední zpracovaná hranice záznamu (lokální unix čas z DS3231)
  uint32_t periodSec;     // perioda, pro kterou lastBoundary platí
  float lastRaw;          // čítač posledního záznamu (na rozhodnutí o ověřovacích pulzech)
  uint8_t logFail;        // po sobě jdoucí selhání zápisu do logu
  bool waitRelease;       // magnet zůstal u relé: čekáme na rozepnutí
  bool hibernate;         // hluboce vybitý článek: budit se jen jednou za VBAT_HIBERNATE_SEC (viz config.h)
  bool saving;            // šetřící režim: článek pod VBAT_SAVE_V, interval záznamu je dvojnásobný (od v6.5)
  // Učení chyby časovače spánku (timeutil.h, driftUpdate): kolik skutečných sekund (DS3231) připadá na sekundu
  // zadanou časovači, a odkdy/na kolik se naposledy usnulo.
  float driftK;           // 0 = zatím nenaučeno (bere se 1.0)
  uint32_t sleepAtUnix;   // čas DS3231 při usnutí (0 = neplatí: probuzení magnetem, start…)
  uint32_t sleepTimerSec; // co dostal časovač
  // Čítače pro odhad spotřeby (Servis). Platí od posledního zapnutí/restartu (RTC RAM).
  uint32_t statSince;     // čas DS3231, odkdy se počítá (0 = ještě neznámý)
  uint32_t statBoots;     // probuzení (mimo web)
  uint32_t statLogs;      // zapsané záznamy
  uint32_t statAwakeMs;   // doba bdění mimo web (ms, včetně odhadu bootloaderu)
  uint32_t statWebSec;    // doba běhu webu (s)
  uint32_t statSleepSec;  // doba spánku (s, podle plánu)
};
RTC_DATA_ATTR RtcState g_rtc;
const uint32_t kRtcMagic = 0xA4B06505u;  // (změna rozložení RtcState = nový magic; v6.5)

// Při velké změně čítače proti poslednímu záznamu (nebo při rušení) se měření ověří třemi dalšími pulzy a použije
// se medián – bez zpoždění skutečné změny (zalití). Mez skoku je v nastavení (g_set.confDelta, výchozí 150 čítačů).

bool s_logMounted = false;
uint32_t s_lastLiveMs = 0;
uint32_t s_lastClockMs = 0;
uint32_t s_lastFsMs = 0;
uint8_t s_lowBatCount = 0;
uint32_t s_webStartMs = 0;
bool s_webStable = false;
bool s_confirmed = false;  // nový firmware po OTA potvrzen (stránka obsloužila klienta a web stabilně běží)
uint32_t s_lastKickMs = 0;
bool s_webRan = false;      // v tomto probuzení běžel web (statistika spotřeby)
bool s_everClient = false;  // během relace se připojil aspoň jeden telefon
uint32_t s_lastClientMs = 0;  // kdy byl naposledy připojený aspoň jeden telefon

// ---------------------------------------------------------------------------
//  Pomocné
// ---------------------------------------------------------------------------
void setMsg(const char* m) {
  StateLock l;
  snprintf(g_live.msg, sizeof(g_live.msg), "%s", m);
  g_live.msgSeq++;
}

void setBusy(bool b) {
  StateLock l;
  g_live.busy = b;
}

// Skutečná perioda záznamu: v šetřícím režimu dvojnásobná (viz updateSaving).
uint32_t periodSec() { return (uint32_t)g_set.intervalMin * 60UL * (g_rtc.saving ? 2UL : 1UL); }
wifi_power_t txPower();

void ensureLogMounted() {
  if (!s_logMounted) s_logMounted = logMount();
}

void applySettings(const Settings& ns) {
  {
    StateLock l;
    g_set = ns;
    g_live.timeSet = ns.timeSet;
  }
  settingsSave(ns);
  {
    StateLock l;
    locSyncFromSettings(ns);  // kalibrace a prahy patří aktivnímu místu
  }
  // Do NVS až po uvolnění zámku (i změny míst z doLoc / doLocs: všechny končí tady), viz locations.h.
  locSave();
}

void liveMeasure();

// Místa (kalibrační profily): přepnutí, nové, přejmenování, smazání. Data míst čte i webový task -> pod zámkem.
void doLocs(const Cmd& c) {
  Location ls[LOC_MAX];
  uint8_t n = 0;
  bool ok = cmdTakeLocs(ls, n);
  // Tabulku zkontrolovat PŘED přečíslováním logu (stejně jako locReplaceAll): kdyby ji locReplaceAll odmítl až po
  // přepisu logu, nesouhlasila by čísla míst v záznamech s tabulkou.
  for (uint8_t j = 0; ok && j < n; j++) ok = loc::valid(ls[j]);
  // Přečíslování záznamů: i[1] nese pro každé nové místo j (4 bity) jeho původní číslo (1..8), 0 = nové místo;
  // i[2] = 1, když to stránka poslala (import ze souboru ne: tam místa nahrazují ta stará podle pořadí).
  if (ok && c.i[2] == 1) {
    const uint8_t nOld = locCount();
    uint8_t map[LOC_MAX + 1] = {0};
    for (uint8_t j = 0; j < n; j++) {
      const uint8_t o = (uint8_t)(((uint32_t)c.i[1] >> (4 * j)) & 0xFu);
      if (o >= 1 && o <= nOld && map[o] == 0) map[o] = (uint8_t)(j + 1);
    }
    ensureLogMounted();
    if (!logRemapPlaces(map, nOld)) {
      setMsg("err:logmap");
      return;
    }
  }
  if (ok) {
    StateLock lk;
    ok = locReplaceAll(ls, n);
  }
  if (!ok) {
    setMsg("err:locs");
    return;
  }
  Settings ns = g_set;
  ns.locActive = (uint8_t)(c.i[0] >= 0 && c.i[0] < n ? c.i[0] : 0);
  locToSettings(ls[ns.locActive], ns);
  applySettings(ns);
  setMsg("ok:locs");
  liveMeasure();
}

void doLoc(const Cmd& c) {
  Settings ns = g_set;
  bool ok = false, select = false;
  uint8_t idx = (uint8_t)c.i[1];
  switch (c.i[0]) {
    case LOC_OP_SELECT:
      ok = idx < locCount();
      select = ok;
      break;
    case LOC_OP_NEW: {
      Location l;
      locFromSettings(g_set, l);  // prahy a "naprázdno" (vlastnost sondy) z aktuálního místa
      l.gDry = DEF_CAL_G_DRY;     // kalibrace sucho/mokro nová: jiná zemina
      l.gWet = DEF_CAL_G_WET;
      memcpy(l.name, c.name, sizeof(l.name));
      {
        StateLock lk;
        ok = c.name[0] && locAdd(l);
        idx = (uint8_t)(locCount() - 1);
      }
      select = ok;
      break;
    }
    case LOC_OP_RENAME: {
      StateLock lk;
      if (idx < locCount() && c.name[0]) {
        Location l = locGet(idx);
        memcpy(l.name, c.name, sizeof(l.name));
        ok = locSet(idx, l);
      }
      break;
    }
    case LOC_OP_DELETE: {
      // Napřed přečíslovat záznamy v logu (smazané místo -> p0, další o jedno níž), teprve pak místo smazat.
      if (idx < locCount() && locCount() > 1) {
        uint8_t map[LOC_MAX + 1];
        logfmt::placeMapDelete(map, locCount(), idx);
        ensureLogMounted();
        if (!logRemapPlaces(map, locCount())) {
          setMsg("err:logmap");
          return;
        }
      }
      StateLock lk;
      ok = locRemove(idx);
      if (ok) {
        if (g_set.locActive == idx) {
          select = true;
          idx = 0;
        } else if (g_set.locActive > idx) {
          ns.locActive = (uint8_t)(g_set.locActive - 1);
          g_set.locActive = ns.locActive;
        }
      }
      break;
    }
    default: break;
  }
  if (ok && select) {
    Location l;
    {
      StateLock lk;
      l = locGet(idx);
    }
    locToSettings(l, ns);
    ns.locActive = idx;
  }
  if (ok) {
    applySettings(ns);
    if (select) liveMeasure();
  }
  setMsg(ok ? "ok:loc" : "err:loc");
}

void rtcStateInit() {
  memset(&g_rtc, 0, sizeof(g_rtc));
  g_rtc.magic = kRtcMagic;
}

// Plán záznamů začíná od začátku právě probíhajícího intervalu: zapnutí zařízení nebo
// změna intervalu/času tak nevyvolá okamžitý (nebo dvojitý) záznam.
void scheduleReset(uint32_t nowUnix) {
  const uint32_t p = periodSec();
  g_rtc.periodSec = p;
  g_rtc.lastBoundary = (p && nowUnix) ? (nowUnix / p) * p : 0;
}

void scheduleEnsure() {
  if (g_rtc.magic != kRtcMagic) rtcStateInit();
  if (g_rtc.periodSec != periodSec()) scheduleReset(rtcUnixNow());
}

// Šetřící režim podle napětí změřeného při záznamu (bez zátěže Wi-Fi), s hysterezí. Při změně se plán hned
// přepočítá na novou periodu: aktuální hranice (právě zapisovaný záznam) zůstane vyřízená.
void updateSaving(const BatteryReading& b, uint32_t boundary) {
  if (!b.ok) return;
  bool s = g_rtc.saving;
  if (!s && b.volts < VBAT_SAVE_V) s = true;
  else if (s && b.volts > VBAT_SAVE_OFF_V) s = false;
  if (s == g_rtc.saving) return;
  g_rtc.saving = s;
  const uint32_t p = periodSec();
  g_rtc.periodSec = p;
  g_rtc.lastBoundary = (p && boundary) ? (boundary / p) * p : 0;
}

// Učení chyby časovače: volá se po probuzení časovačem, dokud je známé, kdy a na kolik se usnulo.
void driftLearn(uint32_t now) {
  if (now && g_rtc.sleepAtUnix && g_rtc.sleepTimerSec && now > g_rtc.sleepAtUnix)
    g_rtc.driftK = driftUpdate(g_rtc.driftK, g_rtc.sleepTimerSec, now - g_rtc.sleepAtUnix);
  g_rtc.sleepAtUnix = 0;
}

void serialNumber() { powerSerial(g_devId, sizeof(g_devId)); }

// ---------------------------------------------------------------------------
//  Měření a záznam
// ---------------------------------------------------------------------------
// Zveřejní výsledek měření webu (pod zámkem).
void publishMeas(const BatteryReading& b, const ProbeMeasurement& m, float temp) {
  StateLock l;
  g_live.haveMeas = true;
  g_live.vbat = b.ok ? b.volts : NAN;
  g_live.bpct = b.ok ? lifepo4Percent(b.volts) : -1;
  g_live.temp = temp;
  g_live.hum = m.percent;
  g_live.raw = m.raw;
  g_live.rawRaw = m.rawRaw;
  g_live.g = m.g;
  g_live.settleMs = m.settleMs;
  g_live.settled = m.settled;
  g_live.flags = m.flags;
  g_live.rawB = m.rawB;
  g_live.phase = m.phase;
  g_live.measAtMs = millis();
  g_live.busy = false;
}

void refreshClock() {
  const uint32_t u = rtcUnixNow();
  StateLock l;
  g_live.rtcOk = (u != 0);
  g_live.rtcUnix = u;
  g_live.rtcAtMs = millis();
}

void refreshFsStats() {
  const uint32_t tot = (uint32_t)fsTotalBytes();
  const uint32_t used = (uint32_t)fsUsedBytes();
  const uint32_t lg = (uint32_t)logSizeBytes();
  StateLock l;
  g_live.fsTotal = tot;
  g_live.fsUsed = used;
  g_live.logBytes = lg;
  g_live.logErr = g_rtc.logFail > 0;
  s_lastFsMs = millis();
}

// Přechod do hibernace (článek pod VBAT_NOLOG_V): jednou zapsat řádek s napětím a značkou "H", aby bylo v grafu
// vidět, proč měření stojí. Další probuzení v hibernaci už nic nezapisují.
void enterHibernate(const BatteryReading& b) {
  if (g_rtc.hibernate) return;
  g_rtc.hibernate = true;
  ensureLogMounted();
  logfmt::Record rec;
  const uint32_t now = rtcUnixNow();
  if (now) {
    int y, mo, d, h, mi, s;
    unixToParts(now, y, mo, d, h, mi, s);
    rec.year = y;
    rec.month = mo;
    rec.day = d;
    rec.hour = h;
    rec.minute = mi;
  }
  rec.timeOk = now != 0 && g_set.timeSet;
  rec.hum = -1;
  rec.temp = rtcTemperature();
  rec.vbat = b.ok ? b.volts : NAN;
  rec.lowBat = true;
  rec.loc = g_set.locActive + 1;
  rec.hibernate = true;
  logAppend(rec);
}

// Změří a zapíše jeden záznam. boundary = čas hranice (lokální unix) nebo 0, když RTC
// nefunguje. Vrací true při úspěšném zápisu.
bool measureAndLog(uint32_t boundary) {
  ensureLogMounted();
  const BatteryReading b = batteryMeasure(g_set.vbatCorr);
  if (b.ok && b.volts < VBAT_NOLOG_V) {  // ochrana článku: už nezatěžovat, přejít do hibernace
    enterHibernate(b);
    return false;
  }
  const float vexc = b.ok ? b.volts : 0.0f;
  updateSaving(b, boundary);

  ProbeMeasurement m;
  if (g_set.logPulses > 1) {
    probeMeasureMedian(g_set, vexc, g_set.logPulses, m);  // pevně několik měření na záznam (nastavení)
  } else {
    probeMeasureOnce(g_set, vexc, m);
  }
  if (g_set.logPulses <= 1 && moist::needsConfirm(m.flags, m.raw, g_rtc.lastRaw, (float)g_set.confDelta)) {
    ProbeMeasurement m3;
    delay(PROBE_REPEAT_GAP_MS);
    if (probeMeasureMedian(g_set, vexc, 3, m3)) m = m3;
  }

  logfmt::Record rec;
  if (boundary) {
    int y, mo, d, h, mi, s;
    unixToParts(boundary, y, mo, d, h, mi, s);
    rec.year = y;
    rec.month = mo;
    rec.day = d;
    rec.hour = h;
    rec.minute = mi;
  }
  rec.timeOk = (boundary != 0) && g_set.timeSet;
  rec.hum = isnan(m.percent) ? -1 : (int)lroundf(m.percent);  // zkrat / uzel nereaguje: "--%" místo smyšlených 100 %
  rec.temp = rtcTemperature();
  rec.vbat = b.ok ? b.volts : NAN;
  rec.raw = (int)lroundf(m.raw);
  rec.settleMs = m.settleMs;
  rec.settled = m.settled;
  rec.lowBat = b.ok && b.volts < VBAT_WARN_V;
  rec.flood = moist::isFlood(m.flags, g_set.biasPin != 0);  // "F" místo pouhé chyby sondy
  rec.loc = g_set.locActive + 1;  // ke kterému místu záznam patří (stránka ukazuje data vybraného místa)

  const bool ok = logAppend(rec);
  g_rtc.lastRaw = m.raw;
  if (ok) {
    g_rtc.logFail = 0;
    g_rtc.statLogs++;
  }
  else if (g_rtc.logFail < 250) g_rtc.logFail++;
  DBG("log %s: %d%% raw %d %.2fV settle %dms%s\n", ok ? "OK" : "CHYBA", rec.hum, rec.raw, (double)rec.vbat,
      rec.settleMs, m.settled ? "" : " (nenastaveno)");
  return ok;
}

// ---------------------------------------------------------------------------
//  Uspání podle plánu
// ---------------------------------------------------------------------------
[[noreturn]] void sleepPlanned() {
  guardHealthy();  // cyklus doběhl až k uspání: případné pády z minula se přestanou počítat
  const uint32_t p = periodSec();
  uint32_t sec = 0;  // skutečné sekundy
  const uint32_t now = rtcUnixNow();
  if (g_rtc.hibernate) {
    sec = VBAT_HIBERNATE_SEC;  // hluboce vybitý článek: jen občas zkontrolovat, jestli se nenabil
  } else if (p) {
    if (now) sec = scheduleStep(now, p, g_rtc.lastBoundary, 3u, SCHED_AIM_AFTER_SEC).sleepSec;
    else sec = sleepSecondsFor(p, millis());  // RTC nefunguje: aspoň přibližně
  }
  const uint32_t timer = sec ? timerSecFor(sec, g_rtc.driftK) : 0;  // oprava o naučenou chybu časovače
  g_rtc.sleepAtUnix = (timer && now) ? now : 0;
  g_rtc.sleepTimerSec = timer;
  // Statistika spotřeby: tohle probuzení (web zvlášť) a plánovaný spánek.
  const uint32_t up = millis();
  if (s_webRan) {
    const uint32_t webMs = up - s_webStartMs;
    g_rtc.statWebSec += webMs / 1000UL;
    g_rtc.statAwakeMs += (up - webMs) + BOOT_OVERHEAD_MS;
  } else {
    g_rtc.statAwakeMs += up + BOOT_OVERHEAD_MS;
  }
  g_rtc.statBoots++;
  g_rtc.statSleepSec += sec;
  DBG("spim %u s (casovac %u s, k=%.4f)\n", (unsigned)sec, (unsigned)timer, (double)g_rtc.driftK);
  powerSleep(timer, g_rtc.waitRelease);
}

// Krátké probuzení: zapiš záznam, když je na čase, a spi dál. Nikdy se nevrací.
// logWithoutRtc: když hodiny nefungují, hranici nelze určit a záznam se zapíše při každém probuzení
// časovačem. Na konci webové relace to ale neplatí (viz enterSleep), jinak by každé zapnutí webu
// přidalo jeden záznam navíc.
// timerWake: probuzení časovačem; probuzení kousek před hranicí (wakeWindowSec) se pak počítá k ní.
[[noreturn]] void loggingWake(bool logWithoutRtc = true, bool timerWake = false) {
  scheduleEnsure();
  if (g_rtc.hibernate) {
    // Hibernace: jen změřit článek. Nabil se (nad VBAT_RESUME_V)? Pak normální provoz od aktuálního intervalu
    // (žádné doháněné záznamy), jinak spát dalších VBAT_HIBERNATE_SEC.
    const BatteryReading b = batteryMeasure(g_set.vbatCorr);
    g_rtc.hibernate = batteryHibernate(true, b.ok, b.volts, VBAT_NOLOG_V, VBAT_RESUME_V);
    if (g_rtc.hibernate) sleepPlanned();
    scheduleReset(rtcUnixNow());
  }
  const uint32_t p = periodSec();
  if (p) {
    const uint32_t now = rtcUnixNow();
    if (now) {
      const SchedPlan plan = scheduleStep(now, p, g_rtc.lastBoundary, timerWake ? wakeWindowSec(p) : 3u);
      if (plan.logNow) {
        measureAndLog(plan.boundary);
        g_rtc.lastBoundary = plan.boundary;  // i při selhání: neopakovat v téže periodě
      }
    } else if (logWithoutRtc) {
      measureAndLog(0);  // RTC nefunguje: hranici nelze určit, zapíše se při každém probuzení
    }
  }
  sleepPlanned();
}

// Konec webové relace: vypnout rádio a spát. Přes loggingWake(), ne přímo sleepPlanned(): záznam
// se podle plánu zapisuje až 3 s před hranicí (scheduleStep), zatímco webBoundaryTick() běží
// jednou za vteřinu – relace končící těsně před hranicí by jinak jeden záznam přeskočila.
// (Měření pak ještě jednou hlídá napětí článku, VBAT_NOLOG_V.)
[[noreturn]] void enterSleep() {
  digitalWrite(LED_PIN, LOW);
  guardAlive(DEADMAN_BOOT_MS);  // zbytek (vypnutí rádia, případný záznam) má vlastní, delší limit
  // Relace proběhla bez pádu (i kratší než WEB_STABLE_MS) a stránka obsloužila klienta -> firmware je potvrzený.
  // Nikdo se nepřipojil (nepotvrzený firmware po OTA): nepotvrzuje se, powerSleep() pak firmware vrátí na
  // předchozí. Jinak by se z firmwaru, jehož Wi-Fi nebo web nefungují, stal nenahraditelný.
  if (webuiServed()) guardConfirm();
  delay(20);
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  loggingWake(false);
}

// ---------------------------------------------------------------------------
//  Webové příkazy
// ---------------------------------------------------------------------------
void setSession(uint32_t totalSec) {
  StateLock l;
  g_live.sessionTotalSec = totalSec;
  g_live.sessionEndMs = g_live.sessionStartMs + totalSec * 1000UL;
}

void liveMeasure() {
  setBusy(true);
  const BatteryReading b = batteryMeasure(g_set.vbatCorr);
  ProbeMeasurement m;
  probeMeasureOnce(g_set, b.ok ? b.volts : 0.0f, m);
  publishMeas(b, m, rtcTemperature());
  s_lastLiveMs = millis();

  // Při Wi-Fi napětí článku kleslo pod hranici ochrany -> raději vypnout web.
  if (b.ok && b.volts < VBAT_NOLOG_V) {
    if (++s_lowBatCount >= 2) {
      enterHibernate(b);  // po vypnutí webu rovnou hibernace
      enterSleep();
    }
  } else {
    s_lowBatCount = 0;
  }
}

// Ruční zápis (přenosný logger): změří (medián ze 3 pulzů) a zapíše řádek se značkou M, místem a poznámkou.
// Čas je skutečný okamžik (minuta), ne hranice intervalu. Volitelně prodlouží stránku, aby při obcházení
// stromů nezhasla (aspoň 5 min do konce, nejvýš SESSION_MAX_MAN_SEC).
void doManual(const Cmd& c) {
  if (!g_set.manOn) {
    setMsg("err:manoff");
    return;
  }
  setBusy(true);
  ensureLogMounted();
  const BatteryReading b = batteryMeasure(g_set.vbatCorr);
  ProbeMeasurement m;
  probeMeasureMedian(g_set, b.ok ? b.volts : 0.0f, MANUAL_PULSES, m);
  const float temp = rtcTemperature();
  publishMeas(b, m, temp);
  s_lastLiveMs = millis();
  logfmt::Record rec;
  const uint32_t now = rtcUnixNow();
  if (now) {
    int y, mo, d, h, mi, s;
    unixToParts(now, y, mo, d, h, mi, s);
    rec.year = y;
    rec.month = mo;
    rec.day = d;
    rec.hour = h;
    rec.minute = mi;
  }
  rec.timeOk = now != 0 && g_set.timeSet;
  rec.hum = isnan(m.percent) ? -1 : (int)lroundf(m.percent);
  rec.temp = temp;
  rec.vbat = b.ok ? b.volts : NAN;
  rec.raw = (int)lroundf(m.raw);
  rec.settleMs = m.settleMs;
  rec.settled = m.settled;
  rec.lowBat = b.ok && b.volts < VBAT_WARN_V;
  rec.flood = moist::isFlood(m.flags, g_set.biasPin != 0);
  rec.loc = g_set.locActive + 1;
  rec.manual = true;
  rec.note = c.name[0] ? c.name : nullptr;
  const bool ok = logAppend(rec);
  if (ok) g_rtc.statLogs++;
  refreshFsStats();
  if (ok && g_set.manExt) {
    const uint32_t elapsed = (millis() - g_live.sessionStartMs) / 1000UL;
    const uint32_t want = elapsed + SESSION_EXTEND_SEC;
    if (want > g_live.sessionTotalSec) setSession(want > sessionMaxSec(g_set) ? sessionMaxSec(g_set) : want);
  }
  setBusy(false);
  setMsg(ok ? "ok:manual" : "err:manual");
}

// Zveřejní výsledek testu zapojení (pod zámkem).
void publishWire(const wiring::Sample* smp, size_t n, const wiring::Result& r) {
  StateLock l;
  g_live.wireHave = true;
  g_live.wireRes = (uint8_t)r.verdict;
  g_live.wirePin = r.pin;
  g_live.wireN = (uint8_t)(n > wiring::kPinCount ? wiring::kPinCount : n);
  for (size_t i = 0; i < wiring::kPinCount; i++) {
    const bool have = i < g_live.wireN;
    g_live.wirePins[i] = have ? smp[i].pin : 0;
    g_live.wireLo[i] = have ? smp[i].lo : 0.0f;
    g_live.wireHi[i] = have ? smp[i].hi : 0.0f;
  }
}

// Zjistí, který pin buzení sondy (BIAS) opravdu hýbe uzlem, a nalezený uloží i použije. lazy = GPIO25 se zkouší jen
// když z prvních dvou kandidátů není nic jasného (start webu), ručně z webu se zkusí všichni.
wiring::Verdict detectBias(bool lazy) {
  wiring::Sample smp[wiring::kPinCount];
  const uint8_t cur = probeBiasPin();
  const size_t n = probeWiringTest(smp, wiring::kPinCount, lazy, cur);
  const wiring::Result r = wiring::choose(smp, n, cur);
  publishWire(smp, n, r);
  DBG("zapojeni: verdikt %d pin %d (pouzivany %d)\n", (int)r.verdict, (int)r.pin, (int)cur);
  if (r.verdict != wiring::V_FOUND) return r.verdict;
  probeUsePin(r.pin);
  if (g_set.biasPin != r.pin) {
    Settings ns = g_set;
    ns.biasPin = r.pin;
    applySettings(ns);  // uloží do NVS: příště se už nehledá
  }
  return r.verdict;
}

void doWiring() {
  setBusy(true);
  const wiring::Verdict v = detectBias(false);
  liveMeasure();  // hned nové měření s nalezeným pinem, ať stránka ukáže výsledek (po něm je busy zase false)
  setMsg(v == wiring::V_FOUND ? "ok:wiring" : (v == wiring::V_AMBIGUOUS ? "err:wiring2" : "err:wiring"));
}

void doProfile() {
  setBusy(true);
  static float t[48], v[48];
  const size_t n = probeCaptureProfile(t, v, 48);
  {
    StateLock l;
    for (size_t i = 0; i < n; i++) {
      g_live.profT[i] = t[i];
      g_live.profV[i] = v[i];
    }
    g_live.profN = (uint8_t)n;
    g_live.profValid = n > 0;
    g_live.busy = false;
  }
  setMsg(n > 0 ? "ok:profile" : "err:meas");
}

// Kalibrace bodu "sucho" / "mokro": medián z několika pulzů.
void doCalPoint(bool wet) {
  setBusy(true);
  const BatteryReading b = batteryMeasure(g_set.vbatCorr);
  ProbeMeasurement m;
  probeMeasureMedian(g_set, b.ok ? b.volts : 0.0f, PROBE_CAL_PULSES, m);
  publishMeas(b, m, rtcTemperature());

  if (moist::isFault(m.flags)) {
    setMsg("err:short");  // zkrat / uzel nereaguje na BIAS: špatně zapojená sonda nebo pin, kalibrovat nelze
    return;
  }
  if (m.flags & moist::FLAG_NOISY) {
    setMsg("err:flags");
    return;
  }
  Settings ns = g_set;
  if (!moist::setCalPoint(ns.cal, wet, m.g)) {  // "sucho" s nasyceným ADC dostane spodní mez (viz moisture.h)
    setMsg("err:cal");
    return;
  }
  applySettings(ns);
  setMsg(wet ? "ok:wet" : "ok:dry");
}


void doCalVbat(float volts) {
  setBusy(true);
  const BatteryReading b = batteryMeasure(1.0f);  // bez korekce
  setBusy(false);
  if (!b.ok || b.voltsRaw <= 0.0f) {
    setMsg("err:vbat");
    return;
  }
  const float corr = volts / b.voltsRaw;
  if (!(corr >= 0.80f && corr <= 1.20f)) {
    setMsg("err:vbat");
    return;
  }
  Settings ns = g_set;
  ns.vbatCorr = corr;
  applySettings(ns);
  {
    StateLock l;
    g_live.vbat = volts;
    g_live.bpct = lifepo4Percent(volts);
  }
  setMsg("ok:vbat");
}

void doSetTime(const Cmd& c) {
  if (!rtcAvailable()) {
    setMsg("err:rtc");
    return;
  }
  if (!rtcSetParts(c.i[0], c.i[1], c.i[2], c.i[3], c.i[4], c.i[5])) {
    setMsg("err:time");
    return;
  }
  Settings ns = g_set;
  ns.timeSet = true;
  applySettings(ns);
  scheduleReset(rtcUnixNow());  // čas skočil -> plán záznamů od začátku aktuálního intervalu
  refreshClock();
  setMsg("ok:time");
}

void doSetSettings(const Cmd& c) {
  Settings ns = g_set;
  const uint16_t oldWeb = ns.webMin;
  const uint16_t oldInt = ns.intervalMin;
  applySettingsMasked(ns, c.set, c.mask);
  applySettings(ns);
  if (c.mask & SM_BIAS) {
    // Ručně zvolený pin platí hned. "Automaticky" (0) spustí rozpoznání hned: nalezený pin se uloží.
    probeUsePin(settingsBiasPin(ns));
    if (ns.biasPin == 0) detectBias(false);
    liveMeasure();
  }
  if (c.mask & SM_RET) {
    probeUseRetPin(ns.retPin);  // druhá elektroda: platí hned, nové měření ukáže, jestli fáze B reaguje
    liveMeasure();
  } else if (c.mask & (SM_PROBE | SM_PMODE)) {  // nový způsob měření: hned ukázat jeho hodnotu
    liveMeasure();
  }
  if (ns.intervalMin != oldInt) scheduleReset(rtcUnixNow());
  if (c.mask & SM_TXP) WiFi.setTxPower(txPower());  // platí hned (telefon by se při nízkém výkonu mohl odpojit)
  if ((c.mask & SM_WEB) && ns.webMin != oldWeb) {
    // Nová doba běhu webu platí hned, ale uživatele nevyhodí dřív než za 20 s.
    uint32_t total = (uint32_t)ns.webMin * 60UL;
    const uint32_t elapsed = (millis() - g_live.sessionStartMs) / 1000UL;
    if (total < elapsed + 20UL) total = elapsed + 20UL;
    if (total > sessionMaxSec(g_set)) total = sessionMaxSec(g_set);
    setSession(total);
  }
  setMsg(c.mask == SM_LANG ? "ok:lang" : "ok:settings");
}

void handleCmd(const Cmd& c) {
  switch (c.type) {
    case CMD_MEASURE:
      liveMeasure();
      setMsg("ok:measure");
      break;
    case CMD_PROFILE: doProfile(); break;
    case CMD_CAL_DRY: doCalPoint(false); break;
    case CMD_CAL_WET: doCalPoint(true); break;
    case CMD_CAL_RESET: {
      Settings ns = g_set;
      const uint8_t keepScale = ns.cal.scale;
      ns.cal = moist::Calib();
      ns.cal.gDry = DEF_CAL_G_DRY;
      ns.cal.gWet = DEF_CAL_G_WET;
      ns.cal.rawOpen = DEF_CAL_RAW_OPEN;
      ns.cal.scale = keepScale;
      applySettings(ns);
      setMsg("ok:reset");
      break;
    }
    case CMD_CAL_VBAT: doCalVbat(c.f); break;
    case CMD_WIRING: doWiring(); break;
    case CMD_LOC: doLoc(c); break;
    case CMD_LOCS: doLocs(c); break;
    case CMD_SET_TIME: doSetTime(c); break;
    case CMD_SET_SETTINGS: doSetSettings(c); break;
    case CMD_MANUAL:
      doManual(c);
      break;
    case CMD_EXTEND: {
      uint32_t total = g_live.sessionTotalSec + SESSION_EXTEND_SEC;
      if (total > sessionMaxSec(g_set)) total = sessionMaxSec(g_set);
      setSession(total);
      setMsg("ok:extend");
      break;
    }
    case CMD_FACTORY_RESET:
      // Od v7.0-rc1 jako z výroby: smaže se i souborový systém (záznamy, nahrané soubory), pak nastavení v NVS.
      delay(300);                     // ať odpověď na požadavek odejde dřív, než se flash zaměstná formátem
      guardAlive(DEADMAN_LOOP_MS);    // formát celého oddílu trvá několik sekund
      logFormatAll(3000);
      guardAlive(DEADMAN_LOOP_MS);
      settingsFactoryReset();
      guardConfirm();  // úmyslný restart: nepotvrzený firmware by se jinak bootloaderem vrátil na předchozí
      delay(500);      // ať odpověď stihne odejít
      ESP.restart();
      break;
    case CMD_WIFI_PW: {
      // Nové heslo platí po restartu (Wi-Fi se spustí s ním); telefon se pak připojí znovu.
      Settings ns = g_set;
      snprintf(ns.wifiPw, sizeof(ns.wifiPw), "%s", c.pw);
      applySettings(ns);
      guardConfirm();
      delay(800);  // ať odpověď stihne odejít
      ESP.restart();
      break;
    }
    case CMD_RESTART:
      guardConfirm();
      delay(500);
      ESP.restart();
      break;
    default: break;
  }
  if (g_live.busy) setBusy(false);
  refreshFsStats();
}

// ---------------------------------------------------------------------------
//  Webový režim
// ---------------------------------------------------------------------------
wifi_power_t txPower() { return g_set.txLow ? WIFI_TX_LOW_ENUM : WIFI_TX_POWER_DBM_ENUM; }

// Čítače spotřeby a šetřící režim pro web (Servis -> Spotřeba). Web běží a bdí: připočte se i běžící relace.
void publishPower() {
  const uint32_t up = millis();
  StateLock l;
  g_live.pwrSince = g_rtc.statSince;
  g_live.pwrBoots = g_rtc.statBoots;
  g_live.pwrLogs = g_rtc.statLogs;
  g_live.pwrAwakeMs = g_rtc.statAwakeMs + s_webStartMs + BOOT_OVERHEAD_MS;  // start před webem
  g_live.pwrWebSec = g_rtc.statWebSec + (up - s_webStartMs) / 1000UL;
  g_live.pwrSleepSec = g_rtc.statSleepSec;
  g_live.saving = g_rtc.saving;
  g_live.intEffMin = (uint16_t)(periodSec() / 60UL);
  g_live.driftK = g_rtc.driftK;
}

void startWebMode() {
  guardPhase(bootguard::P_WEB_START);  // pád v prvních WEB_STABLE_MS = příští start rovnou nouzový režim
  s_webStartMs = millis();
  s_webRan = true;
  setCpuFrequencyMhz(WEB_CPU_MHZ);  // Wi-Fi vyžaduje >= 80 MHz; od v6.5 80 místo 160 (úspora baterie)
  digitalWrite(LED_PIN, HIGH);
  ensureLogMounted();
  if (s_logMounted) logRemoveLegacy();  // staré soubory webu (chart.js, style.css) už nejsou potřeba: místo pro log
  scheduleEnsure();

  // První měření ještě před zapnutím Wi-Fi: žádný rádiový šum, žádný pokles napětí.
  const BatteryReading b = batteryMeasure(g_set.vbatCorr);
  if (b.ok && b.volts < VBAT_NOWIFI_V) {
    // Wi-Fi by článek dorazilo (podpětí -> restart ve smyčce). Jen ohlásit a spát dál.
    if (b.volts < VBAT_NOLOG_V) enterHibernate(b);
    digitalWrite(LED_PIN, LOW);
    powerBlink(10, 70, 70);
    loggingWake();
  }
  if (b.ok && b.volts < VBAT_WARN_V) {
    // Slabý článek (2,90–3,00 V): web se spustí, ale napřed 3 pomalá bliknutí = „dejte baterii dobít“.
    digitalWrite(LED_PIN, LOW);
    delay(300);
    powerBlink(3, 450, 300);
    delay(300);
    digitalWrite(LED_PIN, HIGH);
  }
  // Pin BIAS ještě nebyl rozpoznán (první start, nebo volba "Automaticky"): zkusit ho poznat. Když sonda hýbe uzlem
  // jen na jiném pinu, než pro jaký je sestavení, nalezený se uloží a měření (i záznam) pak běží správně.
  if (g_set.biasPin == 0) detectBias(true);
  ProbeMeasurement m;
  probeMeasureOnce(g_set, b.ok ? b.volts : 0.0f, m);
  publishMeas(b, m, rtcTemperature());

  {
    StateLock l;
    g_live.timeSet = g_set.timeSet;
    g_live.sessionStartMs = millis();
  }
  setSession((uint32_t)g_set.webMin * 60UL);
  refreshClock();
  refreshFsStats();

  // Wi-Fi AP. persistent(false): konfigurace se nezapisuje do flash při každém startu.
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);
  WiFi.setTxPower(txPower());
  WiFi.softAPConfig(IPAddress(10, 10, 10, 10), IPAddress(10, 10, 10, 10), IPAddress(255, 255, 255, 0));
  char ssid[32];
  snprintf(ssid, sizeof(ssid), "%s%s", AP_SSID_PREFIX, g_devId);
  const char* pw = settingsApPassword(g_set);
  const bool apOk = WiFi.softAP(ssid, pw[0] ? pw : nullptr, AP_CHANNEL, 0, AP_MAX_CLIENTS);
  if (!apOk) {
    digitalWrite(LED_PIN, LOW);
    powerBlink(5, 400, 200);
    loggingWake();
  }
  WiFi.setTxPower(txPower());  // znovu po startu AP: na některých verzích jádra se dřívější nastavení nepřevezme
  webuiBegin();
  if (MDNS.begin(MDNS_HOSTNAME)) MDNS.addService("http", "tcp", 80);

  digitalWrite(LED_PIN, LOW);
  s_lastLiveMs = s_lastClockMs = s_lastClientMs = millis();
  publishPower();
  DBG("web: %s, %s\n", ssid, pw[0] ? "heslo" : "otevrena sit");
}

// Záznam podle plánu i během webu (časovač spánku neběží, dokud je zařízení vzhůru).
void webBoundaryTick() {
  const uint32_t p = periodSec();
  if (!p) return;
  const uint32_t now = g_live.rtcUnix;
  if (!g_live.rtcOk || !now) return;
  const SchedPlan plan = scheduleStep(now, p, g_rtc.lastBoundary);
  if (!plan.logNow) return;
  setBusy(true);
  measureAndLog(plan.boundary);
  g_rtc.lastBoundary = plan.boundary;
  setBusy(false);
  refreshFsStats();
}

void ledTick(uint32_t now) {
  static uint32_t last = 0;
  static bool on = false;
  if (!on && now - last >= 5000UL) {
    digitalWrite(LED_PIN, HIGH);
    on = true;
    last = now;
  } else if (on && now - last >= 40UL) {
    digitalWrite(LED_PIN, LOW);
    on = false;
  }
}

}  // namespace

// ============================================================================
void setup() {
  powerReleaseHolds();  // piny držené z deep sleep uvolnit jako první
  const BootKind kind = powerBootKind();

  // OCHRANA PŘED ZABRICKOVÁNÍM (firmware se nahrává jen přes OTA): musí být před vším, co může
  // spadnout nebo viset (nastavení, ADC, I²C, souborový systém, web). Viz bootguard.h.
  const bootguard::Cause cause = powerBootCause();
  const bool reedHeld = (cause == bootguard::C_POWER) && powerReedHeld(REED_SAFE_HOLD_MS);
  const bootguard::Decision guard = guardBoot(cause, reedHeld);
  guardAlive(DEADMAN_BOOT_MS);  // zaseknutí při startu nebo krátkém probuzení = pád (viz config.h)
  if (guard.safe) safeModeRun(kind, guard.forced, guard.giveUp);  // nevrací se

  if (kind != BK_POWER && kind != BK_REED) setCpuFrequencyMhz(80);  // krátká probuzení: nižší spotřeba

  powerInitPins();
  probeInit();
  batteryInit();
  cmdInit();
#if ARBOLOG_DEBUG
  Serial.begin(115200);
  Serial.printf("\n[ARBOLOK %s] start, kind=%d\n", FW_VERSION, (int)kind);
#endif
  serialNumber();

  settingsLoad(g_set);  // při prázdné NVS zůstanou výchozí hodnoty
  if (safeModeTakePwReset() && g_set.wifiPw[0]) {  // zapomenuté heslo smazané z nouzového režimu (magnet)
    g_set.wifiPw[0] = 0;
    settingsSave(g_set);
  }
  g_live.timeSet = g_set.timeSet;
  probeUsePin(settingsBiasPin(g_set));  // uložený pin BIAS (nebo ten ze sestavení, dokud se nerozpozná)
  probeUseRetPin(g_set.retPin);         // druhá elektroda (0 = napevno na GND)
  locLoadAll(g_set);                    // místa (kalibrační profily); prázdná tabulka -> "Místo 1" z nastavení
  if (g_set.locActive >= locCount()) {
    g_set.locActive = 0;
    locToSettings(locGet(0), g_set);
  }
  locSyncFromSettings(g_set);
  locSave();

  bool lostPower = false;
  if (rtcInit(lostPower) && lostPower) {
    // RTC přišlo o napájení (vybitá záložní baterie): nastavit aspoň čas kompilace
    // a označit, že čas není spolehlivý (záznamy dostanou "?").
    rtcSetToBuildTime();
    g_set.timeSet = false;
    g_live.timeSet = false;
    settingsSave(g_set);
  }
  if (g_rtc.magic != kRtcMagic) rtcStateInit();
  {
    const uint32_t now = rtcUnixNow();
    if (kind == BK_TIMER) driftLearn(now);  // jak dlouho se doopravdy spalo -> přesnější příští spánek
    else g_rtc.sleepAtUnix = 0;             // spánek přerušený magnetem (nebo start): nic se neměří
    if (!g_rtc.statSince) g_rtc.statSince = now;
  }

  const bool releaseWake = (kind == BK_REED && g_rtc.waitRelease);
  if (releaseWake) g_rtc.waitRelease = false;

  if (kind == BK_CRASH) {
    // Po pádu je g_rtc vynulovaný (RTC_DATA_ATTR se při restartu mimo deep sleep načte z flash), takže
    // plán záznamů začne od aktuálního intervalu (scheduleEnsure) – žádná smyčka restartů a měření.
    powerBlink(2, 120, 150);
  } else if (kind == BK_BROWNOUT) {
    powerBlink(3, 120, 150);
  }

  if (kind == BK_TIMER || kind == BK_CRASH || kind == BK_BROWNOUT || releaseWake) {
    loggingWake(true, kind == BK_TIMER);  // nevrací se
  }
  startWebMode();  // BK_POWER, BK_REED
}

void loop() {
  webuiPoll();

  Cmd c;
  while (cmdTake(c)) handleCmd(c);

  const uint32_t now = millis();  // až po příkazech: měření a kalibrace trvají stovky ms

  if (elapsedAtLeast(now, s_lastKickMs, 1000UL)) {
    s_lastKickMs = now;
    guardAlive(DEADMAN_LOOP_MS);  // smyčka žije
  }
  if (elapsedAtLeast(now, s_lastClockMs, 1000UL)) {
    s_lastClockMs = now;
    refreshClock();
    webBoundaryTick();
    publishPower();
    if (WiFi.softAPgetStationNum() > 0) {
      s_everClient = true;
      s_lastClientMs = now;
    }
  }
  if (!g_live.ota && elapsedAtLeast(now, s_lastLiveMs, (uint32_t)g_set.liveSec * 1000UL)) liveMeasure();
  if (elapsedAtLeast(now, s_lastFsMs, 5000UL)) refreshFsStats();
  if (!s_webStable && elapsedAtLeast(now, s_webStartMs, WEB_STABLE_MS)) {
    s_webStable = true;
    guardWebStable();  // start webu přežil: pád po této chvíli už nespustí nouzový režim hned
  }
  // Nový firmware po OTA se prokáže, až když web běží stabilně A obsloužil klienta (aspoň jeden požadavek):
  // teprve pak se zruší možnost návratu na předchozí firmware. Firmware s nefunkčním Wi-Fi/HTTP se tak nepotvrdí.
  if (s_webStable && !s_confirmed && webuiServed()) {
    s_confirmed = true;
    guardConfirm();
  }
  ledTick(now);

  // Konec relace: usnout, pokud neběží stahování/nahrávání/aktualizace. Dřív, než vyprší nastavená doba, když
  // se nikdo nepřipojil (WEB_NOCLIENT_SEC) nebo se poslední telefon odpojil (WEB_IDLE_SEC) – úspora baterie.
  const bool timeUp = (int32_t)(now - g_live.sessionEndMs) >= 0;
  const bool idle = s_everClient ? elapsedAtLeast(now, s_lastClientMs, WEB_IDLE_SEC * 1000UL)
                                 : elapsedAtLeast(now, s_webStartMs, WEB_NOCLIENT_SEC * 1000UL);
  if ((timeUp || idle) && !webuiTransferActive() && !g_live.busy) enterSleep();

  delay(15);
}
