# ARBOLOK – historie verzí

Nejnovější nahoře. Funkční popis zařízení je v [README.md](README.md), HTTP API a formát logu v [docs/API.md](docs/API.md).

## 7.0 (finální vydání)

Proti 7.0-rc2 přibyl zápis přelité půdy do logu a PIN k Servisu; jinak se liší jen výchozí hodnoty (platí pro nové zařízení a po
továrním nastavení; uložené nastavení zůstává):

- **Wi-Fi vysílá ve výchozím stavu normálním výkonem** (15 dBm) v obou firmwarech. Nízký výkon (8,5 dBm) z rc1/rc2
  zpomaloval přenos dat; zapnout jde dál v *Servis → Wi-Fi a zobrazení → Vysílač Wi-Fi*.
- **Výchozí zapojení sondy podle desky, bez rozpoznávání:**
  - `arbolok_v7.0.bin` (stará deska): BIAS **GPIO32**, druhá elektroda na **GND**,
  - `arbolok_v7.0_nova-deska.bin` (nová deska): BIAS **GPIO26**, druhá elektroda na **GPIO14** (obrácená polarita).
  Volba *Automaticky* v *Servis → Sonda a měření* pin BIAS dál rozpozná za běhu.
- Měření v obou firmwarech: logaritmická stupnice a krátký pulz (beze změny). Krátký pulz je výchozí i pro starou
  desku proti GND: tam se elektrody polarizují nejvíc a krátký pulz odečte hodnotu dřív, než polarizace začne.
- **Přelitá půda se zapisuje do logu.** Když sonda při ověřeném zapojení měří skoro nulový odpor, řádek dostane
  značku `F` (vlhkost dál `-`, nedá se určit). Dřív se takový záznam nedal odlišit od chyby sondy. V *Záznamech* je
  u něj 🌊 *přelito* místo ⚠️ *chyba sondy*, ruční zápis ukáže „Přelito“, *Stáhnout .txt* zachová `F` a CSV má na
  konci sloupec `prelito`. Starší záznamy se nemění.
- **Servis je zamčený PINem `0852`** (dotaz při otevření karty, i z tlačítek v bannerech; špatný PIN vrátí na
  předchozí kartu). Jen proti náhodnému zásahu, API zůstává otevřené.

## 7.0-rc2

- **Záznamy a místa se po otevření stránky zobrazí hned i s velkým logem.** Stránka dřív při každém otevření
  stahovala celý log (až 600 kB), což přes Wi-Fi zařízení trvalo i několik vteřin – a do té doby byly grafy a
  záznamy prázdné. Teď:
  - log si pamatuje prohlížeč a ze zařízení se stahují **jen nové řádky** od minula (pár set bajtů),
  - bez zapamatovaného logu přijde nejdřív jen **konec logu** (posledních ~64 kB = poslední dny) a zbytek
    se dotáhne na pozadí,
  - platnost zapamatovaného logu hlídá **verze logu**: mění se při každém přepsání (zředění, smazání,
    přečíslování míst, nouzové zkrácení, tovární nastavení), takže prohlížeč nikdy neukáže zastaralé záznamy.
- API: `GET /api/log?from=N` a `?tail=N`, hlavičky `X-Log-Size`, `X-Log-From`, `X-Log-Gen`; ve stavu nové `fs.gen`
  (viz [docs/API.md](docs/API.md)).
- Pozn.: výchozí slabší vysílač Wi-Fi z rc1 (8,5 dBm) přenos dat zpomaluje. Pokud se stránka načítá pomalu
  nebo telefon není přímo u stromu, přepněte *Servis → Wi-Fi a zobrazení → Vysílač Wi-Fi* na *Normální*.

## 7.0-rc1

- **Wi-Fi ve výchozím stavu vysílá nízkým výkonem** (8,5 dBm místo 15 dBm), aby šetřila baterii. Platí pro nová
  zařízení a po továrním nastavení; kdo už má vysílač nastavený, tomu zůstane. Při potížích s dosahem jde v
  *Servis → Wi-Fi a zobrazení → Vysílač Wi-Fi* přepnout na *Normální*. Nouzový režim vysílá dál normálně.
- **Tovární nastavení vrátí zařízení do stavu z výroby:** kromě nastavení, míst, kalibrací a hesla Wi-Fi teď
  zformátuje i souborový systém, takže smaže všechny záznamy a nahrané soubory. Potvrzovací okno na to upozorní.
  Rozdělané stahování nechá nejvýš 3 s doběhnout.
- Původní vydání 7.0 je přejmenované na **7.0-rc0**.

## 7.0-rc0 (dřív vydané jako 7.0)

- **Heslo Wi-Fi ve webu:** *Servis → Wi-Fi a zobrazení → Heslo Wi-Fi*. Ve výchozím stavu je síť dál otevřená. Heslo
  (8–63 znaků bez diakritiky) se uloží do zařízení a zařízení se restartuje. Zapomenuté heslo smaže nouzový režim
  spuštěný magnetem (tlačítko *Smazat heslo Wi-Fi*), nebo tovární nastavení. Heslo zadané při sestavení
  (`AP_PASSWORD`) funguje dál a platí i v nouzovém režimu.
