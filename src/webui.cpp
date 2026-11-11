// ============================================================================
//  webui.cpp  –  HTTP API a vložená stránka
//
//  Pravidla:
//   * Všechny handlery běží v JEDNOM tasku (AsyncTCP), proto smí sdílet statické
//     buffery. Na hardware nesahají – stav čtou z kopie (liveSnapshot), změny
//     posílají hlavní smyčce příkazem (cmdPost).
//   * Každé POST volání ověřuje hlavičku Origin (ochrana proti cizím stránkám
//     otevřeným v telefonu, které by jinak mohly zařízení "ťuknout").
//   * Parametry se parsují přísně (webparse.h); neplatná hodnota = 400 + "err".
// ============================================================================
#include "webui.h"

#include "locations.h"

#include <Arduino.h>
#include <DNSServer.h>
#include <ESPAsyncHTTPUpdateServer.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <Update.h>

#include <memory>
#include <string.h>

#include "cmd.h"
#include "config.h"
#include "guard.h"
#include "jsonbuf.h"
#include "logstore.h"
#include "moisture.h"
#include "settings.h"
#include "state.h"
#include "timeutil.h"
#include "web_assets.h"
#include "webjson.h"
#include "webparse.h"

namespace {

// Zapomenutý (přerušený) OTA příznak se po této době zruší, ať zařízení neusíná donekonečna.
const uint32_t OTA_MAX_MS = 10UL * 60UL * 1000UL;
// Po skončení čtení souboru ještě chvíli počkáme (soubor se zavírá až při zrušení požadavku).
const uint32_t READER_GRACE_MS = 300;

AsyncWebServer* s_server = nullptr;
ESPAsyncHTTPUpdateServer s_updater;

DNSServer s_dns;                      // captive portal (viz captivePoll)
bool s_dnsOn = false;
volatile uint8_t s_captive = 0;       // režim po připojení (Settings::captive), čte webový task
volatile bool s_portalDone = false;   // rozcestník potvrzen ("Hotovo"): testy internetu dál dostanou "je internet"
volatile uint32_t s_served = 0;       // kolik požadavků na stránku / stav server obsloužil (viz webuiServed)
volatile int s_readers = 0;           // právě probíhající stahování souborů
volatile uint32_t s_readerEndMs = 0;  // kdy skončilo poslední (0 = nikdy)
volatile uint32_t s_otaBeginMs = 0;

char s_buf[3072];  // sdílený buffer pro JSON (viz poznámka o jednom tasku)

// ---------------------------------------------------------------------------
//  Odpovědi
// ---------------------------------------------------------------------------
void sendJson(AsyncWebServerRequest* r, int code, const char* body) {
  AsyncWebServerResponse* resp = r->beginResponse(code, "application/json", body);
  resp->addHeader("Cache-Control", "no-store");
  r->send(resp);
}

void sendOk(AsyncWebServerRequest* r) { sendJson(r, 200, "{\"ok\":true}"); }

void sendErr(AsyncWebServerRequest* r, int code, const char* err) {
  char b[64];
  snprintf(b, sizeof(b), "{\"ok\":false,\"err\":\"%s\"}", err);
  sendJson(r, code, b);
}

// Hlavička Origin (posílá ji prohlížeč u POST) musí mít stejného hostitele jako Host.
bool originOk(AsyncWebServerRequest* r) {
  if (!r->hasHeader("Origin")) return true;  // curl a pod.
  return webparse::sameOrigin(r->header("Origin").c_str(), r->header("Host").c_str());
}

// Hodnota parametru z URL (pro POST bez těla i pro GET), nebo nullptr.
const char* qp(AsyncWebServerRequest* r, const char* k) {
  const AsyncWebParameter* p = r->getParam(k);
  return p ? p->value().c_str() : nullptr;
}

void onPost(const char* path, void (*h)(AsyncWebServerRequest*)) {
  s_server->on(path, HTTP_POST, [h](AsyncWebServerRequest* r) {
    if (!originOk(r)) {
      sendErr(r, 403, "origin");
      return;
    }
    h(r);
  });
}

void postCmdAndReply(AsyncWebServerRequest* r, const Cmd& c) {
  if (cmdPost(c)) sendOk(r);
  else sendErr(r, 503, "queue");
}

// ---------------------------------------------------------------------------
//  Čtení souborů z flash (stahování): počítáme rozdělaná čtení, aby se pod nimi
//  nemazal ani nezkracoval log. Soubor se zavírá až při zrušení požadavku, proto
//  čekáme ještě READER_GRACE_MS.
// ---------------------------------------------------------------------------
bool readersBusy() {
  if (s_readers > 0) return true;
  const uint32_t e = s_readerEndMs;
  return e != 0 && (uint32_t)(millis() - e) < READER_GRACE_MS;
}

void trackReader(AsyncWebServerRequest* r) {
  s_readers = s_readers + 1;
  r->onDisconnect([]() {
    if (s_readers > 0) s_readers = s_readers - 1;
    s_readerEndMs = millis() | 1u;
  });
}

void sendEmpty(AsyncWebServerRequest* r, const char* ctype, const char* dispo) {
  AsyncWebServerResponse* e = r->beginResponse(200, ctype, "");
  e->addHeader("Cache-Control", "no-store");
  if (dispo) e->addHeader("Content-Disposition", dispo);
  r->send(e);
}

// Pošle soubor jako odpověď.
//  - emptyIfMissing: chybějící soubor = prázdný soubor (log, který ještě nevznikl nebo byl smazán),
//    jinak 404. Prázdný soubor knihovna posílat neumí (404), proto se řeší zvlášť.
//  - Log se otevírá a započítává mezi čtenáře POD ZÁMKEM LOGU: mezi otevřením a započtením tak
//    nemůže začít zředění (to by log smazalo/přejmenovalo pod otevřeným souborem). Zředění už
//    probíhá-li, zámek se nezíská včas a odpověď je 503 "busy" (stránka to zkusí znovu).
void sendFile(AsyncWebServerRequest* r, const String& path, const char* ctype, const char* dispo,
              bool emptyIfMissing = false) {
  const bool isLog = (path == LOG_PATH);
  LogGuard guard(isLog, LOG_WEB_LOCK_WAIT_MS);
  if (!guard.ok()) {
    sendErr(r, 503, "busy");
    return;
  }
  size_t sz = 0;
  {
    File f = LittleFS.open(path, "r");
    if (!f || f.isDirectory()) {
      if (emptyIfMissing) sendEmpty(r, ctype, dispo);
      else sendErr(r, 404, "notfound");
      return;
    }
    sz = f.size();
    f.close();
  }
  if (sz == 0) {
    sendEmpty(r, ctype, dispo);
    return;
  }
  AsyncWebServerResponse* resp = r->beginResponse(LittleFS, path, ctype);
  if (!resp) {  // knihovna vrací NULL, když soubor mezitím zmizel
    if (isLog) sendErr(r, 503, "busy");
    else sendErr(r, 404, "notfound");
    return;
  }
  resp->addHeader("Cache-Control", "no-store");
  if (dispo) resp->addHeader("Content-Disposition", dispo);  // nahradí výchozí "inline"
  trackReader(r);
  r->send(resp);
}

// ---------------------------------------------------------------------------
//  GET /   (vložená stránka, gzip + ETag)
// ---------------------------------------------------------------------------
// Test internetu z telefonu / cizí adresa v prohlížeči (DNS server posílá vše sem). true = vyřízeno.
bool captiveHandle(AsyncWebServerRequest* r) {
  if (r->method() != HTTP_GET) return false;
  const webparse::CaptiveAction a = webparse::captiveAction(s_captive, s_portalDone, r->host().c_str(), r->url().c_str());
  if (a == webparse::CA_NONE) return false;
  if (a == webparse::CA_PORTAL) {
    r->redirect("http://10.10.10.10/portal");
  } else if (a == webparse::CA_HOME) {
    r->redirect("http://10.10.10.10/");
  } else {
    switch (webparse::probeKind(r->url().c_str())) {
      case webparse::PR_APPLE:
        r->send(200, "text/html", "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");
        break;
      case webparse::PR_MSFT: r->send(200, "text/plain", "Microsoft Connect Test"); break;
      case webparse::PR_NCSI: r->send(200, "text/plain", "Microsoft NCSI"); break;
      case webparse::PR_FF_CANON:
        r->send(200, "text/html", "<meta http-equiv=\"refresh\" content=\"0;url=https://support.mozilla.org/kb/captive-portal\"/>");
        break;
      case webparse::PR_FF_OK: r->send(200, "text/plain", "success\n"); break;
      default: r->send(204); break;  // Android / Chrome (generate_204)
    }
  }
  return true;
}

void sendIcon(AsyncWebServerRequest* r, const uint8_t* data, size_t len) {
  AsyncWebServerResponse* resp = r->beginResponse(200, "image/png", data, len);
  resp->addHeader("Cache-Control", "max-age=604800");
  r->send(resp);
}

void handlePortal(AsyncWebServerRequest* r) {
  s_served = s_served + 1;  // rozcestník je také důkaz, že web funguje
  AsyncWebServerResponse* resp = r->beginResponse(200, "text/html; charset=utf-8", PORTAL_HTML_GZ, PORTAL_HTML_GZ_LEN);
  resp->addHeader("Content-Encoding", "gzip");
  resp->addHeader("Cache-Control", "no-store");
  r->send(resp);
}

void handleIndex(AsyncWebServerRequest* r) {
  if (captiveHandle(r)) return;
  s_served = s_served + 1;  // (volatile: bez ++, které je u volatile zastaralé; čte se jen "je nenulové")
  static const char etag[] = "\"" WEB_ASSET_HASH "\"";
  if (r->hasHeader("If-None-Match") && r->header("If-None-Match").indexOf(WEB_ASSET_HASH) >= 0) {
    AsyncWebServerResponse* nm = r->beginResponse(304);
    nm->addHeader("ETag", etag);
    nm->addHeader("Cache-Control", "no-cache");
    r->send(nm);
    return;
  }
  AsyncWebServerResponse* resp = r->beginResponse(200, "text/html; charset=utf-8", INDEX_HTML_GZ, INDEX_HTML_GZ_LEN);
  resp->addHeader("Content-Encoding", "gzip");
  resp->addHeader("ETag", etag);
  resp->addHeader("Cache-Control", "no-cache");
  r->send(resp);
}

// ---------------------------------------------------------------------------
//  GET /api/status
// ---------------------------------------------------------------------------
void handleStatus(AsyncWebServerRequest* r) {
  s_served = s_served + 1;
  LiveState L;
  Settings S;
  liveSnapshot(L, S);
  const uint32_t now = millis();

  webjson::StatusView v;
  // Zkušební provoz po OTA (firmware ještě není potvrzený, bootloader ho při resetu vrátí): přípona "(trial)".
  char fwText[24];
  if (guardTrial()) snprintf(fwText, sizeof(fwText), "%s (trial)", FW_VERSION);
  else snprintf(fwText, sizeof(fwText), "%s", FW_VERSION);
  v.fw = fwText;
  v.id = g_devId;
  v.board = ARBOLOG_BOARD;
  v.upSec = now / 1000UL;
  v.heap = ESP.getFreeHeap();

  v.clockOk = L.rtcOk;
  v.clockUnix = L.rtcOk ? (L.rtcUnix + (now - L.rtcAtMs) / 1000UL) : 0;
  v.clockSet = L.timeSet;

  v.batV = L.vbat;
  v.batPct = L.bpct;
  v.batLevel = 0;
  if (!isnan(L.vbat)) v.batLevel = (L.vbat < VBAT_NOWIFI_V) ? 2 : ((L.vbat < VBAT_WARN_V) ? 1 : 0);

  v.mHave = L.haveMeas && !isnan(L.raw);
  if (v.mHave) {
    // Procenta se počítají z uloženého čítače a AKTUÁLNÍ kalibrace – změna kalibrace
    // se tak projeví hned, ne až při dalším měření.
    // Zkrat / uzel nereaguje na BIAS (moist::isFault): o zemině to nic neříká, procento je null a stránka ukáže chybu.
    v.mPct = moist::isFault(L.flags) ? NAN : moist::percentFromRaw(L.raw, S.cal);
    v.raw = L.raw;
    v.rawRaw = L.rawRaw;
    v.g = L.g;
    v.ms = L.settleMs;
    v.settled = L.settled;
    v.flags = L.flags;
    v.rawB = L.rawB;
    v.phase = L.phase;
    v.ageSec = (int)((now - L.measAtMs) / 1000UL);
  }
  v.temp = L.temp;

  v.busy = L.busy;
  v.msg = L.msg;
  v.seq = L.msgSeq;
  int left = (int)((int32_t)(L.sessionEndMs - now) / 1000);
  if (left < 0) left = 0;
  v.sessLeft = left;
  v.sessTotal = (int)L.sessionTotalSec;
  v.sessMax = (int)sessionMaxSec(S);
  v.fsTotal = L.fsTotal;
  v.fsUsed = L.fsUsed;
  v.logBytes = L.logBytes;
  v.logGen = logGeneration();
  v.logMax = LOG_MAX_BYTES;
  v.logErr = L.logErr;
  v.ota = L.ota;

  v.lang = S.lang;
  v.webMin = S.webMin;
  v.intMin = S.intervalMin;
  v.dry = S.dryPct;
  v.wet = S.wetPct;
  v.gDry = S.cal.gDry;
  v.gWet = S.cal.gWet;
  v.rawOpen = S.cal.rawOpen;
  v.scale = S.cal.scale;
  v.vexc = S.vexcTrack;
  v.pmode = S.probeMode;
  v.rPull = S.rPullK;
  v.vcorr = S.vbatCorr;
  v.bias = S.biasPin;
  v.biasUsed = settingsBiasPin(S);
  {
    StateLock l;  // tabulku míst mění hlavní smyčka
    v.locN = locCount();
    for (int i = 0; i < v.locN && i < (int)LOC_MAX; i++) {
      const Location& l = locGet((uint8_t)i);
      strncpy(v.locName[i], l.name, sizeof(v.locName[i]) - 1);
      v.locDry[i] = l.dry;
      v.locWet[i] = l.wet;
      v.locGd[i] = l.gDry;
      v.locGw[i] = l.gWet;
      v.locName[i][sizeof(v.locName[i]) - 1] = 0;
    }
  }
  v.loc = S.locActive;
  v.ret = S.retPin;
  v.maxMs = S.maxMs;
  v.eps = S.epsCounts;
  v.pdis = S.predischMs;
  v.lpul = S.logPulses;
  v.cdel = S.confDelta;
  v.wjmp = S.wJumpPct;
  v.wwin = S.wWinH;
  v.cport = S.captive;
  v.txp = S.txLow;
  v.pw = S.wifiPw[0] ? 1 : (AP_PASSWORD[0] ? 2 : 0);
  v.man = S.manOn;
  v.mnote = S.manNote;
  v.mext = S.manExt;
  v.live = S.liveSec;
  v.pwrSince = L.pwrSince;
  v.pwrBoots = L.pwrBoots;
  v.pwrLogs = L.pwrLogs;
  v.pwrAwakeMs = L.pwrAwakeMs;
  v.pwrWebSec = L.pwrWebSec;
  v.pwrSleepSec = L.pwrSleepSec;
  v.saving = L.saving;
  v.intEff = L.intEffMin;
  v.driftK = L.driftK;
  v.capMah = BAT_CAPACITY_MAH;
  v.early = S.earlyUs;

  v.wireHave = L.wireHave;
  v.wireRes = L.wireRes;
  v.wirePin = L.wirePin;
  v.wireN = L.wireHave ? L.wireN : 0;
  // Kopírovat jen tolik, kolik je skutečných kandidátů (od v6.1 dva). Dřív tu bylo pevné 3 a četlo se za konec
  // polí ve stavu (state.h) – nedefinované chování, na které upozornil překladač.
  static_assert(wiring::kPinCount <= sizeof(v.wireP) / sizeof(v.wireP[0]), "webjson: malo mista pro kandidaty BIAS");
  for (size_t i = 0; i < wiring::kPinCount; i++) {
    v.wireP[i] = L.wirePins[i];
    v.wireLo[i] = L.wireLo[i];
    v.wireHi[i] = L.wireHi[i];
  }

  v.profValid = L.profValid;
  v.profN = L.profValid ? L.profN : 0;

  const int len = webjson::buildStatus(s_buf, sizeof(s_buf), v);
  if (len < 0) {
    sendErr(r, 500, "buf");
    return;
  }
  sendJson(r, 200, s_buf);
}

void handleProfile(AsyncWebServerRequest* r) {
  LiveState L;
  Settings S;
  liveSnapshot(L, S);
  const int n = L.profValid ? (int)L.profN : 0;
  const int len = webjson::buildProfile(s_buf, sizeof(s_buf), L.profT, L.profV, n);
  if (len < 0) {
    sendErr(r, 500, "buf");
    return;
  }
  sendJson(r, 200, s_buf);
}

// ---------------------------------------------------------------------------
//  Příkazy (POST) – zařadí se do fronty hlavní smyčky
// ---------------------------------------------------------------------------
void handleMeasure(AsyncWebServerRequest* r) {
  Cmd c;
  c.type = CMD_MEASURE;
  postCmdAndReply(r, c);
}

// Ruční zápis (přenosný logger): note = poznámka (volitelná, očistí se jako jméno místa, nejvýš 24 bajtů).
void handleManual(AsyncWebServerRequest* r) {
  Cmd c;
  c.type = CMD_MANUAL;
  const char* n = qp(r, "note");
  if (n) loc::sanitizeName(n, c.name, sizeof(c.name));
  postCmdAndReply(r, c);
}

void handleExtend(AsyncWebServerRequest* r) {
  Cmd c;
  c.type = CMD_EXTEND;
  postCmdAndReply(r, c);
}

void handleProfileCapture(AsyncWebServerRequest* r) {
  Cmd c;
  c.type = CMD_PROFILE;
  postCmdAndReply(r, c);
}

void handleCal(AsyncWebServerRequest* r) {
  const char* w = qp(r, "which");
  Cmd c;
  if (!w) c.type = CMD_NONE;
  else if (!strcmp(w, "dry")) c.type = CMD_CAL_DRY;
  else if (!strcmp(w, "wet")) c.type = CMD_CAL_WET;
  else if (!strcmp(w, "reset")) c.type = CMD_CAL_RESET;
  if (c.type == CMD_NONE) {
    sendErr(r, 400, "param");
    return;
  }
  postCmdAndReply(r, c);
}

// Místa: op=sel|new|ren|del, i = index, name = jméno (new, ren)
void handleLoc(AsyncWebServerRequest* r) {
  Cmd c;
  c.type = CMD_LOC;
  const char* op = qp(r, "op");
  if (!op) return sendErr(r, 400, "param");
  if (!strcmp(op, "sel")) c.i[0] = LOC_OP_SELECT;
  else if (!strcmp(op, "new")) c.i[0] = LOC_OP_NEW;
  else if (!strcmp(op, "ren")) c.i[0] = LOC_OP_RENAME;
  else if (!strcmp(op, "del")) c.i[0] = LOC_OP_DELETE;
  else return sendErr(r, 400, "param");
  long lv = 0;
  if (c.i[0] != LOC_OP_NEW) {
    if (!webparse::parseLong(qp(r, "i"), 0, LOC_MAX - 1, lv)) return sendErr(r, 400, "param");
    c.i[1] = (int32_t)lv;
  }
  if (c.i[0] == LOC_OP_NEW || c.i[0] == LOC_OP_RENAME) {
    if (loc::sanitizeName(qp(r, "name"), c.name, sizeof(c.name)) == 0) return sendErr(r, 400, "param");
  }
  postCmdAndReply(r, c);
}

// Všechna místa najednou (Servis → Místa a kalibrace: úprava v tabulce, import ze souboru).
// n = počet (1..8), pro každé i: n<i> jméno, d<i> zalévat pod, w<i> mokro nad, gd<i>/gw<i> kalibrace; a = vybrané místo.
void handleLocs(AsyncWebServerRequest* r) {
  long n = 0, lv = 0;
  if (!webparse::parseLong(qp(r, "n"), 1, LOC_MAX, n)) return sendErr(r, 400, "param");
  Location ls[LOC_MAX];
  char k[8];
  for (long i = 0; i < n; i++) {
    Location& l = ls[i];
    snprintf(k, sizeof(k), "n%ld", i);
    if (loc::sanitizeName(qp(r, k), l.name, sizeof(l.name)) == 0) return sendErr(r, 400, "param");
    snprintf(k, sizeof(k), "d%ld", i);
    if (!webparse::parseLong(qp(r, k), 0, 99, lv)) return sendErr(r, 400, "param");
    l.dry = (uint8_t)lv;
    snprintf(k, sizeof(k), "w%ld", i);
    if (!webparse::parseLong(qp(r, k), 1, 100, lv)) return sendErr(r, 400, "param");
    l.wet = (uint8_t)lv;
    snprintf(k, sizeof(k), "gd%ld", i);
    if (!webparse::parseFloat(qp(r, k), 0.0001f, 999.0f, l.gDry)) return sendErr(r, 400, "param");
    snprintf(k, sizeof(k), "gw%ld", i);
    if (!webparse::parseFloat(qp(r, k), 0.0001f, 999.0f, l.gWet)) return sendErr(r, 400, "param");
    if (!loc::valid(l)) return sendErr(r, 400, "loc");
  }
  long a = 0;
  if (qp(r, "a") && !webparse::parseLong(qp(r, "a"), 0, n - 1, a)) return sendErr(r, 400, "param");
  // o<i> (od v6.7): původní index místa i (0..7), -1 = nové místo. Podle toho se přečíslují záznamy v logu.
  uint32_t origPack = 0;
  bool haveOrig = false;
  for (long i = 0; i < n; i++) {
    snprintf(k, sizeof(k), "o%ld", i);
    const char* s = qp(r, k);
    if (!s) continue;
    if (!webparse::parseLong(s, -1, LOC_MAX - 1, lv)) return sendErr(r, 400, "param");
    haveOrig = true;
    if (lv >= 0) origPack |= (uint32_t)(lv + 1) << (4 * i);
  }
  if (!cmdStageLocs(ls, (uint8_t)n)) return sendErr(r, 503, "queue");
  Cmd c;
  c.type = CMD_LOCS;
  c.i[0] = (int32_t)a;
  c.i[1] = (int32_t)origPack;
  c.i[2] = haveOrig ? 1 : 0;
  if (!cmdPost(c)) {
    Location tmp[LOC_MAX];
    uint8_t tn = 0;
    cmdTakeLocs(tmp, tn);  // uvolnit odloženou tabulku
    return sendErr(r, 503, "queue");
  }
  sendOk(r);
}

void handleWiring(AsyncWebServerRequest* r) {
  Cmd c;
  c.type = CMD_WIRING;
  postCmdAndReply(r, c);
}

void handleCalVbat(AsyncWebServerRequest* r) {
  float v = 0;
  if (!webparse::parseFloat(qp(r, "v"), 2.0f, 4.5f, v)) {
    sendErr(r, 400, "vbat");
    return;
  }
  Cmd c;
  c.type = CMD_CAL_VBAT;
  c.f = v;
  postCmdAndReply(r, c);
}

void handleTime(AsyncWebServerRequest* r) {
  static const char* const K[6] = {"y", "mo", "d", "h", "mi", "s"};
  Cmd c;
  c.type = CMD_SET_TIME;
  for (int k = 0; k < 6; k++) {
    long v = 0;
    if (!webparse::parseLong(qp(r, K[k]), 0, 3000, v)) {
      sendErr(r, 400, "time");
      return;
    }
    c.i[k] = (int32_t)v;
  }
  if (!validDateTime(c.i[0], c.i[1], c.i[2], c.i[3], c.i[4], c.i[5])) {
    sendErr(r, 400, "time");
    return;
  }
  postCmdAndReply(r, c);
}

void handleSettings(AsyncWebServerRequest* r) {
  Cmd c;
  c.type = CMD_SET_SETTINGS;
  {
    StateLock l;  // výchozí = současné nastavení: skupinové masky (SM_PROBE, SM_WDET) pak nemění pole, která v URL nejsou
    c.set = g_set;
  }
  uint32_t mask = 0;
  const char* s;
  long lv = 0;

  if ((s = qp(r, "lang"))) {
    if (strcmp(s, "cz") && strcmp(s, "en") && strcmp(s, "ua")) return sendErr(r, 400, "param");
    strcpy(c.set.lang, s);
    mask |= SM_LANG;
  }
  if ((s = qp(r, "web"))) {
    if (!webparse::parseLong(s, 1, 30, lv)) return sendErr(r, 400, "param");
    c.set.webMin = (uint16_t)lv;
    mask |= SM_WEB;
  }
  if ((s = qp(r, "int"))) {
    if (!webparse::parseLong(s, 0, INTERVAL_MAX_MIN, lv)) return sendErr(r, 400, "param");
    c.set.intervalMin = (uint16_t)lv;
    mask |= SM_INT;
  }
  if ((s = qp(r, "dry"))) {
    if (!webparse::parseLong(s, 0, 99, lv)) return sendErr(r, 400, "param");
    c.set.dryPct = (uint8_t)lv;
    mask |= SM_DRY;
  }
  if ((s = qp(r, "wet"))) {
    if (!webparse::parseLong(s, 1, 100, lv)) return sendErr(r, 400, "param");
    c.set.wetPct = (uint8_t)lv;
    mask |= SM_WET;
  }
  if ((mask & SM_DRY) && (mask & SM_WET) && c.set.wetPct <= c.set.dryPct) return sendErr(r, 400, "param");
  if ((s = qp(r, "pmode"))) {  // 2 = krátký pulz (výchozí), 1 = dlouhý pulz 300 ms (starší sondy); 0 = dřív dlouhý do ustálení -> 1
    if (!webparse::parseLong(s, 0, 2, lv)) return sendErr(r, 400, "param");
    c.set.probeMode = (uint8_t)(lv == 0 ? 1 : lv);
    mask |= SM_PMODE;
  }
  if ((s = qp(r, "scale"))) {
    if (!webparse::parseLong(s, 0, 1, lv)) return sendErr(r, 400, "param");
    c.set.cal.scale = (uint8_t)lv;
    mask |= SM_SCALE;
  }
  if ((s = qp(r, "vexc"))) {
    if (!webparse::parseLong(s, 0, 1, lv)) return sendErr(r, 400, "param");
    c.set.vexcTrack = (lv == 1);
    mask |= SM_VEXC;
  }
  if ((s = qp(r, "bias"))) {
    if (!webparse::parseLong(s, 0, 60, lv)) return sendErr(r, 400, "param");
    if (lv != 0 && !wiring::validPin((uint8_t)lv)) return sendErr(r, 400, "param");
    c.set.biasPin = (uint8_t)lv;
    mask |= SM_BIAS;
  }
  if ((s = qp(r, "cport"))) {
    if (!webparse::parseLong(s, 0, 2, lv)) return sendErr(r, 400, "param");
    c.set.captive = (uint8_t)lv;
    mask |= SM_CAPT;
  }
  if ((s = qp(r, "txp"))) {  // vysílač Wi-Fi: 0 normální, 1 nízký (od v6.5)
    if (!webparse::parseLong(s, 0, 1, lv)) return sendErr(r, 400, "param");
    c.set.txLow = (uint8_t)lv;
    mask |= SM_TXP;
  }
  // ruční zápis (od v6.10): man (0/1), mnote (0 žádná, 1 text, 2 automatické číslo), mext (0/1) – jedna skupina
  if ((s = qp(r, "man")) || qp(r, "mnote") || qp(r, "mext")) {
    {
      StateLock l;  // nezadané položky skupiny zůstanou, jak jsou
      c.set.manOn = g_set.manOn;
      c.set.manNote = g_set.manNote;
      c.set.manExt = g_set.manExt;
    }
    if ((s = qp(r, "man"))) {
      if (!webparse::parseLong(s, 0, 1, lv)) return sendErr(r, 400, "param");
      c.set.manOn = (uint8_t)lv;
    }
    if ((s = qp(r, "mnote"))) {
      if (!webparse::parseLong(s, 0, 2, lv)) return sendErr(r, 400, "param");
      c.set.manNote = (uint8_t)lv;
    }
    if ((s = qp(r, "mext"))) {
      if (!webparse::parseLong(s, 0, 1, lv)) return sendErr(r, 400, "param");
      c.set.manExt = (uint8_t)lv;
    }
    mask |= SM_MAN;
  }
  if ((s = qp(r, "live"))) {  // obnovování živé hodnoty (s), od v6.11
    if (!webparse::parseLong(s, 5, 120, lv)) return sendErr(r, 400, "param");
    c.set.liveSec = (uint8_t)lv;
    mask |= SM_LIVE;
  }
  if ((s = qp(r, "ret"))) {
    if (!webparse::parseLong(s, 0, 60, lv)) return sendErr(r, 400, "param");
    if (lv != 0 && !wiring::validRetPin((uint8_t)lv)) return sendErr(r, 400, "param");
    c.set.retPin = (uint8_t)lv;
    mask |= SM_RET;
  }
  if ((s = qp(r, "vcorr"))) {  // korekce napětí baterie (obnova zálohy sondy)
    float fv = 0.0f;
    if (!webparse::parseFloat(s, 0.80f, 1.20f, fv)) return sendErr(r, 400, "param");
    c.set.vbatCorr = fv;
    mask |= SM_VCORR;
  }
  // Vyhodnocení zalití (skupina SM_WDET). Ostatní parametry pulzu jsou od v5.1 pevné (výchozí), API je nepřijímá.
  if ((s = qp(r, "wjmp"))) {
    if (!webparse::parseLong(s, 1, 50, lv)) return sendErr(r, 400, "param");
    c.set.wJumpPct = (uint8_t)lv;
    mask |= SM_WDET;
  }
  if ((s = qp(r, "wwin"))) {
    if (!webparse::parseLong(s, 1, 48, lv)) return sendErr(r, 400, "param");
    c.set.wWinH = (uint8_t)lv;
    mask |= SM_WDET;
  }
  if (!mask) return sendErr(r, 400, "param");
  c.mask = mask;
  postCmdAndReply(r, c);
}

// Heslo Wi-Fi (od v7.0): pw = 8..63 znaků ASCII, prázdné = otevřená síť. Zařízení heslo uloží a restartuje se
// (Wi-Fi s novým heslem); telefon se pak musí znovu připojit. Heslo se nikdy neposílá zpět (stav hlásí jen cfg.pw).
void handleWifiPw(AsyncWebServerRequest* r) {
  const char* s = qp(r, "pw");
  if (!s || !webparse::validWifiPassword(s)) return sendErr(r, 400, "pw");
  Cmd c;
  c.type = CMD_WIFI_PW;
  snprintf(c.pw, sizeof(c.pw), "%s", s);
  postCmdAndReply(r, c);
}

void handleRestart(AsyncWebServerRequest* r) {
  Cmd c;
  c.type = CMD_RESTART;
  postCmdAndReply(r, c);  // hlavní smyčka před restartem chvíli počká, ať odpověď odejde
}

void handleFactory(AsyncWebServerRequest* r) {
  Cmd c;
  c.type = CMD_FACTORY_RESET;
  postCmdAndReply(r, c);
}

// ---------------------------------------------------------------------------
//  Log a soubory
// ---------------------------------------------------------------------------
// Log, který ještě nevznikl (záznam vypnutý) nebo byl smazán, je prostě prázdný: 200 + prázdné tělo.
// GET /api/log – celý log, nebo jen jeho část (stránka si log drží v prohlížeči; velký log by se přes
// Wi-Fi zařízení tahal pokaždé několik vteřin):
//   ?from=N  bajty od N do konce = jen nové řádky; N větší než log -> 409 "stale" (log byl mezitím přepsán)
//   ?tail=N  jen posledních ~N bajtů, od začátku řádku = rychlé první zobrazení
// Hlavičky: X-Log-Size (velikost logu při otevření), X-Log-From (první poslaný bajt), X-Log-Gen (verze obsahu,
// mění se při každém přepsání logu – viz logGeneration()).
void handleLog(AsyncWebServerRequest* r) {
  long from = 0, tail = 0;
  const char* fs = qp(r, "from");
  const char* ts = qp(r, "tail");
  if (fs && !webparse::parseLong(fs, 0, 0x7fffffffL, from)) return sendErr(r, 400, "param");
  if (ts && !webparse::parseLong(ts, 1, 0x7fffffffL, tail)) return sendErr(r, 400, "param");
  const uint32_t gen = logGeneration();
  // Otevření a započtení čtenáře pod zámkem logu (viz sendFile): pod otevřeným souborem nezačne zředění.
  LogGuard guard(true, LOG_WEB_LOCK_WAIT_MS);
  if (!guard.ok()) return sendErr(r, 503, "busy");
  auto f = std::make_shared<File>(LittleFS.open(LOG_PATH, "r"));
  const size_t size = (*f && !f->isDirectory()) ? f->size() : 0;
  if ((size_t)from > size) return sendErr(r, 409, "stale");
  size_t start = (size_t)from;
  if (tail > 0 && size > (size_t)tail && (size_t)tail < size - start) {
    start = size - (size_t)tail;
    f->seek(start);
    uint8_t b[128];
    bool found = false;
    size_t got;
    while (!found && (got = f->read(b, sizeof(b))) > 0) {
      for (size_t i = 0; i < got; i++)
        if (b[i] == '\n') { start += i + 1; found = true; break; }
      if (!found) start += got;
    }
  }
  const size_t len = size - start;
  AsyncWebServerResponse* resp;
  if (len == 0) {
    resp = r->beginResponse(200, "text/plain; charset=utf-8", "");
  } else {
    f->seek(start);
    auto left = std::make_shared<size_t>(len);
    resp = r->beginResponse("text/plain; charset=utf-8", len, [f, left](uint8_t* buf, size_t maxLen, size_t) -> size_t {
      const size_t want = maxLen < *left ? maxLen : *left;
      const size_t got = want ? f->read(buf, want) : 0;
      *left -= got;
      return got;
    });
  }
  if (!resp) return sendErr(r, 500, "mem");
  resp->addHeader("Cache-Control", "no-store");
  resp->addHeader("X-Log-Size", String((uint32_t)size));
  resp->addHeader("X-Log-From", String((uint32_t)start));
  resp->addHeader("X-Log-Gen", String(gen));
  if (len) trackReader(r);
  r->send(resp);
}

void handleLogDownload(AsyncWebServerRequest* r) {
  char d[64];
  snprintf(d, sizeof(d), "attachment; filename=\"%s.txt\"", g_devId);
  sendFile(r, LOG_PATH, "text/plain", d, true);
}

// Smazání logu přes logstore (zámek proti souběhu se zápisem, odmítne se při stahování).
void clearLogAndReply(AsyncWebServerRequest* r) {
  bool busy = false;
  if (logClear(&busy)) sendOk(r);
  else if (busy) sendErr(r, 409, "busy");
  else sendErr(r, 500, "fs");
}

void handleLogClear(AsyncWebServerRequest* r) {
  if (webuiFsBusy()) return sendErr(r, 409, "busy");
  clearLogAndReply(r);
}

// Soubory, které uživateli nezobrazujeme (vnitřní dočasné).
bool hiddenName(const char* n) { return !strcmp(n, "log.tmp") || !strcmp(n, "up.tmp"); }

void handleFiles(AsyncWebServerRequest* r) {
  JsonBuf j(s_buf, sizeof(s_buf));
  j.arr();
  File root = LittleFS.open("/");
  if (root && root.isDirectory()) {
    int cnt = 0;
    for (File f = root.openNextFile(); f && cnt < 40; f = root.openNextFile()) {
      if (f.isDirectory()) continue;
      const char* nm = f.name();
      if (!nm || !*nm || hiddenName(nm)) continue;
      j.obj().kvStr("n", nm).kvUInt("s", (uint32_t)f.size()).end();
      cnt++;
    }
  }
  j.end();
  if (!j.ok()) return sendErr(r, 500, "buf");
  sendJson(r, 200, s_buf);
}

void handleFileGet(AsyncWebServerRequest* r) {
  const char* nm = qp(r, "name");
  if (!webparse::validExistingName(nm) || hiddenName(nm)) return sendErr(r, 400, "param");
  String path = String("/") + nm;
  sendFile(r, path, "application/octet-stream", nullptr);
}

void handleFileDelete(AsyncWebServerRequest* r) {
  const char* nm = qp(r, "name");
  if (!webparse::validExistingName(nm) || hiddenName(nm)) return sendErr(r, 400, "param");
  if (webuiFsBusy()) return sendErr(r, 409, "busy");
  if (!strcmp(nm, "log.txt")) {  // log maže logstore (zámek proti souběhu se zápisem)
    clearLogAndReply(r);
    return;
  }
  const String path = String("/") + nm;
  if (!LittleFS.exists(path)) return sendErr(r, 404, "notfound");
  if (LittleFS.remove(path)) sendOk(r);
  else sendErr(r, 500, "fs");
}

// ---------------------------------------------------------------------------
//  Nahrávání souboru (multipart, POST /upload)
//   - data jdou do /up.tmp; po úspěšném dokončení se atomicky přejmenují
//   - soubor se uloží pod svým (očištěným) jménem; log (log.txt) nahrávání nikdy nepřepíše (od v7.0, do v6.16
//     nahradil log každý soubor .txt) – jméno log.txt se odmítne
//   - nahrávání nikdy nepřepíše existující soubor, dokud není celé přijaté
// ---------------------------------------------------------------------------
struct UpState {
  bool active = false;
  bool failed = false;
  int failCode = 500;
  char name[40] = "";
  AsyncWebServerRequest* owner = nullptr;
  File f;
};
UpState s_up;

void upAbort() {
  if (s_up.f) s_up.f.close();
  if (s_up.active) LittleFS.remove(UP_TMP_PATH);
  s_up.active = false;
  s_up.failed = false;
  s_up.owner = nullptr;
}

void upFail(int code) {
  s_up.failed = true;
  s_up.failCode = code;
}

void onUploadData(AsyncWebServerRequest* r, const String& filename, size_t index, uint8_t* data, size_t len, bool final) {
  (void)final;
  if (index == 0) {
    if (s_up.active) upAbort();  // zapomenutý předchozí pokus
    s_up = UpState();
    s_up.owner = r;
    r->onDisconnect([r]() {
      if (s_up.active && s_up.owner == r) upAbort();
    });

    char clean[40];
    if (!originOk(r)) upFail(403);
    else if (!webparse::sanitizeUploadName(filename.c_str(), clean, sizeof(clean))) upFail(400);
    else if (!strcmp(clean, "log.txt") || !strcmp(clean, "log.tmp") || !strcmp(clean, "up.tmp")) upFail(400);
    else {
      strcpy(s_up.name, clean);
      // Po nahrání musí logger mít dost místa (LOG_MIN_FREE_BYTES + dočasný soubor pro zředění),
      // jinak by u každého zápisu zbytečně zřeďoval. Velikost logu z poslední obnovy stavu
      // (bez zámku logu: task AsyncTCP nesmí čekat).
      LiveState L;
      Settings S;
      liveSnapshot(L, S);
      const size_t tot = fsTotalBytes(), used = fsUsedBytes();
      const size_t freeB = tot > used ? tot - used : 0;
      if (!webparse::uploadFits(r->contentLength(), freeB, L.logBytes, false, LOG_MIN_FREE_BYTES,
                                UPLOAD_HEADROOM_BYTES))
        upFail(507);  // nevejde se (i s rezervou pro logger)
      else {
        s_up.f = LittleFS.open(UP_TMP_PATH, "w");
        if (!s_up.f) upFail(500);
      }
    }
    s_up.active = true;  // i po chybě: konec požadavku to uklidí
  }
  if (!s_up.active || s_up.owner != r || s_up.failed) return;
  if (len && s_up.f.write(data, len) != len) upFail(507);
}

void onUploadDone(AsyncWebServerRequest* r) {
  if (!s_up.active || s_up.owner != r) {  // v požadavku nebyl žádný soubor
    sendErr(r, 400, "upload");
    return;
  }
  const bool failed = s_up.failed;
  const int code = s_up.failCode;
  if (s_up.f) s_up.f.close();
  bool ok = !failed;
  bool busy = false;
  if (ok) {
    const String dst = String("/") + s_up.name;
    if (LittleFS.exists(dst)) {
      if (readersBusy()) {  // soubor se právě stahuje – nepřepisovat pod rukama
        busy = true;
        ok = false;
      } else {
        LittleFS.remove(dst);
      }
    }
    if (ok) ok = LittleFS.rename(UP_TMP_PATH, dst);
  }
  if (!ok) LittleFS.remove(UP_TMP_PATH);
  s_up.active = false;
  s_up.owner = nullptr;
  if (ok) sendOk(r);
  else if (busy) sendErr(r, 409, "busy");
  else sendErr(r, failed ? code : 500, !failed ? "fs" : (code == 403 ? "origin" : (code == 400 ? "param" : "fs")));
}

// ---------------------------------------------------------------------------
//  OTA (/update) – příznak "neusínat"
// ---------------------------------------------------------------------------
void onOtaBegin(const UpdateType type, int& result) {
  (void)type;
  // Právě běžící firmware obsluhuje stránku /update, tedy se prokázal. Nepotvrzený (zkušební) firmware se potvrdí
  // TEĎ, před zápisem: Arduino-ESP32 zapisuje nový obraz do "druhého" oddílu bez ohledu na stav, takže by
  // aktualizace spuštěná ve zkušební době přepsala předchozí, jediný ověřený firmware (po úspěšném zápisu by pak
  // byl ověřen jen ten nejnovější a návrat by neměl kam).
  guardConfirm();
  if (Update.isRunning()) Update.abort();  // zapomenutý přerušený pokus by jinak blokoval nový
  s_otaBeginMs = millis() | 1u;
  {
    StateLock l;
    g_live.ota = true;
  }
  result = UPDATE_OK;  // bez toho zůstává stav v knihovně neinicializovaný
}

void onOtaEnd(const UpdateType type, int& result) {
  if (result != UPDATE_OK) {
    StateLock l;
    g_live.ota = false;
  } else if (type == FIRMWARE) {
    guardClear();  // nový firmware dostane čistý štít (počítadlo pádů i nouzový režim, viz bootguard.h)
  }
}

}  // namespace

