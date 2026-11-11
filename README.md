# ARBOLOK – logger vlhkosti půdy

**Verze 7.0-rc1** (kandidát na vydání) · firmware pro ESP32 + webová aplikace v zařízení · historie změn: [CHANGELOG.md](CHANGELOG.md) ·
HTTP API a formát logu: [docs/API.md](docs/API.md)

ARBOLOK hlídá vlhkost půdy u stromů. Sonda se dvěma nerezovými elektrodami je zapíchnutá v zemi, zařízení většinu
času spí a v nastaveném intervalu (výchozí 30 min) se probudí, změří vlhkost, teplotu a napětí baterie a zapíše řádek
do záznamů. Přiložením magnetu se zapne Wi-Fi a v telefonu se otevře webová aplikace: grafy, vyhodnocení zalévání,
kalibrace, nastavení a export dat. Nic se neinstaluje, žádný internet ani cloud není potřeba.

---

## 1. Jak se zařízení používá

1. **Přiložte magnet** k místu s jazýčkovým relé. LED se rozsvítí a po chvíli zhasne; pak každých ~5 s krátce blikne
   (web běží).
2. V telefonu se **připojte k Wi-Fi `ARBOLOK-<číslo>`**. Síť je otevřená, pokud v Servisu nenastavíte heslo
   (kapitola 4.5). Telefon může ohlásit „bez internetu“, to je v pořádku.
3. Otevřete **`http://10.10.10.10/`** (nebo `http://arbolok.local/`). Podle nastavení se stránka může otevřít i sama
   (kapitola 4.5).
4. Web běží nastavenou dobu (výchozí 4 min; lišta dole ukazuje zbývající čas, tlačítkem *+5 min* se prodlouží).
   Skončí dřív, když se do 90 s nikdo nepřipojí nebo když je telefon odpojený déle než minutu. Pak zařízení
   vypne Wi-Fi a spí dál; záznamy běží podle plánu i během webu.

Stránku jde přidat na plochu telefonu („Přidat na plochu“), pak se otevírá jako aplikace s ikonou.
Jazyky: čeština, angličtina, ukrajinština. Světlý i tmavý režim podle telefonu.

---

## 2. Webová aplikace

Aplikace má čtyři karty. Všechny volby se ukládají samy hned po změně (kromě hesla Wi-Fi).

### 2.1 Přehled

- **Měřák vlhkosti** v procentech mezi kalibrovaným „suchem“ (0 %) a „mokrem“ (100 %), se stavem *Zalít!* /
  *V pořádku* / *Mokro* podle prahů místa. Hodnota se při otevřené stránce sama obnovuje (výchozí každých 15 s).
- **Ruční zápis** (je-li zapnutý v Servisu): tlačítko *Zapsat měření* pro obcházení stromů s jednou sondou.
- **Zalévání:** kdy se naposledy zalévalo, počet zalití v období, nejdelší doba bez zalití, min / průměr / max
  vlhkosti a doba pod prahem zalévání. Zalití se pozná automaticky jako rychlý nárůst vlhkosti (kapitola 4.3).
- **Grafy** vlhkosti s teplotou a napětí baterie za 24 h, 7 dní, 14 dní, 30 dní, celé období nebo vlastní rozsah od–do.
  Zalití jsou v grafu modré pásy.
- Upozornění: slabá baterie, nenastavené nebo rozjeté hodiny (tlačítko *Nastavit z telefonu*), chyba sondy,
  šetřící režim, období, kdy měření stálo kvůli vybité baterii.

### 2.2 Záznamy

- **Zpráva do PDF** za vybrané místo a období: souhrn, grafy, seznam zalití, tabulka po dnech. Otevře se tisk
  prohlížeče, v něm *Uložit jako PDF*.
- **Ruční měření:** seznam ručních zápisů s poznámkami.
- **Záznamy po dnech** pro vybrané místo; **Stáhnout pro Excel** (CSV) a **Stáhnout .txt** (čitelný text s jednotkami).
- **Surový záznam** (pro servis): obsah `log.txt` tak, jak je uložený v zařízení.