- **Název sítě `ARBOLOK-<číslo>`** (dřív `ARBOL0K-` s nulou), nouzový režim `ARBOLOK-SAFE-<číslo>`. Telefon si
  síť po aktualizaci uloží znovu.
- **Nahrávání souborů nikdy nepřepíše záznamy.** Do v6.16 nahradil log každý nahraný soubor `.txt`; teď se každý soubor
  uloží pod svým jménem a jméno `log.txt` se odmítne.
- Dokumentace: README přepsané na funkční popis, historie verzí přesunutá sem.
- `tools/gen_web_assets.py` dává stejný výstup s každou verzí Pythonu (pevný bajt OS v hlavičce gzipu).
- Hotový `firmware.bin` je přiložený k vydání na GitHubu.

## Starší verze

### 6.16 – kalibrace až po obou krocích, stabilnější ukládání míst.


- Seznam míst a *Servis → Stav sondy* dřív hlásily „zkalibrováno“ už po jednom kroku (jen *Teď je sucho*). Teď je místo
  zkalibrované až po obou krocích; po jednom ukážou **„napůl zkalibrováno (chybí krok)“**, stejně jako průvodce v Nastavení.
- Tabulka míst (kalibrace, prahy, jména) se dřív zapisovala do NVS uvnitř kritické sekce. Když stránka zrovna četla
  stav, mohla se obě jádra zablokovat a zařízení spadnout na watchdog (v prvních 45 s webu = nouzový režim). Změny se
  teď drží v RAM a do NVS je zapíše `locSave()` až po uvolnění zámku.
- Nahrání firmwaru (`POST /update`, i v nouzovém režimu) odmítne požadavek z cizí stránky (hlavička Origin jiná než
  Host), stejně jako ostatní POST. Heslo Wi-Fi (`AP_PASSWORD`) zůstává volitelné.
- Zředění a přečíslování míst: řádek delší než 159 znaků (nahraný log) nebo poslední řádek bez konce dostane `\r\n`,
  takže se už neslepí s dalším záznamem.
- Zředěný / přečíslovaný log nahrazuje původní atomickým přejmenováním (dřív smazat a přejmenovat).
- Úprava všech míst najednou kontroluje tabulku dřív, než přečísluje log.

### 6.15 – „přelitá půda“ místo „zkratu“.

 Když sonda při ověřeném zapojení měří skoro nulový odpor, Přehled
ukáže **Přelito** (ručka na straně Mokro) a klidný pruh „Půda je přelitá vodou … počkejte, až voda vsákne“. Bez
ověřeného zapojení: „Sonda teď neměří: půda je buď přelitá, nebo ještě není ověřené zapojení“ s tlačítkem na zapojení.
Kalibrace v tu chvíli hlásí „Kalibraci teď nejde provést: půda je přelitá vodou nebo sonda neměří“. „Chyba sondy“ je
všude „Sonda neměří“; slovo „zkrat“ uživatel nikde neuvidí.

### 6.14 – teplota v logu v celých °C.

 Řádek je tak o 2 B kratší (asi 22 B, např. `2611091300 52 21 325`).
Při záznamu po 30 min to dělá **asi 19 měsíců v plném rozlišení**. Teplota z DS3231 je teplota desky, desetiny
nic podstatného nenesou. Starší řádky s desetinami stránka čte dál.

### 6.13 – úspornější log, vydrží asi dvakrát déle.

 Nové záznamy se ukládají krátkým řádkem
`2611091300 52 21.3 325` (datum a čas, vlhkost %, teplota °C, napětí v setinách V) místo
`2026-11-09 13:00 52% 21.3C 3.25V r2873 s1 p1` – asi 24 B místo 46 B. Vynechává se surový čítač ADC, doba ustálení
u krátkého pulzu (je pořád stejná) a číslo místa 1. Limit logu před zřeďováním je 600 kB (dřív 560 kB). Při záznamu
po 30 min to dělá **asi 17 měsíců v plném rozlišení** (dřív 8,5), po 10 min asi 6 měsíců, po hodině asi 3 roky.
Stávající log se nepřevádí: starší řádky zůstávají a stránka čte oba formáty. *Stáhnout .txt* skládá čitelný text
(s datem a jednotkami) stránka. Graf ADC na Přehledu (technický režim) zmizel, živá hodnota ADC zůstává.

### 6.12 – srozumitelné Záznamy.

 *Záznamy* ukazují:
- kolik paměti log zabírá a kolik dní ještě vydrží v plném rozlišení, než začnou starší záznamy řídnout,
- záznamy po dnech (nejnovější nahoře, u dne průměr, minimum, maximum), u každého čas, vlhkost (tečka v barvě
  zalít / v pořádku / mokro), teplotu a baterii; značky jsou ikony s vysvětlivkou (💧 zalito, ✋ ruční zápis,
  ⚠️ chyba sondy, 🔋 slabá baterie, 🕒 čas nejistý, 💤 vybitá baterie),
- výběr místa (výchozí to, kde je sonda). Surový log je pod *Surový záznam (pro servis)* i s vysvětlením značek.
Stav (`/api/status`) nese navíc `fs.lmax` (od kolika bajtů se log zřeďuje).

