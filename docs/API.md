# ARBOLOK – webové API (verze firmwaru v5)

Tohle je kontrakt mezi firmwarem (`src/webui.cpp`, `src/webjson.h`) a stránkou
(`web/app.js`). Simulátor zařízení `tests_web/mock_server.py` implementuje totéž
rozhraní, takže se stránka dá zkoušet bez hardwaru.

- Adresa: `http://10.10.10.10/` nebo `http://arbolok.local/` (Wi-Fi `ARBOLOK-<id>`).
- Všechny odpovědi mají `Cache-Control: no-store` (kromě stránky `/`, ta má ETag).
- Odpovědi API jsou JSON (`application/json`), výjimkou je log (text) a soubory.
- Příkazy jsou **POST** s parametry v URL (`?a=1&b=2`), tělo je prázdné.
- Každý POST musí mít hlavičku `Origin` se stejným hostitelem jako `Host` (prohlížeč
  ji posílá sám; `curl` ji nemá, a to se toleruje). Jinak `403 {"ok":false,"err":"origin"}`.

## Jak fungují příkazy

HTTP odpověď `{"ok":true}` znamená jen „příkaz přijat do fronty“. Skutečný výsledek
(a případná chyba, např. neplatná kalibrace) přijde ve stavu:

1. Webová stránka volá `GET /api/status` jednou za 2 s (za 0,7 s, dokud je `busy`).
2. Každý dokončený příkaz nastaví `msg` (např. `ok:dry`, `err:cal`) a zvýší `seq`.
3. Když se `seq` změní, stránka ukáže potvrzení (`ok:*`) nebo chybu (`err:*`).
4. Dokud příkaz běží, je `busy` = `true` (měření trvá od několika milisekund do zhruba sekundy).

Chyba zjištěná hned při přijetí (špatný parametr, plná fronta…) se vrací rovnou jako
`4xx/5xx` s tělem `{"ok":false,"err":"<kód>"}`.

### Kódy chyb v HTTP odpovědi

| HTTP | `err`      | Význam                                                          |
|-----:|------------|-----------------------------------------------------------------|
| 400  | `param`    | chybějící nebo neplatný parametr                                |
| 400  | `vbat`     | napětí baterie mimo 2,0–4,5 V                                   |
| 400  | `time`     | neplatné datum/čas                                              |
| 400  | `pw`       | heslo Wi-Fi: jiná délka než 8–63 znaků, nebo jiné než tisknutelné znaky ASCII |
| 400  | `upload`   | v požadavku nebyl žádný soubor                                  |
| 403  | `origin`   | hlavička `Origin` nesouhlasí s `Host` (i u `/upload`)           |
| 404  | `notfound` | neznámá adresa nebo soubor                                      |
| 409  | `busy`     | soubor se právě stahuje/nahrává, nebo hlavní smyčka právě zapisuje/ředí log (zámek se nezískal do 0,5 s): nic se nestalo, zkusit znovu |
| 500  | `fs`       | chyba zápisu/mazání ve flash                                    |
| 500  | `buf`      | odpověď se nevešla do bufferu (nemělo by nastat)                |
| 503  | `queue`    | fronta příkazů je plná, zkusit znovu                            |
| 503  | `busy`     | `GET` logu: právě probíhá zředění (stránka to po 2,5 s sama zopakuje) |
| 507  | `fs`       | nahrávaný soubor se nevejde: po uložení musí zůstat 48 kB + 75 % logu + 8 kB volného místa |

U `/upload` je `err` podle příčiny: `origin` (403), `param` (400, neplatné jméno souboru nebo jméno `log.txt`), `busy` (409), jinak `fs`.

### Kódy v `msg` (po dokončení příkazu)