### 2.3 Nastavení

- **Místa:** zařízení rozlišuje až 8 míst (stromů / záhonů). Každé má vlastní kalibraci a prahy. Přepnutím místa
  se další měření zapisují k němu; grafy a zprávy ukazují vybrané místo.
- **Kalibrace** vybraného místa: *Teď je sucho* a *Teď je mokro* (kapitola 3).
- **Kdy zalévat:** prahy „zalít pod %“ a „mokro nad %“.
- **Jak často měřit:** interval záznamu 10, 15, 30 (doporučeno), 60 nebo 120 min, nebo vypnuto. Ukazuje, kdy bude
  další záznam.
- **Hodiny:** nastavení času z telefonu.
- **Jazyk.**

### 2.4 Servis

Podrobnosti pro údržbu a odborníky, ve sbalitelných sekcích:

| Sekce | Co obsahuje |
|-------|-------------|
| Stav sondy | souhrn: baterie, hodiny, sonda, kalibrace, záznam, volné místo, firmware; u problému tlačítko, které vede k nápravě |
| Sonda a měření | obnovování živé hodnoty; zapojení sondy (pin BIAS, *Najít zapojení*); způsob měření (krátký / dlouhý pulz); druhá elektroda (obrácená polarita); přepočet na procenta; diagnostika průběhu napětí (*Zachytit průběh*) |
| Místa – čísla kalibrace | všechna místa v tabulce (název, prahy, kalibrace), export a import do souboru JSON |
| Vyhodnocení zalití | jak velký nárůst vlhkosti a za jak dlouho se počítá jako zalití |
| Ruční zápis | zapnutí tlačítka *Zapsat měření*, poznámky k zápisům, prodlužování stránky |
| Wi-Fi a zobrazení | vysílač (normální / nízký), co se stane po připojení k Wi-Fi, **heslo Wi-Fi**, doba běhu stránky, technické údaje na přehledu |
| Baterie a spotřeba | odhad spotřeby a výdrže, kalibrace napětí podle multimetru |
| Údržba | aktualizace firmwaru, restart, smazání záznamů, **tovární nastavení** |

---

## 3. Uvedení do provozu

1. **Hodiny:** *Nastavení → Hodiny → Nastavit z telefonu.* Dokud hodiny nejsou nastavené, záznamy mají příznak
   nejistého času (`?`).
2. **Zapojení sondy:** při prvním spuštění webu zařízení samo zjistí, na kterém pinu je buzení sondy (BIAS: GPIO32
   na staré desce, GPIO26 na nové), a uloží ho. Stav je v *Servis → Stav sondy*. Ukazuje-li přehled *Chyba sondy*,
   použijte *Servis → Sonda a měření → Najít zapojení* (elektrody přitom nesmí být ve zkratu).
3. **Místo:** v *Nastavení → Místa* pojmenujte místo, kde sonda stojí (další místa přidáte později).
4. **Kalibrace** (pro každé místo zvlášť, sonda zapíchnutá tam, kde bude měřit):
   - když je zem suchá a strom by potřeboval zalít, stiskněte **Teď je sucho**,
   - po vydatném zalití, až voda vsákne, stiskněte **Teď je mokro**.

   Místo je zkalibrované až po obou krocích; do té doby ukazuje „napůl zkalibrováno“ a počítá s výchozími hodnotami.
   Kalibrace se odmítne, když je sonda ve zkratu, nereaguje nebo je měření rušené.
5. **Prahy:** *Nastavení → Kdy zalévat* (výchozí zalít pod 30 %, mokro nad 85 %).
6. **Interval záznamu:** *Nastavení → Jak často měřit* (výchozí 30 min).
7. Volitelně **kalibrace baterie:** změřte článek multimetrem a hodnotu zadejte v *Servis → Baterie a spotřeba*.

Sonda ve vzduchu ukazuje 0 % a příznak „naprázdno“: to není chyba, sonda nic nevede.

