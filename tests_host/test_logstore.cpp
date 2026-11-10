// Host test logstore.cpp nad funkční atrapou LittleFS v paměti (tests_host/fsmem).
// Ověřuje: zápis řádků, zředění nad LOG_MAX_BYTES, nouzové zkrácení při plném disku, dořešení
// přerušeného zředění po startu, smazání a nahrazení logu, formát při nepřipojitelném FS.
// Sestavení a spuštění:  tools/run_host_tests.sh
#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#include <Arduino.h>
#include <LittleFS.h>  // funkční atrapa z tests_host/fsmem
#include <freertos/semphr.h>

#include "../src/config.h"
#include "../src/logstore.h"
#include "../src/timeutil.h"

// ---------------------------------------------------------------------------
//  Atrapa mutexu (stub semphr.h jen deklaruje): počítáme hloubku, ať se ověří vyváženost.
// ---------------------------------------------------------------------------
static int g_lockDepth = 0;
static bool g_lockHeldByOther = false;  // simuluje: mutex drží jiný task (zředění běží)
static TickType_t g_lastWait = 0;       // s jakým čekáním se o mutex naposled žádalo
static int g_takes = 0, g_gives = 0;
SemaphoreHandle_t xSemaphoreCreateMutex() { return (void*)1; }
SemaphoreHandle_t xSemaphoreCreateRecursiveMutex() { return (void*)1; }
BaseType_t xSemaphoreTake(SemaphoreHandle_t, TickType_t) { return pdTRUE; }
BaseType_t xSemaphoreGive(SemaphoreHandle_t) { return pdTRUE; }
BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t, TickType_t wait) {
  g_lastWait = wait;
  if (g_lockHeldByOther) return pdFALSE;  // čekání vypršelo
  g_lockDepth++;
  g_takes++;
  return pdTRUE;
}
BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t) {
  g_lockDepth--;
  g_gives++;
  return pdTRUE;
}

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                            \
  do {                                                         \
    if (cond) {                                                \
      g_pass++;                                                \
    } else {                                                   \
      g_fail++;                                                \
      printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                          \
  } while (0)

using fs::mem;

static std::string fileText(const char* path) {
  auto it = mem().files.find(path);
  return it == mem().files.end() ? std::string("<chybí>") : it->second;
}

// Záznam č. i: půl hodiny po sobě od 2026-01-01 00:00
static logfmt::Record rec(uint32_t i) {
  logfmt::Record r;
  int y, mo, d, h, mi, s;
  unixToParts(1767225600u + i * 1800u, y, mo, d, h, mi, s);  // 2026-01-01 00:00:00
  r.year = y;
  r.month = mo;
  r.day = d;
  r.hour = h;
  r.minute = mi;
  r.hum = (int)(i % 100);
  r.temp = 18.5f;
  r.vbat = 3.25f;
  r.raw = 2000 + (int)(i % 1000);
  r.settleMs = 12;
  return r;
}

// Souvislý log záznamů from .. from+count-1 (přímo do paměťového FS, bez zápisů přes logAppend)
static std::string makeLog(uint32_t from, uint32_t count) {
  std::string t;
  char line[128];
  for (uint32_t i = 0; i < count; i++) {
    const int n = logfmt::formatLine(line, sizeof(line), rec(from + i));
    t.append(line, (size_t)n);
  }
  return t;
}

static int g_readers = 0;  // simulace: kolik souborů se právě stahuje z webu
static bool readerProbe() { return g_readers > 0; }

// "RRMMDDhhmm" (krátký formát, od v6.13) -> číslo, které roste s časem; -1 = řádek je vadný
static long long keyOf(const std::string& line) {
  int y, mo, d, h, mi;
  if (line.size() < 11 || line[10] != ' ' || sscanf(line.c_str(), "%2d%2d%2d%2d%2d", &y, &mo, &d, &h, &mi) != 5) return -1;
  return (((((long long)y * 100 + mo) * 100 + d) * 100 + h) * 100) + mi;
}

static std::vector<std::string> lines(const std::string& t) {
  std::vector<std::string> v;
  size_t p = 0;
  while (p < t.size()) {
    size_t e = t.find('\n', p);
    if (e == std::string::npos) {
      v.push_back(t.substr(p));  // nedokončený řádek bez \n
      break;
    }
    v.push_back(t.substr(p, e - p + 1));
    p = e + 1;
  }
  return v;
}

static bool allWellFormedAndIncreasing(const std::vector<std::string>& v) {
  long long prev = -1;
  for (const auto& l : v) {
    if (l.size() < 18 || l.compare(l.size() - 2, 2, "\r\n") != 0) return false;
    const long long k = keyOf(l);
    if (k < 0 || k <= prev) return false;
    prev = k;
  }
  return true;
}

// ---------------------------------------------------------------------------
static void test_mount_and_append() {
  printf("[logstore] připojení a zápis\n");
  mem().reset();
  CHECK(logMount());
  CHECK(logSizeBytes() == 0);
  for (uint32_t i = 0; i < 50; i++) CHECK(logAppend(rec(i)));
  const std::string t = fileText(LOG_PATH);
  CHECK(t.size() == logSizeBytes());
  const auto v = lines(t);
  CHECK(v.size() == 50);
  CHECK(allWellFormedAndIncreasing(v));
  CHECK(v[0].rfind("2601010000 0 19 325 s12", 0) == 0);
  CHECK(g_lockDepth == 0);
}

static void test_mount_recovery() {
  printf("[logstore] dořešení přerušeného zředění / nahrávání po startu\n");
  // a) hotový tmp, log chybí (výpadek mezi remove a rename) -> tmp se stane logem
  mem().reset();
  mem().files[LOG_TMP_PATH] = "2026-01-01 00:00 5% 1.0C 3.30V\r\n";
  CHECK(logMount());
  CHECK(!mem().files.count(LOG_TMP_PATH));
  CHECK(fileText(LOG_PATH) == "2026-01-01 00:00 5% 1.0C 3.30V\r\n");
  // b) log i tmp existují (tmp nedokončený) -> tmp se zahodí, log zůstane
  mem().reset();
  mem().files[LOG_PATH] = "LOG\r\n";
  mem().files[LOG_TMP_PATH] = "NEDOKONCENY";
  CHECK(logMount());
  CHECK(fileText(LOG_PATH) == "LOG\r\n");
  CHECK(!mem().files.count(LOG_TMP_PATH));
  // c) zbytek přerušeného nahrávání
  mem().reset();
  mem().files[UP_TMP_PATH] = "POLOVINA";
  CHECK(logMount());
  CHECK(!mem().files.count(UP_TMP_PATH));
}

static void test_format_on_mount_failure() {
  printf("[logstore] nepřipojitelný souborový systém se naformátuje\n");
  mem().reset();
  mem().files["/stary.bin"] = "x";
  mem().mountFailsOnce = true;
  CHECK(logMount());
  CHECK(mem().files.empty());
  CHECK(logAppend(rec(0)));
}

// Očekávaný začátek řádku ("RRRR-MM-DD HH:MM") pro záznam č. i
static std::string stampOf(uint32_t i) {
  logfmt::Record r = rec(i);
  char buf[24];
  snprintf(buf, sizeof(buf), "%02d%02d%02d%02d%02d", r.year % 100, r.month, r.day, r.hour, r.minute);
  return buf;
}

static void test_compaction_at_limit() {
  printf("[logstore] zředění nad LOG_MAX_BYTES (stovky dní záznamů)\n");
  mem().reset();
  CHECK(logMount());
  const uint32_t N = 30000;  // ~ 625 dní po 30 min, ~ 840 kB krátkých řádků -> překročí strop
  bool allOk = true;
  size_t maxSeen = 0;
  for (uint32_t i = 0; i < N; i++) {
    if (!logAppend(rec(i))) allOk = false;
    const size_t sz = logSizeBytes();
    if (sz > maxSeen) maxSeen = sz;
  }
  CHECK(allOk);
  CHECK(maxSeen <= LOG_MAX_BYTES + 200);  // strop + jeden řádek
  const auto v = lines(fileText(LOG_PATH));
  CHECK(allWellFormedAndIncreasing(v));
  CHECK(!v.empty() && v.back().rfind(stampOf(N - 1), 0) == 0);  // poslední zapsaný je poslední v souboru
  // nejnovější část zůstala celá (bez děr): posledních K řádků = záznamy N-K .. N-1
  const size_t K = 2000;
  bool contiguous = v.size() > K;
  for (size_t i = 0; contiguous && i < K; i++)
    if (v[v.size() - K + i].rfind(stampOf(N - K + (uint32_t)i), 0) != 0) contiguous = false;
  CHECK(contiguous);
  CHECK(v.size() > 4000);  // zředěním se neztratilo všechno
  CHECK(v.size() < N);     // ale něco se zředilo
  CHECK(!mem().files.count(LOG_TMP_PATH));
  CHECK(mem().used() <= mem().capacity);
  CHECK(g_lockDepth == 0);
}

static void test_emergency_trim_full_disk() {
  printf("[logstore] plný disk: nouzové zkrácení, záznamy dál přibývají\n");
  mem().reset();
  mem().capacity = 160 * 1024;  // malý oddíl, aby nebylo místo ani na dočasný soubor zředění
  CHECK(logMount());
  const uint32_t N = 6000;
  uint32_t failed = 0;
  for (uint32_t i = 0; i < N; i++)
    if (!logAppend(rec(i))) failed++;
  CHECK(failed == 0);
  const auto v = lines(fileText(LOG_PATH));
  CHECK(allWellFormedAndIncreasing(v));
  CHECK(v.size() > 1000);
  CHECK(v.back().rfind(stampOf(N - 1), 0) == 0);
  CHECK(mem().used() <= mem().capacity);
}

static void test_clear_and_replace() {
  printf("[logstore] smazání a nahrazení logu\n");
  mem().reset();
  CHECK(logMount());
  for (uint32_t i = 0; i < 10; i++) logAppend(rec(i));
  CHECK(logClear());
  CHECK(!mem().files.count(LOG_PATH));
  CHECK(logClear());  // opakované mazání je v pořádku
  CHECK(logSizeBytes() == 0);

  // nahrazení hotovým souborem
  for (uint32_t i = 0; i < 10; i++) logAppend(rec(i));
  mem().files[UP_TMP_PATH] = "2026-02-02 02:02 50% 20.0C 3.30V\r\n";
  CHECK(logReplaceWith(UP_TMP_PATH));
  CHECK(fileText(LOG_PATH) == "2026-02-02 02:02 50% 20.0C 3.30V\r\n");
  CHECK(!mem().files.count(UP_TMP_PATH));

  // nahrazení, když log ještě neexistuje
  logClear();
  mem().files[UP_TMP_PATH] = "NOVY\r\n";
  CHECK(logReplaceWith(UP_TMP_PATH));
  CHECK(fileText(LOG_PATH) == "NOVY\r\n");

  // neexistující zdroj nesmí zničit stávající log
  CHECK(!logReplaceWith("/neexistuje.tmp"));
  CHECK(fileText(LOG_PATH) == "NOVY\r\n");
  CHECK(!logReplaceWith(nullptr));
  CHECK(g_lockDepth == 0);
}


static void test_remap_places() {
  printf("[logstore] přečíslování míst v logu (smazání místa)\n");
  mem().reset();
  CHECK(logMount());
  for (uint32_t i = 0; i < 30; i++) {
    logfmt::Record r = rec(i);
    r.loc = 1 + (int)(i % 3);
    CHECK(logAppend(r));
  }
  const std::string before = fileText(LOG_PATH);
  uint8_t m[9];
  logfmt::placeMapDelete(m, 3, 1);
  CHECK(logRemapPlaces(m, 3));
  const std::string after = fileText(LOG_PATH);
  const auto v = lines(after);
  CHECK(v.size() == 30 && allWellFormedAndIncreasing(v));
  CHECK(after.find(" p3") == std::string::npos && after.find(" p0") != std::string::npos);
  CHECK(!mem().files.count(LOG_TMP_PATH));
  // nic ke změně (identita): soubor se nepřepisuje
  uint8_t id[9] = {0, 1, 2};
  CHECK(logRemapPlaces(id, 2) && fileText(LOG_PATH) == after);
  // log se právě stahuje: nepřepisuje se, volající dostane false (místo se nesmaže)
  logSetReaderProbe(readerProbe);
  g_readers = 1;
  CHECK(!logRemapPlaces(m, 3));
  CHECK(fileText(LOG_PATH) == after);
  g_readers = 0;
  logSetReaderProbe(nullptr);
  // málo místa na dočasný soubor: false a log beze změny
  mem().capacity = mem().used() + after.size() / 2;
  CHECK(!logRemapPlaces(m, 3));
  CHECK(fileText(LOG_PATH) == after);
  // log neexistuje: v pořádku
  mem().reset();
  CHECK(logMount());
  CHECK(logRemapPlaces(m, 3));
  (void)before;
  CHECK(g_lockDepth == 0);
}

// ---------------------------------------------------------------------------
//  Souběh s webem (nález kontroly kódu): web nesmí čekat na zámek donekonečna a nesmí
//  sahat na log, který se právě stahuje. Hlavní smyčka log pod čtenářem nepřepisuje.
// ---------------------------------------------------------------------------
static void test_web_side_busy_lock() {
  printf("[logstore] web: omezené čekání na zámek, busy místo zablokování\n");
  mem().reset();
  CHECK(logMount());
  for (uint32_t i = 0; i < 10; i++) CHECK(logAppend(rec(i)));
  CHECK(g_lastWait == portMAX_DELAY);  // hlavní smyčka čeká neomezeně (web drží zámek jen na okamžik)
  const std::string before = fileText(LOG_PATH);

  // zámek drží jiný task (probíhá zředění): clear / replace skončí busy a NIC nezmění
  mem().files[UP_TMP_PATH] = "NOVY\r\n";
  g_lockHeldByOther = true;
  bool busy = false;
  CHECK(!logClear(&busy));
  CHECK(busy);
  CHECK(g_lastWait == LOG_WEB_LOCK_WAIT_MS);  // omezené čekání (web nesmí stát)
  CHECK(fileText(LOG_PATH) == before);
  busy = false;
  CHECK(!logReplaceWith(UP_TMP_PATH, &busy));
  CHECK(busy);
  CHECK(fileText(LOG_PATH) == before);
  CHECK(mem().files.count(UP_TMP_PATH) == 1);  // zdroj zůstal volajícímu (ten ho uklidí)
  // LogGuard: bez zámku ok() == false; bez použití (use=false) je vždy ok a zámek se nebere
  {
    LogGuard g(true, 123);
    CHECK(!g.ok());
    CHECK(g_lastWait == 123);
  }
  const int takesBefore = g_takes;
  {
    LogGuard g(false, 123);
    CHECK(g.ok());
  }
  CHECK(g_takes == takesBefore);
  g_lockHeldByOther = false;
  CHECK(g_lockDepth == 0);  // nevrátilo se nic, co se nezískalo
  CHECK(g_takes == g_gives);

  // zámek je volný: stejné volání projde, *busy zůstane false
  busy = true;
  CHECK(logReplaceWith(UP_TMP_PATH, &busy));
  CHECK(!busy);
  CHECK(fileText(LOG_PATH) == "NOVY\r\n");
  busy = true;
  CHECK(logClear(&busy));
  CHECK(!busy);
  CHECK(!mem().files.count(LOG_PATH));
  {
    LogGuard g(true, 50);
    CHECK(g.ok());
    CHECK(g_lockDepth == 1);
  }
  CHECK(g_lockDepth == 0);
  CHECK(g_takes == g_gives);
}

static void test_readers_block_destructive_ops() {
  printf("[logstore] při stahování se log nemaže, nenahrazuje ani nezřeďuje\n");
  mem().reset();
  CHECK(logMount());
  logSetReaderProbe(readerProbe);
  for (uint32_t i = 0; i < 20; i++) CHECK(logAppend(rec(i)));
  const std::string before = fileText(LOG_PATH);
  mem().files[UP_TMP_PATH] = "NOVY\r\n";

  g_readers = 1;
  bool busy = false;
  CHECK(!logClear(&busy) && busy);
  busy = false;
  CHECK(!logReplaceWith(UP_TMP_PATH, &busy) && busy);
  CHECK(!logCompactNow());
  CHECK(fileText(LOG_PATH) == before);
  CHECK(mem().files.count(UP_TMP_PATH) == 1);

  g_readers = 0;  // stahování skončilo
  CHECK(logCompactNow());
  CHECK(fileText(LOG_PATH).size() < before.size());  // ředí se, až když nikdo nečte
  busy = true;
  CHECK(logReplaceWith(UP_TMP_PATH, &busy) && !busy);
  CHECK(fileText(LOG_PATH) == "NOVY\r\n");
  CHECK(g_lockDepth == 0 && g_takes == g_gives);
  logSetReaderProbe(nullptr);
}

static void test_append_defers_compaction_while_downloading() {
  printf("[logstore] zápis při stahování: bez zředění, log pod čtenářem zůstane beze změny\n");
  mem().reset();
  CHECK(logMount());
  logSetReaderProbe(readerProbe);
  const uint32_t N = 30000;  // ~ 840 kB krátkých řádků: nad LOG_MAX_BYTES
  const std::string big = makeLog(0, N);
  CHECK(big.size() > LOG_MAX_BYTES);
  mem().files[LOG_PATH] = big;

  g_readers = 1;
  CHECK(logAppend(rec(N)));
  CHECK(logAppend(rec(N + 1)));
  std::string t = fileText(LOG_PATH);
  CHECK(t.size() > big.size());
  CHECK(t.compare(0, big.size(), big) == 0);  // původní obsah nedotčen, jen přibyly řádky
  CHECK(lines(t).size() == N + 2);
  CHECK(!mem().files.count(LOG_TMP_PATH));

  g_readers = 0;  // po stažení se úklid provede při dalším zápisu
  CHECK(logAppend(rec(N + 2)));
  t = fileText(LOG_PATH);
  CHECK(t.size() < big.size());
  const auto v = lines(t);
  CHECK(allWellFormedAndIncreasing(v));
  CHECK(v.back().rfind(stampOf(N + 2), 0) == 0);
  CHECK(!mem().files.count(LOG_TMP_PATH));
  CHECK(g_lockDepth == 0);
  logSetReaderProbe(nullptr);
}

static void test_download_with_full_disk_drops_record_not_data() {
  printf("[logstore] stahování + plný disk: záznam se nezapíše, ale log se pod čtenářem nezkrátí\n");
  mem().reset();
  mem().capacity = 160 * 1024;
  CHECK(logMount());
  logSetReaderProbe(readerProbe);
  // log zabere skoro celý oddíl (zbývá jediný blok = míň než řádek + rezerva 4 kB)
  std::string big;  // tolik řádků, aby zbyl míň než blok + řádek (délka řádku závisí na formátu)
  for (uint32_t cnt = 3000; cnt < 12000; cnt += 40) {
    big = makeLog(0, cnt);
    mem().files[LOG_PATH] = big;
    if (mem().capacity - mem().used() < 4096 + 44) break;
  }
  CHECK(mem().capacity - mem().used() < 4096 + 44);
  g_readers = 1;
  CHECK(!logAppend(rec(20000)));             // nevejde se a nesmí se sahat na čtený soubor
  CHECK(fileText(LOG_PATH) == big);
  g_readers = 0;
  CHECK(logAppend(rec(20001)));              // po stažení nouzové zkrácení uvolní místo
  CHECK(allWellFormedAndIncreasing(lines(fileText(LOG_PATH))));
  logSetReaderProbe(nullptr);
}

// ---------------------------------------------------------------------------
//  Málo místa kvůli CIZÍM souborům (nahrané přes web): malý log se nezřeďuje
// ---------------------------------------------------------------------------
static void test_small_log_not_thinned_when_other_files_fill_disk() {
  printf("[logstore] málo místa kvůli jiným souborům: malý log se nezřeďuje, zapisuje se dál\n");
  mem().reset();
  mem().capacity = 256 * 1024;
  CHECK(logMount());
  mem().files["/nahrany.bin"] = std::string(210 * 1024, 'x');  // volno < LOG_MIN_FREE_BYTES
  CHECK(mem().capacity - mem().used() < LOG_MIN_FREE_BYTES);

  uint32_t ok = 0;
  bool firstFailSeen = false;
  const uint64_t writesBefore = mem().writes;
  for (uint32_t i = 0; i < 2000; i++) {
    if (logAppend(rec(i))) {
      if (firstFailSeen) ok = 0xFFFFFFFFu;  // po prvním selhání už nesmí nic projít
      else ok++;
    } else {
      firstFailSeen = true;
    }
  }
  CHECK(ok > 300 && ok != 0xFFFFFFFFu);  // zapisovalo se, dokud se řádek vešel
  CHECK(firstFailSeen);                  // a pak se přestalo (disk je opravdu plný)
  const auto v = lines(fileText(LOG_PATH));
  CHECK(v.size() == ok);                 // NIC se nezředilo ani nezkrátilo (žádné ztracené řádky)
  CHECK(allWellFormedAndIncreasing(v));
  CHECK(v[0].rfind(stampOf(0), 0) == 0);
  // jeden zápis = jeden řádek: žádné přepisování celého souboru
  CHECK(mem().writes - writesBefore <= (uint64_t)ok + 2000u);
  CHECK(mem().files.count("/nahrany.bin") == 1);
  CHECK(mem().files.count(LOG_TMP_PATH) == 0);
}

static void test_large_log_trimmed_when_disk_nearly_full() {
  printf("[logstore] málo místa a velký log: zkrátí se (nejnovější data zůstanou), pokrok je zaručený\n");
  mem().reset();
  mem().capacity = 400 * 1024;
  CHECK(logMount());
  mem().files["/nahrany.bin"] = std::string(210 * 1024, 'x');
  const std::string seed = makeLog(0, 5300);  // ~ 145 kB krátkých řádků
  mem().files[LOG_PATH] = seed;
  CHECK(mem().capacity - mem().used() < LOG_MIN_FREE_BYTES);
  CHECK(seed.size() >= LOG_COMPACT_MIN_BYTES);

  uint32_t failed = 0;
  size_t prevSize = logSizeBytes();
  uint32_t shrinks = 0;
  for (uint32_t i = 0; i < 400; i++) {
    if (!logAppend(rec(5300 + i))) failed++;
    const size_t now = logSizeBytes();
    if (now < prevSize) shrinks++;
    prevSize = now;
  }
  CHECK(failed == 0);
  CHECK(shrinks >= 1 && shrinks <= 3);  // zkrátilo se jednou-dvakrát, ne při každém zápisu
  const auto v = lines(fileText(LOG_PATH));
  CHECK(allWellFormedAndIncreasing(v));
  CHECK(v.back().rfind(stampOf(5300 + 399), 0) == 0);
  CHECK(mem().capacity - mem().used() >= 4096);
}

static void test_emergency_trim_always_halves() {
  printf("[logstore] nouzové zkrácení uvolní vždy aspoň polovinu logu (žádné opakované přepisování)\n");
  mem().reset();
  mem().capacity = 130 * 1024;  // na dočasný soubor zředění místo není
  CHECK(logMount());
  const std::string seed = makeLog(0, 4100);  // ~ 103 kB krátkých řádků: těsně nad LOG_EMERGENCY_KEEP_BYTES (96 kB)
  CHECK(seed.size() > LOG_EMERGENCY_KEEP_BYTES && seed.size() < 2 * LOG_EMERGENCY_KEEP_BYTES);
  mem().files[LOG_PATH] = seed;
  CHECK(mem().capacity - mem().used() < LOG_MIN_FREE_BYTES);
  CHECK(logAppend(rec(4100)));
  const std::string t = fileText(LOG_PATH);
  CHECK(t.size() <= seed.size() / 2 + 128);  // původně by zůstalo ~96 kB (uvolnilo by se pár kB)
  const auto v = lines(t);
  CHECK(allWellFormedAndIncreasing(v));
  CHECK(v.back().rfind(stampOf(4100), 0) == 0);
}

// Musí běžet PŘED prvním logMount() (stav "připojeno" je v logstore.cpp statický).
static void test_unmounted() {
  printf("[logstore] bez připojeného FS se nic neděje a nic nespadne\n");
  mem().reset();
  CHECK(!logAppend(rec(1)));
  CHECK(logSizeBytes() == 0);
  CHECK(fsTotalBytes() == 0 && fsUsedBytes() == 0);
  CHECK(!logClear());
  CHECK(!logCompactNow());
  mem().files[UP_TMP_PATH] = "x";
  CHECK(!logReplaceWith(UP_TMP_PATH));
  CHECK(mem().files.size() == 1);  // nic se nezměnilo
}

static void test_remove_legacy() {
  printf("[logstore] smazání starých souborů webu, log zůstane\n");
  mem().reset();
  CHECK(logMount());
  for (uint32_t i = 0; i < 5; i++) CHECK(logAppend(rec(i)));
  { File f = LittleFS.open("/chart.js", "w"); f.write((const uint8_t*)"x", 1); f.close(); }
  { File f = LittleFS.open("/style.css", "w"); f.write((const uint8_t*)"y", 1); f.close(); }
  { File f = LittleFS.open("/moje.txt", "w"); f.write((const uint8_t*)"z", 1); f.close(); }
  CHECK(logRemoveLegacy() == 2);
  CHECK(!LittleFS.exists("/chart.js") && !LittleFS.exists("/style.css"));
  CHECK(LittleFS.exists("/moje.txt") && LittleFS.exists(LOG_PATH) && lines(fileText(LOG_PATH)).size() == 5);
  CHECK(logRemoveLegacy() == 0);
}

int main() {
  test_unmounted();
  test_remove_legacy();
  test_mount_and_append();
  test_mount_recovery();
  test_format_on_mount_failure();
  test_compaction_at_limit();
  test_emergency_trim_full_disk();
  test_clear_and_replace();
  test_remap_places();
  test_web_side_busy_lock();
  test_readers_block_destructive_ops();
  test_append_defers_compaction_while_downloading();
  test_download_with_full_disk_drops_record_not_data();
  test_small_log_not_thinned_when_other_files_fill_disk();
  test_large_log_trimmed_when_disk_nearly_full();
  test_emergency_trim_always_halves();
  printf("\n== logstore: %d OK, %d FAIL ==\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