| `msg`          | Vyvolal                | Význam                                                      |
|----------------|------------------------|-------------------------------------------------------------|
| `ok:measure`   | `/api/measure`         | změřeno                                                     |
| `ok:profile`   | `/api/profile/capture` | průběh napětí zachycen                                      |
| `ok:dry`       | `/api/cal?which=dry`   | uložen bod „sucho“                                          |
| `ok:wet`       | `/api/cal?which=wet`   | uložen bod „mokro“                                          |
| `ok:reset`     | `/api/cal?which=reset` | kalibrace vrácena na výchozí                                |
| `ok:vbat`      | `/api/calvbat`         | korekce napětí baterie uložena                              |
| `ok:time`      | `/api/time`            | hodiny nastaveny                                            |
| `ok:settings`  | `/api/settings`        | nastavení uloženo                                           |
| `ok:lang`      | `/api/settings`        | změněn pouze jazyk                                          |
| `ok:extend`    | `/api/extend`          | web prodloužen                                              |
| `ok:locs`      | `/api/locs`            | všechna místa uložena (import / úprava)                     |
| `err:locs`     | `/api/locs`            | tabulka míst neplatná, nic se nezměnilo                     |
| `ok:loc`       | `/api/loc`             | místo vybráno / přidáno / přejmenováno / smazáno            |
| `err:loc`      | `/api/loc`             | neplatné místo, plno (8 míst) nebo pokus smazat poslední místo |
| `err:logmap`   | `/api/loc`, `/api/locs` | místo se nesmazalo / tabulka se neuložila: záznamy v logu nejde přečíslovat (log se stahuje, plná paměť) |
| `ok:wiring`    | `/api/wiring`, `/api/settings` (`bias=0`) | nalezen pin BIAS, uložen a použit        |
| `err:meas`     | měření/profil          | měření selhalo                                              |
| `err:flags`    | kalibrace sucho/mokro  | měření je nespolehlivé (silný šum), zopakovat               |
| `err:short`    | kalibrace sucho/mokro/naprázdno | chyba sondy: uzel ≈ 0 V (zkrat, nebo pin BIAS dělič nebudí) nebo bez odezvy na BIAS (`m.flags & 20`); nic se neuložilo |
| `err:wiring`   | `/api/wiring`          | žádný kandidát na pin BIAS nezvedl uzel o aspoň 80 čítačů (zkrat, chybí pull-up, jiný pin) |
| `err:wiring2`  | `/api/wiring`          | nejednoznačné: reaguje víc pinů podobně silně; zvolit ručně (`bias=<pin>`) |
| `err:cal`      | kalibrace sucho/mokro  | výsledná kalibrace by byla neplatná (sucho musí být méně vodivé než mokro) |
| `err:vbat`     | kalibrace baterie      | změřené/zadané napětí mimo rozsah nebo korekce mimo 0,80–1,20 |
| `err:rtc`      | `/api/time`            | hodiny DS3231 neodpovídají                                  |
| `err:time`     | `/api/time`            | datum/čas nešlo uložit                                      |

Stránka překládá `ok:X` na text `m_X` a `err:X` na `e_X` (`web/i18n.js`); neznámý kód
ukáže jako obecnou chybu komunikace.

## Čtení (GET)

### `GET /` a `/index.html`
Jednostránková aplikace, vložená do firmwaru jako gzip (`Content-Encoding: gzip`).
`ETag` = hash obsahu (`WEB_ASSET_HASH`), `If-None-Match` → `304`.

### `GET /api/status`
Celý stav zařízení. Pole, která zařízení nezná, jsou `null`.

```json
{
  "fw": "v4.9", "id": "0123456", "board": 1, "up": 123, "heap": 183456,
  "clock": {"ok": true, "unix": 1793638980, "set": true},
  "bat":   {"v": 3.271, "pct": 55, "lvl": 0},
  "m":     {"have": true, "pct": 43.2, "raw": 2870.0, "rawRaw": 2871.0, "g": 0.4268,
            "ms": 12, "settled": true, "flags": 0, "age": 4, "rawB": null, "ph": 0},
  "temp": 18.4,
  "busy": false, "msg": "ok:measure", "seq": 7,
  "sess": {"left": 212, "total": 240, "max": 1800},
  "fs":   {"total": 1441792, "used": 380000, "log": 51234},
  "logErr": false, "ota": false,
  "cfg":  {"lang": "cz", "web": 4, "int": 30, "dry": 30, "wet": 85,
           "gd": 0.05, "gw": 3.0, "open": 4095, "scale": 1,
           "vexc": true, "pmode": 0, "rpull": 10.0, "vcorr": 0.97,
           "bias": 26, "biasUsed": 26, "ret": 0,
           "maxms": 300, "eps": 5, "pdis": 30, "lpul": 1, "cdel": 150, "wjmp": 8, "wwin": 6, "cport": 0, "txp": 0, "early": 1000,
           "loc": 0, "locs": ["Místo 1", "Lípa u školy"]},
  "wire": {"have": true, "res": 1, "pin": 26, "n": 2,
           "p": [32, 26], "lo": [0, 2], "hi": [3, 4095]},
  "prof": {"valid": false, "n": 0},
  "pwr": {"save": false, "int": 30, "since": 1782993600, "boots": 146, "logs": 144,
          "awake": 65700, "web": 420, "sleep": 258700, "k": 1.0412, "cap": 2000}
}
```

