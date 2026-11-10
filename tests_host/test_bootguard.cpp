// Testy ochrany před zabrickováním (bootguard.h): rozhodování o nouzovém režimu, scénáře
// "nový firmware padá dokola", časovač nouzového režimu. Čisté C++, žádný ESP32.
#include <stdio.h>
#include <string.h>

#include "../src/bootguard.h"

using namespace bootguard;

static int g_ok = 0, g_fail = 0;
#define CHECK(c)                                                                   \
  do {                                                                             \
    if (c) g_ok++;                                                                 \
    else {                                                                         \
      g_fail++;                                                                    \
      printf("  CHYBA %s:%d: %s\n", __FILE__, __LINE__, #c);                       \
    }                                                                              \
  } while (0)

static State fresh() {
  State s;
  memset(&s, 0xA5, sizeof(s));  // jako náhodná paměť RTC po zapnutí napájení
  return s;
}

// Jeden start s danou příčinou. Vrací rozhodnutí.
static Decision boot(State& s, Cause c, bool reed = false) { return onBoot(s, c, reed); }

static void test_basics() {
  printf("[bootguard] záznam, kontrolní součet, náhodná paměť\n");
  State s = fresh();
  CHECK(!valid(s));
  Decision d = boot(s, C_WAKE);  // paměť neplatná -> čistý štít, nic se nestane
  CHECK(valid(s) && !d.safe && !d.forced && !d.giveUp);
  CHECK(s.streak == 0 && s.safe == 0 && s.phase == P_BOOT);

  // jakákoli změna bitu = neplatné
  for (int byte = 0; byte < (int)sizeof(State); byte++) {
    State t = s;
    ((uint8_t*)&t)[byte] ^= 0x10;
    CHECK(!valid(t));
  }
  // poškozený záznam po pádu se bere jako čistý štít (ne jako nouzový režim)
  State bad = s;
  bad.streak = 200;
  CHECK(!valid(bad));
  d = boot(bad, C_CRASH);
  CHECK(valid(bad) && bad.streak == 1 && !d.safe);
}

static void test_power_on_clears() {
  printf("[bootguard] zapnutí napájení = čistý štít (i z nouzového režimu)\n");
  State s = fresh();
  boot(s, C_POWER);
  s.streak = 5;
  s.safe = 1;
  seal(s);
  Decision d = boot(s, C_POWER);
  CHECK(!d.safe && !d.giveUp && s.streak == 0 && s.safe == 0);
}

static void test_crash_streak() {
  printf("[bootguard] tři pády v řadě -> nouzový režim; úspěšný cyklus počítadlo smaže\n");
  State s = fresh();
  boot(s, C_POWER);
  CHECK(!boot(s, C_CRASH).safe && s.streak == 1);
  CHECK(!boot(s, C_CRASH).safe && s.streak == 2);
  Decision d = boot(s, C_CRASH);
  CHECK(d.safe && !d.forced && !d.giveUp && s.streak == 3 && s.safe == 1);

  // úspěšný cyklus mezi pády počítadlo vynuluje
  s = fresh();
  boot(s, C_POWER);
  boot(s, C_CRASH);
  boot(s, C_CRASH);
  healthy(s);
  CHECK(s.streak == 0 && s.phase == P_IDLE);
  CHECK(!boot(s, C_CRASH).safe && s.streak == 1);
}

static void test_web_start_crash() {
  printf("[bootguard] pád při startu webu = hned nouzový režim; po stabilizaci ne\n");
  State s = fresh();
  boot(s, C_POWER);
  setPhase(s, P_WEB_START);
  Decision d = boot(s, C_CRASH);
  CHECK(d.safe && s.streak >= kCrashLimit && s.safe == 1);

  // web běžel dost dlouho (stabilní): pád už je jen jeden pád
  s = fresh();
  boot(s, C_POWER);
  setPhase(s, P_WEB_START);
  webStable(s);
  CHECK(s.streak == 0 && s.phase == P_WEB_STABLE);
  d = boot(s, C_CRASH);
  CHECK(!d.safe && s.streak == 1);

  // pád během záznamu (fáze BOOT) váží 1
  s = fresh();
  boot(s, C_POWER);
  d = boot(s, C_CRASH);
  CHECK(!d.safe && s.streak == 1);
}

static void test_not_counted() {
  printf("[bootguard] podpětí, restart (OTA), probuzení z deep sleep se jako pád nepočítají\n");
  State s = fresh();
  boot(s, C_POWER);
  for (int i = 0; i < 10; i++) {
    CHECK(!boot(s, C_BROWNOUT).safe);
    CHECK(!boot(s, C_SW).safe);
    CHECK(!boot(s, C_WAKE).safe);
    CHECK(!boot(s, C_OTHER).safe);
  }
  CHECK(s.streak == 0 && s.safe == 0);

  // restart v nouzovém režimu stav nemění (nouzový režim trvá)
  s = fresh();
  boot(s, C_POWER);
  boot(s, C_CRASH);
  boot(s, C_CRASH);
  boot(s, C_CRASH);
  CHECK(s.safe == 1);
  CHECK(boot(s, C_SW).safe);
  CHECK(boot(s, C_WAKE).safe);       // probuzení magnetem po uspání nouzového režimu = zase nouzový
  CHECK(boot(s, C_BROWNOUT).safe);   // (Wi-Fi se po podpětí stejně nezapne, řeší safemode.cpp)
}

static void test_forced_by_reed() {
  printf("[bootguard] magnet při připojení baterie = nouzový režim (jednorázově)\n");
  State s = fresh();
  Decision d = boot(s, C_POWER, true);
  CHECK(d.safe && d.forced && !d.giveUp);
  CHECK(s.safe == 0);               // jednorázové: příští start je normální
  CHECK(s.phase == P_SAFE);

  d = boot(s, C_WAKE);              // po uspání nouzového režimu a probuzení magnetem
  CHECK(!d.safe);
  d = boot(s, C_SW, true);          // restart (např. po OTA) s magnetem u relé NESMÍ spustit nouzový režim
  CHECK(!d.safe);
  d = boot(s, C_WAKE, true);        // ani probuzení magnetem
  CHECK(!d.safe);
  d = boot(s, C_CRASH, true);
  CHECK(!d.safe);
  d = boot(s, C_POWER, false);
  CHECK(!d.safe);
}

static void test_give_up() {
  printf("[bootguard] nouzový režim padá dokola -> Wi-Fi se nezapíná (ochrana baterie)\n");
  State s = fresh();
  boot(s, C_POWER);
  setPhase(s, P_WEB_START);
  Decision d = boot(s, C_CRASH);  // streak 3, nouzový režim
  CHECK(d.safe && !d.giveUp);
  d = boot(s, C_CRASH);           // nouzový režim spadl (fáze SAFE -> váha 1)
  CHECK(d.safe && !d.giveUp && s.streak == 4);
  d = boot(s, C_CRASH);
  CHECK(d.safe && !d.giveUp && s.streak == 5);
  d = boot(s, C_CRASH);
  CHECK(d.safe && d.giveUp && s.streak == 6);
  // streak nepřeteče
  for (int i = 0; i < 1000; i++) d = boot(s, C_CRASH);
  CHECK(d.giveUp && s.streak == 250);
  // baterie reset = všechno znovu
  d = boot(s, C_POWER);
  CHECK(!d.safe && !d.giveUp);
}

static void test_clear_after_ota() {
  printf("[bootguard] úspěšné OTA / ruční návrat = čistý štít\n");
  State s = fresh();
  boot(s, C_POWER);
  boot(s, C_CRASH);
  boot(s, C_CRASH);
  boot(s, C_CRASH);
  CHECK(s.safe == 1);
  clear(s);                         // OTA proběhlo (hák v onOtaEnd) -> restart
  CHECK(valid(s));
  Decision d = boot(s, C_SW);
  CHECK(!d.safe && s.streak == 0);
  // nový firmware je zase špatný: tři pády v řadě -> nouzový režim znovu
  boot(s, C_CRASH);
  boot(s, C_CRASH);
  CHECK(boot(s, C_CRASH).safe);
}

static void test_scenarios() {
  printf("[bootguard] scénáře: špatný firmware nahraný přes OTA\n");
  // A) nový firmware padá hned při startu webu (po restartu po OTA)
  State s = fresh();
  boot(s, C_POWER);                 // starý firmware běží
  clear(s);                         // OTA hotové
  Decision d = boot(s, C_SW);       // nový firmware nastartoval
  CHECK(!d.safe);
  setPhase(s, P_WEB_START);         // spustil web
  d = boot(s, C_CRASH);             // a spadl
  CHECK(d.safe);                    // -> uživatel hned dostane nouzovou síť a může nahrát opravu
  clear(s);                         // oprava nahrána
  d = boot(s, C_SW);
  CHECK(!d.safe);

  // B) padá pokaždé při krátkém probuzení (záznam): do 3. pádu nouzový režim
  s = fresh();
  boot(s, C_POWER);
  boot(s, C_WAKE);
  CHECK(!boot(s, C_CRASH).safe);
  CHECK(!boot(s, C_CRASH).safe);
  CHECK(boot(s, C_CRASH).safe);

  // C) občasný pád (jednou za čas), mezi nimi úspěšné cykly: nouzový režim se nikdy nespustí
  s = fresh();
  boot(s, C_POWER);
  for (int i = 0; i < 100; i++) {
    CHECK(!boot(s, C_CRASH).safe);
    healthy(s);
    boot(s, C_WAKE);
    healthy(s);
  }

  // D) web běží (stabilní), občas spadne: žádný nouzový režim
  s = fresh();
  boot(s, C_POWER);
  for (int i = 0; i < 20; i++) {
    setPhase(s, P_WEB_START);
    webStable(s);
    CHECK(!boot(s, C_CRASH).safe);
  }
  CHECK(s.streak == 1);  // webStable() nulovalo, pád přičetl 1
}

static void test_safe_timer() {
  printf("[bootguard] nouzový režim: časovač spánku\n");
  const uint32_t S = 1000;  // start
  CHECK(!safeExpired(S, S, S, false, 0));
  CHECK(!safeExpired(S + kSafeIdleMs, S, S, false, 0));
  CHECK(safeExpired(S + kSafeIdleMs + 1, S, S, false, 0));
  // připojený telefon posouvá "poslední aktivitu"
  CHECK(!safeExpired(S + kSafeIdleMs + 5000, S, S + 6000, false, 0));
  // probíhá OTA: nespí
  CHECK(!safeExpired(S + kSafeIdleMs + 1, S, S, true, S + 100));
  // zapomenuté OTA (nikdy neskončilo) po kSafeOtaMs už nebrání spánku
  CHECK(safeExpired(S + kSafeOtaMs + kSafeIdleMs + 10, S, S, true, S + 100));
  // tvrdý strop platí vždy, i při OTA a připojeném telefonu
  CHECK(safeExpired(S + kSafeMaxMs + 1, S, S + kSafeMaxMs, true, S + kSafeMaxMs - 5));
  CHECK(!safeExpired(S + kSafeMaxMs, S, S + kSafeMaxMs, true, S + kSafeMaxMs - 5));
  // přetečení millis()
  const uint32_t s2 = 0xFFFFFF00u;
  CHECK(!safeExpired(s2 + 1000, s2, s2, false, 0));
  CHECK(safeExpired(s2 + kSafeIdleMs + 1, s2, s2, false, 0));  // now po přetečení
  CHECK(!safeExpired(s2 + kSafeIdleMs - 1, s2, s2, false, 0));
}

static void test_led() {
  printf("[bootguard] nouzový režim: LED (dvojité bliknutí každé 2 s)\n");
  int onMs = 0;
  int edges = 0;
  bool prev = safeLedOn(0);
  for (uint32_t t = 0; t < 2000; t++) {
    const bool on = safeLedOn(t);
    if (on) onMs++;
    if (on && !prev) edges++;
    prev = on;
  }
  CHECK(onMs == 240);               // 2 × 120 ms
  CHECK(edges == 1);                // druhý záblesk začíná v t=300; první v t=0 (prev bylo true)
  CHECK(safeLedOn(0) && !safeLedOn(200) && safeLedOn(300) && !safeLedOn(500) && !safeLedOn(1999));
  CHECK(safeLedOn(2000) && safeLedOn(2300));  // opakuje se
}

// Start s příznakem, který obraz (oddíl aplikace) právě běží.
static Decision bootImg(State& s, uint32_t part, Cause c, bool reed = false) { return onBoot(s, c, reed, part); }

static void test_other_image() {
  printf("[bootguard] pády a nouzový režim patří obrazu, který je napočítal (OTA, návrat bootloaderu)\n");
  const uint32_t A = 0x10000, B = 0x150000;

  // stejný obraz: počítání pokračuje jako dřív (a oddíl se zapisuje do záznamu)
  State s = fresh();
  bootImg(s, A, C_POWER);
  CHECK(valid(s) && s.part == A);
  CHECK(!bootImg(s, A, C_CRASH).safe && s.streak == 1);
  CHECK(!bootImg(s, A, C_CRASH).safe && s.streak == 2);
  CHECK(bootImg(s, A, C_CRASH).safe && s.safe == 1 && s.streak == 3 && s.part == A);

  // pád nového obrazu B, bootloader vrátí A: A pád nepočítá a nezůstane v nouzovém režimu
  s = fresh();
  bootImg(s, A, C_POWER);
  bootImg(s, B, C_SW);  // po OTA nabootoval B
  CHECK(s.part == B && s.streak == 0);
  setPhase(s, P_WEB_START);
  Decision d = bootImg(s, A, C_CRASH);  // B spadlo při startu webu, běží zase A
  CHECK(!d.safe && !d.giveUp && s.streak == 0 && s.safe == 0 && s.part == A && s.phase == P_BOOT);

  // totéž, ale B už stihlo napočítat tři pády a nouzový režim: A začíná načisto
  s = fresh();
  bootImg(s, B, C_POWER);
  bootImg(s, B, C_CRASH);
  bootImg(s, B, C_CRASH);
  CHECK(bootImg(s, B, C_CRASH).safe && s.safe == 1);
  d = bootImg(s, A, C_WAKE);
  CHECK(!d.safe && s.streak == 0 && s.safe == 0 && s.part == A);

  // A -> B -> A -> B: každá změna obrazu = čistý štít
  s = fresh();
  bootImg(s, A, C_POWER);
  bootImg(s, A, C_CRASH);
  CHECK(s.streak == 1);
  CHECK(!bootImg(s, B, C_SW).safe && s.streak == 0);
  bootImg(s, B, C_CRASH);
  CHECK(s.streak == 1 && s.part == B);
  CHECK(!bootImg(s, A, C_CRASH).safe && s.streak == 0 && s.part == A);

  // nouzový režim stejného obrazu přetrvává přes restarty (to se nezměnilo)
  s = fresh();
  bootImg(s, A, C_POWER);
  bootImg(s, A, C_CRASH);
  bootImg(s, A, C_CRASH);
  bootImg(s, A, C_CRASH);
  CHECK(bootImg(s, A, C_SW).safe && bootImg(s, A, C_WAKE).safe);

  // neznámý oddíl (part == 0) = chování jako dřív, obraz se nerozlišuje
  s = fresh();
  boot(s, C_POWER);
  boot(s, C_CRASH);
  boot(s, C_CRASH);
  CHECK(s.streak == 2 && s.part == 0);
  CHECK(boot(s, C_CRASH).safe);
  // záznam s neznámým oddílem + nový obraz: nepovažuje se za cizí (nic nevíme), pády se počítají dál
  s = fresh();
  boot(s, C_POWER);
  boot(s, C_CRASH);
  CHECK(!bootImg(s, A, C_CRASH).safe && s.streak == 2 && s.part == A);

  // opačně: záznam oddíl zná, ale běžící oddíl se zjistit nepodařilo (part == 0): obrazy se nerozlišují,
  // pády se počítají dál a známý oddíl v záznamu zůstane
  s = fresh();
  bootImg(s, A, C_POWER);
  bootImg(s, A, C_CRASH);
  CHECK(s.streak == 1 && s.part == A);
  CHECK(!bootImg(s, 0, C_CRASH).safe && s.streak == 2 && s.part == A);
  CHECK(bootImg(s, 0, C_CRASH).safe && s.streak == 3);

  // zapnutí napájení a magnet fungují nezávisle na oddílu
  s = fresh();
  bootImg(s, B, C_POWER);
  bootImg(s, B, C_CRASH);
  d = bootImg(s, A, C_POWER, true);
  CHECK(d.safe && d.forced && s.streak == 0 && s.safe == 0 && s.part == A);

  // clear() oddíl nuluje; kontrolní součet ho kryje
  s = fresh();
  bootImg(s, A, C_POWER);
  CHECK(s.part == A);
  clear(s);
  CHECK(s.part == 0 && valid(s));
  bootImg(s, A, C_POWER);
  State t = s;
  t.part ^= 0x10000;
  CHECK(!valid(t));
}

int main() {
  test_basics();
  test_power_on_clears();
  test_crash_streak();
  test_web_start_crash();
  test_not_counted();
  test_forced_by_reed();
  test_give_up();
  test_clear_after_ota();
  test_scenarios();
  test_other_image();
  test_safe_timer();
  test_led();
  printf("\n== bootguard: %d OK, %d FAIL ==\n", g_ok, g_fail);
  return g_fail ? 1 : 0;
}
