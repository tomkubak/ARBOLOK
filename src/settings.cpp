#include "settings.h"

#include <Preferences.h>

// Jmenný prostor i klíče "language", "web", "sleep" zůstávají z verze 3, aby
// po aktualizaci firmwaru zůstalo nastavení (jazyk, interval, doba webu).
static const char* NS = "arbolok";

bool settingsLoad(Settings& s) {
  settingsDefaults(s);
  Preferences p;
  if (!p.begin(NS, false)) return false;  // RW režim: nevypisuje chybu NOT_FOUND při prvním startu

  const bool haveV4 = p.isKey("v4");
  const bool haveV3 = p.isKey("web") || p.isKey("sleep") || p.isKey("language");

  String lang = p.getString("language", "cz");
  snprintf(s.lang, sizeof(s.lang), "%s", lang.c_str());

  if (haveV4) {
    s.webMin = p.getUShort("webm", s.webMin);
    s.intervalMin = p.getUShort("intm", s.intervalMin);
    s.dryPct = p.getUChar("dry", s.dryPct);
    s.wetPct = p.getUChar("wet", s.wetPct);
    s.cal.gDry = p.getFloat("gdry", s.cal.gDry);
    s.cal.gWet = p.getFloat("gwet", s.cal.gWet);
    s.cal.rawOpen = p.getUShort("open", s.cal.rawOpen);
    s.cal.scale = p.getUChar("scale", s.cal.scale);
    s.vexcTrack = p.getBool("vexc", s.vexcTrack);
    s.vbatCorr = p.getFloat("vcorr", s.vbatCorr);
    s.timeSet = p.getBool("tset", s.timeSet);
    s.biasPin = p.getUChar("bpin", s.biasPin);  // chybí-li (firmware do v4.2), zůstane výchozí pin desky
    // pokročilé (od v4.4); chybí-li, zůstanou výchozí
    // Způsob měření (krátký pulz / dlouhý pulz pro starší desky se sondou na GND) jde zvolit v Servisu.
    s.probeMode = p.getUChar("pmode", s.probeMode);
    // Ostatní parametry pulzu (strop, tolerance, vybití, medián, ověření, délka krátkého pulzu) se od v5.1 nečtou:
    // platí vždy výchozí hodnoty ze settingsDefaults(), staré uložené klíče smaže settingsSave().
    s.retPin = p.getUChar("rpin", s.retPin);
    s.wJumpPct = p.getUChar("wjmp", s.wJumpPct);
    s.wWinH = p.getUChar("wwin", s.wWinH);
    s.captive = p.getUChar("cpmode", s.captive);  // (starší klíče cport/cport2 se už nečtou)
    s.txLow = p.getUChar("txlow", s.txLow);       // od v6.5; chybí-li, zůstane 0 = normální výkon
    s.manOn = p.getUChar("mon", s.manOn);           // ruční zápis (od v6.10)
    s.manNote = p.getUChar("mnote", s.manNote);
    s.manExt = p.getUChar("mext", s.manExt);
    s.liveSec = p.getUChar("lives", s.liveSec);    // od v6.11
    s.locActive = p.getUChar("loca", s.locActive);
    const String pw = p.getString("appw", "");  // od v7.0; chybí-li, otevřená síť
    if (pw.length() < sizeof(s.wifiPw)) snprintf(s.wifiPw, sizeof(s.wifiPw), "%s", pw.c_str());  // delší = poškozené
  } else if (haveV3) {
    // migrace z v3: "web" a "sleep" byly v SEKUNDÁCH. Před přetypováním na uint16_t se hodnota
    // ořízne (záporná nebo obří hodnota by se jinak zalomila); jemné meze doladí settingsSanitize().
    const int webSec = p.getInt("web", DEF_WEB_RUNTIME_MIN * 60);
    const int sleepSec = p.getInt("sleep", 0);
    auto secToMin = [](int sec, int lo, int hi, int dflt) -> uint16_t {
      if (sec <= 0) return (uint16_t)dflt;
      int m = sec / 60;
      if (m < lo) m = lo;
      if (m > hi) m = hi;
      return (uint16_t)m;
    };
    s.webMin = secToMin(webSec, 1, 30, DEF_WEB_RUNTIME_MIN);
    s.intervalMin = secToMin(sleepSec, 1, INTERVAL_MAX_MIN, 0);  // v3: 0 = záznam vypnutý
    s.timeSet = true;  // zařízení už někdo používal -> hodiny pravděpodobně nastavené
    // v3 měla lineární stupnici; nová log-stupnice je výchozí, uživatel ji může přepnout zpět
  }
  p.end();
  settingsSanitize(s);
  return haveV4 || haveV3;
}

bool settingsSave(const Settings& sIn) {
  Settings s = sIn;
  settingsSanitize(s);
  Preferences p;
  if (!p.begin(NS, false)) return false;
  p.putString("language", s.lang);
  p.putUShort("webm", s.webMin);
  p.putUShort("intm", s.intervalMin);
  p.putUChar("dry", s.dryPct);
  p.putUChar("wet", s.wetPct);
  p.putFloat("gdry", s.cal.gDry);
  p.putFloat("gwet", s.cal.gWet);
  p.putUShort("open", s.cal.rawOpen);
  p.putUChar("scale", s.cal.scale);
  p.putBool("vexc", s.vexcTrack);
  p.putFloat("vcorr", s.vbatCorr);
  p.putBool("tset", s.timeSet);
  p.putUChar("bpin", s.biasPin);
  p.putUChar("rpin", s.retPin);
  p.putUChar("wjmp", s.wJumpPct);
  p.putUChar("wwin", s.wWinH);
  p.putUChar("cpmode", s.captive);
  p.putUChar("txlow", s.txLow);
  p.putUChar("mon", s.manOn);
  p.putUChar("mnote", s.manNote);
  p.putUChar("mext", s.manExt);
  p.putUChar("lives", s.liveSec);
  p.putUChar("loca", s.locActive);
  p.putUChar("pmode", s.probeMode);
  if (s.wifiPw[0]) p.putString("appw", s.wifiPw);
  else if (p.isKey("appw")) p.remove("appw");
  // pevné parametry pulzu: staré klíče pryč (výchozí hodnoty)
  static const char* const kGone[] = {"rpull", "maxms", "eps", "pdis", "lpul", "cdel", "early"};
  for (const char* k : kGone)
    if (p.isKey(k)) p.remove(k);
  // zpětná kompatibilita: v3 klíče držíme aktuální (kdyby někdo vracel firmware)
  p.putInt("web", (int)s.webMin * 60);
  p.putInt("sleep", (int)s.intervalMin * 60);
  p.putUChar("v4", 1);
  p.end();
  return true;
}

void settingsFactoryReset() {
  Preferences p;
  if (p.begin(NS, false)) {
    p.clear();
    p.end();
  }
}