Při chybě sondy (`m.flags & 20`, viz níže) je `m.pct` `null` (ne číslo):

```json
"m": {"have": true, "pct": null, "raw": 3.0, "rawRaw": 3.0, "g": 1364.0,
      "ms": 4, "settled": true, "flags": 4, "age": 2}
```

| Pole | Význam |
|------|--------|
| `fw`, `id`, `board` | verze firmwaru, sériové číslo (z eFuse), varianta sestavení = **výchozí** pin BIAS (1 = GPIO32, 2 = GPIO26); skutečně používaný pin je `cfg.biasUsed` |
| `up`, `heap` | doba od startu (s), volná halda (B) |
| `clock.ok` | hodiny DS3231 odpovídají |
| `clock.unix` | „lokální“ čas jako unix sekundy (stejně jako RTC, bez časových pásem); stránka ho extrapoluje |
| `clock.set` | hodiny byly někdy nastaveny (po ztrátě napájení RTC = `false`, záznamy dostanou `?`) |
| `bat.v`, `bat.pct`, `bat.lvl` | napětí (V), orientační % z křivky LiFePO4, 0 = v pořádku, 1 = slabá (< 3,00 V), 2 = kritická (< 2,90 V) |
| `m.have` | existuje nějaké měření |
| `m.pct` | vlhkost v % podle **aktuální** kalibrace (přepočítává se při každém dotazu z `raw`); `null` při chybě sondy (`m.flags & 20`) |
| `m.raw` | ustálená hodnota ADC (0–4095), normalizovaná na buzení 3,3 V (`cfg.vexc`); plně nasycený čítač (≥ 4080) se nepřepočítává |
| `m.rawRaw` | totéž bez normalizace |
| `m.g` | index vodivosti `g = (open − raw) / raw` (úměrný vodivosti zeminy) |
| `m.ms`, `m.settled` | za jak dlouho se napětí ustálilo; `false` = nenašlo se plató (strop 300 ms) |
| `m.flags` | bitové příznaky: 1 neustáleno, 2 uzel naprázdno (sonda ve vzduchu / velmi sucho), 4 čítač uzlu = 0 (zkrat, nebo pin BIAS dělič nebudí; od v5.2 jen přesně 0, pár čítačů = velmi mokro), 8 šum, 16 uzel nereaguje na buzení BIAS (odezva menší než 25 čítačů, u nízkého plató menší než jeho polovina; špatný pin, chybí pull-up), 32 obrácená polarita zapnutá, ale druhá elektroda na zvoleném pinu nebudí (hodnota je z přímé polarity, není to chyba sondy). **Chyba sondy** = `flags & 20` (4 nebo 16): `m.pct` je `null`, kalibrace se odmítne (`err:short`) |
| `m.age` | stáří měření (s) |
| `m.rawB`, `m.ph` | obrácená polarita: surové plató fáze B (`null` = neproběhla) a odkud je hodnota: 0 = přímá polarita, 1 = obrácená (suchá zemina), 2 = poměr obou (`g = rawB / rawRaw`). `m.raw` je pak ekvivalentní čítač přímé fáze (`open / (1 + g)`), takže log, procenta i kalibrace fungují stejně |
| `temp` | teplota z DS3231 (°C) – je to teplota **elektroniky**, ne půdy |
| `busy`, `msg`, `seq` | viz „Jak fungují příkazy“ |
| `sess.left/total/max` | zbývající/celková/nejdelší doba, po kterou web zůstane zapnutý (s) |
| `fs.total/used/log` | LittleFS: velikost, využití, velikost `log.txt` (B) |
| `fs.lmax` | velikost logu, od které se starší polovina zřeďuje (B, `LOG_MAX_BYTES`; od v6.12) |
| `logErr` | poslední zápis do logu selhal |
| `ota` | probíhá aktualizace firmwaru (zařízení neusne) |
| `cfg.lang` | `cz` / `en` / `ua` |
| `cfg.web` | jak dlouho (min) zůstane po zapnutí web aktivní |
| `cfg.int` | interval záznamu (min), 1–120, 0 = záznam vypnutý (výchozí 30) |
| `cfg.dry`, `cfg.wet` | prahy v % pro stav „zalít“ a „mokro“ |
| `cfg.gd`, `cfg.gw`, `cfg.open` | kalibrace: index vodivosti pro 0 %, pro 100 %, čítač naprázdno |
| `cfg.scale` | 1 = logaritmická škála (nová), 0 = lineární `100 − raw/4095` (jako ve v3) |
| `cfg.vexc` | normalizovat čítač podle napětí baterie (ESP32 běží přímo z LiFePO4, BIAS = napětí článku, ADC má pevnou referenci); výchozí zapnuto |
| `cfg.rpull` | odpor pull-upu v kΩ, od v5.4 vždy 10 (nenastavuje se); jen pro zobrazení odporu zeminy |
| `cfg.vcorr` | korekce děliče napětí baterie |
| `cfg.bias` | uložený pin BIAS sondy (32, 26 nebo 25); `0` = zatím neurčeno (při dalším startu webu se hledá, do té doby platí výchozí z `board`) |
| `cfg.biasUsed` | pin BIAS, který se právě používá (uložený, jinak výchozí z prostředí) |
| `cfg.ret` | pin druhé elektrody: 0 = napevno na GND (výchozí), 14 / 18 / 19 / 23 = obrácená polarita |
| `cfg.maxms`, `cfg.eps`, `cfg.pdis` | (jen ke čtení, pevné výchozí) strop pulzu (ms, 20–1000), tolerance ustálení (čítače, 1–50), vybití elektrod před pulzem (ms, 0–500) |
| `cfg.lpul`, `cfg.cdel` | měření na záznam do logu (1 = jeden pulz s ověřením při skoku nad `cdel` čítačů nebo rušení; 2–5 = vždy medián), mez skoku (20–2000) |
| `cfg.wjmp`, `cfg.wwin` | detekce zalití na stránce: nárůst aspoň o `wjmp` % (1–50) během nejvýš `wwin` h (1–48); zařízení je jen ukládá |
| `cfg.pmode`, `cfg.early` | způsob měření (nastavitelný v Servisu), `early` jen ke čtení (1000): 0 = automatický (plató), 1 = pevné okno = `maxms`, 2 = krátký pulz `early` µs (300–20 000, hodnota = medián druhé poloviny pulzu) |
| `cfg.locd` | pro každé místo `[zalévat pod, mokro nad, g sucho, g mokro]` (stejné pořadí jako `locs`) |
| `cfg.loc`, `cfg.locs` | index vybraného místa (0…7) a názvy všech míst (1–8 položek, nejvýš 24 znaků). `dry`, `wet`, `gd`, `gw`, `open` jsou vždy hodnoty **vybraného** místa. Změna kalibrace nebo prahů se uloží i do místa. |
| `cfg.cport` | po připojení k Wi-Fi: 0 = nic (výchozí), 1 = okno s rozcestníkem `/portal` (do potvrzení `GET /portal/ok` dostane každý test internetu i cizí adresa `302` na `/portal`, potom testy „internet je“), 2 = tiše (testy internetu hned „internet je“: Apple `Success`, Android `204`, Windows `Microsoft Connect Test` / `Microsoft NCSI`, Firefox `success`; cizí adresa `302` na `/`). DNS server odpovídá adresou zařízení v režimech 1 a 2. |
| `cfg.man`, `cfg.mnote`, `cfg.mext` | ruční zápis (od v6.10): zapnuto, poznámka (0 žádná, 1 vlastní text, 2 automatické číslo), prodlužovat stránku |
| `cfg.txp` | vysílač Wi-Fi (od v6.5): 0 = normální 15 dBm, 1 = nízký 8,5 dBm (výchozí od v7.0-rc1). Nouzový režim vysílá vždy normálně. |
| `cfg.pw` | heslo Wi-Fi (od v7.0): 0 = otevřená síť (výchozí), 1 = heslo nastavené ve webu, 2 = heslo ze sestavení (`AP_PASSWORD`). Heslo samo API nikdy neposílá. |
| `pwr.save`, `pwr.int` | šetřící režim (od v6.5): článek změřený při záznamu pod 3,10 V, interval záznamu je dvojnásobný (`int` = skutečný interval v min); vypne se nad 3,20 V |
| `pwr.since`, `pwr.boots`, `pwr.logs` | čítače pro odhad spotřeby: od kdy se počítá (čas DS3231), počet probuzení mimo web a zapsaných záznamů. Nulují se odpojením baterie a každým restartem (jsou v RTC RAM). |
| `pwr.awake`, `pwr.web`, `pwr.sleep` | doba bdění mimo web (ms, včetně odhadu 150 ms bootloaderu), doba webu (s, i běžící relace), plánovaná doba spánku (s) |
| `pwr.k` | naučená chyba časovače spánku: skutečné sekundy na sekundu časovače (1.0412 = časovač spí o 4,1 % déle, než se mu zadá); `null` = zatím nenaučeno |
| `pwr.cap` | kapacita článku pro odhad výdrže (mAh, `BAT_CAPACITY_MAH` v `config.h`) |
| `wire.have` | od startu proběhl test zapojení (automaticky při startu webu, nebo `POST /api/wiring`) |
| `wire.res` | výsledek posledního testu: 0 = nic nereaguje, 1 = nalezen jediný pin, 2 = nejednoznačné |
| `wire.pin` | nalezený pin (jen při `res` = 1, jinak 0) |
| `wire.n`, `wire.p`, `wire.lo`, `wire.hi` | kolik kandidátů se zkoušelo (2: GPIO32 a GPIO26; od v6.1 se GPIO25 nezkouší), jejich piny a čítač uzlu při BIAS dole (`lo`) a nahoře (`hi`); u pravého pinu je rozdíl `hi − lo` stovky až tisíce čítačů |
| `prof.valid/n` | je k dispozici diagnostický průběh a kolik má bodů |