### 6.11 – Přehled bez tlačítka „Změřit teď“.

 Hodnota se při otevřené stránce obnovuje sama, pod měřákem je
„před … s · obnovuje se samo každých 15 s“. Jak často, jde nastavit v *Servis → Sonda a měření → Živá hodnota na stránce*
(5 s až 2 min, výchozí 15 s; jen při otevřené stránce, záznam do logu se nastavuje zvlášť). API `POST /api/measure` zůstává.

### 6.10 – ruční zápis (přenosný logger).

 Sondu jde nosit od stromu ke stromu a měření zapisovat tlačítkem.
- Zapíná se v *Servis → Ruční zápis (přenosný logger)* (výchozí vypnuto). Tam se volí i poznámka k zápisu
  (automatické číslo „Bod 1, Bod 2, …“, vlastní text s předvyplněným dalším číslem, nebo nic) a jestli má každý zápis
  prodloužit stránku (aspoň 5 min do konce; s ručním zápisem smí stránka běžet až hodinu místo 30 min).
- Na *Přehledu* je pak karta *Ruční zápis* s tlačítkem **Zapsat měření** a posledními pěti zápisy. Zařízení změří znovu
  (medián ze 3 pulzů) a zapíše řádek se skutečným časem, vybraným místem, značkou `M` a poznámkou za ` # `.
- Ruční zápisy nejsou v grafech ani ve vyhodnocení zalití (každý je jiný bod). Jsou v *Záznamy → Ruční měření*
  a v exportu do Excelu (sloupce `rucne`, `poznamka`). Zředění logu je nemaže, přečíslování míst nečte poznámku.
- Pravidelné měření běží dál podle nastavení. Příkaz čeká, když se právě ukládá jiná volba (dřív se mohl zahodit).

### 6.9 – srozumitelnější Nastavení.

 Pro koncové uživatele, všechno se ukládá samo (bez tlačítek *Uložit*):
- **Kalibrace místa „…“** je průvodce ve dvou krocích (1. půda je suchá → *Teď je sucho*, 2. po vydatném zalití →
  *Teď je mokro*). U každého kroku je vidět *hotovo* / *zatím ne* a nahoře jednou větou, jestli je místo zkalibrované.
  *Vrátit výchozí kalibraci* je jen nenápadný odkaz.
- **Kdy zalévat:** barevný pruh *Zalít / V pořádku / Mokro* se šipkou u aktuální vlhkosti a dvě pole *Hlásit „Zalít!“
  pod* a *Hlásit „Mokro“ nad*. Uloží se po opuštění pole; „Mokro“ pod „Zalít!“ se neuloží a stránka to řekne.
- **Jak často měřit:** výběr (10, 15, 30 min, 1 h, 2 h, neměřit) místo čísla; pod ním, kdy bude další záznam, a že
  častější měření zkracuje výdrž baterie. Dřív nastavená jiná hodnota (např. 45 min) se ve výběru objeví.
- **Hodiny:** jednou větou, jestli jdou správně; tlačítko *Nastavit čas z telefonu* je výrazné jen tehdy, když nejdou.
- *Doba běhu stránky* se přesunula do *Servis → Wi-Fi a zobrazení*.

### 6.8 – přehlednější Servis a záznamy smazaných míst.


- **Servis** začíná kartou *Stav sondy*: baterie (a odhad výdrže), hodiny, sonda, kalibrace vybraného místa, záznam
  a firmware, každé se zelenou / oranžovou / červenou značkou. U problému je tlačítko, které vede rovnou k nápravě.
- Pod ní jsou **sbalitelné sekce** (Sonda a měření, Místa – čísla kalibrace, Vyhodnocení zalití, Wi-Fi a zobrazení,
  Baterie a spotřeba, Údržba). V záhlaví každé je stručný aktuální stav; otevřené sekce si prohlížeč pamatuje.
- **Volby se ukládají hned** po změně (pin BIAS, způsob měření, druhá elektroda, stupnice, korekce, vysílač, okno po
  připojení). Tlačítko *Uložit* zůstalo jen u číselných polí.
- Dlouhá vysvětlení jsou schovaná pod *Co to znamená?*. Restart, tovární nastavení a mazání záznamů jsou v *Údržbě*.
- **Přesun sondy na jiné místo** (*Nastavení → Místa*) je bez potvrzovacího okna.
- **Oprava: záznamy po smazání místa.** Záznamy nesou číslo místa; smazání místa, které nebylo poslední, dřív posunulo
  čísla ostatních a záznamy se pak ukazovaly u špatných míst. Teď zařízení při smazání (i při úpravě tabulky míst
  v Servisu) záznamy přečísluje: záznamy smazaného místa dostanou `p0` (v exportu „smazané místo“, v grafech nejsou),
  ostatní zůstanou u svého místa. Když log nejde přepsat (právě se stahuje, plná paměť), místo se nesmaže a stránka
  to ohlásí. Import míst ze souboru záznamy nepřečíslovává (nahrazuje místa podle pořadí, jako dosud).

### 6.7 – přehlednější karta Místa.

 *Nastavení → Místa* je seznam míst. U každého je vidět, jestli je zkalibrované,
