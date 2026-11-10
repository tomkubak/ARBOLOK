// Host test načítání/ukládání nastavení (src/settings.cpp) nad FUNKČNÍ atrapou NVS v paměti.
// Hlavně: převzetí nastavení z verze 3 po aktualizaci firmwaru, kruhový průchod a poškozená NVS.
// Sestavení a spuštění:  tools/run_host_tests.sh
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <map>
#include <string>

#include "Preferences.h"  // atrapa z tests_host/stubs (jen deklarace)
#include "../src/locations.h"
#include "../src/settings.h"

// ---------------------------------------------------------------------------
//  Atrapa NVS: typované klíče; čtení jiného typu vrací výchozí hodnotu (jako skutečná NVS).
// ---------------------------------------------------------------------------
namespace nvs {
enum Type { U8, U16, I32, F32, BOOL, STR };
struct Value {
  Type t = U8;
  double d = 0;
  std::string s;
};
static std::map<std::string, std::map<std::string, Value>> g_store;
static std::string g_ns;
static bool g_ro = false;
static std::map<std::string, Value>& cur() { return g_store[g_ns]; }
static void put(const char* k, Type t, double d, const std::string& s = std::string()) {
  Value v;
  v.t = t;
  v.d = d;
  v.s = s;
  cur()[k] = v;
}
static const Value* find(const char* k, Type t) {
  auto it = cur().find(k);
  return (it != cur().end() && it->second.t == t) ? &it->second : nullptr;
}
}  // namespace nvs

bool Preferences::begin(const char* name, bool readOnly, const char*) {
  nvs::g_ns = name;
  nvs::g_ro = readOnly;
  return true;
}
void Preferences::end() { nvs::g_ns.clear(); }
bool Preferences::clear() {
  if (nvs::g_ro) return false;
  nvs::cur().clear();
  return true;
}
bool Preferences::isKey(const char* key) { return nvs::cur().count(key) > 0; }
bool Preferences::remove(const char* key) { return nvs::cur().erase(key) > 0; }
size_t Preferences::putUChar(const char* k, uint8_t v) { nvs::put(k, nvs::U8, v); return 1; }
size_t Preferences::putUShort(const char* k, uint16_t v) { nvs::put(k, nvs::U16, v); return 2; }
size_t Preferences::putInt(const char* k, int32_t v) { nvs::put(k, nvs::I32, v); return 4; }
size_t Preferences::putFloat(const char* k, float v) { nvs::put(k, nvs::F32, v); return 4; }
size_t Preferences::putBool(const char* k, bool v) { nvs::put(k, nvs::BOOL, v ? 1 : 0); return 1; }
size_t Preferences::putString(const char* k, const char* v) { nvs::put(k, nvs::STR, 0, v ? v : ""); return strlen(v ? v : ""); }
size_t Preferences::putString(const char* k, String v) { return putString(k, v.c_str()); }
uint8_t Preferences::getUChar(const char* k, uint8_t d) { auto* v = nvs::find(k, nvs::U8); return v ? (uint8_t)v->d : d; }
uint16_t Preferences::getUShort(const char* k, uint16_t d) { auto* v = nvs::find(k, nvs::U16); return v ? (uint16_t)v->d : d; }
int32_t Preferences::getInt(const char* k, int32_t d) { auto* v = nvs::find(k, nvs::I32); return v ? (int32_t)v->d : d; }
float Preferences::getFloat(const char* k, float d) { auto* v = nvs::find(k, nvs::F32); return v ? (float)v->d : d; }
bool Preferences::getBool(const char* k, bool d) { auto* v = nvs::find(k, nvs::BOOL); return v ? v->d != 0 : d; }
String Preferences::getString(const char* k, String d) { auto* v = nvs::find(k, nvs::STR); return v ? String(v->s.c_str()) : d; }

// Definice, kterou stuby deklarují (settings.cpp ji nepoužívá, ale WString.h ji slibuje).
const String emptyString;

// ---------------------------------------------------------------------------
static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                \
  do {                                                             \
    if (cond) {                                                    \
      g_pass++;                                                    \
    } else {                                                       \
      g_fail++;                                                    \
      printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);     \
    }                                                              \
  } while (0)

static void nvsReset() { nvs::g_store.clear(); }

static void putV3(int webSec, int sleepSec, const char* lang) {
  Preferences p;
  p.begin("arbolok", false);
  p.putInt("web", webSec);
  p.putInt("sleep", sleepSec);
  if (lang) p.putString("language", lang);
  p.end();
}