### `GET /api/profile`
Diagnostický průběh napětí na uzlu děliče po sepnutí buzení (vyvolá se příkazem
`/api/profile/capture`). Časy jsou v ms od sepnutí, hodnoty v ADC čítačích.

```json
{"n": 4, "t": [0.3, 0.41, 0.55, 0.74], "v": [3120.0, 3004.0, 2950.0, 2931.0]}
```
Bez zachyceného průběhu `{"n":0,"t":[],"v":[]}`.

### `GET /api/log`, `GET /log.txt`
Obsah `log.txt` jako `text/plain; charset=utf-8`. Log, který neexistuje (záznam vypnutý, nebo smazaný) nebo je
prázdný, = `200` s prázdným tělem. `503 busy`, když zrovna běží zředění logu (otevření souboru čeká na zámek
nejvýš 0,5 s). Po dobu stahování se log nezřeďuje ani nepřepisuje.
Formát řádku je níže.

### `GET /api/log/download`, `GET /download`
Totéž s hlavičkou `Content-Disposition: attachment; filename="<id>.txt"` (i prázdný soubor, když log neexistuje).

### `GET /api/files`
(Web od v5.0 záložku Soubory nemá; API zůstává pro servis.) Seznam souborů v LittleFS (max. 40, vnitřní dočasné soubory `log.tmp`, `up.tmp` se
nezobrazují): `[{"n":"log.txt","s":51234}, …]`.