práh zalévání a kolik má záznamů. Místo, kde je sonda, je zvýrazněné („Sonda je tady“); na jiné se sonda přesune
klepnutím. Přejmenovat a smazat jde přímo u místa. Nové místo má vlastní formulář s prázdným názvem
a vysvětlením, co se stane. Firmware jinak beze změny.

### 6.6 – bez lehkého spánku.

 Pauzy při měření (vybití elektrod, mezera mezi ověřovacími pulzy) jsou zase obyčejné
čekání jako do v6.4. Lehký spánek z v6.5 šetřil jen zlomek procenta a na desce nebyl ověřený. Jinak stejné jako v6.5.

### 6.5 – úspora baterie.


- **Jedno probuzení na záznam.** Do v6.4 zařízení spalo po etapách (7/8 zbytku a znovu), tedy 3–4 probuzení na jeden
  záznam. Teď spí rovnou do další hranice intervalu. Chybu časovače spánku (RC oscilátor ESP32, až ±10 %) se učí: po
  každém probuzení porovná, kolik doopravdy uběhlo podle DS3231, a další spánek o to opraví. Probuzení kousek před
  hranicí (do osminy intervalu) se k ní počítá a záznam se zapíše hned. Po 2–3 cyklech je záznam do pár sekund od
  hranice; první cyklus po zapnutí může být o desetinu intervalu vedle.
- **Web končí dřív, když na něm nikdo není:** do 90 s po přiložení magnetu se nikdo nepřipojil, nebo je telefon déle
  než minutu odpojený. Nastavená doba (4 min) dál platí jako horní mez. Pozor po OTA: připojte se do 90 s, jinak se
  nový firmware nepotvrdí a vrátí se předchozí.
- **Web běží na 80 MHz** místo 160 MHz.
- **Vysílač Wi-Fi: Normální / Nízký** (*Servis → Zobrazení a Wi-Fi*). Nízký (8,5 dBm) stačí, když stojíte u sondy.
  Nouzový režim vysílá vždy normálně.
- **Šetřící režim:** když článek při záznamu klesne pod 3,10 V, interval záznamu se zdvojnásobí a přehled to ohlásí.
  Nad 3,20 V se vrátí. Hranice 2,90 V (bez Wi-Fi) a 2,70 V (hibernace) zůstávají.
- **Servis → Spotřeba (odhad):** počet probuzení a záznamů, doba bdění a webu, naučená chyba časovače a odhad mAh za
  den a výdrže článku 2000 mAh. Je to výpočet z předpokládaných proudů (bdění 30 mA, web 100 mA, spánek 0,15 mA),
  ne měření. Čítače se nulují odpojením baterie a restartem.
- Po nahrání se plán záznamů a čítače jednou vynulují (změnilo se rozložení paměti RTC).

### 6.4 – oprava čtení za koncem pole.

 Odpověď `/api/status` kopírovala výsledky hledání pinu BIAS pro tři kandidáty, ale od v6.1 jsou jen dva (GPIO32, GPIO26), takže se četla jedna položka za koncem pole. Na stránce se to neprojevilo (třetí hodnota se neposílala), formálně to ale bylo nedefinované chování. Jinak beze změny.

### 6.3 – pole s datem v Safari.

 Pole Od/Do (zpráva do PDF, období na přehledu) na iPhonu přetékala a slévala se; teď drží šířku sloupce.

### 6.2 – bez zálohy celé sondy.

 Karta *Záloha celé sondy* (v5.9) zmizela. Export a import míst s kalibrací
v *Servis → Místa a kalibrace* zůstává. API `/api/settings?vcorr=` (korekce napětí baterie) zůstává.

### 6.1 – méně voleb v zapojení.

 Způsob měření: jen *krátký pulz* (výchozí) a *dlouhý pulz* pro starší sondy
(300 ms, jako původní firmware). Dřívější „dlouhý do ustálení“ se převede na dlouhý pulz. Druhá elektroda: jen *GND* nebo
*GPIO14* (uložené 18/19/23 se vrátí na GND). BIAS: *automaticky*, *GPIO32* nebo *GPIO26*. GPIO25 se už nikdy nebudí,
ani při hledání zapojení.

### 6.0 – období od–do a zpráva do PDF.


- **Přehled:** k volbám 24 h / 7 dní / 30 dní / vše přibylo *od–do* se dvěma kalendáři. Graf i karta Zalévání ukazují
  přesně zvolené dny, celé, od 0:00 do 24:00 času sondy. Volba si pamatuje v prohlížeči.
- **Záznamy → Zpráva do PDF:** místo, datum od–do, volitelně tabulka po dnech. Zpráva obsahuje:
  - hlavičku: místo, období, ID sondy a firmware, kdy vznikla, prahy,
  - souhrn: počet zalití, nejdelší doba bez zalití, vlhkost min/Ø/max, hodiny pod prahem, počet záznamů,
  - upozornění na hibernaci, pokud v období byla,
  - grafy vlhkosti a teploty (zalití vyznačená) a baterie,
  - seznam zalití a tabulku po dnech.
  Otevře se tisk, v něm *Uložit jako PDF* (iPhone: Tisk → Sdílet → Uložit do Souborů; Android: Tisk → Uložit jako PDF;
  Mac: PDF → Uložit jako PDF). PDF vyrábí prohlížeč, sonda jen data. Funguje v běžném prohlížeči, ne v okně po připojení k Wi-Fi.
  Období nejvýš 400 dní.