// ---------------------------------------------------------------------------
//  Veřejné rozhraní
// ---------------------------------------------------------------------------
bool webuiFsBusy() { return s_up.active || readersBusy(); }

bool webuiServed() { return s_served != 0; }

bool webuiTransferActive() { return s_readers > 0 || s_up.active || g_live.ota; }

// Captive portal: DNS server odpovídá na každý dotaz adresou zařízení, takže telefon po připojení pošle svůj test
// internetu (captive.apple.com, connectivitycheck.gstatic.com, msftconnecttest.com...) sem, dostane přesměrování
// na stránku a otevře ji sám. Zapíná / vypíná se podle nastavení (g_set.captive) z hlavní smyčky.
static void captivePoll() {
  uint8_t mode;
  {
    StateLock l;
    mode = g_set.captive;
  }
  s_captive = mode;
  const bool want = mode != 0;
  if (want && !s_dnsOn) {
    s_dns.setErrorReplyCode(DNSReplyCode::NoError);
    s_dns.setTTL(10);
    s_dnsOn = s_dns.start(53, "*", IPAddress(10, 10, 10, 10));
  } else if (!want && s_dnsOn) {
    s_dns.stop();
    s_dnsOn = false;
  }
  if (s_dnsOn) s_dns.processNextRequest();
}

