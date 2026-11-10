// locations.cpp – viz locations.h
#include "locations.h"

#include <Preferences.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "settings.h"

namespace loc {

size_t sanitizeName(const char* in, char* out, size_t outCap) {
  if (!out || outCap == 0) return 0;
  size_t n = 0;
  const size_t lim = outCap - 1 < LOC_NAME ? outCap - 1 : LOC_NAME;
  if (in) {
    while (*in == ' ') in++;
    for (const unsigned char* p = (const unsigned char*)in; *p;) {
      const unsigned char c = *p;
      size_t len = c < 0x80 ? 1 : (c >= 0xF0 ? 4 : (c >= 0xE0 ? 3 : (c >= 0xC0 ? 2 : 0)));
      if (len == 0) {  // osamělý pokračovací bajt: vynechat
        p++;
        continue;
      }
      bool whole = true;
      for (size_t k = 1; k < len; k++)
        if ((p[k] & 0xC0) != 0x80) whole = false;
      if (!whole) {
        p++;
        continue;
      }
      if (len == 1 && (c < 0x20 || c == 0x7F || c == ';' || c == '"' || c == '\\' || c == '<' || c == '>')) {
        p++;
        continue;
      }
      if (n + len > lim) break;
      memcpy(out + n, p, len);
      n += len;
      p += len;
    }
  }
  while (n > 0 && out[n - 1] == ' ') n--;
  out[n] = 0;
  return n;
}

bool encode(const Location& l, char* out, size_t cap) {
  const int k = snprintf(out, cap, "%u;%u;%.5f;%.5f;%u;%s", (unsigned)l.dry, (unsigned)l.wet, (double)l.gDry, (double)l.gWet,
                         (unsigned)l.rawOpen, l.name);
  return k > 0 && (size_t)k < cap;
}

bool valid(const Location& l) {
  if (!l.name[0] || l.dry > 99 || l.wet < 1 || l.wet > 100 || l.wet <= l.dry) return false;
  moist::Calib c;
  c.gDry = l.gDry;
  c.gWet = l.gWet;
  c.rawOpen = DEF_CAL_RAW_OPEN;
  return moist::calibValid(c);
}

bool decode(const char* s, Location& l) {
  if (!s) return false;
  char* e = nullptr;
  Location t;
  const long dry = strtol(s, &e, 10);
  if (*e != ';') return false;
  const long wet = strtol(e + 1, &e, 10);
  if (*e != ';') return false;
  const double gd = strtod(e + 1, &e);
  if (*e != ';') return false;
  const double gw = strtod(e + 1, &e);
  if (*e != ';') return false;
  const long open = strtol(e + 1, &e, 10);
  if (*e != ';') return false;
  if (dry < 0 || dry > 99 || wet < 1 || wet > 100 || wet <= dry) return false;
  if (open < 500 || open > 4095) return false;
  moist::Calib c;
  c.gDry = (float)gd;
  c.gWet = (float)gw;
  c.rawOpen = (uint16_t)open;
  if (!moist::calibValid(c)) return false;
  t.dry = (uint8_t)dry;
  t.wet = (uint8_t)wet;
  t.gDry = c.gDry;
  t.gWet = c.gWet;
  t.rawOpen = DEF_CAL_RAW_OPEN;  // naprázdno se od v5.7 nekalibruje: vždy plný rozsah
  sanitizeName(e + 1, t.name, sizeof(t.name));
  l = t;
  return true;
}

}  // namespace loc

void locFromSettings(const Settings& s, Location& l) {
  l.gDry = s.cal.gDry;
  l.gWet = s.cal.gWet;
  l.rawOpen = s.cal.rawOpen;
  l.dry = s.dryPct;
  l.wet = s.wetPct;
}

void locToSettings(const Location& l, Settings& s) {
  s.cal.gDry = l.gDry;
  s.cal.gWet = l.gWet;
  s.cal.rawOpen = DEF_CAL_RAW_OPEN;
  s.dryPct = l.dry;
  s.wetPct = l.wet;
}

static Location s_loc[LOC_MAX];
static uint8_t s_n = 0;
static bool s_dirty = false;  // tabulka v RAM se liší od NVS (uloží locSave)
static const char* kNs = "arbolok";

static void keyOf(uint8_t i, char* k) { snprintf(k, 6, "loc%u", (unsigned)i); }

static bool saveOne(Preferences& p, uint8_t i) {
  char k[6], v[96];
  keyOf(i, k);
  if (!loc::encode(s_loc[i], v, sizeof(v))) return false;
  return p.putString(k, v) > 0;
}

static void saveAll() {
  Preferences p;
  if (!p.begin(kNs, false)) return;
  for (uint8_t i = 0; i < s_n; i++) saveOne(p, i);
  for (uint8_t i = s_n; i < LOC_MAX; i++) {
    char k[6];
    keyOf(i, k);
    if (p.isKey(k)) p.remove(k);
  }
  p.putUChar("locn", s_n);
  p.end();
  s_dirty = false;
}

void locLoadAll(const Settings& s) {
  s_n = 0;
  s_dirty = false;
  Preferences p;
  if (p.begin(kNs, true)) {
    uint8_t n = p.getUChar("locn", 0);
    if (n > LOC_MAX) n = LOC_MAX;
    for (uint8_t i = 0; i < n; i++) {
      char k[6];
      keyOf(i, k);
      Location l;
      if (loc::decode(p.getString(k, "").c_str(), l)) s_loc[s_n++] = l;  // poškozené místo se vynechá
    }
    p.end();
  }
  if (s_n == 0) {
    Location l;
    locFromSettings(s, l);
    loc::sanitizeName("Místo 1", l.name, sizeof(l.name));
    s_loc[0] = l;
    s_n = 1;
    saveAll();
  }
}

uint8_t locCount() { return s_n; }
const Location& locGet(uint8_t i) { return s_loc[i < s_n ? i : 0]; }

bool locSet(uint8_t i, const Location& l) {
  if (i >= s_n) return false;
  s_loc[i] = l;
  s_dirty = true;
  return true;
}

bool locAdd(const Location& l) {
  if (s_n >= LOC_MAX) return false;
  s_loc[s_n++] = l;
  s_dirty = true;
  return true;
}

bool locRemove(uint8_t i) {
  if (i >= s_n || s_n <= 1) return false;
  for (uint8_t k = i; k + 1 < s_n; k++) s_loc[k] = s_loc[k + 1];
  s_n--;
  s_dirty = true;
  return true;
}

bool locReplaceAll(const Location* ls, uint8_t n) {
  if (!ls || n < 1 || n > LOC_MAX) return false;
  for (uint8_t i = 0; i < n; i++)
    if (!loc::valid(ls[i])) return false;
  for (uint8_t i = 0; i < n; i++) {
    s_loc[i] = ls[i];
    s_loc[i].rawOpen = DEF_CAL_RAW_OPEN;
  }
  s_n = n;
  s_dirty = true;
  return true;
}

bool locSyncFromSettings(const Settings& s) {
  const uint8_t a = s.locActive < s_n ? s.locActive : 0;
  if (s_n == 0) return false;
  Location l = s_loc[a];
  locFromSettings(s, l);
  if (l.dry == s_loc[a].dry && l.wet == s_loc[a].wet && l.rawOpen == s_loc[a].rawOpen && fabsf(l.gDry - s_loc[a].gDry) < 1e-6f &&
      fabsf(l.gWet - s_loc[a].gWet) < 1e-6f)
    return false;
  s_loc[a] = l;
  s_dirty = true;
  return true;
}

bool locSave() {
  if (!s_dirty) return false;
  saveAll();
  return true;
}
