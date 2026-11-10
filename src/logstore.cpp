#include "logstore.h"

#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>

#include <stdlib.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "config.h"

static bool s_mounted = false;

// Souborové operace nad logem se dějí z hlavní smyčky (zápis, zředění) i z webového
// tasku (smazání, nahrání nového logu, otevření ke stahování). Rekurzivní mutex je serializuje.
// Hlavní smyčka na něj čeká neomezeně (web ho drží jen na okamžik), web jen omezenou dobu.
static SemaphoreHandle_t s_mx = nullptr;
static LogReaderProbe s_readerProbe = nullptr;

namespace {
class LogLock {
 public:
  explicit LogLock(TickType_t wait = portMAX_DELAY) : ok_(true), held_(false) {
    if (s_mx) {  // bez mutexu (ještě nepřipojeno) není co serializovat
      ok_ = xSemaphoreTakeRecursive(s_mx, wait) == pdTRUE;
      held_ = ok_;
    }
  }
  ~LogLock() {
    if (held_) xSemaphoreGiveRecursive(s_mx);  // vrací jen to, co se opravdu získalo
  }
  bool ok() const { return ok_; }

 private:
  LogLock(const LogLock&) = delete;
  LogLock& operator=(const LogLock&) = delete;
  bool ok_, held_;
};

bool readersActive() { return s_readerProbe && s_readerProbe(); }

// Hotový soubor (zředěný / přečíslovaný / nahraný log) nahradí log. LittleFS přejmenování nad existujícím souborem
// provádí atomicky (cíl se nahradí), takže log nikdy nezmizí. Jen kdyby VFS nahrazení odmítlo: smazat a přejmenovat
// (výpadek mezi tím dořeší logMount: log chybí, tmp zůstal -> přejmenuje ho).
bool swapIntoLog(const char* tmpPath) {
  if (LittleFS.rename(tmpPath, LOG_PATH)) return true;
  if (LittleFS.exists(LOG_PATH) && !LittleFS.remove(LOG_PATH)) return false;
  return LittleFS.rename(tmpPath, LOG_PATH);
}

size_t freeBytes() {
  const size_t tot = fsTotalBytes();
  const size_t used = fsUsedBytes();
  return tot > used ? tot - used : 0;
}
}  // namespace

void logSetReaderProbe(LogReaderProbe fn) { s_readerProbe = fn; }

LogGuard::LogGuard(bool use, uint32_t waitMs) : ok_(true), held_(false) {
  if (!use || !s_mx) return;
  ok_ = xSemaphoreTakeRecursive(s_mx, pdMS_TO_TICKS(waitMs)) == pdTRUE;
  held_ = ok_;
}

LogGuard::~LogGuard() {
  if (held_) xSemaphoreGiveRecursive(s_mx);
}

// ---------------------------------------------------------------------------
//  Adaptéry pro logfmt::compact()
// ---------------------------------------------------------------------------
namespace {

// Čtení po řádcích s vlastním bufferem (rychlejší než read() po bajtu).
class BufLineReader {
 public:
  explicit BufLineReader(File& f) : f_(f), len_(0), pos_(0) {}

  bool readLine(char* buf, size_t cap, size_t& rawLen) {
    size_t out = 0;
    rawLen = 0;
    bool any = false;
    for (;;) {
      if (pos_ >= len_) {
        len_ = f_.read(chunk_, sizeof(chunk_));
        pos_ = 0;
        if (len_ == 0) break;  // konec souboru
      }
      const char c = (char)chunk_[pos_++];
      any = true;
      rawLen++;
      if (out + 1 < cap) buf[out++] = c;  // delší řádek se zkrátí, zbytek se zahodí
      if (c == '\n') break;
    }
    buf[out] = '\0';
    return any;
  }

 private:
  File& f_;
  uint8_t chunk_[512];
  size_t len_, pos_;
};

// Zápis s bufferem (méně malých zápisů do flash).
class BufWriter {
 public:
  explicit BufWriter(File& f) : f_(f), n_(0), ok_(true) {}
  bool writeLine(const char* b, size_t len) {
    if (!ok_) return false;
    if (n_ + len > sizeof(buf_)) flush();
    if (len > sizeof(buf_)) {
      ok_ = f_.write((const uint8_t*)b, len) == len;
      return ok_;
    }
    memcpy(buf_ + n_, b, len);
    n_ += len;
    return ok_;
  }
  bool flush() {
    if (n_ && ok_) ok_ = f_.write(buf_, n_) == n_;
    n_ = 0;
    return ok_;
  }
  bool ok() const { return ok_; }

 private:
  File& f_;
  uint8_t buf_[1024];
  size_t n_;
  bool ok_;
};

}  // namespace