static void test_empty_nvs() {
  printf("[settings] prázdná NVS = výchozí hodnoty\n");
  nvsReset();
  Settings s;
  CHECK(!settingsLoad(s));
  Settings d;
  settingsDefaults(d);
  CHECK(memcmp(&s.cal, &d.cal, sizeof(s.cal)) == 0);
  CHECK(s.webMin == DEF_WEB_RUNTIME_MIN && s.intervalMin == DEF_INTERVAL_MIN);
  CHECK(strcmp(s.lang, "cz") == 0);
  CHECK(!s.timeSet);
  CHECK(s.cal.scale == 1);
  CHECK(s.biasPin == 0);                                  // pin BIAS zatím neurčen (rozpozná se při startu webu)
  CHECK(settingsBiasPin(s) == (uint8_t)SONDA_BIAS_PIN);   // dokud není určen, platí pin ze sestavení
}

static void test_migrate_v3() {
  printf("[settings] převzetí nastavení z verze 3\n");
  nvsReset();
  putV3(240, 3600, "en");  // 4 min web, záznam po hodině, angličtina
  Settings s;
  CHECK(settingsLoad(s));
  CHECK(s.webMin == 4);
  CHECK(s.intervalMin == 60);
  CHECK(strcmp(s.lang, "en") == 0);
  CHECK(s.timeSet);              // zařízení už někdo používal
  CHECK(s.cal.scale == 1);       // nová stupnice je výchozí
  CHECK(moist::calibValid(s.cal));
  CHECK(s.dryPct < s.wetPct);

  // jen jazyk (v3 s výchozím webem a vypnutým záznamem)
  nvsReset();
  {
    Preferences p;
    p.begin("arbolok", false);
    p.putString("language", "ua");
    p.end();
  }
  CHECK(settingsLoad(s));
  CHECK(strcmp(s.lang, "ua") == 0);
  CHECK(s.webMin == DEF_WEB_RUNTIME_MIN);
  CHECK(s.intervalMin == 0);
}

static void test_migrate_v3_extremes() {
  printf("[settings] v3: nesmyslné hodnoty se ořežou (žádné zalomení uint16_t)\n");
  struct Case {
    int web, sleep;
    int expWeb, expInt;
  } cs[] = {
      {-5, -1, DEF_WEB_RUNTIME_MIN, 0},
      {0, 0, DEF_WEB_RUNTIME_MIN, 0},
      {30, 59, 1, 1},                // pod minutu = nejméně 1 min
      {999999, 99999999, 30, INTERVAL_MAX_MIN},  // meze
      {2147483647, 2147483647, 30, INTERVAL_MAX_MIN},
      {-2147483647 - 1, -2147483647 - 1, DEF_WEB_RUNTIME_MIN, 0},
      {1800, 86400, 30, INTERVAL_MAX_MIN},
      {120, 300, 2, 5},
  };
  for (const Case& c : cs) {
    nvsReset();
    putV3(c.web, c.sleep, "cz");
    Settings s;
    CHECK(settingsLoad(s));
    CHECK(s.webMin == c.expWeb);
    CHECK(s.intervalMin == c.expInt);
  }
}

static void test_roundtrip() {
  printf("[settings] uložení a načtení beze ztráty\n");
  nvsReset();
  Settings a;
  settingsDefaults(a);
  strcpy(a.lang, "ua");
  a.webMin = 12;
  a.intervalMin = 45;
  a.dryPct = 22;
  a.wetPct = 91;
  a.cal.gDry = 0.0123f;
  a.cal.gWet = 4.5f;
  a.cal.rawOpen = 4012;
  a.cal.scale = 0;
  a.vexcTrack = true;
  a.probeMode = 1;
  a.rPullK = 47.5f;
  a.vbatCorr = 1.0425f;
  a.timeSet = true;
  a.biasPin = 26;
  CHECK(settingsSave(a));

  Settings b;
  CHECK(settingsLoad(b));
  CHECK(strcmp(b.lang, "ua") == 0);
  CHECK(b.webMin == 12 && b.intervalMin == 45);
  CHECK(b.dryPct == 22 && b.wetPct == 91);
  CHECK(b.cal.gDry == a.cal.gDry && b.cal.gWet == a.cal.gWet && b.cal.rawOpen == DEF_CAL_RAW_OPEN && b.cal.scale == 0);
  CHECK(b.vexcTrack && b.probeMode == 1);  // způsob měření (Servis) se ukládá
  CHECK(b.rPullK == DEF_PULLUP_KOHM && b.vbatCorr == a.vbatCorr);  // pull-up je pevně 10 kΩ
  CHECK(b.timeSet);
  CHECK(b.biasPin == 26 && settingsBiasPin(b) == 26);

  // klíče z verze 3 se drží aktuální (návrat ke starému firmwaru nezahodí interval a dobu webu)
  Preferences p;
  p.begin("arbolok", true);
  CHECK(p.getInt("web", -1) == 12 * 60);
  CHECK(p.getInt("sleep", -1) == 45 * 60);
  p.end();
}