### 5.9 – záloha celé sondy.

 *Servis → Záloha celé sondy* stáhne `zaloha_<ID>_<datum>.json`: všechna místa
s kalibrací, vybrané místo a nastavení sondy:

| Klíč v `nastaveni` | Co to je | Rozsah |
|---|---|---|
| `jazyk` | jazyk stránky | `cz`, `en`, `ua` |
| `interval_min` | interval záznamu | 0–120 (0 = nezaznamenávat) |
| `web_min` | doba běhu webu | 1–30 |
| `po_pripojeni` | chování po připojení k Wi-Fi | 0 nic, 1 okno, 2 tiše |
| `zpusob_mereni` | způsob měření | 2 krátký pulz, 1 dlouhý pulz (starší sondy) |
| `stupnice` | stupnice % | 1 logaritmická, 0 lineární |
| `korekce_buzeni` | přepočet podle napětí článku | 0/1 |
| `pin_bias` | pin BIAS | 0 automaticky, 32, 26 |
| `pin_druha_elektroda` | druhá elektroda | 0 GND, 14 |
| `zaliti_narust_pct`, `zaliti_okno_h` | vyhodnocení zalití | 1–50 %, 1–48 h |
| `baterie_korekce` | korekce napětí baterie (kalibrace podle multimetru) | 0,80–1,20 |

*Obnovit ze zálohy* soubor zkontroluje, zeptá se a pak nastaví sondu i všechna místa. Hodí se k nastavení více sond se
stejným zapojením. Chybějící položka v `nastaveni` zůstane beze změny. Nezálohuje se čas, záznamy ani heslo Wi-Fi
(to je ve firmwaru). Korekci napětí baterie nově přijímá i API (`/api/settings?vcorr=`).
Kontrola verze na stránce `/update` už počítá i s verzemi 10 a výš.

### 5.8 – export, import a úprava míst.

 *Servis → Místa a kalibrace* ukazuje všechna místa najednou (název,
zalévat pod %, mokro nad %, g sucho, g mokro). Jdou upravit, přidat, odebrat a uložit jedním tlačítkem. Hodnoty se
kontrolují na stránce i ve firmwaru: neplatná tabulka se neuloží a nic se nezmění.
*Exportovat do souboru* stáhne `mista_<ID>_<datum>.json`:

```json
{
  "format": "arbolok-mista", "verze": 1, "zarizeni": "4616444", "datum": "2026-11-08",
  "mista": [
    { "nazev": "Lípa u školy", "zalevat_pod": 30, "mokro_nad": 85, "g_sucho": 0.05, "g_mokro": 3.2 }
  ]
}
```

Soubor jde upravit v textovém editoru a *Importovat ze souboru* ho načte zpět, i do jiné sondy. Import hodnoty jen
vyplní do formuláře, uloží je až *Uložit*. Pravidla: 1–8 míst, název neprázdný (nejvýš 24 bajtů), 0 ≤ zalévat pod <
mokro nad ≤ 100 (celá čísla), g sucho > 0, g mokro aspoň 1,2× větší a menší než 1000.

### 5.7 – kalibrace jen v Nastavení.

 Karta „Sonda a kalibrace“ ze Servisu zmizela. Tlačítko *Výchozí kalibrace*
je v *Nastavení → Kalibrace* (s potvrzením, platí pro vybrané místo). Kalibrace „sonda ve vzduchu“ (naprázdno) zmizela úplně:
naprázdno je vždy plný rozsah ADC (4095), dřív uložené hodnoty se ignorují a API `which=open` vrací `400`. Detail sondy (ADC, g, odpor, fáze) a varování o chybě sondy
jsou v *Servis → Zapojení sondy*.

### 5.6 – upozornění na slabou baterii.


- **LED při přiložení magnetu:** při 2,90–3,00 V nejdřív 3 pomalá bliknutí („dejte baterii dobít“), pak se web spustí.
  Pod 2,90 V platí dál 10 rychlých bliknutí a web se nespustí.
- **Záznam o hibernaci:** při přechodu do hibernace se do logu zapíše jeden řádek s napětím a značkou `H`
  (`--%`, `!`, `H`). V grafech Přehledu je od něj do dalšího záznamu červený pás „Vybitá baterie“. Pokud měření stále stojí, je nahoře
  červený pruh „Měření stojí od …“. Po obnovení ještě týden visí modrý pruh „Měření stálo … – … kvůli vybité baterii“.
  CSV má nový poslední sloupec `vybita_baterie`.
- Text kritického pruhu (pod 2,90 V) teď říká, co se stane: pod 2,7 V se měření zastaví, dokud se baterie nedobije.

### 5.5 – hibernace při vybitém článku.

 Pod 2,70 V se zařízení přestane budit v intervalu záznamu: probudí se