---

## 4. Jak zařízení funguje

### 4.1 Měření

Sonda je dělič napětí: z pinu BIAS vede pull-up 10 kΩ do uzlu na ADC (GPIO35), elektrodami a zeminou do země.
Čím je zem vlhčí, tím menší odpor a nižší napětí uzlu.

- Měří se **krátkým pulzem** (výchozí 1 ms): hodnota se odečte dřív, než se elektrody polarizují. Mezi měřeními je sonda
  bez napětí. Pro starší sondy jde zvolit dlouhý pulz 300 ms.
- ESP32 je napájené přímo z článku, takže buzení sondy kolísá s napětím baterie. Čítač ADC se proto přepočítává
  na 3,3 V.
- Z čítače se počítá index vodivosti a z něj procenta podle kalibrace místa (logaritmická stupnice; lineární jde zvolit
  v Servisu).
- Při podezřelé hodnotě (šum, skok o víc než 150 čítačů proti minulému záznamu) se měření ověří třemi dalšími pulzy
  a použije se medián.
- **Chyba sondy** se nevydává za vlhkost: uzel ≈ 0 V (zkrat) nebo bez odezvy na buzení (špatný pin, chybí pull-up)
  znamená `--` místo procent a `--%` v záznamu.
- **Obrácená polarita** (volitelně): druhá elektroda přepojená přes odpor 100–470 Ω z GND na GPIO14 umožní měřit oběma
  polaritami (širší rozsah u suché zeminy, menší polarizace). Volbu nastavte v Servisu až po přepojení.

### 4.2 Záznamy a plán

- Záznamy jsou v **pravidelných hranicích intervalu** podle hodin DS3231 (např. 17:00, 17:30…). Časovač spánku ESP32
  je nepřesný (až ±10 %), zařízení se jeho chybu učí z DS3231 a další spánek o ni opraví. Na jeden záznam stačí jedno
  probuzení (~0,2 s).
- Log `log.txt` je v souborovém systému (LittleFS) v úsporném formátu (asi 22 B na řádek, formát v
  [docs/API.md](docs/API.md)). Když přeroste ~600 kB, **starší polovina se zředí** (každý druhý záznam), takže celá
  historie zůstane v grafu a paměť se nezaplní.
- Ruční zápisy (`M`) mají skutečný čas a poznámku a nejsou v grafech.
- Při změně intervalu, nastavení času nebo zapnutí zařízení se plán zarovná na začátek aktuálního intervalu (žádný
  zdvojený záznam).

### 4.3 Vyhodnocení zalití

Zalití je nárůst vlhkosti aspoň o 8 % během nejvýš 6 h (obojí jde změnit v *Servis → Vyhodnocení zalití*).
Vyhodnocuje to stránka ze záznamů; zařízení samo nic nespouští.

### 4.4 Napájení a baterie

Zařízení běží přímo z jednoho článku **LiFePO4** (3,0–3,5 V, bez stabilizátoru).

| Napětí článku | Chování |
|---------------|---------|
| pod 3,10 V | **šetřící režim:** interval záznamu se zdvojnásobí (hlásí to stránka), nad 3,20 V se vrátí |
| pod 3,00 V | upozornění na stránce, před startem webu 3 pomalá bliknutí |
| pod 2,90 V | Wi-Fi se nezapne (10 rychlých bliknutí), záznam běží dál |
| pod 2,70 V | **hibernace:** měření stojí (v záznamech značka `H`), zařízení se budí jen jednou za 6 h; provoz se obnoví nad 3,00 V |

Pro úsporu běží procesor na 80 MHz, Wi-Fi vysílá ve výchozím stavu nízkým výkonem 8,5 dBm (telefon u stromu; při potížích s dosahem jde v Servisu přepnout na normální 15 dBm) a výstupy se před spánkem
drží v LOW, aby nic neodebíralo. *Servis → Baterie a spotřeba* ukazuje odhad spotřeby a výdrže (pro článek 2000 mAh).