### `GET /api/file?name=<jméno>`
Obsah souboru (`application/octet-stream`). Jméno musí být platné (`webparse::validExistingName`),
jinak `400 param`; neexistující `404 notfound`.

### Po připojení k Wi-Fi (`cfg.cport`), viz tabulka výše; níže chování režimu 1 před potvrzením
Je-li zapnutý, DNS server zařízení odpovídá na každý dotaz adresou `10.10.10.10` a každý `GET` s cizí hlavičkou `Host`
(test internetu telefonu: `captive.apple.com/hotspot-detect.html`, `connectivitycheck.gstatic.com/generate_204`,
`www.msftconnecttest.com/connecttest.txt`, `detectportal.firefox.com/...`) dostane `302 Location: http://10.10.10.10/`.
Vlastní adresy (`10.10.10.10`, `arbolok.local`) se chovají jako dřív (neznámá cesta = `404`).

### Staré adresy z verze 3
`/settings`, `/logs`, `/upload` (GET) přesměrují na odpovídající stránku aplikace
(`/#/settings` apod.).

## Příkazy (POST)

| Adresa | Parametry | Poznámka |
|--------|-----------|----------|
| `/api/measure` | – | Okamžité měření „naživo“ (jeden pulz BIAS). Výsledek ve stavu `m`. (Měření do logu si při podezřelé hodnotě pulz ještě ověří – viz README.) |
| `/api/profile/capture` | – | Zachytí průběh napětí; po dokončení `GET /api/profile`. |
| `/api/cal` | `which=dry` \| `wet` \| `reset` | `dry`/`wet`: medián ze 3 pulzů → nový kalibrační bod (`err:short` při chybě sondy, `err:flags` při šumu, `err:cal`). Nasycený čítač u `dry` (velmi suchá zemina, `g` ≈ 0) uloží spodní mez `gd = 0.05`. `reset`: výchozí `gd=0.05`, `gw=3.0`. Naprázdno je od v5.7 pevně 4095 (`cfg.open` jen ke čtení; `which=open` → `400`). |
| `/api/wiring` | – | Najde pin BIAS: každého kandidáta (32, 26, 25) krátce zapne (BIAS dole/nahoře, ~25 ms) a změří čítač uzlu. Nalezený pin (`wire.res` = 1) se uloží (`cfg.bias`) a použije, hned se provede nové měření. Výsledek: `ok:wiring`, `err:wiring` (nic nereaguje), `err:wiring2` (nejednoznačné); podrobnosti v `wire`. Sonda nesmí být ve zkratu. |
| `/api/calvbat` | `v=<V>` (2,0–4,5) | Změří baterii a dopočítá korekci děliče podle voltmetru. Čárka i tečka jako desetinný oddělovač se tolerují. |
| `/api/time` | `y`, `mo`, `d`, `h`, `mi`, `s` | Nastaví hodiny DS3231 (lokální čas telefonu). Plánovač záznamů se přenastaví na začátek aktuálního intervalu (žádný zdvojený záznam). |
| `/api/settings` | libovolná z: `lang`, `web` (1–30), `int` (0–120), `dry` (0–99), `wet` (1–100), `scale` (0/1), `pmode` (2 = krátký pulz, výchozí; 1 = dlouhý pulz 300 ms – starší sondy; 0 se od v6.1 bere jako 1), `vexc` (0/1), `bias` (0, 32 nebo 26), `ret` (0 nebo 14), `wjmp` (1–50), `wwin` (1–48), `cport` (0–2), `txp` (0/1, vysílač Wi-Fi; od v6.5, platí hned), `man` (0/1 ruční zápis), `mnote` (0 bez poznámky, 1 vlastní text, 2 automatické číslo), `mext` (0/1 zápis prodlouží stránku; od v6.10), `live` (5–120 s, obnovování živé hodnoty při webu; od v6.11), `vcorr` (0.80–1.20, korekce napětí baterie; od v5.9) | Pull-up (`rpull`) a ostatní parametry pulzu (`maxms`, `eps`, `pdis`, `lpul`, `cdel`, `early`) jsou od v5.1 pevné: v `cfg` se dál hlásí, ale API je nepřijímá (jako neznámý parametr). Mění se jen to, co je v URL (maska). `wet` musí být > `dry`. Změna `web` platí hned (nejméně o 20 s víc, než už web běží). `bias=<pin>` pin uloží a ihned použije; `bias=0` uložený pin zapomene a hned spustí hledání jako `/api/wiring` (nenajde-li se nic, zůstane 0 a platí výchozí pin z `board`). Jiná hodnota = `400 param`. Pole, která v URL nejsou, zůstávají, jak jsou (i uvnitř skupin). `ret` platí hned a hned se změří. |
| `/api/manual` | `note=<text>` (volitelně) | Ruční zápis (přenosný logger, jen se zapnutým `cfg.man`): změří (medián ze 3 pulzů) a zapíše řádek se značkou `M` a poznámkou. Výsledek `ok:manual`, `err:manoff` (vypnuto), `err:manual` (zápis selhal). S `cfg.mext` = 1 prodlouží stránku na aspoň 5 min do konce (nejvýš 60 min celkem). |
| `/api/loc` | `op=sel&i=<n>` \| `op=new&name=<název>` \| `op=ren&i=<n>&name=<název>` \| `op=del&i=<n>` | Místa (kalibrační profily, max. 8, uložená v NVS). `sel` přepne a přenese kalibraci a prahy místa do měření, hned změří. `new` přidá místo s prahy a hodnotou naprázdno z aktuálního místa a výchozí kalibrací a vybere ho. `ren` přejmenuje. `del` napřed přečísluje záznamy v logu (smazané místo `p0`, další o jedno níž; nepovede-li se, `err:logmap` a nic se nesmaže), pak místo smaže (poslední místo smazat nejde), vybrané místo se posune. Název se očistí (řídicí znaky, `;`), prázdný = `400 param`. Výsledek `ok:loc` / `err:loc`. |
| `/api/locs` | `n=<1..8>`, pro každé `i` od 0: `n<i>` název, `d<i>` zalévat pod (0–99), `w<i>` mokro nad (1–100), `gd<i>`, `gw<i>` kalibrace; `a=<index>` vybrané místo; volitelně `o<i>` = původní index místa (0–7, `-1` = nové; od v6.8) | Nahradí všechna místa najednou (Servis: úprava v tabulce, import ze souboru). Každé místo musí být platné (název, `d < w`, `gd > 0`, `gw > 1,2·gd`, `gw < 1000`), jinak `400 loc` / `400 param` a nic se nezmění. Pak se vybere místo `a`, jeho kalibrace a prahy se použijí a hned se změří. S `o<i>` se nejdřív přečíslují záznamy v logu (místo, které v tabulce chybí, dostane `p0`); bez `o<i>` (import ze souboru) se záznamy nemění. Výsledek `ok:locs` / `err:locs` / `err:logmap`. |
| `/api/extend` | – | Prodlouží relaci webu o 5 minut (do `sess.max`). |
| `/api/restart` | – | Restart zařízení (po ~0,5 s, aby odpověď stihla odejít). |
| `/api/wifipw` | `pw=<heslo>` | Heslo Wi-Fi (od v7.0): 8–63 tisknutelných znaků ASCII, `pw=` (prázdné) heslo zruší. Uloží se do NVS a zařízení se restartuje (Wi-Fi naběhne s novým heslem). Neplatné heslo = `400 pw`. Platí jen v běžném režimu; nouzový režim používá jen `AP_PASSWORD` ze sestavení. |
| `/api/factory` | – | Zformátuje souborový systém (log i nahrané soubory, od v7.0-rc1), smaže nastavení, místa, kalibraci i heslo Wi-Fi (NVS) a restartuje. |
| `/api/log/clear` | – | Smaže `log.txt`. `409 busy`, když se právě stahuje nebo když se zámek logu nezískal do 0,5 s (běží zředění). |
| `/api/files/delete` | `name=<jméno>` | Smaže soubor (`log.txt` přes zámek logu). `409 busy`, `404 notfound`. |
| `/upload` | multipart, pole `file` | Nahraje soubor a uloží ho pod očištěným jménem. Log nahrávání nikdy nepřepíše (od v7.0; do v6.16 nahradil log každý soubor `.txt`), jméno `log.txt` se odmítne (`400 param`). Data jdou do `/up.tmp`, existující soubor se nepřepíše, dokud není vše přijato. `507`, když by po něm nezůstalo místo pro logger (viz výše). `409 busy`, když se soubor (nebo log) právě stahuje, nebo běží zředění logu. |