jen jednou za 6 h, změří článek a spí dál (žádné měření sondy, žádný zápis, žádná Wi-Fi; magnet = 10 rychlých
bliknutí). Normální provoz se vrátí sám, až článek bude mít aspoň 3,00 V (hystereze), a plán záznamů začne od
aktuálního intervalu. Do hibernace vede i web, kterému napětí dvakrát po sobě klesne pod 2,70 V.

### 5.4 – způsob měření zpět v Servisu, pull-up pevně 10 kΩ.

 *Servis → Zapojení sondy → Způsob měření*:
krátký pulz (výchozí), nebo pro starší desky se sondou pevně na GND dlouhý pulz do ustálení či pevných 300 ms
(hodnoty jako dřív). Volba se ukládá; po změně je potřeba znovu kalibrovat sucho a mokro. Ostatní parametry pulzu
zůstávají pevné. Nastavení pull-upu zmizelo: je vždy 10 kΩ (staré uložené hodnoty se ignorují a smažou).

### 5.3 – rozložení v Safari.

 Bloky uvnitř karet (Upravit místa, prahy, Pokročilé, zapojení) mají mezery
okraji místo flexu, takže v Safari pole a tlačítka nesplývají. Výběry (select) vypadají ve všech prohlížečích stejně
(vlastní šipka, stejná výška jako pole). Teplota zmizela i ze zápatí.

### 5.2 – velmi mokrá zem není zkrat.

Chyba „zkrat“ se hlásí jen při čítači ADC **opravdu 0**. Dřív stačilo ≤ 8,
a hlavně kontrola „uzel nereaguje na BIAS“ chtěla nárůst aspoň 25 čítačů. Hned po vydatném zalití má dělič jen
~15 čítačů, takže se to hlásilo jako chyba. Teď stačí nárůst o polovinu hodnoty (u nízkých hodnot). Čítač 16 tak
znamená „extrémně mokro“ (100 %). Uzel, který na BIAS opravdu nereaguje (špatný pin, chybí pull-up), se pozná dál.

### 5.1 – pevné měření a čistší přehled.


- **Měření nemá nastavení.** Vždy se měří krátkým pulzem (odečet 1 ms po sepnutí, medián druhé poloviny pulzu), dřív než
  se elektrody polarizují. Ostatní parametry pulzu mají výchozí hodnoty: strop 300 ms, tolerance 5 čítačů, vybití 30 ms,
  1 měření na záznam s ověřením skoku nad 150 čítačů. Karta „Měření (pro odborníky)“ a volba režimu měření
  v Pokročilých zmizely. API je nepřijímá, firmware staré uložené hodnoty ignoruje a při dalším uložení nastavení je smaže.
  Simulace sondy s polarizací (jako tvůj květináč) dává krátkým pulzem ohmickou hodnotu (~565 čítačů místo ~1100).
  Zůstává volba druhé elektrody (obrácená polarita), vyhodnocení zalití a průběh napětí v Servisu.
- **Přehled:** odpor půdy je jen malým šedým písmem v okénku měřáku pod procenty („∞ kΩ“ bez kontaktu se zemí).
  Teplota z přehledu zmizela: v zapnutém zařízení se čip zahřívá, takže živá hodnota nic neříká.
  Teplota ze záznamů (měřená po probuzení) zůstává v grafu Záznamů a v CSV.

### 5.0 – jednodušší web pro zákazníky, místa a Servis.


- **Čtyři záložky:** *Přehled*, *Záznamy*, *Nastavení*, *Servis*. Záložka *Soubory* zmizela; nic se do zařízení
  nenahrává. Souborový systém dál drží log. Staré soubory `chart.js` a `style.css` z dřívějších verzí se při zapnutí
  webu samy smažou.
- **Přehled:** pod ručkou je **odpor půdy v kΩ** (z pull-upu v Servisu, výchozí 10 kΩ). Bez sondy v zemi ukazuje „naprázdno“.
  Údaje ADC a vodivost a přepínač grafu ADC/% se ukážou jen po zapnutí *Servis → Technické údaje na přehledu*.
  Volba platí jen v tom prohlížeči.
- **Místa (kalibrační profily):** až 8 pojmenovaných míst, každé má svou kalibraci sucho/mokro, hodnotu naprázdno a prahy.
  Místa se přidávají, přejmenovávají a mažou v *Nastavení → Místo → Upravit místa*. Přepínají se tam nebo
  výběrem nad ručkou na přehledu. Nové místo převezme prahy a hodnotu naprázdno, kalibraci začíná výchozí.
  Každý záznam v logu nese číslo místa (`p1`…`p8`). Grafy, souhrn zalévání i detekce zalití ukazují jen vybrané místo,
  graf baterie ukazuje vše. CSV má sloupec `misto` s názvem.
- **Nastavení** obsahuje jen to, co zákazník potřebuje: místo, kalibraci dvěma tlačítky („Teď je sucho“ / „Teď je mokro“),
  prahy, interval záznamu, dobu běhu stránky, hodiny a jazyk.
- **Servis** (zatím bez hesla) obsahuje všechno podrobné: chování po připojení k Wi-Fi, detail sondy
  a kalibrace (naprázdno, výchozí), zapojení sondy (BIAS, druhá elektroda), měření (pulzy, krátký pulz), vyhodnocení
  zalití, průběh napětí, pokročilé (pull-up, škála, baterie), odkaz na aktualizaci firmwaru a smazání záznamů.
  Banner „Chyba sondy“ teď vede do Servisu.