// ---------------------------------------------------------------------------
size_t fsTotalBytes() { return s_mounted ? LittleFS.totalBytes() : 0; }
size_t fsUsedBytes() { return s_mounted ? LittleFS.usedBytes() : 0; }

size_t logSizeBytes() {
  if (!s_mounted) return 0;
  LogLock lk;
  File f = LittleFS.open(LOG_PATH, "r");
  if (!f) return 0;
  const size_t sz = f.size();
  f.close();
  return sz;
}

bool logMount() {
  if (!s_mx) s_mx = xSemaphoreCreateRecursiveMutex();
  if (!LittleFS.begin(false)) {
    // Souborový systém nejde připojit (nenaformátovaný / poškozený). Logger, který
    // nemůže logovat, je k ničemu, proto formát. Webové soubory se pak dají
    // znovu nahrát přes stránku "Souborový systém".
    if (!LittleFS.begin(true)) return false;
  }
  s_mounted = true;

  // Dořešení přerušeného zředění (výpadek napájení)
  if (LittleFS.exists(LOG_TMP_PATH)) {
    if (!LittleFS.exists(LOG_PATH))
      LittleFS.rename(LOG_TMP_PATH, LOG_PATH);  // tmp byl kompletní, jen se nestihlo přejmenovat
    else
      LittleFS.remove(LOG_TMP_PATH);            // nedokončený tmp
  }
  if (LittleFS.exists(UP_TMP_PATH)) LittleFS.remove(UP_TMP_PATH);  // přerušené nahrávání souboru
  return true;
}

// Smazání a nahrazení logu volá webový task. Nesmí se čekat na zámek donekonečna (hlavní smyčka
// může právě ředit velký log; async_tcp je hlídaný watchdogem), a nesmí se sahat na log, který
// se právě stahuje. V obou případech: nic se nestane, *busy = true.
bool logClear(bool* busy) {
  if (busy) *busy = false;
  if (!s_mounted) return false;
  LogLock lk(pdMS_TO_TICKS(LOG_WEB_LOCK_WAIT_MS));
  if (!lk.ok() || readersActive()) {
    if (busy) *busy = true;
    return false;
  }
  if (LittleFS.exists(LOG_PATH)) return LittleFS.remove(LOG_PATH);
  return true;
}

// Nahradí log hotovým souborem (nahrání .txt přes web). Starý log se maže až tady,
// takže nepovedené nahrávání o záznamy nepřijde.
bool logReplaceWith(const char* tmpPath, bool* busy) {
  if (busy) *busy = false;
  if (!s_mounted || !tmpPath) return false;
  LogLock lk(pdMS_TO_TICKS(LOG_WEB_LOCK_WAIT_MS));
  if (!lk.ok() || readersActive()) {
    if (busy) *busy = true;
    return false;
  }
  if (!LittleFS.exists(tmpPath)) return false;  // není čím nahradit: stávající log se nesmí ztratit
  return swapIntoLog(tmpPath);
}

// ---------------------------------------------------------------------------
//  Zředění / nouzové zkrácení
// ---------------------------------------------------------------------------
// Zředění pod zámkem (volající ho drží a ověřil, že log nikdo nečte).
static bool compactLocked() {
  if (!LittleFS.exists(LOG_PATH)) return true;
  if (LittleFS.exists(LOG_TMP_PATH)) LittleFS.remove(LOG_TMP_PATH);

  File in = LittleFS.open(LOG_PATH, "r");
  if (!in) return false;
  const size_t total = in.size();

  // Na dočasný soubor potřebujeme ~75 % původní velikosti. Nevejde se -> nouzový režim.
  const size_t freeB = freeBytes();
  if (freeB < (total * 3) / 4 + 8192) {
    in.close();
    return false;
  }

  File out = LittleFS.open(LOG_TMP_PATH, "w");
  if (!out) {
    in.close();
    return false;
  }

  bool ok;
  size_t nIn = 0, nOut = 0;
  {
    BufLineReader rd(in);
    BufWriter wr(out);
    ok = logfmt::compact(rd, wr, total, &nIn, &nOut) && wr.flush();
  }
  in.close();
  out.close();

  if (!ok || nOut == 0) {
    LittleFS.remove(LOG_TMP_PATH);
    return false;
  }
  return swapIntoLog(LOG_TMP_PATH);
}