static void test_v4_beats_v3() {
  printf("[settings] hodnoty v4 mají přednost před klíči z v3\n");
  nvsReset();
  putV3(60, 600, "cz");  // staré hodnoty: 1 min / 10 min
  Settings a;
  settingsDefaults(a);
  a.webMin = 7;
  a.intervalMin = 30;
  settingsSave(a);  // přepíše v3 klíče i v4 klíče
  // někdo (jiný firmware) pak změnil jen v3 klíč; v4 klíče platí dál
  {
    Preferences p;
    p.begin("arbolok", false);
    p.putInt("web", 9999);
    p.end();
  }
  Settings b;
  CHECK(settingsLoad(b));
  CHECK(b.webMin == 7);
  CHECK(b.intervalMin == 30);
}

static void test_corrupt_values() {
  printf("[settings] poškozená NVS: nesmysly se ošetří\n");
  nvsReset();
  {
    Preferences p;
    p.begin("arbolok", false);
    p.putUChar("v4", 1);
    p.putString("language", "xx");
    p.putUShort("webm", 0);
    p.putUShort("intm", 5000);
    p.putUChar("dry", 90);
    p.putUChar("wet", 40);  // wet <= dry
    p.putFloat("gdry", NAN);
    p.putFloat("gwet", -1.0f);
    p.putUShort("open", 100);
    p.putUChar("scale", 7);
    p.putUChar("pmode", 9);
    p.putFloat("rpull", NAN);
    p.putFloat("vcorr", 5.0f);
    p.putUChar("bpin", 77);  // pin, který se nikdy nesmí budit
    p.end();
  }
  Settings s;
  CHECK(settingsLoad(s));
  CHECK(s.biasPin == 0 && settingsBiasPin(s) == (uint8_t)SONDA_BIAS_PIN);
  CHECK(strcmp(s.lang, "cz") == 0);
  CHECK(s.webMin == 1);
  CHECK(s.intervalMin == INTERVAL_MAX_MIN);
  CHECK(s.dryPct < s.wetPct && s.wetPct <= 100);
  CHECK(moist::calibValid(s.cal));
  CHECK(s.cal.scale == 1);
  CHECK(s.probeMode == 2);
  CHECK(s.rPullK == DEF_PULLUP_KOHM && s.vexcTrack == DEF_VEXC_TRACK);  // migrace z v3 dostane nové výchozí hodnoty
  CHECK(fabsf(s.vbatCorr - VBAT_DEF_CORRECTION) < 1e-6f);
}

static void test_upgrade_from_v42() {
  printf("[settings] aktualizace z v4.2: nastavení zůstane, pin BIAS je neurčen (rozpozná se sám)\n");
  nvsReset();
  {
    Preferences p;  // přesně klíče, které ukládala verze 4.2 (bez "bpin")
    p.begin("arbolok", false);
    p.putUChar("v4", 1);
    p.putString("language", "en");
    p.putUShort("webm", 9);
    p.putUShort("intm", 20);
    p.putUChar("dry", 33);
    p.putUChar("wet", 80);
    p.putFloat("gdry", 0.07f);
    p.putFloat("gwet", 2.5f);
    p.putUShort("open", 4090);
    p.putUChar("scale", 1);
    p.putBool("vexc", true);
    p.putUChar("pmode", 0);
    p.putFloat("rpull", 10.0f);
    p.putFloat("vcorr", 0.98f);
    p.putBool("tset", true);
    p.end();
  }
  Settings s;
  CHECK(settingsLoad(s));
  CHECK(s.biasPin == 0);
  CHECK(strcmp(s.lang, "en") == 0 && s.webMin == 9 && s.intervalMin == 20);
  CHECK(s.dryPct == 33 && s.wetPct == 80 && s.cal.gDry == 0.07f && s.cal.rawOpen == DEF_CAL_RAW_OPEN && s.timeSet);
  // po uložení rozpoznaného pinu se příští start už nehledá
  s.biasPin = 32;
  CHECK(settingsSave(s));
  Settings t;
  CHECK(settingsLoad(t));
  CHECK(t.biasPin == 32);
  CHECK(t.webMin == 9 && t.cal.rawOpen == DEF_CAL_RAW_OPEN);
  // sanitize: povolené jsou jen piny z wiring::kPins a 0
  const uint8_t good[] = {0, 32, 26};
  for (uint8_t v : good) {
    Settings x;
    settingsDefaults(x);
    x.biasPin = v;
    settingsSanitize(x);
    CHECK(x.biasPin == v);
  }
  const uint8_t bad[] = {1, 4, 12, 13, 25, 27, 33, 34, 35, 255};  // 25 od v6.1 ne  // piny, které má firmware jinde (LED, reed, dělič, ADC)
  for (uint8_t v : bad) {
    Settings x;
    settingsDefaults(x);
    x.biasPin = v;
    settingsSanitize(x);
    CHECK(x.biasPin == 0);
  }
}