- Texty jsou zkrácené. Zmínky o starších verzích a deskách z rozhraní zmizely.

### 4.9 – ručkový měřák a ikona na plochu.

 Hodnota na přehledu je teď analogový přístroj jako polní vlhkoměr
(krémový ciferník, mosazný rámeček, stupnice 0–100 %, barevné pásmo sucho / optimum / mokro podle prahů z nastavení,
rudá ručka s tlumeným dokmitem; v tmavém režimu tmavý ciferník; bez hodnoty nebo při chybě sondy leží ručka na dorazu).
*Sdílet → Přidat na plochu* (iPhone) nebo *Přidat na plochu / Instalovat* (Android) teď dá ikonu měřáku a stránka se
otevírá bez lišty prohlížeče. Ikona je ve `web/icons/` (zdroj `icon.svg`, PNG vyrenderuje `tools/make_icons.py`).
Ikona na ploše otevře `http://10.10.10.10/`, takže funguje jen připojený k Wi-Fi zařízení.

### 4.8 – co se stane po připojení k Wi-Fi zařízení

Nastavení (*Nastavení → Provoz → Po připojení k Wi-Fi*). Otevřít rovnou
běžný prohlížeč žádný telefon ani Mac nedovolí; jde jen buď systémové okénko, nebo nic. Proto tři režimy:
- **Nic** (výchozí): jako dosud, adresu `10.10.10.10` zadáte sami.
- **Okno s rozcestníkem**: iPhone, Android i Mac samy otevřou okénko s malou stránkou: „Hotovo, otevřu prohlížeč“
  (zařízení pak telefonu odpoví, že internet je, takže okénko jde zavřít tlačítkem Hotovo a telefon zůstane připojený;
  stránka ukáže návod pro iPhone/Mac nebo Android a adresu) nebo „Otevřít přehled“ rovnou v okénku.
- **Tiše**: žádné okénko; zařízení na testy internetu z telefonu odpovídá „internet je“, takže telefon ani Mac nehlásí
  „bez internetu“ a neodpojují se. Adresu zadáte v prohlížeči; přesměruje se i jakákoli jiná adresa (třeba `arbolok`).
V obou režimech s oknem/tiše je internet v telefonu po dobu připojení přes tuto Wi-Fi nedostupný (telefon si myslí, že
ho má přes ni). Firmware nahrávejte vždy v běžném prohlížeči. Chování skutečných telefonů jsem neověřoval, jen logiku
(testy na PC) a stránky v prohlížeči s napodobeným iPhonem a Androidem.

### 4.7 – nová stránka nahrávání firmwaru

Stránka (`http://10.10.10.10/update`, odkaz *Aktualizace firmwaru* v Nastavení).
Vzhled jako zbytek webu, ukáže běžící verzi a baterii, soubor jde vybrat nebo přetáhnout. **Před odesláním ho zkontroluje**
stejně jako `tools/check_bin.py` (hlavička 0xE9, čip ESP32 – obraz pro C3/S3 odmítne, úseky a XOR součet, popis aplikace,
limit OTA 1 306 624 B, firmware verze 4 s nouzovým režimem, verze v souboru); jen SHA-256 prohlížeč na `http://` spočítat
neumí. Soubor bez nouzového režimu (návrat na v3) pustí až po zaškrtnutí potvrzení. Nahrávání ukazuje průběh v %, po něm
stránka čeká, až se zařízení po restartu ozve, a ukáže novou verzi s odkazem na hlavní stránku (tu je pak potřeba nechat
minutu otevřenou kvůli potvrzení). Samotný příjem souboru v zařízení dělá dál knihovna beze změny, takže postup OTA i
ochrany proti zabrickování zůstávají stejné. Nouzový režim má svou jednoduchou stránku jako dřív.

**Aktualizace jedním dvojklikem (Mac):** v projektu je `Aktualizovat.command`. Stáhněte nový `Arbolog_4.zip` do Stažených
a na skript dvakrát klikněte (poprvé pravým tlačítkem → Otevřít). Přepíše projekt na místě podle zipu (složky src, web,
tools a testy přesně podle zipu; `.pio` s překladem a vaše vlastní soubory zůstanou), sestaví ho, zkontroluje
`firmware.bin` a dá ho na Plochu jako `arbolog_vX.Y.bin`. Mazat složku a znovu importovat do PlatformIO už není potřeba.

### 4.6

přidává režim měření **Krátký pulz** (*Nastavení → Pokročilé → Režim měření*), délka 300–20 000 µs
v *Měření (pro odborníky)*, výchozí 1000 µs. Hodnota je medián druhé poloviny pulzu, tedy dřív, než se elektrody
polarizují. Důvod: první skutečný průběh z květináče (6. 11.) stoupal bez plató od ~560 čítačů (0,3 ms) po ~1150
(300 ms), tedy zdánlivý odpor zeminy z ~1,6 kΩ na ~3,9 kΩ jen tím, jak dlouho se měří. Automatický režim v takové zemině
hodnotu „ustálí“ na místě, které závisí na délce pulzu; krátký pulz měří odporovou část. Funguje i s obrácenou polaritou
(obě fáze krátké). Po přepnutí režimu je potřeba znovu kalibrovat sucho/mokro (hodnoty jsou nižší). Na desce ověřené není.