### 4.5 Wi-Fi a zabezpečení

- **Název sítě:** `ARBOLOK-<číslo zařízení>`, adresa `10.10.10.10`, nejvýš 4 připojené telefony.
- **Heslo Wi-Fi:** ve výchozím stavu je síť otevřená. V *Servis → Wi-Fi a zobrazení → Heslo Wi-Fi* jde zapnout heslo
  (8–63 znaků bez diakritiky). Po uložení se zařízení restartuje a telefon se připojí znovu, už s heslem.
  - **Zapomenuté heslo:** spusťte nouzový režim magnetem (kapitola 5.3), připojte se k otevřené síti
    `ARBOLOK-SAFE-<číslo>`, otevřete `10.10.10.10` a stiskněte *Smazat heslo Wi-Fi a restartovat*. Heslo smaže
    i tovární nastavení.
  - Heslo jde zadat i při sestavení firmwaru (`-DAP_PASSWORD=\"…\"` v `platformio.ini`). Takové heslo platí
    i v nouzovém režimu a chrání stránku `/update` (uživatel `admin`); heslo z webu má přednost.
- **Po připojení k Wi-Fi** (Servis): *nic* (adresu zadáte sami, výchozí), *okno s rozcestníkem* (telefon sám otevře
  malé okno), nebo *tiše* (telefon nehlásí „bez internetu“).
- Zařízení mění stav jen příkazy POST a každý ověřuje hlavičku `Origin`, takže cizí stránka otevřená v telefonu
  zařízením nepohne.

### 4.6 Tovární nastavení

*Servis → Údržba → Tovární nastavení* vrátí zařízení do stavu z výroby: smaže nastavení, místa, kalibrace, heslo
Wi-Fi a **zformátuje souborový systém, takže zmizí i všechny záznamy a nahrané soubory**. Pak se zařízení restartuje.
Záznamy si předtím stáhněte (*Záznamy → Stáhnout*). Jen záznamy bez nastavení maže tlačítko *Smazat záznamy*.

---

## 5. Aktualizace firmwaru a ochrana před zabrickováním

Zařízení nemá sériový převodník, firmware se nahrává jen přes Wi-Fi (OTA). Proto má několik nezávislých vrstev
ochrany, aby se nikdy nedostalo do stavu, kdy nejde opravit.

### 5.1 Nahrání nové verze

1. Vezměte hotový firmware ze složky [`firmware/`](firmware/) v repozitáři (`arbolok_v7.0-rc1.bin`) nebo z vydání
   na GitHubu, případně si ho sestavte (kapitola 7).
2. Článek by měl být aspoň z poloviny nabitý.
3. Přiložte magnet, připojte se k Wi-Fi zařízení a otevřete **`http://10.10.10.10/update`** (odkaz *Aktualizace
   firmwaru* v *Servis → Údržba*). Nejlépe v běžném prohlížeči, ne v okénku, které se ukáže po připojení k Wi-Fi.
4. Vyberte soubor. Stránka ho před odesláním zkontroluje (platný obraz pro ESP32, velikost) a ukáže průběh.
5. Zařízení se samo restartuje (do ~15 s). Připojte se znovu, **otevřete `http://10.10.10.10/` a nechte web běžet
   aspoň minutu**: v zápatí je nejdřív `v7.0-rc1 (trial)`, po potvrzení přípona zmizí.

Nastavení, místa, kalibrace i záznamy aktualizace zachová. Po přechodu z verze 6.x si telefon uloží síť znovu
(změnil se název z `ARBOL0K-` na `ARBOLOK-`).

### 5.2 Vrstvy ochrany

1. **Přepnutí až po ověření:** nový firmware se zapisuje do druhého oddílu; přerušené nahrávání nic nezničí.
2. **Zkušební provoz:** nový firmware se potvrdí, až web běží 45 s a stránka někoho obsloužila. Restartuje-li se
   zařízení dřív (pád, odpojení baterie, nikdo se nepřipojil), bootloader spustí **předchozí firmware**.