static void test_type_mismatch() {
  printf("[settings] klíč jiného typu = výchozí hodnota\n");
  nvsReset();
  {
    Preferences p;
    p.begin("arbolok", false);
    p.putUChar("v4", 1);
    p.putInt("webm", 20);     // v4 čeká uint16_t
    p.putUShort("intm", 15);  // správný typ
    p.end();
  }
  Settings s;
  CHECK(settingsLoad(s));
  CHECK(s.webMin == DEF_WEB_RUNTIME_MIN);
  CHECK(s.intervalMin == 15);
}

static void test_factory_reset() {
  printf("[settings] tovární nastavení\n");
  nvsReset();
  Settings a;
  settingsDefaults(a);
  a.intervalMin = 20;
  a.cal.gDry = 0.2f;
  settingsSave(a);
  settingsFactoryReset();
  Settings b;
  CHECK(!settingsLoad(b));
  CHECK(b.intervalMin == DEF_INTERVAL_MIN);
  CHECK(b.cal.gDry == DEF_CAL_G_DRY);
}

static void test_wifi_password() {
  printf("[settings] heslo Wi-Fi (v7.0): výchozí otevřená síť, uložení, smazání, poškozené heslo, tovární nastavení\n");
  nvsReset();
  Settings a;
  settingsDefaults(a);
  CHECK(a.wifiPw[0] == 0);
  CHECK(!strcmp(settingsApPassword(a), AP_PASSWORD));  // bez hesla ve webu platí to ze sestavení (výchozí prázdné)
  settingsSave(a);
  {
    Preferences p;
    p.begin("arbolok", true);
    CHECK(!p.isKey("appw"));  // otevřená síť se do NVS nezapisuje
    p.end();
  }
  snprintf(a.wifiPw, sizeof(a.wifiPw), "%s", "tajne-heslo 1");
  settingsSave(a);
  Settings b;
  CHECK(settingsLoad(b) && !strcmp(b.wifiPw, "tajne-heslo 1"));
  CHECK(!strcmp(settingsApPassword(b), "tajne-heslo 1"));
  // jiné nastavení heslo nemění (maska ho neobsahuje)
  Settings c = b, src;
  settingsDefaults(src);
  src.intervalMin = 15;
  applySettingsMasked(c, src, SM_INT);
  CHECK(!strcmp(c.wifiPw, "tajne-heslo 1"));
  // vypnutí: klíč zmizí
  b.wifiPw[0] = 0;
  settingsSave(b);
  {
    Preferences p;
    p.begin("arbolok", true);
    CHECK(!p.isKey("appw"));
    p.end();
  }
  // poškozené / neplatné heslo v NVS (krátké, dlouhé, řídicí znaky, diakritika) = otevřená síť, AP pak naběhne
  const char* bad[] = {"kratke", "1234567", "tab\theslo12", "příliš-heslo", "0123456789012345678901234567890123456789012345678901234567890123"};
  for (const char* x : bad) {
    Preferences p;
    p.begin("arbolok", false);
    p.putString("appw", x);
    p.end();
    Settings d;
    CHECK(settingsLoad(d) && d.wifiPw[0] == 0);
  }
  // hraniční délky
  CHECK(webparse::validWifiPassword(""));
  CHECK(!webparse::validWifiPassword(nullptr));
  CHECK(webparse::validWifiPassword("12345678"));
  CHECK(webparse::validWifiPassword("012345678901234567890123456789012345678901234567890123456789012"));  // 63
  // tovární nastavení heslo smaže
  snprintf(a.wifiPw, sizeof(a.wifiPw), "%s", "12345678");
  settingsSave(a);
  settingsFactoryReset();
  Settings e;
  CHECK(!settingsLoad(e) && e.wifiPw[0] == 0);
}

