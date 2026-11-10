// ============================================================================
//  logfmt.h  –  formát řádku logu a zředění starých záznamů (ČISTÉ C++)
//
//  Řádek (zpětně kompatibilní s v3 – prvních 5 sloupců je stejných):
//    2026-11-02 17:03 45% 21.3C 3.25V r2873 s12 p2 [?] [!]
//     datum    čas   vlhk. teplota  Ubat  surový ADC, doba ustálení (ms) [čas?] [slabá baterie]
//  Za "s" je případně písmeno "u" = nenalezeno plató (měření dosáhlo stropu). "p2" = místo č. 2 (od v5.0).
//  "H" na konci = zařízení tu přešlo do hibernace kvůli vybitému článku (od v5.6; vlhkost "--%", platí napětí).
//  Webová stránka čte sloupce podle pozice a nové sloupce ignoruje.
// ============================================================================
#pragma once
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace logfmt {

struct Record {
  int year = 2000, month = 1, day = 1, hour = 0, minute = 0;
  int hum = -1;          // 0..100, <0 = neznámé
  float temp = NAN;      // °C, NAN = chyba čtení RTC
  float vbat = NAN;      // V
  int raw = -1;          // surový ADC (po mediánu okna); od v6.13 se do logu nezapisuje (zůstává pro testy a API)
  int settleMs = -1;     // doba ustálení, <0 = neuvedeno
  bool settled = true;
  bool timeOk = true;    // false = hodiny nebyly nastaveny (čas je pochybný)
  bool lowBat = false;
  int loc = -1;          // číslo místa (od 1, locations.h), <1 = neuvedeno
  bool hibernate = false;  // " H": odtud měření stojí (vybitý článek), dokud se baterie nedobije
  bool manual = false;     // " M": ruční zápis tlačítkem (přenosný logger, od v6.10)
  const char* note = nullptr;  // poznámka ručního zápisu, na konci řádku za " # " (už očištěná, bez CR/LF)
};

// Krátký formát řádku (od v6.13; asi 22 B místo 46 B, log tak vydrží přes dvakrát déle):
//   RRMMDDhhmm VLHKOST TEPLOTA NAPĚTÍ [sNN[u]] [pN] [?] [!] [H] [M] [# poznámka]
//   2611091300 52 21 325              = 9. 11. 2026 13:00, 52 %, 21 °C, 3,25 V
// Vlhkost v %, teplota v celých °C (od v6.14; v6.13 s 1 desetinným místem), napětí v setinách voltu; neznámá = "-".
// Surový čítač ADC se od v6.13 nezapisuje. sNN jen u neustáleného nebo dlouhého pulzu (> 1 ms), pN jen u míst 2..8
// (bez p = místo 1). Starší řádky ("2026-11-09 13:00 52% 21.3C 3.25V r2873 s12 p1") v logu zůstávají a stránka
// čte oba formáty; přečíslování míst a zředění s oběma počítají (řádek záznamu začíná číslicí).
// Vrací délku (bez NUL), nebo -1 když se řádek nevešel do bufferu.
inline int formatLine(char* out, size_t cap, const Record& r) {
  int n = snprintf(out, cap, "%02d%02d%02d%02d%02d", r.year % 100, r.month, r.day, r.hour, r.minute);
  if (n < 0 || (size_t)n >= cap) return -1;
  size_t pos = (size_t)n;

  if (r.hum >= 0) n = snprintf(out + pos, cap - pos, " %d", r.hum);
  else n = snprintf(out + pos, cap - pos, " -");
  if (n < 0 || pos + (size_t)n >= cap) return -1;
  pos += (size_t)n;

  if (!isnan(r.temp)) n = snprintf(out + pos, cap - pos, " %ld", lroundf(r.temp));  // celé °C (od v6.14)
  else n = snprintf(out + pos, cap - pos, " -");
  if (n < 0 || pos + (size_t)n >= cap) return -1;
  pos += (size_t)n;

  if (!isnan(r.vbat)) n = snprintf(out + pos, cap - pos, " %ld", lroundf(r.vbat * 100.0f));
  else n = snprintf(out + pos, cap - pos, " -");
  if (n < 0 || pos + (size_t)n >= cap) return -1;
  pos += (size_t)n;

  if (r.settleMs >= 0 && (!r.settled || r.settleMs > 1)) {
    n = snprintf(out + pos, cap - pos, " s%d%s", r.settleMs, r.settled ? "" : "u");
    if (n < 0 || pos + (size_t)n >= cap) return -1;
    pos += (size_t)n;
  }
  if (r.loc >= 2) {
    n = snprintf(out + pos, cap - pos, " p%d", r.loc);
    if (n < 0 || pos + (size_t)n >= cap) return -1;
    pos += (size_t)n;
  }
  if (!r.timeOk) {
    n = snprintf(out + pos, cap - pos, " ?");
    if (n < 0 || pos + (size_t)n >= cap) return -1;
    pos += (size_t)n;
  }
  if (r.lowBat) {
    n = snprintf(out + pos, cap - pos, " !");
    if (n < 0 || pos + (size_t)n >= cap) return -1;
    pos += (size_t)n;
  }
  if (r.hibernate) {
    n = snprintf(out + pos, cap - pos, " H");
    if (n < 0 || pos + (size_t)n >= cap) return -1;
    pos += (size_t)n;
  }
  if (r.manual) {
    n = snprintf(out + pos, cap - pos, " M");
    if (n < 0 || pos + (size_t)n >= cap) return -1;
    pos += (size_t)n;
  }
  if (r.note && r.note[0]) {
    n = snprintf(out + pos, cap - pos, " # %s", r.note);
    if (n < 0 || pos + (size_t)n >= cap) return -1;
    pos += (size_t)n;
  }
  n = snprintf(out + pos, cap - pos, "\r\n");
  if (n < 0 || pos + (size_t)n >= cap) return -1;
  pos += (size_t)n;
  return (int)pos;
}

// Kde končí údaje řádku: před poznámkou " # …" (ruční zápis), jinak délka řádku.
inline size_t dataEnd(const char* in, size_t len) {
  for (size_t i = 0; i + 2 < len; i++)
    if (in[i] == ' ' && in[i + 1] == '#' && (in[i + 2] == ' ' || in[i + 2] == '\r' || in[i + 2] == '\n')) return i;
  return len;
}

// Ruční zápis (" M" mezi údaji, ne v poznámce)?
inline bool isManualLine(const char* in, size_t len) {
  const size_t end = dataEnd(in, len);
  for (size_t i = 0; i + 1 < end; i++)
    if (in[i] == ' ' && in[i + 1] == 'M' && (i + 2 >= end || in[i + 2] == ' ' || in[i + 2] == '\r' || in[i + 2] == '\n'))
      return true;
  return false;
}

// Řádek bez konce (delší než buffer čtečky, a proto zkrácený, nebo poslední řádek souboru bez CR/LF) dostane "\r\n",
// jinak by se při přepisu logu slepil s následujícím řádkem (nebo s dalším zápisem logAppend). Když se do bufferu
// nevejde, přepíšou se jím poslední dva znaky. buf má kapacitu cap (včetně NUL). Vrací novou délku.
inline size_t terminateLine(char* buf, size_t len, size_t cap) {
  if (len == 0 || buf[len - 1] == '\n' || cap < 3) return len;
  if (len + 2 >= cap) len = cap - 3;
  buf[len++] = '\r';
  buf[len++] = '\n';
  buf[len] = '\0';
  return len;
}

// ---------------------------------------------------------------------------
//  Zředění logu
//
//  Novější polovina souboru (podle bajtů) zůstane celá, starší polovina se zředí
//  na každý druhý řádek (kromě ručních zápisů " M", ty zůstávají všechny). Opakovaně aplikované: nejstarší data jsou nejřidší, ale
//  celá sezóna zůstane v grafu "všechny záznamy" a soubor nepřeroste strop.
//
//  Reader:  bool readLine(char* buf, size_t cap, size_t& rawLen)
//             buf = řádek včetně '\n' (zakončený NUL), rawLen = kolik bajtů proudu
//             řádek zabral (může být > strlen, když byl řádek delší než cap).
//             false = konec souboru.
//  Writer:  bool writeLine(const char* buf, size_t len)
// ---------------------------------------------------------------------------
template <class Reader, class Writer>
inline bool compact(Reader& in, Writer& out, size_t totalBytes, size_t* linesIn = nullptr,
                    size_t* linesOut = nullptr) {
  char buf[160];
  size_t rawLen = 0, pos = 0, lineNo = 0, nIn = 0, nOut = 0;
  const size_t split = totalBytes / 2;
  while (in.readLine(buf, sizeof(buf), rawLen)) {
    pos += rawLen;
    size_t len = strnlen(buf, sizeof(buf));
    if (len < 8) continue;  // prázdný / zkomolený řádek
    len = terminateLine(buf, len, sizeof(buf));
    nIn++;
    bool keep = true;
    if (pos <= split && !isManualLine(buf, len)) keep = ((lineNo++ & 1u) == 0);  // ruční zápisy zůstávají všechny
    if (keep) {
      if (!out.writeLine(buf, len)) return false;
      nOut++;
    }
  }
  if (linesIn) *linesIn = nIn;
  if (linesOut) *linesOut = nOut;
  return true;
}

// ---------------------------------------------------------------------------
//  Přečíslování míst (od v6.7)
//
//  Záznam nese číslo místa ("pN", 1–8; bez "p" = místo 1). Po smazání nebo přeuspořádání míst se čísla musí
//  přepsat, jinak by se záznamy smazaného místa ukazovaly u následujícího a záznamy dalších míst u špatných míst.
//  map[k] (k = 1..nOld) = nové číslo místa; 0 = místo se smazalo: záznam dostane "p0" (zůstane v logu a v exportu
//  jako "smazané místo", stránka ho u žádného místa neukazuje). Hodnota mimo 1..nOld (i už zapsané "p0") se nemění.
//  Řádek, který nezačíná číslicí (není to záznam), se nemění.
//  Vrací délku nového řádku (bez NUL), nebo -1, když se nevejde do cap.
// ---------------------------------------------------------------------------
inline int remapPlaceLine(const char* in, size_t len, char* out, size_t cap, const uint8_t* map, uint8_t nOld) {
  if (cap == 0) return -1;
  const bool record = len > 0 && in[0] >= '0' && in[0] <= '9';
  const size_t lim = dataEnd(in, len);  // poznámka se nepřečísluje (může v ní být cokoli, i " p3")
  // hledání tokenu " pN" (p a číslice, za ní mezera / konec řádku)
  size_t ps = 0, pe = 0;  // [ps, pe) = číslice
  bool found = false;
  if (record) {
    for (size_t i = 1; i + 1 < lim; i++) {
      if (in[i] != 'p' || in[i - 1] != ' ' || in[i + 1] < '0' || in[i + 1] > '9') continue;
      size_t j = i + 1;
      while (j < lim && in[j] >= '0' && in[j] <= '9') j++;
      if (j < lim && in[j] != ' ' && in[j] != '\r' && in[j] != '\n') continue;
      ps = i + 1;
      pe = j;
      found = true;
      break;
    }
  }
  long oldN = 1;
  if (found) {
    oldN = 0;
    for (size_t i = ps; i < pe && oldN < 1000; i++) oldN = oldN * 10 + (in[i] - '0');
  }
  const bool change = record && oldN >= 1 && oldN <= (long)nOld && map[oldN] != (uint8_t)oldN;
  if (!change) {
    if (len + 1 > cap) return -1;
    memcpy(out, in, len);
    out[len] = '\0';
    return (int)len;
  }
  char num[8];
  const int nl = snprintf(num, sizeof(num), "%u", (unsigned)map[oldN]);
  size_t o = 0;
  auto put = [&](const char* s, size_t n) {
    if (o + n + 1 > cap) return false;
    memcpy(out + o, s, n);
    o += n;
    return true;
  };
  bool ok;
  if (found) {
    ok = put(in, ps) && put(num, (size_t)nl) && put(in + pe, len - pe);
  } else {  // bez "p" (místo 1): " pN" před konec údajů (před poznámku, jinak před konec řádku)
    size_t body = lim;
    while (body > 0 && (in[body - 1] == '\n' || in[body - 1] == '\r')) body--;
    ok = put(in, body) && put(" p", 2) && put(num, (size_t)nl) && put(in + body, len - body);
  }
  if (!ok) return -1;
  out[o] = '\0';
  return (int)o;
}

// Přečíslování celého logu (Reader/Writer jako u compact). changed = kolik řádků se změnilo.
template <class Reader, class Writer>
inline bool remapPlaces(Reader& in, Writer& out, const uint8_t* map, uint8_t nOld, size_t* changed = nullptr) {
  char buf[160], o[176];
  size_t rawLen = 0, ch = 0;
  while (in.readLine(buf, sizeof(buf), rawLen)) {
    size_t len = strnlen(buf, sizeof(buf));
    if (len == 0) continue;
    len = terminateLine(buf, len, sizeof(buf));
    const int n = remapPlaceLine(buf, len, o, sizeof(o), map, nOld);
    if (n < 0) return false;
    if ((size_t)n != len || memcmp(o, buf, len) != 0) ch++;
    if (!out.writeLine(o, (size_t)n)) return false;
  }
  if (changed) *changed = ch;
  return true;
}

// Mapa po smazání místa idx (0..n-1) z n míst: dřívější stejná, smazané 0, další o jedno níž.
inline void placeMapDelete(uint8_t* map, uint8_t n, uint8_t idx) {
  map[0] = 0;
  for (uint8_t k = 1; k <= n; k++) map[k] = (k - 1 < idx) ? k : (k - 1 == idx ? 0 : (uint8_t)(k - 1));
}

// Nouzové zkrácení: najdi začátek prvního CELÉHO řádku v bloku dat (typicky ocas souboru).
inline size_t firstFullLineOffset(const char* data, size_t len) {
  for (size_t i = 0; i < len; i++)
    if (data[i] == '\n') return i + 1;
  return len;
}

}  // namespace logfmt
