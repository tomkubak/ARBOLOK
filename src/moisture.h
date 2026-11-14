// ============================================================================
//  moisture.h  –  měřicí logika sondy (ČISTÉ C++, žádná závislost na Arduinu)
//
//  Díky tomu jde celé jádro testovat na PC (tests_host/), na simulovaných
//  sondách s polarizací, šumem a rušením od Wi-Fi.
//
//  Obsah:
//    1) SettleDetector – hledá "plató" napětí na uzlu děliče po sepnutí BIAS
//    2) Calib + conductance()/percentFromRaw() – převod ADC čítače na vlhkost
//    3) needsConfirm() – kdy se jednopulzové měření ověří dalšími pulzy
//    4) buildProfile() – podklad pro diagnostický graf průběhu napětí
// ============================================================================
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace moist {

// ---------------------------------------------------------------------------
//  1) Detekce ustálení
// ---------------------------------------------------------------------------
enum : uint8_t {
  FLAG_UNSETTLED = 1,  // nenašlo se plató, použit konec okna (strop délky)
  FLAG_SAT_HIGH = 2,   // uzel ~ naprázdno (sonda ve vzduchu / velmi suchá zem)
  FLAG_SAT_LOW = 4,    // uzel ~ 0 V: zkrat sondy, NEBO pin BIAS vůbec nebudí dělič (uzel není čím nabít)
  FLAG_NOISY = 8,      // rozptyl uvnitř okna je neobvykle velký (rušení)
  FLAG_NO_RESP = 16,   // uzel nereaguje na BIAS (čítač je při BIAS = HIGH stejný jako při LOW, a přitom ne ~0):
                       // BIAS nebudí dělič (špatný pin), chybí pull-up, nebo uzel visí ve vzduchu
  FLAG_RET_BAD = 32    // obrácená polarita zapnutá, ale druhá elektroda na zvoleném pinu nebudí (není přepojená?):
                       // použije se jen přímá polarita (hodnota platí, jen bez rozšíření rozsahu)
};

// Měření s těmito příznaky nic neříká o vlhkosti zeminy (hodnota se nezobrazuje, nezapisuje a nekalibruje se).
inline bool isFault(uint8_t flags) { return (flags & (FLAG_SAT_LOW | FLAG_NO_RESP)) != 0; }
// Uzel ~ 0 V při ověřeném zapojení (pin BIAS dělič budí): skoro nulový odpor = půda přelitá vodou (stránka
// "Přelito", v logu značka F). Bez ověřeného zapojení to může být i špatný pin, proto jen "chyba sondy".
inline bool isFlood(uint8_t flags, bool wiringOk) {
  return wiringOk && (flags & FLAG_SAT_LOW) && !(flags & FLAG_NO_RESP);
}

struct SettleParams {
  uint32_t t0Us = 1500;       // začátek prvního okna (po sepnutí BIAS)
  uint32_t maxUs = 300000;    // tvrdý strop délky měření
  float epsCounts = 5.0f;     // tolerance ustálení (ADC čítače)
  uint16_t minSamples = 10;   // minimum vzorků, aby se okno vyhodnotilo
  bool stopEarly = true;      // false = vždy měřit až do maxUs (chování verze 3)
};

struct SettleResult {
  float value = 0.0f;         // ustálená hodnota: medián okna (ADC čítače 0..4095)
  uint32_t tSettleUs = 0;     // začátek okna, kde bylo plató nalezeno
  bool settled = false;       // true = plató nalezeno, false = vypršel strop
  float spread = 0.0f;        // max-min v okně použitém pro hodnotu
  uint16_t n = 0;             // počet vzorků v tomto okně
  uint8_t flags = 0;          // FLAG_*
};

// Průběžný analyzátor. Volající ho krmí vzorky (čas od sepnutí BIAS, ADC) a
// po každém vzorku se ptá, jestli je hotovo.
//
//   okna:  [t0, 2*t0), [2*t0, 4*t0), [4*t0, 8*t0), ...  (poslední se ořízne na maxUs)
//   hotovo: |medián(k) - medián(k-1)| <= tolerance
//   tolerance = max(epsCounts, 3 * sigma / sqrt(n))   (sigma = IQR/1.349 okna)
//
// Zdvojující se okna jsou důležitá: kdyby měl uzel kondenzátor s časovou
// konstantou tau, pevně široké okno by "ustálení" prohlásilo předčasně (strmost
// klesne pod práh dřív, než je plató). Zdvojená okna to vyřeší sama.
class SettleDetector {
 public:
  static const size_t kWinMax = 128;

  explicit SettleDetector(const SettleParams& p = SettleParams());
  void reset();

  // true = hotovo (plató nalezeno, nebo vypršel strop)
  bool feed(uint32_t tUs, uint16_t raw);

  // Doporučená pauza mezi vzorky v právě běžícím okně (µs). Krátká okna se
  // čtou naplno, dlouhá okna se rozprostřou rovnoměrně (kWinMax vzorků).
  uint32_t gapUs() const;

  // Vynucené ukončení (např. vypršel čas). Uzavře rozpracované okno.
  void finish();

  SettleResult result() const { return res_; }
  bool done() const { return done_; }

 private:
  void closeWindow(bool force);

  SettleParams p_;
  uint16_t buf_[kWinMax];
  uint16_t n_;
  uint32_t winStart_, winEnd_;
  bool havePrev_;
  float prevMedian_;
  bool done_;
  SettleResult res_;
};

// ---------------------------------------------------------------------------
//  2) Převod na vlhkost
// ---------------------------------------------------------------------------
// Sonda tvoří dělič:   BIAS --R-- uzel(ADC) --Rp(zem)-- GND
//   raw / rawOpen = Rp / (R + Rp)      (rawOpen = čítač naprázdno)
//   index vodivosti  g = (rawOpen - raw) / raw = R / Rp     (úměrný vodivosti zeminy)
// Platí přesně jen při lineárním ADC a neoříznutém rawOpen. Při článku nad ~3,1 V je rawOpen oříznutý
// (4095 = nejvýš ~3,1 V, sonda ve vzduchu dává ~3,3 V) a g je u vysokých Rp podhodnocené (Rp z g vyjde
// o třetinu větší při 30 k a asi 3x větší při 100 k; výpočet z modelu, neměřeno). Procenta to nevadí:
// g je monotónní a kalibrace sucho/mokro stupnici natáhne. Odhad kOhm (webjson/UI) je jen orientační.
//
// Vodivost zeminy se mění o řády (mokrá ~ 1..10 k, suchá ~ 100 k..1 M), proto je
// "procento" definované na LOGARITMICKÉ škále g mezi dvěma kalibračními body:
//   0 %   ... gDry (sucho)        100 % ... gWet (mokro)
struct Calib {
  float gDry = 0.05f;
  float gWet = 3.0f;
  uint16_t rawOpen = 4095;
  uint8_t scale = 1;  // 1 = log vodivost (nové), 0 = lineární "100 - raw/4095" (jako v3)
};

bool calibValid(const Calib& c);

// Nejmenší povolená hodnota kalibračního bodu "sucho". ESP32 je napájeno přímo z článku (3,0–3,5 V) a jeho
// ADC má pevnou referenci: při plném článku je uzel u suché zeminy nad rozsahem ADC (~3,1 V), čítač je nasycený
// a naměřené g vyjde ~0. Logaritmická stupnice by se pak zbytečně natáhla a vlhká zemina by vycházela na
// 70–95 %. Nasycené "sucho" proto dostane tuto spodní mez (= výchozí gDry).
const float kDryAnchorMin = 0.05f;

// Zapíše kalibrační bod (wet = "mokro", jinak "sucho") z naměřeného indexu vodivosti g. false = výsledná
// kalibrace by nebyla platná (nebo g je nesmysl): c se nezmění.
bool setCalPoint(Calib& c, bool wet, float g);

// Přepočet čítače na hodnotu, jakou bychom naměřili při buzení VEXC_REF (ESP32 běží přímo z LiFePO4,
// BIAS = napětí článku, ADC má pevnou referenci). vexcVolts mimo 2..4 V (nebo NaN) => bez korekce.
// Plně nasycený čítač (>= 4080) se nemění; výsledek se ořízne na 4095.
float normalizeRaw(float raw, float vexcVolts, float vexcRefVolts);

float conductance(float raw, float rawOpen);       // index g (>0)
float percentFromRaw(float raw, const Calib& c);   // 0..100 (float)

// Plně nasycené hodnoty (diagnostika)
bool isOpenCircuit(float raw, const Calib& c);
bool isShort(float raw);
// Uzel se po zapnutí BIAS nepohnul: plató (ustálená hodnota při BIAS = HIGH) je od čítače těsně před pulzem
// (BIAS = LOW) jen o pár čítačů výš. Skutečný dělič se hýbe vždy: i zkrat s Rp = 60 Ω dá z 3,3 V ~15 čítačů a
// vlhká zemina (1 kΩ) stovky; naprázdno 4095. Mimo to zkrat (plató ~0) hlásí isShort(), tady jde o uzel,
// který stojí na nenulové hodnotě (visí ve vzduchu, je držen jinde, BIAS nic nebudí).
const float kMinResponse = 25.0f;
bool isNoResponse(float plateau, float baseline);

// ---------------------------------------------------------------------------
//  2b) Obrácená polarita (druhá elektroda na GPIO místo GND)
// ---------------------------------------------------------------------------
// Fáze A (přímá):    BIAS=H, druhá elektroda=L   ->  a = F * Rp / (R + Rp)
// Fáze B (obrácená): BIAS=L, druhá elektroda=H   ->  b = F * R  / (R + Rp)      (F = čítač plného buzení)
// Když jsou obě v lineárním okně ADC, g = R/Rp = b / a PŘESNĚ (nezávisí na napětí článku ani na zesílení ADC).
// U suché zeminy je a u stropu ADC (nelineární / přetéká), b je ale uprostřed: g = b / (F - b), F ~ rawOpen.
// U mokré je b u stropu a a uprostřed: platí dosavadní výpočet z a.
const float kLinLo = 150.0f;    // lineární okno ADC (~0,15 až ~3 V při 11 dB; horní mez schválně pod nelinearitou)
const float kLinHi = 3700.0f;
enum Phase : uint8_t { PH_A = 0, PH_B = 1, PH_RATIO = 2 };
struct DualResult {
  float g = 0.0f;        // index vodivosti (platí jen pro PH_B a PH_RATIO)
  uint8_t phase = PH_A;  // PH_A = použít dosavadní výpočet z fáze A
  bool retBad = false;   // fáze B nereaguje, ač fáze A ukazuje proud (druhá elektroda není na zvoleném pinu)
};
// a, b: surové plató obou fází; baseB: čítač těsně před pulzem B; bNorm: b po normalizaci buzení; rawOpen: kalibrace.
DualResult combineDual(float a, float b, float baseB, float bNorm, float rawOpen);
// Index vodivosti -> ekvivalentní (normalizovaný) čítač fáze A, aby log, procenta i kalibrace fungovaly beze změny.
float rawFromG(float g, float rawOpen);

// ---------------------------------------------------------------------------
//  3) Ověřování podezřelých měření
// ---------------------------------------------------------------------------
// Měření do logu je jeden pulz. Je-li podezřelé (rušení, nebo velký skok proti
// předchozímu záznamu), ověří se třemi dalšími pulzy a použije se jejich medián.
// Skutečná změna (zalití) se tak potvrdí hned – bez zpoždění, které by přinesl
// klouzavý medián přes několik probuzení. lastRaw <= 0 = předchozí záznam neznámý.
bool needsConfirm(uint8_t flags, float raw, float lastRaw, float deltaCounts);

// ---------------------------------------------------------------------------
//  4) Profil průběhu napětí (jen pro diagnostiku v UI)
// ---------------------------------------------------------------------------
// Zhustí vzorky (t, raw) do logaritmicky rozložených košů. Vrací počet bodů.
size_t buildProfile(const uint32_t* tUs, const uint16_t* raw, size_t n,
                    float* outTms, float* outRaw, size_t maxOut);

// Pomocné
float medianOfSorted(const uint16_t* a, size_t n);

}  // namespace moist