3. **Nouzový režim po pádech:** po třech pádech v řadě, nebo po pádu v prvních 45 s webu, se místo běžného programu
   spustí nouzový režim (jen Wi-Fi a nahrávání firmwaru; nepoužívá nastavení, sondu ani souborový systém).
4. **Hlídací časovač:** zasekne-li se program na víc než 60–90 s, zařízení se restartuje a pád se započítá do bodu 3.
5. **Nouzový režim magnetem** (kapitola 5.3), funguje i u firmwaru, který vypadá zdravě.
6. **Ochrana baterie:** padá-li i nouzový režim, Wi-Fi se přestane zapínat (6 bliknutí); pomůže odpojit
   a znovu připojit baterii.

### 5.3 Nouzový režim

**Ručně:** odpojte baterii → přiložte magnet a držte ho → připojte baterii → držte magnet ještě ~2 s. LED svítí,
po 2 s zhasne a pak dvojitě bliká.

Síť `ARBOLOK-SAFE-<číslo>` (otevřená, pokud heslo nebylo zadané při sestavení), stránka `http://10.10.10.10/`:
*Nahrát firmware*, *Zkusit normální režim* a (jen při spuštění magnetem) *Smazat heslo Wi-Fi a restartovat*.
Dokud nouzový režim běží, zařízení neměří. Po 10 min bez připojeného telefonu (nejvýš po 60 min) usne a probudí ho
magnet.

---

## 6. Kódy LED a řešení problémů

| LED | Význam |
|-----|--------|
| svítí od startu, pak zhasne | start webu (Wi-Fi se spouští) |
| krátký záblesk každých ~5 s | web běží |
| 3 pomalá bliknutí, pak web | slabý článek (2,90–3,00 V), dejte ho dobít |
| 10 rychlých bliknutí | článek pod 2,90 V, Wi-Fi se nezapne |
| 5 dlouhých bliknutí | Wi-Fi se nepodařilo spustit |
| 2 bliknutí hned po startu | restart po pádu programu (web se přeskočí) |
| 3 bliknutí hned po startu | restart kvůli podpětí (web se přeskočí) |
| svítí ~2 s po připojení baterie | relé je sepnuté, následuje nouzový režim |
| dvojité bliknutí každé 2 s | nouzový režim |
| 6 rychlých bliknutí a tma | nouzový režim padá dokola, Wi-Fi se nezapíná |

| Co vidíte | Co dělat |
|-----------|----------|
| Wi-Fi `ARBOLOK-…` se neukáže | Zkontrolujte baterii (10 bliknutí = slabá), magnet držte u relé ~2 s, hledejte i `ARBOLOK-SAFE-…`. |
| Telefon nechce heslo / heslo nesedí | Zapomenuté heslo smažte z nouzového režimu (kapitola 4.5). |
| Přehled ukazuje *Chyba sondy* a `--` | *Servis → Sonda a měření → Najít zapojení*; zkontrolujte, že elektrody nejsou ve zkratu. |
| Sonda ve vzduchu ukazuje 0 % | V pořádku, sonda nic nevede. |
| Kalibrace „není platná“ | Bod sucho vyšel vodivější než mokro: zopakujte, případně *Výchozí kalibrace*. |
| Měření „nespolehlivé“ | Silný šum; zkuste znovu, zkontrolujte kontakt sondy se zeminou. |
| Po aktualizaci je v zápatí stará verze | Nový firmware se nepotvrdil a vrátil se předchozí: nahrajte znovu, hned otevřete stránku a nechte web minutu běžet. |
| Zařízení nereaguje vůbec | Odpojte baterii aspoň na 10 s; nepomůže-li to, nouzový režim magnetem a nahrání firmwaru. |

---

## 7. Pro vývojáře

### 7.1 Hardware