void webuiPoll() {
  if (s_server) captivePoll();
  const uint32_t b = s_otaBeginMs;
  if (b != 0 && g_live.ota && (uint32_t)(millis() - b) > OTA_MAX_MS) {
    // Update.abort() odsud NE: webový task může být právě uvnitř Update.write() a abort uvolní jeho buffer.
    // Přerušený stav updateru nevadí, zahodí ho další onOtaBegin (ve webovém tasku) nebo hluboký spánek.
    StateLock l;
    g_live.ota = false;
    s_otaBeginMs = 0;
  }
}

void webuiBegin() {
  if (s_server) return;
  s_server = new AsyncWebServer(80);

  // Logger při stahování souborů nezkracuje ani nepřepisuje log (viz logstore.h).
  logSetReaderProbe(readersBusy);

  // OTA aktualizace (/update). Při nastaveném AP_PASSWORD se chrání jménem "admin".
  s_updater.onUpdateBegin = onOtaBegin;
  s_updater.onUpdateEnd = onOtaEnd;
  // Knihovna čte parametr "name" bez kontroly (request->getParam("name")->value()), takže POST na
  // /update bez něj (např. curl -F firmware=@fw.bin) by shodil zařízení. Takový požadavek zachytí
  // tento handler dřív: knihovna páruje handlery v pořadí registrace, první vyhovující vyhrává.
  s_server
      ->on("/update", HTTP_POST, [](AsyncWebServerRequest* r) { r->send(400, "text/plain", "missing name"); })
      .setFilter([](AsyncWebServerRequest* r) { return !r->hasParam("name"); });
  // Firmware z cizí stránky otevřené v telefonu (hlavička Origin jiná než Host): odmítnout dřív, než se začne
  // zapisovat. Stejná ochrana jako u ostatních POST (onPost); knihovna OTA ji sama nemá.
  s_server->on("/update", HTTP_POST, [](AsyncWebServerRequest* r) { sendErr(r, 403, "origin"); })
      .setFilter([](AsyncWebServerRequest* r) { return !originOk(r); });
  // Vlastní stránka /update (vzhled jako zbytek webu, kontrola souboru v prohlížeči, průběh nahrávání). Registruje
  // se PŘED knihovnou: první vyhovující handler vyhrává, takže knihovní GET stránka se už nepoužije. Samotné
  // nahrávání (POST /update?name=firmware) dál obsluhuje knihovna beze změny.
  s_server->on("/update", HTTP_GET, [](AsyncWebServerRequest* r) {
    if (AP_PASSWORD[0] && !r->authenticate("admin", AP_PASSWORD)) return r->requestAuthentication();
    AsyncWebServerResponse* resp = r->beginResponse(200, "text/html; charset=utf-8", UPDATE_HTML_GZ, UPDATE_HTML_GZ_LEN);
    resp->addHeader("Content-Encoding", "gzip");
    resp->addHeader("Cache-Control", "no-cache");
    r->send(resp);
  });
  if (AP_PASSWORD[0]) s_updater.setup(s_server, "admin", AP_PASSWORD);
  else s_updater.setup(s_server);

  // stránka
  // ikona a manifest pro "Přidat na plochu" (iPhone, Android): aplikace pak má ikonu měřáku a otevírá se bez lišty
  s_server->on("/apple-touch-icon.png", HTTP_GET, [](AsyncWebServerRequest* r) { sendIcon(r, ICON_180_PNG, ICON_180_PNG_LEN); });
  s_server->on("/apple-touch-icon-precomposed.png", HTTP_GET, [](AsyncWebServerRequest* r) { sendIcon(r, ICON_180_PNG, ICON_180_PNG_LEN); });
  s_server->on("/icon-192.png", HTTP_GET, [](AsyncWebServerRequest* r) { sendIcon(r, ICON_192_PNG, ICON_192_PNG_LEN); });
  s_server->on("/manifest.webmanifest", HTTP_GET, [](AsyncWebServerRequest* r) {
    AsyncWebServerResponse* resp = r->beginResponse(200, "application/manifest+json",
        "{\"name\":\"Arbolok\",\"short_name\":\"Arbolok\",\"start_url\":\"/\",\"scope\":\"/\",\"display\":\"standalone\","
        "\"background_color\":\"#f3efe6\",\"theme_color\":\"#24452f\","
        "\"icons\":[{\"src\":\"/icon-192.png\",\"sizes\":\"192x192\",\"type\":\"image/png\",\"purpose\":\"any\"},"
        "{\"src\":\"/apple-touch-icon.png\",\"sizes\":\"180x180\",\"type\":\"image/png\"}]}");
    resp->addHeader("Cache-Control", "max-age=86400");
    r->send(resp);
  });
  s_server->on("/portal/ok", HTTP_GET, [](AsyncWebServerRequest* r) {
    s_portalDone = true;  // od teď testy internetu dostanou "je internet": okno ukáže Hotovo, telefon zůstane připojený
    r->send(204);
  });
  s_server->on("/portal", HTTP_GET, handlePortal);
  s_server->on("/", HTTP_GET, handleIndex);
  s_server->on("/index.html", HTTP_GET, handleIndex);

  // čtení
  s_server->on("/api/status", HTTP_GET, handleStatus);
  s_server->on("/api/profile", HTTP_GET, handleProfile);
  // POZOR: knihovna páruje URL i jako předponu ("/api/log" chytí i "/api/log/download"),
  // takže konkrétnější cesty se musí registrovat PŘED obecnějšími.
  s_server->on("/api/log/download", HTTP_GET, handleLogDownload);
  s_server->on("/api/log", HTTP_GET, handleLog);
  s_server->on("/api/files", HTTP_GET, handleFiles);
  s_server->on("/api/file", HTTP_GET, handleFileGet);

  // zápis
  onPost("/api/measure", handleMeasure);
  onPost("/api/extend", handleExtend);
  onPost("/api/manual", handleManual);
  onPost("/api/profile/capture", handleProfileCapture);
  onPost("/api/cal", handleCal);
  onPost("/api/calvbat", handleCalVbat);
  onPost("/api/wiring", handleWiring);
  onPost("/api/loc", handleLoc);
  onPost("/api/locs", handleLocs);
  onPost("/api/time", handleTime);
  onPost("/api/settings", handleSettings);
  onPost("/api/restart", handleRestart);
  onPost("/api/wifipw", handleWifiPw);
  onPost("/api/factory", handleFactory);
  onPost("/api/log/clear", handleLogClear);
  onPost("/api/files/delete", handleFileDelete);
  s_server->on("/upload", HTTP_POST, onUploadDone, onUploadData);

  // staré adresy z verze 3 (záložky v telefonu, skripty)
  s_server->on("/settings", HTTP_GET, [](AsyncWebServerRequest* r) { r->redirect("/#/settings"); });
  s_server->on("/logs", HTTP_GET, [](AsyncWebServerRequest* r) { r->redirect("/#/logs"); });
  s_server->on("/upload", HTTP_GET, [](AsyncWebServerRequest* r) { r->redirect("/"); });  // stránka Soubory už není
  s_server->on("/log.txt", HTTP_GET, handleLog);
  s_server->on("/download", HTTP_GET, handleLogDownload);

  s_server->onNotFound([](AsyncWebServerRequest* r) {
    if (captiveHandle(r)) return;  // test internetu z telefonu nebo cizí adresa (podle režimu po připojení)
    if (r->url() == "/favicon.ico") {
      sendIcon(r, ICON_192_PNG, ICON_192_PNG_LEN);
      return;
    }
    sendErr(r, 404, "notfound");
  });

  s_server->begin();
}