## Aktualizace firmwaru (OTA)

`GET /update` (knihovna ESPAsyncHTTPUpdateServer, `-DESPASYNCHTTPUPDATESERVER_MODE=1`) – jednoduchá stránka
s jediným formulářem *Firmware* (`accept=.bin,.bin.gz`, tlačítko *Update Firmware*, odesílá `POST /update?name=firmware`);
žádné záložky ani volba souborového systému. Je-li při sestavení nastaveno `AP_PASSWORD`, vyžaduje přihlášení (uživatel
`admin`, stejné heslo). Během aktualizace je `ota` = `true` a zařízení neusíná; zapomenutá (přerušená)
aktualizace se po 10 min zruší.
`POST /update` bez parametru `name` (`?name=firmware`, doplňuje ho formulář na stránce `/update`)
odmítne předřazený handler s `400` – knihovna by jinak parametr bez kontroly dereferencovala a zařízení by se restartovalo.
Ruční `?name=filesystem` (např. `curl`) bez příznaku `ESPASYNCHTTPUPDATESERVER_LITTLEFS`, který se záměrně nezapíná,
by neměl nic přepsat: knihovna by si vzala velikost z nepřipojeného SPIFFS (0 B) a `Update.begin` to odmítne
(úsudek z kódu knihovny, na zařízení nezkoušeno).