bool logRemapPlaces(const uint8_t* map, uint8_t nOld) {
  if (!s_mounted) return false;
  LogLock lk;
  if (readersActive()) return false;
  if (!LittleFS.exists(LOG_PATH)) return true;
  if (LittleFS.exists(LOG_TMP_PATH)) LittleFS.remove(LOG_TMP_PATH);
  File in = LittleFS.open(LOG_PATH, "r");
  if (!in) return false;
  const size_t total = in.size();
  if (freeBytes() < total + total / 16 + 8192) {  // nový soubor může být o pár bajtů na řádek delší
    in.close();
    return false;
  }
  File out = LittleFS.open(LOG_TMP_PATH, "w");
  if (!out) {
    in.close();
    return false;
  }
  bool ok;
  size_t changed = 0;
  {
    BufLineReader rd(in);
    BufWriter wr(out);
    ok = logfmt::remapPlaces(rd, wr, map, nOld, &changed) && wr.flush();
  }
  in.close();
  out.close();
  if (!ok) {
    LittleFS.remove(LOG_TMP_PATH);
    return false;
  }
  if (changed == 0) {  // nic se neměnilo: původní soubor zůstává (žádný zbytečný přepis flash)
    LittleFS.remove(LOG_TMP_PATH);
    return true;
  }
  return swapIntoLog(LOG_TMP_PATH);
}

// Ruční zředění. Když se log právě stahuje, nic nedělá (false) – pod čteným souborem se nemaže.
bool logCompactNow() {
  if (!s_mounted) return false;
  LogLock lk;
  if (readersActive()) return false;
  return compactLocked();
}

// Když není místo ani na dočasný soubor: nechej jen ocas souboru (nejnovější data).
// Ocas se načte do RAM, soubor se smaže a zapíše znovu. Okno rizika je ~1 s.
static bool emergencyTrim() {
  File in = LittleFS.open(LOG_PATH, "r");
  if (!in) return false;
  const size_t total = in.size();

  // Nikdy neponechat víc než polovinu: každé zkrácení tak uvolní aspoň půlku logu a opakované
  // zkracování logu těsně nad limitem nemůže donekonečna přepisovat skoro celý soubor.
  size_t keep = LOG_EMERGENCY_KEEP_BYTES;
  if (keep > total / 2) keep = total / 2;
  uint8_t* buf = nullptr;
  while (keep >= 4096) {
    buf = (uint8_t*)malloc(keep);
    if (buf) break;
    keep /= 2;
  }
  if (!buf) {
    in.close();
    LittleFS.remove(LOG_PATH);  // úplně poslední možnost, ať logger vůbec může dál běžet
    return true;
  }

  in.seek(total - keep);
  const size_t got = in.read(buf, keep);
  in.close();

  const size_t off = logfmt::firstFullLineOffset((const char*)buf, got);
  LittleFS.remove(LOG_PATH);
  File out = LittleFS.open(LOG_PATH, "w");
  bool ok = false;
  if (out) {
    ok = out.write(buf + off, got - off) == (got - off);
    out.close();
  }
  free(buf);
  return ok;
}

// Zajistí místo na řádek o velikosti `need` (volá se pod zámkem). Rozhodování:
//   1. dost místa a log není nad stropem            -> nic
//   2. log se právě čte (stahuje z webu)             -> nesahat na něj; zapsat jen když se řádek vejde
//   3. málo místa, ale log je malý                   -> zřeďování nepomůže (místo zabírají jiné soubory),
//                                                       zapsat, dokud se řádek vejde; jinak se nezapíše
//   4. jinak zředění; nejde-li (není místo na tmp)   -> nouzové zkrácení na nejnovější data
static bool ensureSpace(size_t need) {
  const size_t sz = logSizeBytes();
  const size_t freeB = freeBytes();
  const bool tooBig = sz > LOG_MAX_BYTES;
  const bool tooFull = freeB < LOG_MIN_FREE_BYTES + need;
  if (!tooBig && !tooFull) return true;

  const bool fits = freeB >= need + 4096;  // řádek se vejde i bez úklidu
  if (readersActive()) return fits;
  if (!tooBig && sz < LOG_COMPACT_MIN_BYTES) return fits;

  if (!compactLocked()) {
    if (!emergencyTrim()) return false;
  }
  return freeBytes() >= need + 4096;
}

bool logAppend(const logfmt::Record& r) {
  if (!s_mounted) return false;
  LogLock lk;
  char line[128];
  const int n = logfmt::formatLine(line, sizeof(line), r);
  if (n <= 0) return false;
  if (!ensureSpace((size_t)n)) return false;

  File f = LittleFS.open(LOG_PATH, "a");
  if (!f) return false;
  const size_t w = f.write((const uint8_t*)line, (size_t)n);  // jeden řádek = jeden zápis
  f.close();
  return w == (size_t)n;
}

int logRemoveLegacy() {
  static const char* const kOld[] = {"/chart.js", "/style.css"};
  int n = 0;
  for (const char* f : kOld)
    if (LittleFS.exists(f) && LittleFS.remove(f)) n++;
  return n;
}