| Část | Zapojení |
|------|----------|
| Sonda | BIAS GPIO32 (stará deska) / GPIO26 (nová), pull-up 10 kΩ do uzlu na **GPIO35** (ADC), druhá elektroda na GND (nebo přes 100–470 Ω na GPIO14 pro obrácenou polaritu) |
| Baterie | dělič na GPIO34, zapínaný GPIO12; reference 1,235 V na GPIO33, zapínaná GPIO27 |
| Hodiny | DS3231 (I²C), zároveň teplota desky |
| Probuzení | jazýčkové relé na GPIO4 proti GND |
| LED | GPIO13 |
| Napájení | jeden článek LiFePO4 přímo, bez stabilizátoru |

Teplota v záznamech je teplota desky (DS3231), ne teplota půdy.

### 7.2 Sestavení

Potřebujete [PlatformIO](https://platformio.org/) (`brew install platformio` nebo rozšíření pro VS Code).

```
pio run                              # výchozí prostředí esp32doit-devkit-v1
pio run -e nova-deska                # výchozí pin BIAS GPIO26 (jinak stejné; pin se stejně pozná za běhu)
pio run -e ladeni -t upload && pio device monitor   # výpisy po sériové lince (jen s převodníkem)
```

Výsledek je `.pio/build/esp32doit-devkit-v1/firmware.bin`. Před sestavením se z `web/` vygeneruje `src/web_assets.h`
(`tools/gen_web_assets.py`), po něm se vypíše **VELIKOST FIRMWARU** a **KONTROLA PŘED NAHRÁNÍM PŘES OTA**
(`tools/size_report.py`, `tools/check_bin.py`). Limit pro OTA je 1 306 624 B; verze 7.0 má asi 1,19 MB (91 % oddílu).

Platforma (`espressif32@8.0.0`) a knihovny jsou v `platformio.ini` pevně dané a firmware se překládá s `-Os`; bez toho
se nevejde do oddílu. Souborový systém se nenahrává: stránka je součástí firmwaru, log si zařízení vytvoří samo.

### 7.3 Složky

```
src/              firmware (main, webui, probe, wiring, power, logstore, settings, locations, battery, rtcutil,
                  bootguard, guard, safemode …), src/web_assets.h = vygenerovaná stránka
web/              zdroje webové aplikace (index.html, app.css, app.js, plot.js, i18n.js, update.html, portal.html)
docs/API.md       HTTP API a formát logu
tests_host/       testy jádra na PC (g++), atrapy Arduino-ESP32
tests_web/        simulátor zařízení + zkouška stránky v Chromiu (Playwright)
tools/            generátor stránky, kontroly firmwaru, skripty testů
firmware/         hotové firmware.bin k vydané verzi + kontrolní součty
```

### 7.4 Testy

| Co | Příkaz |
|----|--------|
| Jádro (měření, log, plán, nastavení, místa, ochrana před zabrickováním, JSON, kontroly `.bin`) | `bash tools/run_host_tests.sh` |
| Webová aplikace v prohlížeči (vyžaduje `pip install playwright` a `playwright install chromium`) | `python3 tests_web/ui_test.py` |
| Syntaxe a typy všech `src/*.cpp` proti skutečným hlavičkám knihoven (po prvním `pio run`) | `bash tools/syntax_check.sh` |

Na macOS spadne AddressSanitizer v `run_host_tests.sh` ještě před startem testu (chyba toolchainu Apple, ne kódu);
testy pak spusťte jen s UBSan: v `FLAGS` nahraďte `address,undefined` za `undefined`.

### 7.5 Co se ověřit nedá na PC

Logika měření, plánu, logu i ochrany před zabrickováním je otestovaná na PC (simulovaná deska, virtuální hodiny,
paměťová atrapa souborového systému). Analogová část (lineárnost ADC ESP32, polarizace elektrod v konkrétní zemině)
a chování spánku a Wi-Fi na desce se ověřují jen provozem. Odpor zeminy v kΩ na stránce je orientační: ADC nad ~3,1 V
přetéká, takže u suché zeminy vychází odpor nižší, než je; procenta tím netrpí, protože je natáhne kalibrace.