static void test_advanced() {
  printf("[settings] pokročilé parametry (v4.4): výchozí, uložení, poškozené hodnoty, aktualizace z v4.3, masky\n");
  // výchozí
  Settings d;
  settingsDefaults(d);
  CHECK(d.retPin == 0 && d.maxMs == PROBE_MAX_US / 1000UL && d.epsCounts == (uint8_t)PROBE_EPS_COUNTS);
  CHECK(d.predischMs == PROBE_PREDISCHARGE_MS && d.logPulses == 1 && d.confDelta == DEF_CONFIRM_DELTA);
  CHECK(d.wJumpPct == DEF_WJUMP_PCT && d.wWinH == DEF_WWIN_H && d.intervalMin == DEF_INTERVAL_MIN);
  CHECK(d.captive == 0 && d.earlyUs == DEF_EARLY_US);  // po připojení k Wi-Fi se ve výchozím stavu nic neotevírá
  CHECK(DEF_INTERVAL_MIN >= 1 && DEF_INTERVAL_MIN <= INTERVAL_MAX_MIN);
  // uložení a načtení
  nvsReset();
  Settings a = d;
  a.retPin = 14; a.maxMs = 120; a.epsCounts = 9; a.predischMs = 250; a.logPulses = 3; a.confDelta = 400;
  a.wJumpPct = 12; a.wWinH = 24; a.intervalMin = 120; a.captive = 2; a.earlyUs = 600; a.probeMode = 2;
  CHECK(settingsSave(a));
  Settings b;
  CHECK(settingsLoad(b));
  CHECK(b.retPin == 14 && b.wJumpPct == 12 && b.wWinH == 24 && b.intervalMin == 120 && b.captive == 2);
  // parametry pulzu jsou od v5.1 pevné: uložené hodnoty se ignorují, platí výchozí
  CHECK(b.maxMs == d.maxMs && b.epsCounts == d.epsCounts && b.predischMs == d.predischMs && b.logPulses == d.logPulses);
  CHECK(b.confDelta == d.confDelta && b.earlyUs == d.earlyUs && b.probeMode == 2 && d.probeMode == 2);
  // vysílač Wi-Fi (v6.5): výchozí normální, nízký se uloží, nesmysl -> normální, maska mění jen jeho
  CHECK(d.txLow == 0 && b.txLow == 0);
  a.txLow = 1;
  CHECK(settingsSave(a) && settingsLoad(b) && b.txLow == 1);
  { Settings x = b; x.txLow = 7; settingsSanitize(x); CHECK(x.txLow == 0); }
  { Settings x = d, src = d; src.txLow = 1; src.webMin = 9; applySettingsMasked(x, src, SM_TXP); CHECK(x.txLow == 1 && x.webMin == d.webMin); }
  // ruční zápis (v6.10): výchozí vypnuto, automatické číslování, prodlužovat stránku; maska SM_MAN; hodina stránky
  CHECK(d.manOn == 0 && d.manNote == 2 && d.manExt == 1);
  CHECK(sessionMaxSec(d) == SESSION_MAX_SEC);
  a.manOn = 1; a.manNote = 1; a.manExt = 0;
  CHECK(settingsSave(a) && settingsLoad(b) && b.manOn == 1 && b.manNote == 1 && b.manExt == 0);
  CHECK(sessionMaxSec(b) == SESSION_MAX_MAN_SEC);
  { Settings x = b; x.manOn = 5; x.manNote = 9; x.manExt = 3; settingsSanitize(x); CHECK(x.manOn == 0 && x.manNote == 2 && x.manExt == 1); }
  { Settings x = d, src = d; src.manOn = 1; src.manNote = 0; src.txLow = 1; applySettingsMasked(x, src, SM_MAN);
    CHECK(x.manOn == 1 && x.manNote == 0 && x.txLow == 0); }
  // obnovování živé hodnoty (v6.11): výchozí 15 s, uloží se, ořízne na 5..120, maska SM_LIVE
  CHECK(d.liveSec == DEF_LIVE_SEC);
  a.liveSec = 30;
  CHECK(settingsSave(a) && settingsLoad(b) && b.liveSec == 30);
  { Settings x = b; x.liveSec = 2; settingsSanitize(x); CHECK(x.liveSec == 5); x.liveSec = 250; settingsSanitize(x); CHECK(x.liveSec == 120); }
  { Settings x = d, src = d; src.liveSec = 60; src.manOn = 1; applySettingsMasked(x, src, SM_LIVE); CHECK(x.liveSec == 60 && x.manOn == 0); }
  a.liveSec = DEF_LIVE_SEC;
  a.manOn = 0; a.manNote = 2; a.manExt = 1;
  a.txLow = 0;
  a.probeMode = 1;
  CHECK(settingsSave(a));
  CHECK(settingsLoad(b) && b.probeMode == 1);  // dlouhý pulz pro starší sondy zůstane po restartu
  a.probeMode = 0;  // dřívější „dlouhý do ustálení“ -> dlouhý pulz
  CHECK(settingsSave(a) && settingsLoad(b) && b.probeMode == 1);
  // poškozené hodnoty v NVS se ořežou; neznámý pin druhé elektrody se nikdy nebudí
  nvsReset();
  {
    Preferences p;
    p.begin("arbolok", false);
    p.putUChar("v4", 1);
    p.putUShort("intm", 1440);
    p.putUChar("rpin", 26);  // BIAS kandidát: nesmí se stát druhou elektrodou
    p.putUShort("maxms", 60000);
    p.putUChar("eps", 0);
    p.putUShort("pdis", 9999);
    p.putUChar("lpul", 99);
    p.putUShort("cdel", 1);
    p.putUChar("wjmp", 0);
    p.putUChar("wwin", 200);
    p.putUShort("early", 60000);
    p.putUChar("pmode", 7);
    p.end();
  }
  Settings c;
  CHECK(settingsLoad(c));
  CHECK(c.intervalMin == INTERVAL_MAX_MIN && c.retPin == 0);
  CHECK(c.maxMs == d.maxMs && c.epsCounts == d.epsCounts && c.predischMs == d.predischMs && c.logPulses == d.logPulses);
  CHECK(c.confDelta == d.confDelta && c.wJumpPct == 1 && c.wWinH == 48 && c.earlyUs == d.earlyUs && c.probeMode == 2);  // pmode 7 = neplatné -> výchozí krátký pulz
  // staré klíče parametrů pulzu se při uložení smažou
  CHECK(settingsSave(c));
  {
    Preferences p;
    p.begin("arbolok", false);
    for (const char* k : {"rpull", "maxms", "eps", "pdis", "lpul", "cdel", "early"}) CHECK(!p.isKey(k));
    CHECK(p.isKey("wjmp") && p.isKey("rpin") && p.isKey("pmode"));
    p.end();
  }
  for (uint8_t pin : {0, 2, 4, 5, 12, 13, 15, 18, 19, 23, 25, 26, 27, 32, 33, 34, 35}) CHECK(!wiring::validRetPin(pin));
  CHECK(wiring::validRetPin(14) && !wiring::validPin(14));  // druhá elektroda jen GND nebo GPIO14
  // aktualizace z v4.3 (bez nových klíčů): výchozí pokročilé, staré zůstane
  nvsReset();
  {
    Preferences p;
    p.begin("arbolok", false);
    p.putUChar("v4", 1);
    p.putUShort("intm", 0);  // záznam vypnutý zůstane vypnutý
    p.putUChar("bpin", 26);
    p.end();
  }
  Settings e;
  CHECK(settingsLoad(e));
  CHECK(e.captive == 0 && e.intervalMin == 0 && e.biasPin == 26 && e.retPin == 0 && e.maxMs == PROBE_MAX_US / 1000UL && e.logPulses == 1);
  // masky: skupina SM_PROBE nemění druhou elektrodu ani detekci zalití a naopak
  Settings dst = d, src = d;
  src.retPin = 14; src.maxMs = 80; src.logPulses = 2; src.wJumpPct = 20;
  applySettingsMasked(dst, src, SM_PROBE);
  CHECK(dst.maxMs == 80 && dst.logPulses == 2 && dst.retPin == 0 && dst.wJumpPct == DEF_WJUMP_PCT);
  applySettingsMasked(dst, src, SM_RET);
  CHECK(dst.retPin == 14 && dst.wJumpPct == DEF_WJUMP_PCT);
  applySettingsMasked(dst, src, SM_WDET);
  CHECK(dst.wJumpPct == 20);
  src.captive = 1;
  applySettingsMasked(dst, src, SM_PROBE | SM_WDET | SM_RET);
  CHECK(dst.captive == 0);
  applySettingsMasked(dst, src, SM_CAPT);
  CHECK(dst.captive == 1);
  // v4.5 uložila zapnuto pod starým klíčem: od v4.6 se nečte (všude vypnuto, zapnout jde znovu v Nastavení)
  nvsReset();
  {
    Preferences p;
    p.begin("arbolok", false);
    p.putUChar("v4", 1);
    p.putBool("cport", true);
    p.end();
  }
  Settings g;
  CHECK(settingsLoad(g));
  CHECK(g.captive == 0);
  // nesmyslný režim v NVS -> nic
  {
    Preferences p;
    p.begin("arbolok", false);
    p.putUChar("cpmode", 9);
    p.end();
  }
  Settings h;
  CHECK(settingsLoad(h));
  CHECK(h.captive == 0);
}

