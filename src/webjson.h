// ============================================================================
//  webjson.h  –  JSON odpovědi webového API (ČISTÉ C++, testováno na PC)
//
//  Handlery v webui.cpp jen naplní strukturu a zavolají sestavovací funkci.
//  Kontrakt s webovou aplikací (web/app.js) je popsán v docs/API.md.
// ============================================================================
#pragma once
#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "jsonbuf.h"

namespace webjson {

struct StatusView {
  // zařízení
  const char* fw = "";
  const char* id = "";
  int board = 1;
  uint32_t upSec = 0;
  uint32_t heap = 0;
  // hodiny
  bool clockOk = false;
  uint32_t clockUnix = 0;
  bool clockSet = false;
  // baterie
  float batV = NAN;
  int batPct = -1;
  int batLevel = 0;  // 0 = v pořádku, 1 = slabá, 2 = kritická
  // měření
  bool mHave = false;
  float mPct = NAN;
  float raw = NAN;
  float rawRaw = NAN;
  float g = NAN;
  int ms = 0;
  bool settled = true;
  int flags = 0;
  int ageSec = 0;
  float rawB = NAN;   // surové plató fáze B (obrácená polarita), NAN = neproběhla
  int phase = 0;      // 0 = přímá polarita, 1 = obrácená, 2 = poměr obou
  float temp = NAN;
  // stav
  bool busy = false;
  const char* msg = "";
  uint32_t seq = 0;
  int sessLeft = 0, sessTotal = 0, sessMax = 0;
  uint32_t fsTotal = 0, fsUsed = 0, logBytes = 0, logMax = 0;
  bool logErr = false;
  bool ota = false;
  // nastavení
  const char* lang = "cz";
  int webMin = 4, intMin = 0, dry = 30, wet = 85;
  float gDry = 0.05f, gWet = 3.0f;
  int rawOpen = 4095, scale = 1;
  bool vexc = false;
  int pmode = 0;
  float rPull = 0.0f, vcorr = 0.97f;
  int bias = 0;       // uložený pin BIAS (0 = zatím neurčeno)
  int biasUsed = 32;  // pin BIAS, který se skutečně používá
  int ret = 0;        // pin druhé elektrody (0 = GND)
  int maxMs = 300, eps = 5, pdis = 30, lpul = 1, cdel = 150, wjmp = 8, wwin = 6;
  int loc = 0;        // aktivní místo
  int locN = 0;       // počet míst
  char locName[8][25] = {};
  int locDry[8] = {}, locWet[8] = {};
  float locGd[8] = {}, locGw[8] = {};
  int cport = 0;      // po připojení k Wi-Fi: 0 nic, 1 okno s rozcestníkem, 2 tiše
  int early = 1000;   // krátký pulz (µs)
  int txp = 0;        // vysílač Wi-Fi: 0 normální, 1 nízký
  int man = 0, mnote = 2, mext = 1;  // ruční zápis (přenosný logger)
  int live = 15;      // obnovování živé hodnoty (s)
  // spotřeba a šetřící režim (od v6.5)
  uint32_t pwrSince = 0, pwrBoots = 0, pwrLogs = 0, pwrAwakeMs = 0, pwrWebSec = 0, pwrSleepSec = 0;
  bool saving = false;
  int intEff = 0;     // skutečný interval záznamu (min), v šetřícím režimu dvojnásobný
  float driftK = 0.0f;
  int capMah = 0;
  // test zapojení (poslední výsledek)
  bool wireHave = false;
  int wireRes = 0, wirePin = 0, wireN = 0;
  int wireP[3] = {0, 0, 0};
  float wireLo[3] = {0, 0, 0}, wireHi[3] = {0, 0, 0};
  // diagnostický profil
  bool profValid = false;
  int profN = 0;
};

// Vrací délku JSONu, nebo -1 při přetečení bufferu.
inline int buildStatus(char* out, size_t cap, const StatusView& s) {
  JsonBuf j(out, cap);
  j.obj();
  j.kvStr("fw", s.fw).kvStr("id", s.id).kvInt("board", s.board);
  j.kvUInt("up", s.upSec).kvUInt("heap", s.heap);

  j.key("clock").obj().kvBool("ok", s.clockOk).kvUInt("unix", s.clockUnix).kvBool("set", s.clockSet).end();

  j.key("bat").obj();
  j.kvNum("v", s.batV, 3);
  if (s.batPct >= 0) j.kvInt("pct", s.batPct); else j.key("pct").null();
  j.kvInt("lvl", s.batLevel).end();

  j.key("m").obj().kvBool("have", s.mHave);
  j.kvNum("pct", s.mPct, 1).kvNum("raw", s.raw, 1).kvNum("rawRaw", s.rawRaw, 1).kvNum("g", s.g, 4);
  j.kvInt("ms", s.ms).kvBool("settled", s.settled).kvInt("flags", s.flags).kvInt("age", s.ageSec);
  j.kvNum("rawB", s.rawB, 1).kvInt("ph", s.phase).end();

  j.kvNum("temp", s.temp, 1);
  j.kvBool("busy", s.busy).kvStr("msg", s.msg).kvUInt("seq", s.seq);
  j.key("sess").obj().kvInt("left", s.sessLeft).kvInt("total", s.sessTotal).kvInt("max", s.sessMax).end();
  j.key("fs").obj().kvUInt("total", s.fsTotal).kvUInt("used", s.fsUsed).kvUInt("log", s.logBytes).kvUInt("lmax", s.logMax).end();
  j.kvBool("logErr", s.logErr).kvBool("ota", s.ota);

  j.key("cfg").obj();
  j.kvStr("lang", s.lang).kvInt("web", s.webMin).kvInt("int", s.intMin).kvInt("dry", s.dry).kvInt("wet", s.wet);
  j.kvNum("gd", s.gDry, 4).kvNum("gw", s.gWet, 4).kvInt("open", s.rawOpen).kvInt("scale", s.scale);
  j.kvBool("vexc", s.vexc).kvInt("pmode", s.pmode).kvNum("rpull", s.rPull, 2).kvNum("vcorr", s.vcorr, 4);
  j.kvInt("bias", s.bias).kvInt("biasUsed", s.biasUsed).kvInt("ret", s.ret);
  j.kvInt("maxms", s.maxMs).kvInt("eps", s.eps).kvInt("pdis", s.pdis).kvInt("lpul", s.lpul).kvInt("cdel", s.cdel);
  j.kvInt("wjmp", s.wjmp).kvInt("wwin", s.wwin).kvInt("cport", s.cport).kvInt("txp", s.txp).kvInt("early", s.early);
  j.kvInt("man", s.man).kvInt("mnote", s.mnote).kvInt("mext", s.mext).kvInt("live", s.live);
  j.kvInt("loc", s.loc);
  j.key("locs").arr();
  for (int i = 0; i < s.locN && i < 8; i++) j.str(s.locName[i]);
  j.end();
  j.key("locd").arr();  // [zalévat pod, mokro nad, g sucho, g mokro] pro každé místo
  for (int i = 0; i < s.locN && i < 8; i++) {
    j.arr().integer(s.locDry[i]).integer(s.locWet[i]).num(s.locGd[i], 4).num(s.locGw[i], 4).end();
  }
  j.end().end();

  j.key("wire").obj().kvBool("have", s.wireHave).kvInt("res", s.wireRes).kvInt("pin", s.wirePin).kvInt("n", s.wireN);
  const int wn = s.wireN < 0 ? 0 : (s.wireN > 3 ? 3 : s.wireN);
  j.key("p").arr();
  for (int i = 0; i < wn; i++) j.num(s.wireP[i], 0);
  j.end();
  j.key("lo").arr();
  for (int i = 0; i < wn; i++) j.num(s.wireLo[i], 0);
  j.end();
  j.key("hi").arr();
  for (int i = 0; i < wn; i++) j.num(s.wireHi[i], 0);
  j.end();
  j.end();

  j.key("prof").obj().kvBool("valid", s.profValid).kvInt("n", s.profN).end();

  j.key("pwr").obj().kvBool("save", s.saving).kvInt("int", s.intEff);
  j.kvUInt("since", s.pwrSince).kvUInt("boots", s.pwrBoots).kvUInt("logs", s.pwrLogs);
  j.kvUInt("awake", s.pwrAwakeMs).kvUInt("web", s.pwrWebSec).kvUInt("sleep", s.pwrSleepSec);
  if (s.driftK > 0.0f) j.kvNum("k", s.driftK, 4); else j.key("k").null();
  j.kvInt("cap", s.capMah).end();
  j.end();
  return j.ok() ? (int)j.size() : -1;
}

// Diagnostický profil: {"n":N,"t":[ms...],"v":[čítače...]}
inline int buildProfile(char* out, size_t cap, const float* tMs, const float* v, int n) {
  JsonBuf j(out, cap);
  j.obj().kvInt("n", n);
  j.key("t").arr();
  for (int i = 0; i < n; i++) j.num(tMs[i], 2);
  j.end();
  j.key("v").arr();
  for (int i = 0; i < n; i++) j.num(v[i], 1);
  j.end();
  j.end();
  return j.ok() ? (int)j.size() : -1;
}

}  // namespace webjson