Novou verzi zařízení přijme jen celou (přepnutí na nový oddíl až po ověření obrazu); největší přijatelný soubor má
1 306 624 B (`tools/check_bin.py` to hlídá). Nový firmware je po restartu „nepotvrzený“. Potvrdí se, až web běžel
45 s **a obsloužil aspoň jeden požadavek na `/` nebo `/api/status`**, nebo relace skončila čistě po takto obsloužené
stránce, nebo přišel `/api/restart`, `/api/factory`, nebo začíná další nahrávání firmwaru. Dokud není potvrzen, zařízení
neusíná (restartuje se) a první reset (je-li v bootloaderu podpora) vrátí předchozí firmware. Pole `fw` ve
`/api/status` má v té době příponu ` (trial)`.
Postup a záchranné kroky: [README, kapitola 1](../README.md#1-nahrání-přes-ota-bez-kabelu).

### Nouzový režim

Po opakovaných pádech nebo zaseknutí, nebo magnetem při připojení baterie, běží místo běžného programu
**jen** tato malá aplikace (`src/safemode.cpp`, nic z API výše tam není): Wi-Fi `ARBOLOK-SAFE-<id>`, `10.10.10.10`.

| Adresa | Metoda | Význam |
|--------|--------|--------|
| `/`, `/index.html` | GET | stručná stránka: verze, id, důvod spuštění, odkaz na `/update`, tlačítko „Zkusit normální režim“ |
| `/update` | GET, POST | OTA jako výše (stejná ochrana parametru `name`; heslo jen ze sestavení, `AP_PASSWORD`) |
| `/normal` | POST | vynuluje počítadlo pádů a restartuje do běžného režimu (po ~0,8 s) |
| `/pwreset` | POST | jen když byl nouzový režim spuštěn magnetem: jako `/normal` a běžný program po startu smaže heslo Wi-Fi nastavené ve webu (zapomenuté heslo; od v7.0). Jinak `403`. |
| cokoli jiného | GET | přesměrování na `/` |

Po 10 min bez připojeného klienta (a nejvýš po 60 min) usne; probudí ho magnet.

## Formát logu (`log.txt`)

Jeden záznam na řádek, ukončení CRLF. **Od v6.13 krátký formát** (asi 22 B na řádek místo 46 B):

```
2611091300 52 21 325 s12 p2 ? ! H M # poznámka
│          │  │    │   └ volitelné značky (níže); sNN jen u neustáleného měření nebo pulzu delšího než 1 ms,
│          │  │    │     pN jen u míst 2–8 (bez p = místo 1)
│          │  │    └ napětí článku v setinách voltu (325 = 3,25 V)
│          │  └ teplota v celých °C (DS3231; ve v6.13 s 1 desetinným místem, stránka čte obojí)
│          └ vlhkost %
└ RRMMDDhhmm (2611091300 = 9. 11. 2026 13:00)
```
Neznámá hodnota je `-`. Surový čítač ADC (`rNNNN`) se od v6.13 nezapisuje. Řádky ze starších verzí (níže) v logu
zůstávají; stránka, zředění i přečíslování míst čtou oba formáty. *Záznamy → Stáhnout .txt* poskládá čitelný text
(starší tvar s datem a jednotkami) ze všech záznamů; `GET /api/log/download` dává soubor tak, jak je uložený.

Formát do v6.12:

```
2026-11-02 17:03 45% 21.3C 3.25V r2873 s12 ? !
 datum     čas   vlhk. teplota Ubat  ADC   ustálení  │ │
                                                      │ └ slabá baterie (< 3,00 V)
                                                      └ hodiny nebyly nastaveny (čas pochybný)
```

- Prvních pět sloupců je shodných s verzí 3, takže staré skripty a tabulky fungují.
- `rNNNN` – ustálený ADC čítač **po normalizaci** na buzení 3,3 V (`cfg.vexc` zapnuto, výchozí; jinak holý údaj z ADC),
  z něj se dá vlhkost kdykoli přepočítat s jinou kalibrací.
- `H` – na konci řádku: tady zařízení přešlo do hibernace kvůli vybitému článku (od v5.6; vlhkost `--%`, platí napětí). Další záznamy přijdou až po dobití nad 3,00 V.
- `M` – ruční zápis tlačítkem (od v6.10): čas je skutečný okamžik, ne hranice intervalu; za ` # ` na konci řádku může být poznámka (nejvýš 24 bajtů, bez `;` `"` `\\` `<` `>`). Zředění logu ruční zápisy nemaže.
- `pN` – číslo místa (1–8), ke kterému záznam patří (od v5.0; záznam bez `p` patří místu 1). `p0` = místo bylo smazáno (od v6.8: při smazání nebo přeuspořádání míst zařízení záznamy v logu přečísluje, aby zůstaly u svého místa).
- `sNN` – za kolik ms se napětí ustálilo; `sNNu` = plató nenalezeno (dosažen strop 300 ms).
- `--%`, `--C`, `--V` – hodnotu se nepodařilo získat (`--%` také při chybě sondy: zkrat, nebo uzel nereaguje na BIAS; `rNNNN` zůstává).
- Časová značka je **hranice intervalu** (např. 17:00, 17:30…), ne okamžik, kdy se zařízení probudilo.
- Při velikosti nad ~560 kB se starší polovina logu zředí (každý druhý záznam), nejnovější zůstávají celé.