static void test_locations() {
  printf("[místa] jména, kódování, tabulka v NVS, synchronizace s nastavením\n");
  char out[LOC_NAME + 1];
  CHECK(loc::sanitizeName("  Lípa u školy  ", out, sizeof(out)) == strlen("Lípa u školy") && !strcmp(out, "Lípa u školy"));
  CHECK(loc::sanitizeName("a;b\"c\\d<e>f\x01g", out, sizeof(out)) == 7 && !strcmp(out, "abcdefg"));
  CHECK(loc::sanitizeName("", out, sizeof(out)) == 0 && out[0] == 0);
  CHECK(loc::sanitizeName(nullptr, out, sizeof(out)) == 0);
  CHECK(loc::sanitizeName("   ", out, sizeof(out)) == 0);
  // 30 x "ž" (2 bajty) -> ořízne se na 12 celých znaků (24 bajtů), žádný rozbitý znak
  char longName[64] = {0};
  for (int k = 0; k < 30; k++) strcat(longName, "ž");
  CHECK(loc::sanitizeName(longName, out, sizeof(out)) == 24 && strlen(out) == 24);
  CHECK((unsigned char)out[22] == 0xC5 && (unsigned char)out[23] == 0xBE);
  char odd[64] = "xxxxxxxxxxxxxxxxxxxxxxx";  // 23 bajtů + dvoubajtový znak se už nevejde
  strcat(odd, "č");
  CHECK(loc::sanitizeName(odd, out, sizeof(out)) == 23);
  CHECK(loc::sanitizeName("ab\xC5", out, sizeof(out)) == 2);  // useknutý UTF-8 znak se vynechá

  Location a;
  strcpy(a.name, "Dub; 2");  // ';' v jménu by kódování rozbilo -> jméno se vždy čistí sanitizeName
  loc::sanitizeName("Dub 2 / jih", a.name, sizeof(a.name));
  a.gDry = 0.12f; a.gWet = 4.5f; a.rawOpen = 4000; a.dry = 25; a.wet = 80;
  char enc[96];
  CHECK(loc::encode(a, enc, sizeof(enc)));
  Location b;
  CHECK(loc::decode(enc, b));
  CHECK(!strcmp(b.name, "Dub 2 / jih") && b.dry == 25 && b.wet == 80 && b.rawOpen == DEF_CAL_RAW_OPEN);
  // platnost místa (import / úprava v Servisu)
  CHECK(loc::valid(b));
  Location v = b;
  v.name[0] = 0; CHECK(!loc::valid(v));
  v = b; v.dry = 80; v.wet = 80; CHECK(!loc::valid(v));
  v = b; v.wet = 101; CHECK(!loc::valid(v));
  v = b; v.gWet = v.gDry * 1.1f; CHECK(!loc::valid(v));
  v = b; v.gDry = 0.0f; CHECK(!loc::valid(v));
  v = b; v.gWet = 1500.0f; CHECK(!loc::valid(v));
  v = b; v.gDry = NAN; CHECK(!loc::valid(v));
  CHECK(fabsf(b.gDry - 0.12f) < 1e-4f && fabsf(b.gWet - 4.5f) < 1e-4f);
  CHECK(!loc::decode("30;20;0.05;3;4095;x", b));    // mokro pod suchem
  CHECK(!loc::decode("30;85;0.05;3;100;x", b));     // naprázdno nesmysl
  CHECK(!loc::decode("30;85;3;0.05;4095;x", b));    // gDry > gWet
  CHECK(!loc::decode("nesmysl", b) && !loc::decode("", b) && !loc::decode(nullptr, b));
  CHECK(loc::decode("30;85;0.05;3;4095;", b) && b.name[0] == 0);  // prázdné jméno projde (stránka ukáže číslo)

  // tabulka: prázdná NVS -> jedno místo z nastavení
  nvsReset();
  Settings s;
  settingsDefaults(s);
  s.dryPct = 33; s.wetPct = 77; s.cal.gWet = 2.5f;
  locLoadAll(s);
  CHECK(locCount() == 1 && !strcmp(locGet(0).name, "Místo 1") && locGet(0).dry == 33 && locGet(0).wet == 77);
  CHECK(fabsf(locGet(0).gWet - 2.5f) < 1e-4f);
  // přidat do plna
  for (int k = 1; k < LOC_MAX; k++) {
    Location l = locGet(0);
    snprintf(l.name, sizeof(l.name), "Strom %d", k + 1);
    l.dry = (uint8_t)(10 + k);
    CHECK(locAdd(l));
  }
  CHECK(locCount() == LOC_MAX && !locAdd(locGet(0)));
  // změny jsou jen v RAM, dokud je neuloží locSave() (ten se volá mimo StateLock, viz locations.h)
  CHECK(locSave() && !locSave());  // podruhé už není co ukládat
  // po "restartu" se načte stejně
  locLoadAll(s);
  CHECK(locCount() == LOC_MAX && !strcmp(locGet(7).name, "Strom 8") && locGet(7).dry == 17);
  // smazání posouvá, poslední nejde smazat
  CHECK(locRemove(1));
  CHECK(locCount() == LOC_MAX - 1 && !strcmp(locGet(1).name, "Strom 3"));
  locLoadAll(s);  // bez locSave(): smazání se do NVS nedostalo
  CHECK(locCount() == LOC_MAX && !strcmp(locGet(1).name, "Strom 2"));
  CHECK(locRemove(1) && locSave());
  locLoadAll(s);
  CHECK(locCount() == LOC_MAX - 1 && !strcmp(locGet(1).name, "Strom 3"));
  while (locCount() > 1) CHECK(locRemove(0));
  CHECK(!locRemove(0) && locCount() == 1 && !strcmp(locGet(0).name, "Strom 8"));
  CHECK(!locRemove(5));
  // synchronizace: změna kalibrace aktivního místa se uloží do něj
  Location two = locGet(0);
  strcpy(two.name, "Záhon");
  CHECK(locAdd(two));
  Settings t = s;
  locToSettings(locGet(1), t);
  t.locActive = 1;
  CHECK(!locSyncFromSettings(t));  // beze změny
  t.cal.gDry = 0.2f; t.dryPct = 40;
  CHECK(locSyncFromSettings(t));
  CHECK(locSave());
  locLoadAll(s);
  CHECK(locGet(1).dry == 40 && fabsf(locGet(1).gDry - 0.2f) < 1e-4f && !strcmp(locGet(1).name, "Záhon"));
  CHECK(locGet(0).dry != 40);  // ostatní místa se nemění
  // import / úprava všech míst najednou: neplatná tabulka nic nezmění, platná nahradí vše a přežije restart
  {
    Location imp[3];
    for (int k = 0; k < 3; k++) {
      snprintf(imp[k].name, sizeof(imp[k].name), "Import %d", k + 1);
      imp[k].dry = (uint8_t)(20 + k); imp[k].wet = 80; imp[k].gDry = 0.1f; imp[k].gWet = 2.0f + k; imp[k].rawOpen = 3000;
    }
    imp[2].gWet = 0.11f;  // neplatná kalibrace
    CHECK(!locReplaceAll(imp, 3) && locCount() == 2 && !strcmp(locGet(1).name, "Záhon"));
    CHECK(!locReplaceAll(imp, 0) && !locReplaceAll(imp, LOC_MAX + 1) && !locReplaceAll(nullptr, 1));
    imp[2].gWet = 4.0f;
    CHECK(locReplaceAll(imp, 3) && locCount() == 3);
    CHECK(locSave());
    locLoadAll(s);
    CHECK(locCount() == 3 && !strcmp(locGet(2).name, "Import 3") && locGet(2).dry == 22 && fabsf(locGet(2).gWet - 4.0f) < 1e-4f);
    CHECK(locGet(0).rawOpen == DEF_CAL_RAW_OPEN);
    CHECK(locReplaceAll(imp, 1) && locCount() == 1);
    CHECK(locSave());
    locLoadAll(s);
    CHECK(locCount() == 1 && !strcmp(locGet(0).name, "Import 1"));
    CHECK(locReplaceAll(imp, 2) && locSave());  // zbytek testu počítá se dvěma místy
  }
  // poškozený záznam v NVS se vynechá, ostatní zůstanou
  {
    Preferences p;
    p.begin("arbolok", false);
    p.putString("loc0", "rozbite");
    p.end();
  }
  locLoadAll(s);
  CHECK(locCount() == 1 && !strcmp(locGet(0).name, "Import 2"));
  // aktivní místo v nastavení
  nvsReset();
  Settings u;
  settingsDefaults(u);
  CHECK(u.locActive == 0);
  u.locActive = 3;
  CHECK(settingsSave(u));
  Settings w;
  CHECK(settingsLoad(w) && w.locActive == 3);
  {
    Preferences p;
    p.begin("arbolok", false);
    p.putUChar("loca", 200);
    p.end();
  }
  CHECK(settingsLoad(w) && w.locActive == 0);
}

int main() {
  test_locations();
  test_advanced();
  test_empty_nvs();
  test_migrate_v3();
  test_migrate_v3_extremes();
  test_roundtrip();
  test_v4_beats_v3();
  test_corrupt_values();
  test_upgrade_from_v42();
  test_type_mismatch();
  test_factory_reset();
  test_wifi_password();
  printf("\n== settings (NVS): %d OK, %d FAIL ==\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