### 4.5

přidává **samootevírání stránky** (captive portal): po připojení k Wi-Fi zařízení telefon sám nabídne
„Přihlásit se k síti“ a stránku otevře, nemusíte psát `10.10.10.10`. Zařízení k tomu odpovídá na všechny DNS dotazy svou
adresou a test internetu z telefonu (Apple, Android, Windows, Firefox) přesměruje na stránku. Zapnout jde v *Nastavení →
Provoz* (zaškrtávátko „Po připojení k Wi-Fi otevřít stránku samo“). **Od verze 4.6 je ve výchozím stavu vypnuté**
(i na zařízeních, kde ho 4.5 zapnula): na Macu okénko „Připojit se k síti“ překáží při nahrávání firmwaru. Dvě praktické věci: **firmware
nahrávejte v běžném prohlížeči** (okénko pro přihlášení k síti nemusí umět vybrat soubor) a **na iPhonu** po zavření okénka
zvolte „Používat bez internetu“, jinak se iPhone od sítě odpojí. Nouzový režim captive portal nemá (zůstává co
nejjednodušší, adresa `10.10.10.10` platí dál). Jak se chovají konkrétní telefony, jsem neověřoval (jen logiku
přesměrování na PC).

### 4.4

přidává nastavení pro odborníky a vyhodnocení zalití:
- **Obrácená polarita** (volitelná): druhou elektrodu lze přepojit z GND přes odpor 100–470 Ω na volný pin (GPIO14,
  18, 19 nebo 23) a v *Nastavení → Zapojení sondy* zvolit ten pin. Měří se pak oběma polaritami: u střední zeminy
  přesný poměr obou fází (nezávislý na napětí článku i zesílení ADC), u suché zeminy z obrácené fáze (širší rozsah,
  podle výpočtu asi do 210 kΩ místo ~29 kΩ v lineární části ADC), mezi měřeními je sonda úplně odpojená. Výchozí je
  GND, tedy beze změny. Nepřepojená elektroda se pozná (varování, měří se dál přímou polaritou). Viz [kapitola 6](#6-hardware).
- **Parametry měření** na stránce (*Měření (pro odborníky)*): strop pulzu 20–1000 ms, tolerance ustálení, vybití před
  pulzem, počet měření na záznam (medián 1–5) a mez skoku, od které se jednopulzový záznam ověřuje.
- **Interval záznamu 1–120 min** (0 = vypnuto), nově ve výchozím stavu **30 min** (dosud vypnuto). Uložené nastavení
  zůstává; starší hodnota nad 120 min se ořízne na 120.
- **Zalévání na přehledu:** stránka v záznamech najde zalití (nárůst vlhkosti aspoň o X % během nejvýš Y h, obojí
  nastavitelné v *Nastavení → Vyhodnocení zalití*, výchozí 8 % / 6 h) a ukáže: kdy naposledy, kolik zalití v období,
  nejdelší úsek bez zalití (výpadky dat nad 24 h se nepočítají), vlhkost min / průměr / max, dobu pod prahem „zalévat“
  a seznam posledních zalití. V grafu jsou zalití modré pásy. Graf lze přepnout na **ADC čítač** a rozsahy jsou
  24 h / 7 / 30 dní / vše. Na stránce *Záznamy* je **export CSV** (středník, desetinná čárka, sloupec `zaliti`).
- Hodnocení „zalévá se správně / špatně“ stránka záměrně nedělá: to je na uživateli.

### 4.3

opravuje první problém zjištěný na desce: při měření se sondou ve vzduchu ukazovala stránka pořád
**100 %** vlhkosti (a při doteku plošky sonda–zem „0“ a zkrat) a kalibrace nešla. Nejpravděpodobnější příčina (z vašeho
popisu, na desce neověřeno): výchozí sestavení budí BIAS sondy na **GPIO32**, ale vaše deska ho má na **GPIO26**
(to je DAC2). Dělič pak nikdo nebudil, uzel na GPIO35 „visel“ a čítač četl náhodné nízké hodnoty, které se přepočetly na
100 %. Nově si zařízení **správný pin BIAS najde samo** (32, 26, případně 25) a uloží ho, a **chybu sondy ukáže jako
chybu** (*Chyba sondy*, `--`), ne jako 100 %; kalibraci v takovém stavu odmítne s vysvětlením. Podrobně v [1.5](#15-pin-bias-a-chyba-sondy-od-verze-43)
a v kapitole 3.

Verze 4.2 byla tatáž jako 4.1 (změnilo se jen číslo verze), změnil se jen způsob sestavení: 4.1 se nevešla do paměti
pro OTA, protože se přeložila bez optimalizace; 4.2 má platformu zafixovanou, překládá s `-Os` a po sestavení vypíše
velikost (viz 1.1). U vás se 4.2 přeložila, nahrála přes OTA a stránka běží.
