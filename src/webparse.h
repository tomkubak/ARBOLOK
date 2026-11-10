// ============================================================================
//  webparse.h  –  přísné parsování parametrů z webu + jména souborů (ČISTÉ C++)
// ============================================================================
#pragma once
#include <errno.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

namespace webparse {

// Captive portal: požadavek na CIZÍ adresu (telefon po připojení zkouší internet, např. captive.apple.com,
// connectivitycheck.gstatic.com) -> přesměrovat na stránku zařízení, aby se sama otevřela. Vlastní adresy
// (10.10.10.10, arbolok.local) se nepřesměrovávají. Port v hlavičce Host se ignoruje; prázdný Host = vlastní.
inline bool isForeignHost(const char* host);

// Test připojení k internetu, který telefon / počítač po připojení k Wi-Fi posílá (podle cesty; Host je cizí).
enum Probe : uint8_t {
  PR_NONE = 0,
  PR_APPLE,     // captive.apple.com/hotspot-detect.html (iPhone, Mac): čeká stránku "Success"
  PR_204,       // connectivitycheck.gstatic.com/generate_204 (Android, Chrome): čeká 204
  PR_MSFT,      // www.msftconnecttest.com/connecttest.txt (Windows)
  PR_NCSI,      // www.msftncsi.com/ncsi.txt (starší Windows)
  PR_FF_CANON,  // detectportal.firefox.com/canonical.html
  PR_FF_OK      // detectportal.firefox.com/success.txt
};
inline Probe probeKind(const char* path) {
  if (!path) return PR_NONE;
  struct P { const char* p; Probe k; };
  static const P kP[] = {{"/hotspot-detect.html", PR_APPLE}, {"/library/test/success.html", PR_APPLE},
                         {"/generate_204", PR_204},          {"/gen_204", PR_204},
                         {"/connecttest.txt", PR_MSFT},      {"/ncsi.txt", PR_NCSI},
                         {"/canonical.html", PR_FF_CANON},   {"/success.txt", PR_FF_OK}};
  for (const P& e : kP)
    if (strcmp(path, e.p) == 0) return e.k;
  return PR_NONE;
}

// Co udělat s požadavkem podle režimu (0 nic, 1 okno s rozcestníkem, 2 tiše).
enum CaptiveAction : uint8_t {
  CA_NONE = 0,  // normální obsluha (vlastní adresa nebo režim vypnutý)
  CA_SUCCESS,   // odpovědět na test internetu "je internet" -> žádné okno, telefon nehlásí "bez internetu"
  CA_PORTAL,    // přesměrovat na rozcestník /portal (telefon otevře okno)
  CA_HOME       // přesměrovat na stránku zařízení (někdo napsal do prohlížeče cizí adresu)
};
inline CaptiveAction captiveAction(uint8_t mode, bool portalDone, const char* host, const char* path) {
  if (mode == 0 || !isForeignHost(host)) return CA_NONE;
  const bool probe = probeKind(path) != PR_NONE;
  if (mode == 1 && !portalDone) return CA_PORTAL;
  return probe ? CA_SUCCESS : CA_HOME;
}

inline bool isForeignHost(const char* host) {
  if (!host || !*host) return false;
  size_t n = 0;
  while (host[n] && host[n] != ':') n++;
  static const char* const kOwn[] = {"10.10.10.10", "arbolok.local", "arbolok"};
  for (const char* own : kOwn) {
    const size_t m = strlen(own);
    if (m != n) continue;
    size_t i = 0;
    while (i < n) {
      char a = host[i], b = own[i];
      if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
      if (a != b) break;
      i++;
    }
    if (i == n) return false;
  }
  return true;
}

// Hlavička Origin (prohlížeč ji posílá u POST) musí mít stejného hostitele jako Host, jinak požadavek poslala cizí
// stránka otevřená v telefonu. origin = nullptr: hlavička chybí (curl a pod.) -> povoleno.
inline bool sameOrigin(const char* origin, const char* host) {
  if (!origin) return true;
  const char* p = strstr(origin, "://");
  const char* oh = p ? p + 3 : origin;
  if (!host) return false;
  size_t i = 0;
  for (; oh[i] && host[i]; i++) {
    char a = oh[i], b = host[i];
    if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
    if (a != b) return false;
  }
  return oh[i] == '\0' && host[i] == '\0';
}

// Celé číslo v rozsahu <lo, hi>. Odmítne prázdný řetězec, smetí na konci i přetečení.
inline bool parseLong(const char* s, long lo, long hi, long& out) {
  if (!s || !*s) return false;
  errno = 0;
  char* e = nullptr;
  const long v = strtol(s, &e, 10);
  if (errno != 0 || e == s || *e != '\0') return false;
  if (v < lo || v > hi) return false;
  out = v;
  return true;
}

// Desetinné číslo v rozsahu <lo, hi>. Přijme i čárku jako desetinný oddělovač.
// Odmítne NaN / inf / smetí.
inline bool parseFloat(const char* s, float lo, float hi, float& out) {
  if (!s || !*s) return false;
  char tmp[32];
  size_t n = strlen(s);
  if (n >= sizeof(tmp)) return false;
  for (size_t i = 0; i <= n; i++) tmp[i] = (s[i] == ',') ? '.' : s[i];
  errno = 0;
  char* e = nullptr;
  const double v = strtod(tmp, &e);
  if (errno != 0 || e == tmp || *e != '\0') return false;
  if (!isfinite(v)) return false;
  if (v < (double)lo || v > (double)hi) return false;
  out = (float)v;
  return true;
}

// Jméno nahrávaného souboru -> bezpečné jméno v kořeni LittleFS.
//  - ořízne cestu (vše do posledního '/' nebo '\')
//  - povolí jen [A-Za-z0-9._-], ostatní znaky nahradí '_'
//  - odmítne prázdné jméno, jméno začínající tečkou a jména delší než cap-1 (max 31)
inline bool sanitizeUploadName(const char* in, char* out, size_t cap) {
  if (!in || !out || cap < 2) return false;
  const char* base = in;
  for (const char* p = in; *p; ++p)
    if (*p == '/' || *p == '\\') base = p + 1;
  const size_t n = strlen(base);
  if (n == 0 || n > 31 || n + 1 > cap) return false;
  if (base[0] == '.') return false;
  bool alnum = false;
  for (size_t i = 0; i < n; i++) {
    const char c = base[i];
    const bool a = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    if (a) alnum = true;
    out[i] = (a || c == '.' || c == '_' || c == '-') ? c : '_';
  }
  out[n] = '\0';
  return alnum;
}

// Vejde se nahrávaný soubor tak, aby logger měl i potom dost místa?
//  - tělo požadavku (contentLength) se uloží do dočasného souboru vedle stávajících dat,
//  - po nahrání musí zůstat LOG_MIN_FREE (rezerva pro zápis) + 75 % logu (dočasný soubor při
//    zředění) + headroom. replacesLog = nahrávaný soubor nahradí log (od v7.0 se nepoužívá, nahrávání log nemění).
// Jinak by logger u každého zápisu zbytečně zřeďoval (viz logstore.cpp, ensureSpace).
inline bool uploadFits(size_t contentLength, size_t freeBytes, size_t logBytes, bool replacesLog, size_t minFree,
                       size_t headroom) {
  const size_t logAfter = replacesLog ? contentLength : logBytes;
  const size_t need = contentLength + minFree + (logAfter / 4) * 3 + headroom;
  return need >= contentLength && need <= freeBytes;  // první podmínka = ochrana proti přetečení
}

// Jméno existujícího souboru (mazání / stažení): žádné cesty, žádné "..", žádné řídicí znaky.
inline bool validExistingName(const char* s) {
  if (!s) return false;
  const size_t n = strlen(s);
  if (n == 0 || n > 63) return false;
  if (strstr(s, "..")) return false;
  for (size_t i = 0; i < n; i++) {
    const unsigned char c = (unsigned char)s[i];
    if (c < 0x20 || c == '/' || c == '\\' || c == 0x7f) return false;
  }
  return true;
}

// Heslo Wi-Fi nastavené ve webu (Servis, od v7.0): prázdné = otevřená síť, jinak 8 až 63 tisknutelných znaků ASCII
// (to přijme WiFi.softAP() jako WPA2; diakritiku a řídicí znaky by telefony zadávaly nejednotně).
inline bool validWifiPassword(const char* s) {
  if (!s) return false;
  const size_t n = strlen(s);
  if (n == 0) return true;
  if (n < 8 || n > 63) return false;
  for (size_t i = 0; i < n; i++) {
    const unsigned char c = (unsigned char)s[i];
    if (c < 0x20 || c > 0x7e) return false;
  }
  return true;
}

}  // namespace webparse
