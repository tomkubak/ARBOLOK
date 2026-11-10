# ARBOLOK / Arbolog v6

Firmware pro ESP32, které hlídá vlhkost půdy u stromů: dvě nerezové elektrody (7 cm × 4 mm,
rozteč 2,5 cm), jedna na zemi, na druhou se přes **pull-up 10 kΩ** přivádí napětí a z děliče, který
tvoří zemina, se čte ADC. Zařízení je napájené **přímo z jednoho článku LiFePO4** (cca 3,0–3,5 V, bez
stabilizátoru). Většinu času spí (deep sleep), jednou za zvolený interval se probudí, změří a zapíše
do logu; magnetem (jazýčkové relé na GPIO4) se zapne Wi-Fi a webová stránka na `http://10.10.10.10/`
(nebo `http://arbolok.local/`).

Tohle je přepracování verze 3. **Změnil se jen software**, hardware zůstává stejný. Firmware je
navržený pro zařízení, do kterého se **nahrává jen přes OTA** (bez sériového převodníku), proto má
několik vrstev ochrany proti zabrickování: viz [kapitola 1](#1-nahrání-přes-ota-bez-kabelu).

**Verze 6.16: kalibrace až po obou krocích, stabilnější ukládání míst.**
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

**Verze 6.15: „přelitá půda“ místo „zkratu“.** Když sonda při ověřeném zapojení měří skoro nulový odpor, Přehled
ukáže **Přelito** (ručka na straně Mokro) a klidný pruh „Půda je přelitá vodou … počkejte, až voda vsákne“. Bez
ověřeného zapojení: „Sonda teď neměří: půda je buď přelitá, nebo ještě není ověřené zapojení“ s tlačítkem na zapojení.
Kalibrace v tu chvíli hlásí „Kalibraci teď nejde provést: půda je přelitá vodou nebo sonda neměří“. „Chyba sondy“ je
všude „Sonda neměří“; slovo „zkrat“ uživatel nikde neuvidí.

**Verze 6.14: teplota v logu v celých °C.** Řádek je tak o 2 B kratší (asi 22 B, např. `2611091300 52 21 325`).
Při záznamu po 30 min to dělá **asi 19 měsíců v plném rozlišení**. Teplota z DS3231 je teplota desky, desetiny
nic podstatného nenesou. Starší řádky s desetinami stránka čte dál.

**Verze 6.13: úspornější log, vydrží asi dvakrát déle.** Nové záznamy se ukládají krátkým řádkem
`2611091300 52 21.3 325` (datum a čas, vlhkost %, teplota °C, napětí v setinách V) místo
`2026-11-09 13:00 52% 21.3C 3.25V r2873 s1 p1` – asi 24 B místo 46 B. Vynechává se surový čítač ADC, doba ustálení
u krátkého pulzu (je pořád stejná) a číslo místa 1. Limit logu před zřeďováním je 600 kB (dřív 560 kB). Při záznamu
po 30 min to dělá **asi 17 měsíců v plném rozlišení** (dřív 8,5), po 10 min asi 6 měsíců, po hodině asi 3 roky.
Stávající log se nepřevádí: starší řádky zůstávají a stránka čte oba formáty. *Stáhnout .txt* skládá čitelný text
(s datem a jednotkami) stránka. Graf ADC na Přehledu (technický režim) zmizel, živá hodnota ADC zůstává.

**Verze 6.12: srozumitelné Záznamy.** *Záznamy* ukazují:
- kolik paměti log zabírá a kolik dní ještě vydrží v plném rozlišení, než začnou starší záznamy řídnout,
- záznamy po dnech (nejnovější nahoře, u dne průměr, minimum, maximum), u každého čas, vlhkost (tečka v barvě
  zalít / v pořádku / mokro), teplotu a baterii; značky jsou ikony s vysvětlivkou (💧 zalito, ✋ ruční zápis,
  ⚠️ chyba sondy, 🔋 slabá baterie, 🕒 čas nejistý, 💤 vybitá baterie),
- výběr místa (výchozí to, kde je sonda). Surový log je pod *Surový záznam (pro servis)* i s vysvětlením značek.
Stav (`/api/status`) nese navíc `fs.lmax` (od kolika bajtů se log zřeďuje).

**Verze 6.11: Přehled bez tlačítka „Změřit teď“.** Hodnota se při otevřené stránce obnovuje sama, pod měřákem je
„před … s · obnovuje se samo každých 15 s“. Jak často, jde nastavit v *Servis → Sonda a měření → Živá hodnota na stránce*
(5 s až 2 min, výchozí 15 s; jen při otevřené stránce, záznam do logu se nastavuje zvlášť). API `POST /api/measure` zůstává.

**Verze 6.10: ruční zápis (přenosný logger).** Sondu jde nosit od stromu ke stromu a měření zapisovat tlačítkem.
- Zapíná se v *Servis → Ruční zápis (přenosný logger)* (výchozí vypnuto). Tam se volí i poznámka k zápisu
  (automatické číslo „Bod 1, Bod 2, …“, vlastní text s předvyplněným dalším číslem, nebo nic) a jestli má každý zápis
  prodloužit stránku (aspoň 5 min do konce; s ručním zápisem smí stránka běžet až hodinu místo 30 min).
- Na *Přehledu* je pak karta *Ruční zápis* s tlačítkem **Zapsat měření** a posledními pěti zápisy. Zařízení změří znovu
  (medián ze 3 pulzů) a zapíše řádek se skutečným časem, vybraným místem, značkou `M` a poznámkou za ` # `.
- Ruční zápisy nejsou v grafech ani ve vyhodnocení zalití (každý je jiný bod). Jsou v *Záznamy → Ruční měření*
  a v exportu do Excelu (sloupce `rucne`, `poznamka`). Zředění logu je nemaže, přečíslování míst nečte poznámku.
- Pravidelné měření běží dál podle nastavení. Příkaz čeká, když se právě ukládá jiná volba (dřív se mohl zahodit).

**Verze 6.9: srozumitelnější Nastavení.** Pro koncové uživatele, všechno se ukládá samo (bez tlačítek *Uložit*):
- **Kalibrace místa „…“** je průvodce ve dvou krocích (1. půda je suchá → *Teď je sucho*, 2. po vydatném zalití →
  *Teď je mokro*). U každého kroku je vidět *hotovo* / *zatím ne* a nahoře jednou větou, jestli je místo zkalibrované.
  *Vrátit výchozí kalibraci* je jen nenápadný odkaz.
- **Kdy zalévat:** barevný pruh *Zalít / V pořádku / Mokro* se šipkou u aktuální vlhkosti a dvě pole *Hlásit „Zalít!“
  pod* a *Hlásit „Mokro“ nad*. Uloží se po opuštění pole; „Mokro“ pod „Zalít!“ se neuloží a stránka to řekne.
- **Jak často měřit:** výběr (10, 15, 30 min, 1 h, 2 h, neměřit) místo čísla; pod ním, kdy bude další záznam, a že
  častější měření zkracuje výdrž baterie. Dřív nastavená jiná hodnota (např. 45 min) se ve výběru objeví.
- **Hodiny:** jednou větou, jestli jdou správně; tlačítko *Nastavit čas z telefonu* je výrazné jen tehdy, když nejdou.
- *Doba běhu stránky* se přesunula do *Servis → Wi-Fi a zobrazení*.

**Verze 6.8: přehlednější Servis a záznamy smazaných míst.**
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

**Verze 6.7: přehlednější karta Místa.** *Nastavení → Místa* je seznam míst. U každého je vidět, jestli je zkalibrované,
práh zalévání a kolik má záznamů. Místo, kde je sonda, je zvýrazněné („Sonda je tady“); na jiné se sonda přesune
klepnutím. Přejmenovat a smazat jde přímo u místa. Nové místo má vlastní formulář s prázdným názvem
a vysvětlením, co se stane. Firmware jinak beze změny.

**Verze 6.6: bez lehkého spánku.** Pauzy při měření (vybití elektrod, mezera mezi ověřovacími pulzy) jsou zase obyčejné
čekání jako do v6.4. Lehký spánek z v6.5 šetřil jen zlomek procenta a na desce nebyl ověřený. Jinak stejné jako v6.5.

**Verze 6.5: úspora baterie.**
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

**Verze 6.4: oprava čtení za koncem pole.** Odpověď `/api/status` kopírovala výsledky hledání pinu BIAS pro tři kandidáty, ale od v6.1 jsou jen dva (GPIO32, GPIO26), takže se četla jedna položka za koncem pole. Na stránce se to neprojevilo (třetí hodnota se neposílala), formálně to ale bylo nedefinované chování. Jinak beze změny.

**Verze 6.3: pole s datem v Safari.** Pole Od/Do (zpráva do PDF, období na přehledu) na iPhonu přetékala a slévala se; teď drží šířku sloupce.

**Verze 6.2: bez zálohy celé sondy.** Karta *Záloha celé sondy* (v5.9) zmizela. Export a import míst s kalibrací
v *Servis → Místa a kalibrace* zůstává. API `/api/settings?vcorr=` (korekce napětí baterie) zůstává.

**Verze 6.1: méně voleb v zapojení.** Způsob měření: jen *krátký pulz* (výchozí) a *dlouhý pulz* pro starší sondy
(300 ms, jako původní firmware). Dřívější „dlouhý do ustálení“ se převede na dlouhý pulz. Druhá elektroda: jen *GND* nebo
*GPIO14* (uložené 18/19/23 se vrátí na GND). BIAS: *automaticky*, *GPIO32* nebo *GPIO26*. GPIO25 se už nikdy nebudí,
ani při hledání zapojení.

**Verze 6.0: období od–do a zpráva do PDF.**
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

**Verze 5.9: záloha celé sondy.** *Servis → Záloha celé sondy* stáhne `zaloha_<ID>_<datum>.json`: všechna místa
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

**Verze 5.8: export, import a úprava míst.** *Servis → Místa a kalibrace* ukazuje všechna místa najednou (název,
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

**Verze 5.7: kalibrace jen v Nastavení.** Karta „Sonda a kalibrace“ ze Servisu zmizela. Tlačítko *Výchozí kalibrace*
je v *Nastavení → Kalibrace* (s potvrzením, platí pro vybrané místo). Kalibrace „sonda ve vzduchu“ (naprázdno) zmizela úplně:
naprázdno je vždy plný rozsah ADC (4095), dřív uložené hodnoty se ignorují a API `which=open` vrací `400`. Detail sondy (ADC, g, odpor, fáze) a varování o chybě sondy
jsou v *Servis → Zapojení sondy*.

**Verze 5.6: upozornění na slabou baterii.**
- **LED při přiložení magnetu:** při 2,90–3,00 V nejdřív 3 pomalá bliknutí („dejte baterii dobít“), pak se web spustí.
  Pod 2,90 V platí dál 10 rychlých bliknutí a web se nespustí.
- **Záznam o hibernaci:** při přechodu do hibernace se do logu zapíše jeden řádek s napětím a značkou `H`
  (`--%`, `!`, `H`). V grafech Přehledu je od něj do dalšího záznamu červený pás „Vybitá baterie“. Pokud měření stále stojí, je nahoře
  červený pruh „Měření stojí od …“. Po obnovení ještě týden visí modrý pruh „Měření stálo … – … kvůli vybité baterii“.
  CSV má nový poslední sloupec `vybita_baterie`.
- Text kritického pruhu (pod 2,90 V) teď říká, co se stane: pod 2,7 V se měření zastaví, dokud se baterie nedobije.

**Verze 5.5: hibernace při vybitém článku.** Pod 2,70 V se zařízení přestane budit v intervalu záznamu: probudí se
jen jednou za 6 h, změří článek a spí dál (žádné měření sondy, žádný zápis, žádná Wi-Fi; magnet = 10 rychlých
bliknutí). Normální provoz se vrátí sám, až článek bude mít aspoň 3,00 V (hystereze), a plán záznamů začne od
aktuálního intervalu. Do hibernace vede i web, kterému napětí dvakrát po sobě klesne pod 2,70 V.

**Verze 5.4: způsob měření zpět v Servisu, pull-up pevně 10 kΩ.** *Servis → Zapojení sondy → Způsob měření*:
krátký pulz (výchozí), nebo pro starší desky se sondou pevně na GND dlouhý pulz do ustálení či pevných 300 ms
(hodnoty jako dřív). Volba se ukládá; po změně je potřeba znovu kalibrovat sucho a mokro. Ostatní parametry pulzu
zůstávají pevné. Nastavení pull-upu zmizelo: je vždy 10 kΩ (staré uložené hodnoty se ignorují a smažou).

**Verze 5.3: rozložení v Safari.** Bloky uvnitř karet (Upravit místa, prahy, Pokročilé, zapojení) mají mezery
okraji místo flexu, takže v Safari pole a tlačítka nesplývají. Výběry (select) vypadají ve všech prohlížečích stejně
(vlastní šipka, stejná výška jako pole). Teplota zmizela i ze zápatí.

**Verze 5.2: velmi mokrá zem není zkrat.** Chyba „zkrat“ se hlásí jen při čítači ADC **opravdu 0**. Dřív stačilo ≤ 8,
a hlavně kontrola „uzel nereaguje na BIAS“ chtěla nárůst aspoň 25 čítačů. Hned po vydatném zalití má dělič jen
~15 čítačů, takže se to hlásilo jako chyba. Teď stačí nárůst o polovinu hodnoty (u nízkých hodnot). Čítač 16 tak
znamená „extrémně mokro“ (100 %). Uzel, který na BIAS opravdu nereaguje (špatný pin, chybí pull-up), se pozná dál.

**Verze 5.1: pevné měření a čistší přehled.**
- **Měření nemá nastavení.** Vždy se měří krátkým pulzem (odečet 1 ms po sepnutí, medián druhé poloviny pulzu), dřív než
  se elektrody polarizují. Ostatní parametry pulzu mají výchozí hodnoty: strop 300 ms, tolerance 5 čítačů, vybití 30 ms,
  1 měření na záznam s ověřením skoku nad 150 čítačů. Karta „Měření (pro odborníky)“ a volba režimu měření
  v Pokročilých zmizely. API je nepřijímá, firmware staré uložené hodnoty ignoruje a při dalším uložení nastavení je smaže.
  Simulace sondy s polarizací (jako tvůj květináč) dává krátkým pulzem ohmickou hodnotu (~565 čítačů místo ~1100).
  Zůstává volba druhé elektrody (obrácená polarita), vyhodnocení zalití a průběh napětí v Servisu.
- **Přehled:** odpor půdy je jen malým šedým písmem v okénku měřáku pod procenty („∞ kΩ“ bez kontaktu se zemí).
  Teplota z přehledu zmizela: v zapnutém zařízení se čip zahřívá, takže živá hodnota nic neříká.
  Teplota ze záznamů (měřená po probuzení) zůstává v grafu Záznamů a v CSV.

**Verze 5.0: jednodušší web pro zákazníky, místa a Servis.**
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

**Verze 4.9: ručkový měřák a ikona na plochu.** Hodnota na přehledu je teď analogový přístroj jako polní vlhkoměr
(krémový ciferník, mosazný rámeček, stupnice 0–100 %, barevné pásmo sucho / optimum / mokro podle prahů z nastavení,
rudá ručka s tlumeným dokmitem; v tmavém režimu tmavý ciferník; bez hodnoty nebo při chybě sondy leží ručka na dorazu).
*Sdílet → Přidat na plochu* (iPhone) nebo *Přidat na plochu / Instalovat* (Android) teď dá ikonu měřáku a stránka se
otevírá bez lišty prohlížeče. Ikona je ve `web/icons/` (zdroj `icon.svg`, PNG vyrenderuje `tools/make_icons.py`).
Ikona na ploše otevře `http://10.10.10.10/`, takže funguje jen připojený k Wi-Fi zařízení.

**Verze 4.8: co se stane po připojení k Wi-Fi zařízení** (*Nastavení → Provoz → Po připojení k Wi-Fi*). Otevřít rovnou
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

**Verze 4.7: nová stránka nahrávání firmwaru** (`http://10.10.10.10/update`, odkaz *Aktualizace firmwaru* v Nastavení).
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

**Verze 4.6** přidává režim měření **Krátký pulz** (*Nastavení → Pokročilé → Režim měření*), délka 300–20 000 µs
v *Měření (pro odborníky)*, výchozí 1000 µs. Hodnota je medián druhé poloviny pulzu, tedy dřív, než se elektrody
polarizují. Důvod: první skutečný průběh z květináče (6. 11.) stoupal bez plató od ~560 čítačů (0,3 ms) po ~1150
(300 ms), tedy zdánlivý odpor zeminy z ~1,6 kΩ na ~3,9 kΩ jen tím, jak dlouho se měří. Automatický režim v takové zemině
hodnotu „ustálí“ na místě, které závisí na délce pulzu; krátký pulz měří odporovou část. Funguje i s obrácenou polaritou
(obě fáze krátké). Po přepnutí režimu je potřeba znovu kalibrovat sucho/mokro (hodnoty jsou nižší). Na desce ověřené není.

**Verze 4.5** přidává **samootevírání stránky** (captive portal): po připojení k Wi-Fi zařízení telefon sám nabídne
„Přihlásit se k síti“ a stránku otevře, nemusíte psát `10.10.10.10`. Zařízení k tomu odpovídá na všechny DNS dotazy svou
adresou a test internetu z telefonu (Apple, Android, Windows, Firefox) přesměruje na stránku. Zapnout jde v *Nastavení →
Provoz* (zaškrtávátko „Po připojení k Wi-Fi otevřít stránku samo“). **Od verze 4.6 je ve výchozím stavu vypnuté**
(i na zařízeních, kde ho 4.5 zapnula): na Macu okénko „Připojit se k síti“ překáží při nahrávání firmwaru. Dvě praktické věci: **firmware
nahrávejte v běžném prohlížeči** (okénko pro přihlášení k síti nemusí umět vybrat soubor) a **na iPhonu** po zavření okénka
zvolte „Používat bez internetu“, jinak se iPhone od sítě odpojí. Nouzový režim captive portal nemá (zůstává co
nejjednodušší, adresa `10.10.10.10` platí dál). Jak se chovají konkrétní telefony, jsem neověřoval (jen logiku
přesměrování na PC).

**Verze 4.4** přidává nastavení pro odborníky a vyhodnocení zalití:
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

**Verze 4.3** opravuje první problém zjištěný na desce: při měření se sondou ve vzduchu ukazovala stránka pořád
**100 %** vlhkosti (a při doteku plošky sonda–zem „0“ a zkrat) a kalibrace nešla. Nejpravděpodobnější příčina (z vašeho
popisu, na desce neověřeno): výchozí sestavení budí BIAS sondy na **GPIO32**, ale vaše deska ho má na **GPIO26**
(to je DAC2). Dělič pak nikdo nebudil, uzel na GPIO35 „visel“ a čítač četl náhodné nízké hodnoty, které se přepočetly na
100 %. Nově si zařízení **správný pin BIAS najde samo** (32, 26, případně 25) a uloží ho, a **chybu sondy ukáže jako
chybu** (*Chyba sondy*, `--`), ne jako 100 %; kalibraci v takovém stavu odmítne s vysvětlením. Podrobně v [1.5](#15-pin-bias-a-chyba-sondy-od-verze-43)
a v kapitole 3.

Verze 4.2 byla tatáž jako 4.1 (změnilo se jen číslo verze), změnil se jen způsob sestavení: 4.1 se nevešla do paměti
pro OTA, protože se přeložila bez optimalizace; 4.2 má platformu zafixovanou, překládá s `-Os` a po sestavení vypíše
velikost (viz 1.1). U vás se 4.2 přeložila, nahrála přes OTA a stránka běží.

> **Poctivě o ověření.** V prostředí, kde jsem pracoval, nebylo možné firmware přeložit pro
> ESP32 ani ho spustit na desce (chybí toolchain PlatformIO a přístup na GitHub). Proto je
> ověřený *kód logiky* (měření, hledání pinu BIAS, plán záznamů, formát logu, JSON, parsování, rozhodování ochrany
> před zabrickováním; přes tisíc kontrol na PC), *webová stránka* (v prohlížeči proti simulovanému
> zařízení) a *syntaxe a typy* všech zdrojáků proti skutečným hlavičkám knihoven.
> **Překlad, slinkování a nahrání verze 4.2 u vás proběhly a stránka běží** (to je z vašeho hlášení). **Verzi 4.3 jsem
> pro ESP32 nepřeložil a automatické hledání pinu BIAS i zobrazení chyby sondy jsem zkoušel jen na simulované desce
> (virtuální dělič s pull-upem), ne na skutečné.** Měření samo na skutečné sondě také ověřené není. Berte to jako
> zkoušku; viz [„Co ověřeno není“](#8-co-ověřeno-není).

---

## 1. Nahrání přes OTA (bez kabelu)

**Přečtěte celé, než poprvé nahrajete.** Zařízení nemá sériový převodník, takže když se nový
firmware pokazí, nejde ho „přeflashovat kabelem“. Proto se nové verze nahrávají tak, aby se vždy
dalo vrátit.

### 1.1 Jak získat `firmware.bin`

**Hotový `.bin` k balíčku nepřikládám, nemohl jsem ho vyrobit.** V prostředí, kde jsem pracoval, nešel
stáhnout překladač pro ESP32 (PlatformIO, Espressif i stahování z GitHubu jsou tam zablokované), takže
jsem firmware nepřeložil. `firmware.bin` si vyrobíte na svém počítači, kde už PlatformIO máte (verze 3 se tam
sestavila), jedním příkazem:

```
cd Arbolog_4
pio run -t clean            # jen když jste v téže složce už sestavovali (verze 4.1): smaže staré, neoptimalizované objekty
pio run                     # výchozí prostředí esp32doit-devkit-v1
```

Totéž jde z VS Code: otevřít složku `Arbolog_4` a v dolní liště PlatformIO kliknout na ✓ *Build*.
První sestavení stáhne knihovny uvedené v `platformio.ini`, takže potřebuje internet; trvá minuty.

**Platforma a velikost.** `platformio.ini` pevně určuje platformu (`espressif32@8.0.0`) a zapíná `-Os`. Bez toho se
verze 4.1 přeložila *bez jakékoli optimalizace* (podle map z vašeho sestavení i podle hlášení jiných projektů
platforma 8.0.0 žádnou sama nepřidá) a vyšla 1 342 719 B, tedy přes oddíl
pro OTA (1 310 720 B); `pio run` skončilo chybou `program size ... is greater than maximum allowed`. Na konci sestavení
(a ještě před kontrolou velikosti od PlatformIO, takže i při takové chybě) se vypíše blok **VELIKOST FIRMWARU**:
kolik firmware zabírá, kolik zbývá do limitu OTA (1 306 624 B), z čeho se skládá a jestli se kód nepřeložil bez
optimalizace. Totéž jde kdykoli ručně: `python3 tools/size_report.py` (čte `.pio/build/esp32doit-devkit-v1/firmware.map`).
Očekávám zhruba 1,14 až 1,18 MB (87 až 90 % oddílu); je to **odhad** z map sestavení (kapitola 8), skutečné číslo
ukáže první sestavení.

Hotový soubor je

```
.pio/build/esp32doit-devkit-v1/firmware.bin
```

(ne `firmware.elf`, ne `bootloader.bin`, `partitions.bin` ani `littlefs.bin`). Můžete ho přejmenovat
(třeba `v4_9.bin`, jako máte `v3_1s.bin`): stránka `/update` čte jen obsah, název se nepoužije. Přípona
musí zůstat `.bin`, jinak ho okno pro výběr souboru na stránce nemusí nabídnout.

**Kontrola před nahráním.** Na konci sestavení skript soubor sám zkontroluje a vypíše výsledek mezi dvěma
řádky `====`. Stejnou kontrolu lze kdykoli spustit ručně na libovolném souboru:

```
python3 tools/check_bin.py .pio/build/esp32doit-devkit-v1/firmware.bin
```

Ověří, že jde o platný obraz aplikace pro ESP32 (hlavička, kontrolní součet, SHA-256, čip), že se vejde
do toho, co OTA stránka přijme (**1 306 624 B**: slot 1 310 720 B minus 4 kB; verze 3 měla 1 139 504 B, tj. 87 %),
a že je to firmware verze 4 s nouzovým režimem (ne `littlefs.bin`, ne stará verze 3). Při výsledku
**`NENAHRÁVEJTE`** soubor nenahrávejte a pošlete mi výpis (kapitola 9). Řádek *POZOR* nemusí znamenat chybu
(např. u `v3_1s.bin` hlásí, že to není verze 4), ale přečtěte si ho. Kontrola vznikla a ověřila se na
skutečném obrazu z vašeho sestavení verze 3 i na poškozených a useknutých souborech, na samotném firmwaru v4
ale zatím poběží poprvé u vás.

Další poznámky k sestavení:

- **Pin BIAS sondy.** Výchozí prostředí (`esp32doit-devkit-v1`) předpokládá BIAS na **GPIO32**, jako verze 3 (v jejím
  kódu bylo `SONDA_BIAS_PIN 32 //stare desky`); `pio run -e nova-deska` předpokládá **GPIO26** (DAC2, nová deska).
  Od verze 4.3 je to jen **výchozí hodnota**: zařízení si správný pin (32, 26, případně 25) najde samo při prvním
  spuštění webu a uloží ho (viz 1.5), takže na volbě prostředí nezáleží. Verze 4.2 hledání neměla a na desce s BIAS
  na GPIO26 nic neměřila.
- Souborový systém nahrávat **nemusíte** (`uploadfs` se nepoužívá). Nastavení (NVS) i log ve flash
  zůstanou a nastavení z verze 3 (doba webu, interval, jazyk) se převezme.
- **Chyba při sestavení nic nezabrickuje**: bez `firmware.bin` se prostě nic nenahraje. Skončí-li `pio run`
  chybou, pošlete mi výpis.
- Přesáhne-li firmware oddíl pro aplikaci (1 310 720 B), PlatformIO sestavení samo ukončí chybou
  `program size ... is greater than maximum allowed` a žádný `firmware.bin` nevznikne (stalo se u verze 4.1).
  Kontrola `check_bin.py` hlídá ještě o 4 kB přísnější limit, který přijme OTA stránka (1 306 624 B). Blok
  *VELIKOST FIRMWARU* se vypíše i při takové chybě.
- Volitelně Wi-Fi heslo: v `platformio.ini` odkomentujte `-DAP_PASSWORD=\"vasheslo\"` (8 až 63 znaků).
  Stejné heslo pak chrání `/update` (uživatel `admin`) i nouzový režim. Bez něj je síť otevřená jako ve v3.
  Kratší nebo delší heslo sestavení **zastaví** chybou (`AP_PASSWORD musi mit 8 az 63 znaku`): zařízení by s ním
  nikdy nespustilo Wi-Fi, ani nouzový režim, a bez kabelu by se nedalo opravit.
- Dosah Wi-Fi: výkon vysílače je 15 dBm (kvůli špičkám proudu při napájení přímo z článku; verze 3 měla ~19,5 dBm).
  Je-li u stromu dosah malý, zvyšte `WIFI_TX_POWER_DBM_ENUM` v `src/config.h` (např. `WIFI_POWER_19_5dBm`)
  a nahrajte znovu (názvy hodnot jsou v `WiFiGeneric.h` jádra Arduino-ESP32). Platí i pro nouzový režim.
- Platforma i knihovny jsou v `platformio.ini` pevně dané (API ESPAsyncWebServer se mezi verzemi mění; platforma bez
  uvedené verze se při čisté instalaci změní na nejnovější, což verzi 4.1 přivedlo k překladu bez optimalizace).
  `espressif32@8.0.0` = Arduino-ESP32 3.3.12 / ESP-IDF 5.5.5; s ní se verze 4.1 u vás přeložila a slinkovala.
  Verze 3 se podle verzí balíčků v její mapě sestavila s jinou platformou (Arduino-ESP32 3.2.0 / ESP-IDF 5.4.1,
  pravděpodobně pioarduino 54.03.20). Kdyby se s 8.0.0 něco chovalo jinak, je v `platformio.ini` zakomentovaný plán B
  se shodným prostředím (**nezkoušeno**; stáhne jiné balíčky, takže potřebuje internet).
- Ladění po sériové lince (jen když převodník máte): `pio run -e ladeni -t upload && pio device monitor`.

### 1.2 Nahrání

1. **Uschovejte si `v3_1s.bin`** (z vašeho původního projektu, `.pio/build/esp32doit-devkit-v1/`).
   Nahraje se stejným postupem a vrátí zařízení na verzi 3. *Nemažte `chart.js` a `style.css`
   v souborovém systému, dokud se k verzi 3 můžete vracet: její stránka je potřebuje.*
2. Článek by měl být aspoň z poloviny nabitý (podle indikace baterie na stránce). Při nahrávání se
   vysílá Wi-Fi a zapisuje flash a slabý článek by nahrávání přerušil. Přerušení nic neničí, jen se musí opakovat.
3. Přiložte magnet k relé, připojte se na Wi-Fi `ARBOL0K-<číslo>` a otevřete **`http://10.10.10.10/update`**
   (nejlépe z počítače; nabídne-li telefon „přihlášení k síti“, otevřete raději běžný prohlížeč, nahrávání
   souboru v okénku pro přihlášení někdy nefunguje). **Pospěšte si:** web verze 3 se po nastavené době
   (výchozí 4 min, *Doba běhu webserveru* v jejím nastavení) vypne i uprostřed nahrávání a samotné nahrávání
   trvá desítky sekund. Soubor mějte připravený předem a začněte hned po připojení.
4. Stránka má jediný formulář **Firmware**: vyberte soubor `firmware.bin` (ten, který kontrola z 1.1 uznala)
   a stiskněte *Update Firmware*.
5. Po úspěšném nahrání se zařízení samo restartuje (do ~15 s, Wi-Fi na chvíli zmizí) a naběhne webový
   režim jako po zapnutí.
6. Připojte se znovu a **otevřete `http://10.10.10.10/`**; v zápatí stránky zkontrolujte **verzi `v6.16`**.
   **Stránku otevřít musíte a web nechte běžet aspoň minutu**: dokud se firmware nepotvrdí, je „zkušební“
   (viz 1.3, bod 2). Při prvním otevření uvidíte v zápatí `v6.16 (trial)`; po potvrzení (web běží 45 s a stránka byla otevřená)
   přípona po obnovení stránky zmizí. Nikdo-li se nepřipojí, firmware se nepotvrdí a **zařízení se po skončení webové
   relace samo vrátí na verzi 3**. Zařízení do potvrzení neodpojujte (odpojení baterie také vrátí verzi 3).
   Další nahrání firmwaru ve zkušební době nevadí: firmware se před ním sám potvrdí.

**Aktualizace z 4.2–4.8 na 4.9** (zařízení už běží na verzi 4). Postup je stejný: sestavit, zkontrolovat, na
`http://10.10.10.10/update` nahrát `firmware.bin`. Nastavení, kalibrace a log zůstávají; chybějící uložený pin BIAS se
při prvním startu webu najde sám (1.5). Dvě věci se liší od prvního nahrání: (1) záložní firmware, na který se
zařízení při nepotvrzení vrátí, je teď ta, která v zařízení běží (4.2 až 4.8), ne verze 3 (zařízení má jen dva oddíly pro aplikaci a nahrání nové verze přepíše
ten, kde zůstala verze 3; soubor `v3_1s.bin` v počítači ale zůstává a nahrát ji znovu jde kdykoli, dokud běží verze 4
nebo nouzový režim); (2) v 4.2 už stránka a
nouzový režim fungují, takže při potížích platí postup z 1.3 a 1.4 beze změny.

Po neúspěšném nahrávání (přerušené Wi-Fi, chyba knihovny) zůstává v zařízení původní firmware.
Knihovna OTA ve verzi 3 po neúspěšném pokusu další pokus nepřijme, dokud se zařízení nerestartuje.
Odpojte a znovu připojte baterii **bez magnetu u relé** (magnet při připojení baterie spouští nouzový režim)
a zkuste to znovu. Ve verzi 4 je to ošetřené, restart (*Nastavení → Pokročilé → Restart*) ale nikdy neuškodí.

### 1.3 Jak je zajištěno, že se zařízení nezabrickuje

Vrstvy jsou na sobě nezávislé; každá chytá jiný typ závady.

1. **Přepnutí až po ověření.** Nový firmware se zapisuje do *druhého* oddílu aplikace a spouštěný oddíl se
   přepne až po zapsání a ověření celého obrazu. Přerušené Wi-Fi, vybitá baterie nebo vadný soubor
   uprostřed nahrávání = dál běží původní firmware.
2. **Zkušební provoz + návrat bootloaderu.** Po nahrání je nový firmware „nepotvrzený“. Potvrdí se až když
   web běží aspoň 45 s **a stránka obsloužila aspoň jeden požadavek** (někdo ji otevřel). Dál ho potvrdí
   *Restart* nebo *Tovární nastavení* ve stránce a zahájení dalšího nahrávání firmwaru (aby nové nahrávání
   nepřepsalo předchozí, jediný ověřený obraz), nebo čistý konec relace, ve které stránka někoho obsloužila.
   Resetuje-li se zařízení před potvrzením (pád, odpojení baterie), bootloader spustí znovu **předchozí firmware**.
   Nepotvrzený firmware se navíc **neuloží do hlubokého spánku**: místo toho se restartuje, aby se předchozí firmware
   (verze 3 držení pinů z hlubokého spánku nepouští) spustil čistě, a ne s piny zadrženými v LOW. Nikdo-li se po
   nahrání nepřipojí (nebo je slabá baterie, nejde Wi-Fi), zařízení se tedy po skončení relace samo vrátí na
   předchozí verzi. Pozor: to platí, *je-li v bootloaderu podpora návratu*. Z map sestavení verze 3 se to
   dá usuzovat (jádro odkazuje na funkce pro rollback), ale **neověřeno**; nebude-li podpora, tato vrstva
   se prostě nepoužije a ostatní fungují dál. Důsledek v praxi: **po nahrání se připojte, otevřete stránku a nechte
   web běžet aspoň minutu**; když se objeví zase stará stránka z verze 3, nový firmware se nepotvrdil a vrátil se (viz 1.4).
3. **Počítání pádů → nouzový režim.** Počítadlo pádů v paměti RTC (přežije pád i restart, ne odpojení baterie)
   spustí **nouzový režim** po třech pádech v řadě, nebo hned po jediném pádu v prvních 45 s webu (tam je člověk
   u zařízení). Nouzový režim (`safemode.cpp`) je jen Wi-Fi + OTA stránka. Nepoužívá nastavení, sondu, baterii,
   hodiny, souborový systém ani běžnou webovou stránku: kterákoli z těch částí může být právě ta rozbitá.
   Počítadlo si pamatuje, který obraz ho zapsal: pády nového firmwaru se nepřenesou na předchozí firmware,
   na který se bootloader po neúspěchu vrátí (ten by jinak skončil v nouzovém režimu kvůli cizí chybě).
4. **Hlídací časovač.** Visí-li hlavní smyčka nebo start déle než 60–90 s (zaseknutá komunikace, zacyklení),
   zařízení se záměrně shodí a restartuje a pád se započítá do bodu 3. Zaseknutý program tak nevybíjí baterii
   do ztracena a dá se opravit.
5. **Nouzový režim magnetem.** *Odpojte baterii → přiložte magnet k relé a držte ho → připojte baterii →
   držte magnet ještě 2 s.* LED svítí a po ~2 s zhasne; pak dvojitě bliká (nouzový režim). Magnet můžete sundat.
   Funguje u potvrzeného firmwaru i když vypadá zdravě a stačí k opravě i k návratu na `v3_1s.bin`. U právě
   nahraného, ještě nepotvrzeného firmwaru ne: odpojení baterie by ho nejdřív vrátilo na verzi 3 (počkejte
   tedy na potvrzení, bod 2). Reaguje jen na *připojení baterie* (ne na restart po OTA ani na běžné přiložení magnetu).
6. **Ochrana baterie.** Pokud padá i nouzový režim (6 pádů v řadě), nebo se nouzový režim spustí po podpětí,
   Wi-Fi se nezapne: zařízení zabliká (6×, resp. 3×) a spí; probudí ho jen magnet. Po 6 pádech magnet jen zopakuje
   6 bliknutí a pomůže až odpojení a opětovné připojení baterie (viz 1.4); po podpětí v nouzovém režimu (3 bliknutí)
   magnet nouzový režim znovu spustí, je-li článek už lepší. V běžném režimu po podpětí se Wi-Fi také nezapne
   (3 bliknutí), zařízení zaloguje a spí dál podle plánu.

**Nouzový režim:** Wi-Fi `ARBOL0K-SAFE-<číslo>` (stejné heslo jako běžný režim, je-li nastavené),
stránka `http://10.10.10.10/` s odkazem *Nahrát firmware (OTA)* (→ `/update`) a tlačítkem *Zkusit normální režim*.
Dokud běží, zařízení neměří a nezapisuje log.
Po 10 min bez připojeného telefonu usne (běžící aktualizaci tím nepřeruší), po 60 min vždy; spí pak bez časovače
a probudí ho jen magnet. Spuštěný po pádech trvá, dokud se neodpojí baterie, nenahraje se firmware, nebo nestisknete
*Zkusit normální režim* (do té doby se záznam neobnoví). Spuštěný magnetem platí jen jednou: po spánku stačí
přiložit magnet a zařízení nastartuje normálně (záznam se po skončení webu obnoví).

### 1.4 Když se něco pokazí

| Co vidíte | Co to znamená | Co dělat |
|-----------|---------------|----------|
| Po nahrání se otevře zase stará stránka z verze 3 | Nový firmware se do potvrzení neprokázal (nikdo neotevřel stránku do konce relace, padl, nestihl 45 s, nepodařilo se spustit Wi-Fi nebo byla slabá baterie) a vrátila se verze 3. Zařízení měří dál jako dřív. | Nabít článek, nahrát znovu, hned se připojit, otevřít stránku a nechat web běžet minutu. Opakuje-li se to, pošlete mi popis (kapitola 9). |
| V zápatí je `v6.16 (trial)` | Zkušební provoz po nahrání: firmware se ještě nepotvrdil | Nechte stránku otevřenou aspoň minutu, přípona zmizí. Neodpojujte baterii. |
| Stránka ukazuje *Chyba sondy* a `--` místo procent | Uzel sondy je ≈ 0 V (zkrat elektrod), nebo nereaguje na buzení BIAS (špatný pin BIAS, chybí pull-up) | *Servis → Zapojení sondy → Najít zapojení* (1.5). Elektrody nesmí být při hledání ve zkratu. |
| Sonda ve vzduchu ukazuje 0 % a hlášku „naprázdno“ | To je správně: čítač je na stropu (~4095), sonda nic nevede | Žádná chyba. Po zapíchnutí do zeminy se hodnota změní. |
| Kalibrace skončí hláškou „Sonda je ve zkratu nebo nereaguje na buzení“ | Chyba sondy (viz výše), nic se neuložilo | Nejdřív opravit zapojení (1.5), pak kalibrovat. |
| Wi-Fi `ARBOL0K-<č>` se po přiložení magnetu neukáže | Slabá baterie (10 rychlých bliknutí), magnet mimo relé, nebo běží nouzový režim | Zkuste magnet držet ~2 s; hledejte i `ARBOL0K-SAFE-<č>`; zkontrolujte baterii. |
| Síť `ARBOL0K-SAFE-<č>` | Nouzový režim (opakované pády / zaseknutí / magnet při připojení baterie) | Připojit se, `http://10.10.10.10/` → *Nahrát firmware* → opravený `firmware.bin`, nebo `v3_1s.bin`. |
| Chcete nouzový režim hned | – | Postup z 1.3, bod 5. |
| 6 rychlých bliknutí a ticho | Nouzový režim padá dokola, Wi-Fi se nezapíná (ochrana baterie) | Odpojit baterii na pár sekund, připojit **s magnetem** (nouzový režim) a nahrát opravu / `v3_1s.bin`. |
| 3 bliknutí hned po startu | Restart kvůli podpětí (slabá baterie) | Nabít nebo vyměnit článek. |
| LED svítí hned po připojení baterie a po 2 s zhasne | Relé bylo sepnuté: spouští se nouzový režim | Chtěli jste? Pokračujte. Ne? Odpojte baterii, magnet dejte pryč, připojte znovu. |
| Sestavení skončí `program size ... is greater than maximum allowed` | Firmware je větší než oddíl pro aplikaci (1 310 720 B); u verze 4.1 to způsobil překlad bez optimalizace | Zkontrolujte, že `platformio.ini` obsahuje `platform = espressif32@8.0.0` a `-Os` v `build_flags` (neměnit). Spusťte `python3 tools/size_report.py` a pošlete výpis a `firmware.map` (kapitola 9). Nic se tím nezabrickuje: bez `firmware.bin` se nic nenahraje. |
| Nahrávání skončí chybou | Zůstává původní firmware | Restart (viz 1.2), zkontrolovat velikost souboru a baterii, zkusit znovu. Ve verzi 3 hlídejte čas: její web se po nastavené době vypne i uprostřed nahrávání. |
| Zařízení nereaguje vůbec | Prázdný článek, nebo zaseknutý program | Odpojit baterii aspoň na 10 s a připojit. Nepomůže-li to, připojit s magnetem (nouzový režim) a nahrát `v3_1s.bin`. |

### 1.5 Pin BIAS a „Chyba sondy“ (od verze 4.3)

Měření funguje jen tehdy, když zařízení budí ten pin, na kterém je skutečně BIAS sondy (konec pull-upu 10 kΩ):
stará deska **GPIO32**, nová **GPIO26** (to je DAC2 procesoru). Při špatném pinu dělič nikdo nebudí, uzel na GPIO35
„visí“ ve vzduchu a čítač čte náhodné nízké hodnoty. Verze 4.2 z toho ukázala **100 %**, a když jste se prstem dotkl
plošky sonda–zem, čítač spadl k nule (0, „zkrat“). Verze 4.3:

1. **Najde pin sama.** Při prvním spuštění webu (dokud pin není uložený) zkusí kandidáty **32** a **26** (**25** jen
   tehdy, když ani jeden jasně nezabere): u každého krátce (25 ms) nastaví BIAS dolů a nahoru a změří čítač uzlu.
   Pin, kterým se uzel pohne aspoň o 80 čítačů a zároveň aspoň třikrát víc než kterýmkoli jiným, je ten pravý. Uloží se
   do paměti zařízení (NVS, klíč `bpin`) a od té chvíle ho používá i měření do logu při probuzení ze spánku (a drží se
   v LOW i při spánku). Pozná se i s elektrodami ve vzduchu nebo úplně bez připájené sondy: stačí, aby byl na desce
   pull-up 10 kΩ mezi BIAS a uzlem. **Nepozná se při zkratu** na elektrodách (uzel je pořád ≈ 0 V).
2. **Dá se opakovat i nastavit ručně:** *Nastavení → Zapojení sondy*. Tlačítko *Najít zapojení* zkusí všechny tři kandidáty
   a ukáže u každého čítač uzlu při BIAS dole a nahoře (u pravého pinu je rozdíl stovky až tisíce, u ostatních skoro nula).
   Pin jde zvolit i ručně (GPIO32 / GPIO26); volba *Automaticky* uložený pin zapomene a hned hledá znovu.
   Reagují-li dva piny podobně silně (např. jsou na desce propojeny), nepřepíná se: ponechá se právě používaný pin,
   nebo to zařízení ohlásí jako nejednoznačné a pin zvolíte ručně.
3. **Chyba sondy místo 100 %.** Nepůsobí-li BIAS na uzel (čítač se po zapnutí nezměnil, ani když není ≈ 0), nebo je uzel
   ≈ 0 V (zkrat, nebo pin BIAS dělič vůbec nebudí), stránka místo procent ukáže `--` a *Chyba sondy*; ADC hodnota je
   dál vidět. V logu je `--%`, kalibrace se odmítne s vysvětlením (`err:short`) a v modrém pruhu je tlačítko, které vede
   přímo na kartu *Zapojení sondy*.

**Co čekat při správném pinu a sondě ve vzduchu (nebo bez připájené sondy):** čítač kolem 4095, příznak „naprázdno“,
vlhkost 0 %. To není chyba, jen sonda nic nevede. Dotek prstem mezi plošky sonda a zem čítač **jen trochu sníží**
(suchá kůže má stovky kΩ, vlhká desítky kΩ); k „zkratu“ (≈ 0) dojde jen při skutečném zkratu.

**Zkouška bez sondy, na kterou stačí rezistor.** Mezi plošku sondy a GND zapojte rezistor (nebo na chvíli přidržte vývody)
a na stránce *Změřit teď* sledujte čítač `raw`. Orientačně (ADC ESP32 není přesné a nad ~3,1 V přetéká): 1 kΩ ≈ 350 až 450,
10 kΩ ≈ 2000 až 2200, 100 kΩ ≈ 3700 až 4000, bez rezistoru ≈ 4095. Jde o to, aby hodnota rostla s odporem a rezistor 1 kΩ
nedal „zkrat“. Ukáže-li se tohle, měřicí řetěz (BIAS, pull-up, ADC) funguje a zbývá jen zemina.

Sonda je odporový dělič: `BIAS ─ R(10 kΩ) ─ uzel(ADC) ─ zemina Rp ─ GND`, čítač
`raw ≈ 4095 · Rp / (R + Rp)`. Mokrá zemina má malý odpor (nízký čítač), suchá velký (vysoký čítač).
S pull-upem 10 kΩ je čítač nejcitlivější pro zeminu zhruba 1–100 kΩ (nižší pull-up měl podle vás
pro zeminu „strašnou citlivost“, což dává smysl: při `Rp ≫ R` je uzel těsně u napětí buzení a rozdíly mezi
zeminami se ztrácejí).

V květináči je substrát stejnorodý, kyprý a drží vodu, takže odpor `Rp` se pohybuje v úzkém
rozsahu a pár nastavených hodnot „zafunguje“. V zemi se sešlo víc věcí najednou. Verze 3 s nimi
nepočítala.

> Bez měření na vašem zařízení nejde s jistotou říct, která příčina převažuje. Tabulka je zhruba seřazená
> podle pravděpodobnosti a diagnostika v kapitole 4 (průběh napětí, příznaky měření) pomůže to rozlišit.

| # | Příčina | Co dělala verze 3 | Co dělá verze 4 |
|---|---------|-------------------|-----------------|
| 1 | **Odpor zeminy se mění o řády** (řádově od jednotek kΩ ve vlhké půdě po stovky kΩ a víc v suchém písku) a pevný pull-up „vidí“ dobře jen úzký pás kolem své hodnoty. Mimo něj se čítač lepí na dno nebo strop ADC a rozlišení mizí. | Lineární převod `100 − raw/4095·100`, žádná kalibrace. | Procenta se počítají z **vodivosti** `g = (open − raw)/raw` (≈ `R/Rp`, s výhradou pod tabulkou) na **logaritmické** škále mezi dvěma kalibračními body **vaší** zeminy (sucho, mokro). Navíc kalibrace „naprázdno“. |
| 2 | **Polarizace elektrod.** Stejnosměrné napětí na nerezu v elektrolytu nabíjí dvojvrstvu a napětí na uzlu se po sepnutí plazí ještě stovky ms. V zemi (jiné ionty, jiný kontakt) víc než v květináči. | Pevně čeká 300 ms a pak bere 60 vzorků: výsledek závisí na tom, jak daleko je právě drift. | Hledá **plató**: okna se zdvojující délky, hotovo, když se mediány dvou oken shodují (v dobré půdě za milisekundy až desítky ms). Před pulzem krátké **vybití elektrod**. Doba ustálení se ukládá do logu (`sNN`); nenalezené plató se označí (`u`). Měření se tím zároveň zkrátí a elektrody jsou pod napětím méně. |
| 3 | **Napájení přímo z LiFePO4 a Wi-Fi.** Článek má vyšší vnitřní odpor, špičky Wi-Fi stahují napětí a ADC čte hůř; ESP32 bez stabilizátoru je u 3,0 V na spodní hranici. | „Živé“ měření každých 5 s při zapnuté Wi-Fi, plný výkon vysílače. | První měření po zapnutí je **před** startem Wi-Fi; vysílací výkon je snížen na 15 dBm; prahy baterie (viz kapitola 3); měření se vyhodnocuje na šum (`FLAG_NOISY`) a při podezřelé hodnotě se ověří dalšími pulzy. |
| 4 | **Buzení sondy kolísá s baterií.** BIAS dává napětí článku (3,0–3,5 V) a ADC ESP32 má *pevnou* referenci, takže čítač pro stejnou zeminu kolísá podle nabití zhruba o 13 % (3,0 → 3,4 V) a z něj odvozená vodivost ještě víc. | Nepočítáno. | **Normalizace čítače na napětí 3,3 V** (`vexc`, ve výchozím stavu zapnutá). Plně nasycený čítač se nepřepočítává (sonda ve vzduchu / velmi suchá zem). |
| 5 | **Dolní konec rozsahu ADC.** Při plném článku (3,3–3,5 V) přeteče ADC u velmi suché zeminy (zhruba nad 100–150 kΩ) strop ~3,1 V a čítač je plný. | Žádná kontrola. | Příznak „naprázdno“ a nasycený čítač se neškáluje; při kalibraci „sucho“ se bod uloží na spodní mez `g = 0,05`, aby se logaritmická škála zbytečně neroztáhla. |
| 6 | **Špatný kontakt** (kameny, kořeny, vzduchové štěrbiny, zemina se po vyschnutí odtrhne od elektrod). | Jedna hodnota bez ověření. | Při skokové změně nebo šumu se měření **ověří třemi pulzy (medián)**. Příznaky ve stavu: zkrat / naprázdno / šum / neustáleno (v logu značka `u` u neustáleného měření). |
| 7 | **Vodivost závisí i na soli a teplotě**, nejen na vodě. | Nepočítáno. | Kalibrace na místní půdě řeší sůl. Teplota z DS3231 je teplota **elektroniky**, ne půdy, proto se na kompenzaci nepoužívá (viz doporučení k hardwaru). |
| 8 | **Procenta bez návaznosti na realitu.** Prahy typu „zalít při 30 %“ nemají s konkrétní zeminou nic společného. | Pevná škála. | 0 % = „sucho“ a 100 % = „mokro“, jak je u **té** zeminy nastavíte. V logu zůstává i čítač `rNNNN`, takže jde procenta kdykoli přepočítat s jinou kalibrací. |

**Důležité:** v logu v4 je za měřením vždy i čítač `r…`. Je to hodnota **po normalizaci** na buzení 3,3 V
(je-li `vexc` zapnuto, výchozí), ne holý údaj z ADC; holý údaj ukazuje jen živé měření v API (`m.rawRaw`).
Kdyby se kalibrace ukázala špatná, nic se neztratí; z logu jde vlhkost dopočítat zpětně se stejnou normalizací.

**Výhrada k modelu (`g ≈ R/Rp`):** vztah platí přesně jen tehdy, když je uzel v lineárním rozsahu ADC a hodnota
„naprázdno“ není oříznutá. Při článku nad ~3,1 V je „naprázdno“ oříznuté (čítač 4095 odpovídá nejvýš ~3,1 V, kdežto
sonda ve vzduchu dává na uzlu ~3,3 V), takže `g` vychází u vyšších odporů **menší, než by mělo**. Podle jednoduchého
modelu (předpoklady: ADC nasycuje při ~3,1 V, buzení 3,3 V; je to výpočet, ne měření) je odpor zeminy ukázaný na
stránce asi o třetinu vyšší při 30 kΩ a zhruba trojnásobný při 100 kΩ. **Procenta tím netrpí**: stupnice zůstává
monotónní a kalibrace sucho/mokro ji natáhne na vaši zeminu. Číslo v kΩ na stránce ber jako orientační.

---

## 3. Co se změnilo (shrnutí)

### Měření
- Adaptivní hledání plató místo pevných 300 ms (režim *Pevných 300 ms (jako v3)* zůstal k volbě v *Pokročilé → Režim měření*).
- Index vodivosti + logaritmická škála; dvoubodová kalibrace sucho/mokro + naprázdno; lineární škála z v3 je k dispozici (`scale=0`; s verzí 3 se shoduje jen při vypnuté volbě *Korigovat podle napětí baterie* (`vexc`), jinak počítá z čítače přepočteného na 3,3 V).
- Pull-up 10 kΩ a normalizace na napětí článku jsou výchozí (`rpull = 10`, `vexc` zapnuto); stránka podle `rpull` ukazuje odhad odporu zeminy.
- Jeden pulz na probuzení; při šumu nebo skoku větším než 150 čítačů se přidají 3 pulzy a vezme se medián (žádné zpoždění mezi probuzeními, na rozdíl od klouzavého filtru).
- Diagnostika: tlačítko „Zachytit průběh“ ukáže křivku napětí po sepnutí. Plochá křivka = ustálené; plazí se = polarizace.
- Baterie se měří první; při slabém článku se měření nespouští (viz níže).
- **Pin BIAS se pozná za běhu** (od 4.3, viz 1.5): kandidáti GPIO32 a GPIO26 (GPIO25 od v6.1 ne); nalezený pin se uloží a použije
  i ve spánku. Výchozí hodnotu dál určuje prostředí PlatformIO (`ARBOLOG_BOARD`), ale už není nutné ji trefit.
- **Chyba sondy se nevydává za vlhkost** (od 4.3): před každým pulzem se změří výchozí hodnota uzlu (medián z 5 čtení při BIAS dole)
  a po sepnutí se vyžaduje odezva aspoň 25 čítačů. Uzel ≈ 0 (zkrat, nebo BIAS dělič nebudí) nebo bez odezvy na buzení =
  příznak chyby: vlhkost se nepočítá (`--` na stránce, `--%` v logu) a kalibrace se odmítne.

### Plánování záznamů a spánek
- Záznamy v **pravidelných hranicích intervalu** podle DS3231 (např. 17:00, 17:30…), bez ujíždění. Časovač deep sleep (RC oscilátor ESP32 se uhýbá až o ±10 %): od v6.5 jedno probuzení na záznam, chyba časovače se učí z DS3231 a další spánek se o ni opraví (do v6.4 se spalo po etapách 7/8 zbytku, 3–4 probuzení na záznam).
- Žádný zdvojený ani zbytečný záznam po startu, změně intervalu nebo nastavení času (plán se zarovná na začátek aktuálního intervalu).
- Záznam běží i při zapnutém webu (časovač spánku v tu chvíli neběží). Když web skončí těsně před hranicí intervalu, záznam, který je právě na řadě, se ještě zapíše (neposune se o celou periodu).
- Při vybité záložní baterii hodin (RTC přišlo o napájení) se čas označí jako nespolehlivý (`?` v logu).
- Zaseknuté jazýčkové relé (magnet nechán u zařízení) nezpůsobí nekonečné probouzení: záznamy běží dál podle časovače, po odtažení magnetu se zařízení jen krátce probudí a znovu nastaví na přiložení. Web se spustí až při dalším přiložení magnetu.

### Napájení a spolehlivost
- Před spánkem se výstupy (BIAS sondy, napájení děličů, LED) **zadrží v LOW** (`rtc_gpio_hold`), aby nic neodebíralo a sonda nebyla pod napětím. Po probuzení se zámky uvolní jako první.
- Pull-up jazýčkového relé se drží i ve spánku (RTC pull-up).
- Rozpoznání příčiny startu: zapnutí / magnet / časovač / pád / **podpětí (brownout)**. Po pádu nebo podpětí se Wi-Fi nespouští, zařízení jen zaloguje a spí (přerušení smyčky restartů).
- Prahy baterie: pod 3,00 V upozornění v UI, pod 2,90 V se Wi-Fi vůbec nezapne, pod 2,70 V se přestane i logovat (ochrana článku).
- Procesor na 80 MHz u krátkých probuzení i u webu (do v6.4 web 160 MHz); **Wi-Fi výkon 15 dBm** (verze 3 vysílala ~19,5 dBm: nižší proudové špičky při napájení přímo z článku; nastavuje se před startem AP i znovu po něm, protože start AP může výkon vrátit na výchozí), `WiFi.persistent(false)` (žádné zápisy do flash při každém startu).
- GPIO12 (strapping pin, spíná napájení děliče baterie) se před spánkem zadrží v LOW, takže ho při probuzení a resetu čip čte ve správné úrovni; během měření baterie je HIGH jen krátce.
- **Ochrana před zabrickováním** při nahrávání jen přes OTA: zkušební provoz nového firmwaru (potvrdí se až po otevření stránky), nouzový režim, hlídací časovač, nouzový režim magnetem, počítadlo pádů zvlášť pro každý firmware, a heslo Wi-Fi, které se nedá zadat špatně (sestavení zastaví chyba, kapitola 1).

### Log
- Formát je zpětně kompatibilní s v3 (prvních pět sloupců stejně), přibyly `rNNNN`, `sNN[u]`, `?`, `!`. Podrobně v [docs/API.md](docs/API.md).
- Zápis je chráněn zámkem; při zaplnění se **starší polovina zředí** (každý druhý záznam), takže celá sezóna zůstane v grafu a soubor nepřeroste.
- Smazání/nahrazení logu nikdy neproběhne pod rukama probíhajícímu stahování. Po dobu stahování se log ani **nezřeďuje** (zředění se odloží na další zápis).
- Web na zámek logu čeká nejvýš 0,5 s; když zrovna běží zředění velkého logu (vteřiny), odpoví „zaneprázdněné“ (`busy`) a stránka si načtení po chvíli zopakuje. Komunikace se tím nezablokuje.
- Log, který ještě nevznikl (záznam vypnutý) nebo byl smazán, je pro stránku prostě prázdný (žádná chyba).
- Málo volného místa kvůli **jiným** nahraným souborům nezpůsobí, že se malý log zbytečně přepisuje a řídne: zřeďuje se jen log nad 64 kB, jinak se zapisuje, dokud se řádek vejde. Nouzové zkrácení vždy uvolní aspoň polovinu logu.

### Web
- Jedna vložená stránka (gzip, 35 kB) bez závislosti na souborech ve flash; **nemusíte nahrávat souborový systém**. Starší `chart.js` a `style.css` z verze 3 už nejsou potřeba (verze 4 je nepoužívá; smazáním na stránce *Soubory* se uvolní zhruba 210 kB flash pro log). Nechte je jen tehdy, pokud se chcete vracet k verzi 3 (`v3_1s.bin`): její stránka je potřebuje (viz 1.2). Po nahrání 4.3 se zařízení na verzi 3 samo už nevrátí, vedla by k ní jen nová nahrání `v3_1s.bin`; nehodláte-li to, smazat je můžete.
- Grafy vlhkosti, teploty a baterie, živá hodnota, kalibrace, nastavení, průběh napětí, textové záznamy, soubory, jazyky čeština/angličtina/ukrajinština, světlý/tmavý režim, funguje i na úzkém displeji.
- Změny provádějí **jen POST** příkazy (ve v3 šlo mazat log nebo restartovat odkazem). Každý POST ověřuje `Origin` (cizí stránka otevřená v telefonu zařízením nehne).
- Webové handlery nesahají na hardware (měření, I²C, NVS dělá jen hlavní smyčka); stav čtou z kopie. Ve v3 se z asynchronního tasku přistupovalo k RTC (I²C) souběžně s měřením.
- Nahrávání souborů jde do dočasného souboru a po úspěšném přijetí celého se **atomicky** přejmenuje. Přerušené nahrávání už nezničí log. Soubor se přijme jen tehdy, když po něm zůstane místo pro zápis logu (48 kB + 75 % logu + 8 kB), jinak by se log pak neustále zbytečně zřeďoval.
- Aktualizace firmwaru `http://10.10.10.10/update` (OTA), jediný formulář *Firmware*. Během ní zařízení neusne. Požadavek na `/update` bez parametru `name` (např. `curl -F` bez `?name=`) knihovna sama nezvládá a zařízení by restartovala, proto ho odmítá předřazený handler s `400`.

---

## 4. První uvedení do provozu a kalibrace (důležité!)

Bez kalibrace na místní zemině se použijí výchozí body (`g_dry = 0,05`, `g_wet = 3,0`, naprázdno 4095),
které jsou jen hrubý odhad. **Kalibrace je to, co zařízení v zemi opraví.**

1. Přiložte magnet k jazýčkovému relé (LED svítí), připojte se na Wi-Fi `ARBOL0K-<číslo>`, otevřete `http://10.10.10.10/`.
2. **Nastavení → Hodiny → Nastavit čas z telefonu.** Bez toho jsou záznamy označené `?`.
   **Nastavení → Zapojení sondy:** má tam být použitý pin BIAS (u vás nejspíš GPIO26); není-li potvrzený, stiskněte
   *Najít zapojení* (1.5). Dokud stránka hlásí *Chyba sondy*, kalibrace se odmítne.
3. **Naprázdno:** vyndejte sondu a nechte ji čistou a suchou ve vzduchu → *Sonda ve vzduchu*.
   Hodnota musí vyjít nad ~2500, jinak zařízení ohlásí chybu (sonda asi není ve vzduchu nebo je zkrat).
   Máte-li sondu jen jako nepřipájené plošky, jsou naprázdno také; vyjde ~4095.
4. **Sucho:** zapíchněte sondu **tam, kde bude stát**, do zeminy ve stavu, kdy už by strom potřeboval zalít
   → *Je sucho → 0 %*. (Nebo do vzorku suché zeminy z toho místa, ale ve stejném zhutnění.)
   Je-li zemina tak suchá, že čítač přeteče (příznak „naprázdno“), je to v pořádku: bod se uloží na spodní mez.
5. **Mokro:** půdu pořádně zalijte, počkejte, až voda vsákne (hodiny) → *Je mokro → 100 %*.
6. Prahy „zalít“ a „mokro“ v Nastavení jsou v procentech mezi těmito body. Dávejte je s rezervou,
   např. zalít pod 30 %, mokro nad 85 %.
7. **Kalibrace baterie** (*Pokročilé*): změřte článek multimetrem a zadejte hodnotu → *Zkalibrovat podle multimetru*. Zařízení dopočítá korekci děliče.
8. Interval záznamu (1–120 min) nastavte podle potřeby; výchozí 30 min (0 = záznam vypnutý).

Pokud vyjde chyba „Kalibrace není platná“, je bod „sucho“ vodivější než bod „mokro“. Zopakujte měření
nebo nejdřív proveďte *Výchozí kalibrace*. Chyba „Měření je nespolehlivé“ znamená silný šum. Chyba „Sonda je ve zkratu
nebo nereaguje na buzení“ (od 4.3) znamená chybu sondy: zkrat, nebo pin BIAS dělič nebudí; viz 1.5.

### Doporučený postup diagnostiky v zemi
- Pusťte *Zachytit průběh* v květináči i v zemi a porovnejte křivky. Pokud se napětí v zemi plazí
  stovky ms bez plató, jde pravděpodobně o polarizaci. Pomůže vybití elektrod a režim *Automatický*; trvalé řešení je střídavé buzení (viz hardware).
- Hodnoty napevno kolem 4095 (příznak „naprázdno“) znamenají buď špatný kontakt se zeminou, nebo velmi suchou
  zeminu (ADC přeteče). Velmi nízké (≈ 0, příznak zkratu) znamenají zkrat, nebo (od 4.3) že BIAS dělič nebudí; v obou
  případech stránka ukáže *Chyba sondy* a procenta nepočítá. Ověřit řetěz BIAS → pull-up → ADC jde rezistorem bez sondy (1.5).
- Stránka podle nastaveného pull-upu (kΩ, výchozí 10) ukazuje odhad odporu zeminy.

---

## 5. Kódy LED

| Signál | Význam |
|--------|--------|
| Svítí od startu, pak zhasne | Start webového režimu (Wi-Fi se spouští) |
| Krátký záblesk každých ~5 s | Web běží (srdeční tep) |
| 3 pomalá bliknutí, pak start webu | Slabý článek (2,90–3,00 V): dejte baterii dobít |
| 10 rychlých bliknutí | Slabý článek (< 2,90 V), Wi-Fi se nezapne, zařízení jde spát. Pod 2,70 V hibernace: probouzí se jen jednou za 6 h, dokud článek nemá aspoň 3,00 V |
| 5 dlouhých bliknutí | Nepodařilo se spustit Wi-Fi AP |
| 2 bliknutí hned po startu | Restart po pádu programu |
| 3 bliknutí hned po startu | Restart kvůli podpětí (brownout) |
| Svítí ~2 s hned po připojení baterie | Relé je sepnuté: následuje nouzový režim |
| Dvojité bliknutí každé 2 s | **Nouzový režim** (OTA) |
| 6 rychlých bliknutí a ticho | Nouzový režim padá dokola, Wi-Fi se nezapíná (ochrana baterie) |

---

## 6. Hardware

Známá fakta (z vašeho popisu, ne z měření): pull-up sondy **10 kΩ** (nižší hodnoty měly pro zeminu příliš
malou citlivost), ESP32 běží **přímo z LiFePO4** (3,0–3,5 V, bez stabilizátoru), firmware se nahrává **jen přes OTA**.
Software s tím počítá (výchozí hodnoty, normalizace čítače, prahy baterie, výkon Wi-Fi, ochrana při nahrávání).

Doporučení (nepovinná):

- **Kondenzátor 100 nF** z uzlu ADC na GND zklidní šum. Časová konstanta bude `τ = (R‖Rp)·C`;
  adaptivní měření to zvládne (plató se jen hledá později).
- **Střídavé buzení (obrácená polarita): od verze 4.4 ho firmware umí (volitelně), zapojení je na vás.**
  Postup: druhou elektrodu odpojit od GND a přes odpor 100–470 Ω připojit na GPIO14 (jediná volba od v6.1); pak na stránce
  *Nastavení → Zapojení sondy → Druhá elektroda* zvolit ten pin a uložit. V detailu sondy (*Sonda a kalibrace*) uvidíte
  fázi (přímá / obrácená / poměr obou) a čítač fáze B. Když elektroda na zvoleném pinu není, objeví se varování a měří se
  dál jen přímou polaritou. Pozor obráceně: přepojíte-li elektrodu, ale v nastavení necháte GND, sonda „visí“ a ukazuje
  naprázdno (0 %). Na skutečné desce to ověřené není (jen simulace).
  Dnes je jeden pól napevno na zemi a na druhém je stejnosměrné napětí (krátké pulzy).
  - **Co by to obnášelo:** druhou elektrodu nezapojit na GND, ale na volný GPIO přes **malý odpor 100 až 470 Ω** (např. 220 Ω:
    chrání pin před výboji a zkratem kabelu; víc by se přičítalo k odporu mokré zeminy) a měřit dvakrát za sebou,
    s opačnou polaritou, a průměrovat. **Žádnou diodu** do cesty elektrody nedávat: propouští proud jen jedním směrem (zruší
    obrácení polarity) a přidává úbytek 0,2 až 0,7 V závislý na teplotě a proudu, který by měření zkreslil. Tak to dělají i dataloggery: manuál Campbell CR510 (kap. 13.5, můj volný překlad) uvádí, že obrácená
    polarita slouží k depolarizaci iontů a že stejnosměrné buzení může způsobit polarizaci a tím chybné měření, posunout
    kalibraci sondy nebo vést k jejímu rychlému rozpadu. Kmitočet kHz nepotřebujete: stačí dva krátké pulzy s opačnou polaritou (desítky ms
    každý). Skutečné střídání stovek Hz až kHz jde taky, ale je složitější (vzorkování ADC; kondenzátor na uzlu by pak směl mít
    jen jednotky nF, ne 100 nF výše).
  - **Který pin:** firmware 4.3 nepoužívá GPIO 0, 2, 5, 14, 15, 16, 17, 18, 19, 23 (a 36, 39 jen vstupy). Nejvhodnější je
    **GPIO14** (umí držení stavu ve spánku a není strapping pin; při startu na něm bývá chvilku signál PWM, což přes odpor elektrodě nevadí); dál 18, 19, 23 (16 a 17 jen u modulu bez PSRAM, tedy ne
    WROVER). Nepoužívat 0, 2, 5, 12, 15 (strapping: ovlivňují start), 25/26/32 (kandidáti na BIAS) ani piny z `config.h`.
    Co je u vás fyzicky vyvedené, z kódu nevím.
  - **Co z toho čekat:** (1) **širší rozsah na suché straně**: při opačné polaritě je uzel u suché zeminy (desítky až stovky
    kΩ) uprostřed rozsahu ADC, kdežto dnes se u zeminy nad zhruba 30 kΩ čítač blíží stropu a ADC je tam nelineární; při
    pull-upu 10 kΩ a lineárním okně ADC přibližně 0,15 až 2,45 V (11 dB) vychází dnešní rozsah asi 0,5 až 29 kΩ, s oběma
    polaritami asi 0,5 až 210 kΩ (**výpočet, ne měření**); software by pro každé měření vzal polaritu, kde je čítač
    uprostřed; (2) opakovatelnější měření (menší polarizace); (3) sondu jde mezi měřeními úplně odpojit (vysoká
    impedance), takže jí neteče ani náhodný proud ze země. **Ne kvůli korozi:** při našem krátkém pulzování
    vychází podle hrubého výpočtu (Faradayův zákon, všechen náboj by rozpouštěl železo) úbytek kovu od setin miligramu ročně
    (pulz ~12 ms, záznam po 30 min) po jednotky miligramů (pesimisticky: tři pulzy po 300 ms každých 10 min), kdežto elektroda
    má několik gramů. Důlkovou korozi nerezu v solné půdě z toho spočítat neumím, po sezóně elektrody prohlédněte.
  - **Nedělejte to naslepo:** po přepojení druhé elektrody z GND na GPIO by firmware 4.3 neměřil vůbec nic (režim v něm
    není). Napřed je potřeba firmware s tímto režimem (volitelným, výchozí vypnutý) a hlavně data ze *Zachytit průběh* v zemi:
    plochá křivka = polarizace vám nevadí a obrácení polarity nepomůže; plazí se stovky ms = pomůže.
- **BIAS na pinu DAC (GPIO26 = DAC2, GPIO25 = DAC1) — dá se to využít?** Software dnes řídí BIAS jako obyčejný digitální
  výstup (HIGH/LOW); DAC se nepoužívá. Co by DAC přinesl a co ne:
  - **Ano:** nižší, volitelné buzecí napětí (třeba 1 až 1,5 V místo 3,3 V). Elektrodami by tekl menší proud (menší
    polarizace) a uzel by u velmi suché zeminy nenarážel do stropu ADC (~3,1 V), takže by „naprázdno“ nebylo oříznuté;
    šlo by měřit i při dvou napětích a z poměru poznat polarizaci nebo špatný kontakt.
  - **Ne:** DAC je jednopólový (0 až Vdd, 8 bitů, krok ~13 mV) a druhá elektroda je napevno na zemi, takže **pravé střídavé
    buzení (obrácení polarity) s ním nejde**; k tomu je potřeba druhý GPIO a změna zapojení. Výstup DAC je navíc
    **úměrný napájení** (ne pevná reference), takže s článkem 3,0 až 3,5 V se mění i on.
  - **Co by obnášelo:** DAC musí být před spánkem výslovně vypnutý (jinak by do sondy tekl stejnosměrný proud), a kalibraci
    „naprázdno“ (dnes vyžaduje čítač ≥ 2500) by bylo nutné přepsat, protože při nižším buzení nebude uzel ve vzduchu
    na stropu ADC.
  - **Stav:** ve verzi 4.3 **není zapojeno**. S vaším problémem (špatný pin, 100 %) DAC nesouvisí a digitální buzení je
    jednodušší (stejné buzení používala i verze 3, která v květináči fungovala). Přidat by šlo **volitelný, výslovně zkušební režim buzení přes DAC** (výchozí zůstane digitální).
- **Teplotní čidlo v zemi** (DS18B20/NTC): kompenzace vodivosti (≈ 2 %/°C) a detekce zmrznutí
  (zmrzlá zem má velký odpor = falešné „sucho“). Teplota z DS3231 je teplota desky.
- **Umístění:** elektrody svisle, v kořenové zóně, s dobrým kontaktem (zeminu kolem sondy zhutnit),
  mimo přímý kořen a kámen. Jedna sonda hlídá jen svůj kousek, pro velké stromy ideálně dvě v různé hloubce.
- **Napájení:** LiFePO4 má plochou křivku a ESP32 bez stabilizátoru je citlivé na poklesy (špičky Wi-Fi).
  Kondenzátor (stovky µF) u napájení ESP32 pomáhá. Software proto snižuje výkon Wi-Fi a hlídá prahy baterie.
- **Záložní vstup pro převodník:** kdyby se dal vyvést TX/RX/GND/GPIO0 na pájecí plošky nebo konektor, bude
  příště nahrávání kabelem jako poslední záchrana k dispozici. Software ho nepotřebuje.

---

## 7. Složky a testy

```
src/            firmware (main, webui, probe, wiring, power, logstore, settings, battery, rtcutil, bootguard, guard, safemode …)
src/web_assets.h  vložená stránka (generuje se z web/, před sestavením to dělá tools/pio_pre.py)
web/            zdroje stránky (index.html, app.css, app.js, plot.js, i18n.js)
docs/API.md     popis HTTP API a formátu logu
tests_host/     testy jádra na PC (g++), atrapy pro kontrolu syntaxe
tests_web/      simulátor zařízení + zkouška stránky v Chromiu (Playwright)
tools/          generátor webu, kontroly, skripty testů, check_bin.py (kontrola .bin před nahráním), size_report.py (velikost z linkerové mapy), pio_post.py (spouští obojí při sestavení)
```

| Co | Příkaz | Co to ověří |
|----|--------|-------------|
| Jádro a web logika | `bash tools/run_host_tests.sh` | měření, plató, kalibrace (včetně nasycení ADC), formát a zředění logu, plánovač, JSON, parsování, načítání a ukládání nastavení včetně převzetí z v3, zápis, zředění, plný disk, souběh se stahováním a zámek logu a obnova po výpadku napájení nad paměťovou atrapou LittleFS, konec webové relace těsně před hranicí intervalu, skutečná měřicí smyčka `probe.cpp` nad virtuálními hodinami a simulovanou sondou (i s deskou, na které BIAS visí na jiném pinu, a s plovoucím uzlem), **hledání pinu BIAS** (`wiring.h`: rozhodování i `probeWiringTest` na simulované desce, uložení pinu do NVS, příznaky chyby sondy), `battery.cpp` nad atrapou ADC, **rozhodování ochrany před zabrickováním** (`bootguard.h`: scénáře pádů, nouzový režim, magnet, časovače nouzového režimu; testy se ověřovaly i záměrným rozbitím kódu) (s AddressSanitizer/UBSan), kontrola souborů před OTA (`tools/check_bin.py`: syntetické obrazy, poškozené a useknuté soubory, hranice velikosti; volitelně `ARBOLOG_REAL_BIN=<soubor>` i na skutečném obrazu), výpis velikosti z linkerové mapy (`tools/size_report.py` a jeho zapojení do PlatformIO; volitelně `ARBOLOG_REAL_MAP=<mapa>` s `ARBOLOG_REAL_MAP_SIZE=<bajty z PlatformIO>` i na skutečné mapě). `ARBOLOG_OPT=-Os bash tools/run_host_tests.sh` spustí testy jádra s optimalizací, s jakou se překládá firmware |
| Stránka v prohlížeči | `python3 tests_web/ui_test.py` | grafy, přepínání jazyků, kalibrace, nastavení, soubory, výpadek spojení, úzký displej, žádné chyby v konzoli |
| Přeložitelnost zdrojáků | `LIBDEPS=<cesta>/.pio/libdeps/esp32doit-devkit-v1 bash tools/syntax_check.sh` | syntaxe a typy všech `src/*.cpp` proti skutečným hlavičkám knihoven (zbytek jádra jsou atrapy) |

Po úpravě souboru ve `web/` stačí `python3 tools/gen_web_assets.py` (nebo prostě `pio run`).

---

## 8. Co ověřeno není

- **Obrácená polarita a nové parametry (4.4) na skutečné desce.** Měřicí logika obou fází, výběr fáze, kontrola nepřepojené
  elektrody a odpojení sondy mezi měřeními jsou otestované na simulované desce (včetně záměrného rozbití kódu); lineární okno
  ADC (150 až 3700 čítačů) a rozsah „asi do 210 kΩ“ jsou výpočet z typických vlastností ADC ESP32, ne měření. Detekce zalití
  je otestovaná na simulovaném logu; jak dobře sedí na vaši zeminu, ukáže až provoz (proto jsou meze nastavitelné).
- **Hledání pinu BIAS (4.3) na skutečné desce.** Rozhodovací logika (`wiring.h`) i celý test zapojení (`probeWiringTest`)
  jsem zkoušel jen na **simulované desce** (virtuální dělič 10 kΩ + zemina, plovoucí uzel, tři možné piny, zkrat, šum
  a přeslechy), ne na skutečné. Stojí na předpokladech, které z vašeho popisu neumím ověřit: že mezi BIAS a uzlem na GPIO35
  je pull-up 10 kΩ (bez něj uzel BIAS nenásleduje a hledání nic nenajde, uvidíte *Žádný pin nereaguje*), že je BIAS
  na GPIO32, 26 nebo 25 (GPIO26 jsem odvodil z vaší poznámky o DAC), a že se uzel při BIAS nahoře pohne aspoň o 80 čítačů
  (ve vzduchu skočí z ~0 na ~4095). Mez 80 čítačů a převaha 3× jsou odhad (šum a přeslechy na ESP32 bývají do ~20 čítačů),
  ne měření. Při neúspěchu zůstane použitý výchozí pin a stránka ukáže *Chyba sondy*; nic se tím nezabrickuje (hledání
  jen krátce přepíná GPIO32/26/25 jako výstup a po sobě je vrací do vstupu, kromě používaného pinu).
- **Překlad a běh na ESP32.** Syntaktická kontrola proti atrapám jádra Arduino-ESP32 chytí překlepy
  a nesoulad typů proti knihovnám, ale není to překladač pro Xtensa. Verze 4.1 se u vás s platformou 8.0.0
  přeložila i slinkovala bez chyby; **verze 4.2 (s `-Os`) se přeložila, nahrála a stránka běží** (to je z vašeho
  hlášení). **Verzi 4.3 jsem pro Xtensa nepřekládal.** Optimalizace může odhalit chyby, které se bez ní neprojeví
  (nepřiřazené proměnné a podobně); testy na PC proto běží i s `-Os` (`ARBOLOG_OPT=-Os bash tools/run_host_tests.sh`), ale to není
  překladač pro Xtensa. Pokud `pio run` ohlásí chybu, pošlete mi její výpis, opravím ji. (Chyba sestavení nemůže nic
  zabrickovat: bez `firmware.bin` se prostě nic nenahraje.)
- **Velikost firmwaru.** Verze 4.2 se vešla (jinak byste ji nenahrál), její přesné číslo ale neznám. Verze 4.3 je
  větší o odhadem 4 až 8 kB (hledání pinu, karta ve stránce, překlady); ukáže to blok *VELIKOST FIRMWARU* po sestavení.
  Pokud by sestavení ohlásilo, že se firmware nevejde, pošlete mi výpis a mapu (kapitola 9). Co vím z map (verze 3: 1 138 990 B,
  verze 4.1: 1 342 719 B; limit OTA stránky je 1 306 624 B): verze 4.1 se přeložila bez optimalizace (v mapě je 153
  nevložených pomocných šablon `std::move`/`std::forward`, ve verzi 3 žádná) a kód přeložený u vás měl 329 kB, kdežto
  ve verzi 3 (s `-Os`) 113 kB. Hotové knihovny ESP-IDF mají 744 kB (ve verzi 3 707 kB, novější IDF), knihovny překladače
  60 kB. S `-Os` čekám, že se kód z vašeho překladu zmenší na 130 až 155 kB (stejné knihovny měly ve verzi 3 asi 99 kB,
  moje zdrojáky, ~80 kB bez optimalizace, odhaduji na 30 až 45 kB), celkem tedy zhruba **1,14 až 1,18 MB (87 až 90 %
  oddílu, rezerva zhruba 125 až 165 kB)**. To je **odhad** z poměrů v obou mapách, ne měření. Rezervu ukáže blok *VELIKOST
  FIRMWARU*. Rozšířit oddíl by šlo jen novou tabulkou oddílů přes kabel, ne přes OTA.
- **Platforma 8.0.0 je čerstvá (říjen 2026).** Kromě překladu bez optimalizace je u ní hlášená chyba: desky s krystalem
  26 MHz mají špatné hodiny (nejede sériová linka ani Wi-Fi); moduly ESP32-WROOM / DOIT DevKit mají krystal 40 MHz
  a to se netýká. Nový firmware navíc projde zkušebním provozem (1.3, bod 2): kdyby u vás Wi-Fi z jakéhokoli důvodu
  nenaběhla, nepotvrdí se a zařízení se vrátí na verzi 3. Plán B (`platformio.ini`) je pro případ, že byste potřeboval
  stejné prostředí jako ve verzi 3.
- **Návrat na předchozí firmware (rollback) v bootloaderu.** Že je v bootloaderu zapnutý, usuzuji z map
  sestavení verze 3, neověřil jsem to. Funkce `verifyRollbackLater()` a `esp_ota_mark_app_valid_cancel_rollback()`
  odpovídají dokumentaci Arduino-ESP32 / ESP-IDF, ale na zařízení se nezkoušely. Bez podpory se tato vrstva
  neuplatní, ostatní (nouzový režim, hlídací časovač, magnet) fungují nezávisle.
- **Nouzový režim, hlídací časovač, paměť RTC_NOINIT na skutečné desce.** Rozhodovací logika je otestovaná na PC,
  použití API ESP-IDF (`RTC_NOINIT_ATTR`, `esp_timer`, `abort()`) odpovídá dokumentaci, ale nezkoušelo se.
  Vyzkoušejte ho hned po prvním nahrání (kapitola 1.3, bod 5), dokud je zařízení po ruce.
- **Co software bez kabelu nezachrání:** firmware, který by se po zkušební době potvrdil a přitom měl rozbité Wi-Fi
  i nouzový režim; a bootloader bez podpory návratu spolu s chybou, která zastaví start ještě před `setup()`.
  Obojí je nepravděpodobné, ale nemožné to není. Proto jsou kontroly při prvním nahrání (verze `v6.16` v zápatí,
  vyzkoušený nouzový režim, uschovaný `v3_1s.bin`).
- **Chování spánku, relé a Wi-Fi na skutečné desce** (zadržení pinů, RTC pull-up, probuzení časovačem,
  dosah AP, spotřeba, nižší výkon Wi-Fi). Logika je ošetřená podle dokumentace ESP-IDF, ale vyzkoušet to musíte vy.
- **Analogová část:** pull-up 10 kΩ a napájení z LiFePO4 jsou převzaté z vašeho popisu. Hranice nasycení ADC
  (~3,1 V při útlumu 11 dB) a tím i odpor zeminy, od kterého čítač přeteče (~100–150 kΩ), jsou odhad
  z dokumentace; ADC ESP32 má navíc nelinearitu u okrajů rozsahu. Kalibrace *sucho/mokro* převede případné
  (monotónní) zkreslení stupnice na procenta vaší zeminy, ale **neopraví odhad odporu v kΩ**: při článku nad ~3,1 V
  je „naprázdno“ oříznuté, takže `g` je u vysokých odporů podhodnocené (výhrada v kapitole 2; +30 % při 30 kΩ
  a zhruba 3× při 100 kΩ jsou výsledky modelu, ne měření). Model jsem na skutečné sondě neověřoval.
- **Procenta ve v4 nejsou stejná jako ve v3** (jiná škála). Starší záznamy zůstávají v logu, ale nejsou
  s novými přímo srovnatelné. Chcete-li staré chování, zvolte v *Pokročilé → Stupnice %* možnost *Lineární (jako v3)*
  a vypněte *Korigovat podle napětí baterie* (`vexc`); se zapnutou korekcí se lineární stupnice počítá z čítače přepočteného na 3,3 V a od v3 se mírně liší.
- **Živá hodnota při zapnuté Wi-Fi se může lišit od záznamu při vypnuté Wi-Fi** (jiný šum a napětí).
  To je očekávané, v logu je měření bez Wi-Fi.
- **Wi-Fi AP je ve výchozím stavu otevřené** (jako ve v3); heslo se nastavuje při sestavení (kapitola 1.1).
- Stejnosměrné buzení polarizuje elektrody. Software to omezuje (krátké pulzy, vybíjení, hledání plató); úplné řešení
  je obrácená polarita (hardware, viz kapitola 6). Koroze při krátkých pulzech vychází podle hrubého výpočtu zanedbatelně
  (kapitola 6), jde ale o výpočet, ne o měření: po sezóně elektrody prohlédněte.
- Teplota z DS3231 není teplota půdy.

---

## 9. Co mi poslat zpátky, abych to mohl doladit

1. Výpis `pio run`, pokud sestavení skončí chybou (i pár řádků kolem ní, a hlavně řádky `PLATFORM:` a `PACKAGES:` na začátku), blok *VELIKOST FIRMWARU* (nebo výstup `python3 tools/size_report.py`) a blok *KONTROLA PŘED NAHRÁNÍM PŘES OTA* z konce úspěšného sestavení (nebo výstup `python3 tools/check_bin.py <soubor>`). Nevejde-li se firmware, přiložte i `.pio/build/esp32doit-devkit-v1/firmware.map` (zabalený do zipu; má kolem 20 MB).
2. Jak dopadlo nahrání: objevila se po minutě nová stránka s verzí `v6.16`, nebo se vrátila předchozí verze? Jestli šel vyzkoušet nouzový režim magnetem.
3. **Zapojení sondy (od 4.3):** po *Nastavení → Zapojení sondy → Najít zapojení* pošlete snímek karty (nebo opište řádky
   GPIO32 / GPIO26 / GPIO25 s čítači „dole / nahoře“ a výsledek) a které prostředí (`esp32doit-devkit-v1` / `nova-deska`)
   jste sestavil. Přidejte čítač `raw` z hlavní stránky *Změřit teď* pro: elektrody ve vzduchu, rezistor 10 kΩ mezi sondou
   a GND a dotek prstem (1.5). Z toho poznám, jestli je chyba v pinu, v pull-upu, nebo v ADC.
4. Snímek obrazovky *Zachytit průběh* z květináče a ze země (kapitola 4): z tvaru křivky poznám, jestli jde o polarizaci, kontakt, nebo pull-up.
5. Úsek `log.txt` po několika dnech v zemi (soubor jde stáhnout na stránce *Záznamy*) a odhad, kdy se zalévalo.
