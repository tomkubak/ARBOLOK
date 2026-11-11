#!/usr/bin/env python3
"""Zkouška webového rozhraní v Chromiu (Playwright) proti simulovanému zařízení.

    python3 tests_web/ui_test.py            # headless, snímky do tests_web/out/

Kontroluje: žádné chyby v konzoli, vykreslení grafů, přepínání jazyků, kalibraci,
nastavení, místa, Servis, záznamy, ztrátu spojení, světlý/tmavý režim, úzké displeje.
"""
import os
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

import mock_server  # noqa: E402
from playwright.sync_api import sync_playwright  # noqa: E402

OUT = os.path.join(HERE, "out")
os.makedirs(OUT, exist_ok=True)
HTML = os.path.join(OUT, "index.html")
subprocess.check_call([sys.executable, os.path.join(ROOT, "tools", "gen_web_assets.py"), "--html", HTML])

PORT = 8097
mock_server.serve(PORT, HTML)
BASE = "http://127.0.0.1:%d/" % PORT
DEV = mock_server.DEV

results = []


def open_secs(page):
    """Servis (v6.7): rozbalí všechny sbalitelné sekce."""
    page.wait_for_timeout(150)
    page.evaluate("document.querySelectorAll('details.sec').forEach((d) => { d.open = true; })")
    page.wait_for_timeout(150)


def check(name, cond, extra=""):
    results.append((name, bool(cond)))
    print(("  ok    " if cond else "  FAIL  ") + name + (("   " + str(extra)) if (extra and not cond) else ""))


def fake_bin(ver="v6.4", chip=0, magic=0xE9, safe=True, corrupt=False):
    """Minimální obraz aplikace ESP32 se správným XOR součtem (jako ho čte check_bin.py i stránka /update)."""
    import struct
    payload = struct.pack("<I", 0xABCD5432) + bytes(60)
    payload += b"\0" + ver.encode() + b"\0" + (b"ARBOLOG-SAFEMODE\0" if safe else b"\0") + bytes(range(256)) * 40
    hdr = bytearray(24)
    hdr[0] = magic
    hdr[1] = 1
    struct.pack_into("<H", hdr, 12, chip)
    seg = struct.pack("<II", 0x3F400020, len(payload)) + payload
    data = bytearray(hdr + seg)
    xor = 0xEF
    for b in payload:
        xor ^= b
    pad = (15 - len(data)) % 16
    data += bytes(pad) + bytes([xor ^ (1 if corrupt else 0)])
    return bytes(data)


def settle(page, ms=250):
    page.wait_for_timeout(ms)


def canvas_ink(page, sel):
    """Počet neprůhledných pixelů na canvasu (graf musí něco nakreslit)."""
    return page.evaluate(
        """(sel) => { const c = document.querySelector(sel); if (!c || !c.width) return -1;
        const d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data; let n = 0;
        for (let i = 3; i < d.length; i += 4) if (d[i] > 0) n++; return n; }""", sel)


def text(page, sel):
    return page.inner_text(sel).strip()


def wait_text(page, sel, pred, timeout=8000):
    t0 = time.time()
    while (time.time() - t0) * 1000 < timeout:
        v = text(page, sel)
        if pred(v):
            return v
        page.wait_for_timeout(150)
    return text(page, sel)


def toast_text(page):
    return page.inner_text("#toast").strip()


def watch_toasts(page):
    """Zaznamenává každé zobrazení hlášky (toast) – text hlášky totiž zůstává i po jejím zmizení."""
    page.evaluate("""() => { window.__toasts = [];
        new MutationObserver(() => window.__toasts.push(document.querySelector('#toast').textContent))
          .observe(document.querySelector('#toast'), { childList: true, characterData: true, subtree: true }); }""")


def pop_toasts(page):
    return page.evaluate("() => { const a = window.__toasts.slice(); window.__toasts.length = 0; return a; }")


with sync_playwright() as pw:
    browser = pw.chromium.launch()
    ctx = browser.new_context(viewport={"width": 390, "height": 844}, device_scale_factor=2, has_touch=True,
                              locale="cs-CZ", timezone_id="Europe/Prague", color_scheme="light")
    page = ctx.new_page()
    errors = []
    page.on("console", lambda m: errors.append("console." + m.type + ": " + m.text) if m.type in ("error",) else None)
    page.on("pageerror", lambda e: errors.append("pageerror: " + str(e)))
    page.on("dialog", lambda d: d.accept())

    # ------------------------------------------------------------------ přehled
    print("== Přehled")
    page.goto(BASE)
    page.wait_for_function("document.querySelector('#devId').textContent.length > 0", timeout=8000)
    settle(page, 700)
    check("ID zařízení v záhlaví", text(page, "#devId") == "0123456")
    check("hodiny tikají (HH:MM:SS)", len(text(page, "#clock")) == 8 and text(page, "#clock")[2] == ":")
    check("hodnota vlhkosti je číslo", text(page, "#hVal").isdigit(), text(page, "#hVal"))
    deg = float(page.evaluate("document.querySelector('#needle').dataset.deg"))
    pct = int(text(page, "#hVal"))
    check("ručka ukazuje hodnotu (240° stupnice)", abs(deg - (-120 + 2.4 * pct)) <= 1.3, (deg, pct))
    check("ikona na plochu: odkazy v hlavičce", page.locator("link[rel=apple-touch-icon]").count() == 1
          and page.locator("link[rel=manifest]").count() == 1)
    ico = page.evaluate("fetch('/apple-touch-icon.png').then(r => r.arrayBuffer()).then(b => new Uint8Array(b).slice(0, 8).join(','))")
    check("ikona na plochu: PNG se načte", ico == "137,80,78,71,13,10,26,10", ico)
    check("ciferník: stupnice a čísla", page.locator("#gFace text").count() >= 11 and page.locator("#gFace line").count() >= 50)
    check("stav vlhkosti zobrazen", text(page, "#hState") in ("V pořádku", "Zalít!", "Mokro"), text(page, "#hState"))
    check("graf vlhkosti má obsah", canvas_ink(page, "#chHum") > 3000, canvas_ink(page, "#chHum"))
    check("graf baterie má obsah", canvas_ink(page, "#chBat") > 500, canvas_ink(page, "#chBat"))
    check("bez horizontálního přetečení", page.evaluate("document.documentElement.scrollWidth <= window.innerWidth + 1"))
    check("lišta relace ukazuje čas", ":" in text(page, "#sessTxt"), text(page, "#sessTxt"))
    page.screenshot(path=os.path.join(OUT, "01_dash_cz.png"))

    for r in ("7d", "30d", "all", "24h"):
        page.click("#rangeSeg button[data-range='%s']" % r)
        settle(page, 250)
        check("rozsah %s: graf kreslí" % r, canvas_ink(page, "#chHum") > 3000)
        check("rozsah %s: tlačítko označeno" % r, page.evaluate("document.querySelector(\"#rangeSeg button[data-range='%s']\").classList.contains('on')" % r))
    page.click("#rangeSeg button[data-range='7d']")
    settle(page, 300)
    st = page.evaluate("Plot.all[0].stat")
    check("7 dní: vlhkost je čára (ne tečky)", st[0]["segs"] <= 3 and st[0]["pts"] > 100, st)
    check("7 dní: teplota je čára (ne tečky)", st[1]["segs"] <= 3 and st[1]["pts"] > 100, st)
    box = page.locator("#chHum").bounding_box()
    page.mouse.move(box["x"] + box["width"] * 0.62, box["y"] + box["height"] * 0.5)
    settle(page, 250)
    page.screenshot(path=os.path.join(OUT, "02_dash_tooltip_7d.png"))
    page.mouse.move(5, 5)

    page.click("#rangeSeg button[data-range='all']")
    settle(page, 300)
    page.screenshot(path=os.path.join(OUT, "03_dash_all.png"), full_page=True)
    st = page.evaluate("Plot.all[0].stat")
    check("vše (s vlastní osamocenou starou položkou): mezera mezi daty", st[0]["segs"] >= 2, st)

    # čistý log bez osamocené položky -> všechny záznamy jako souvislá čára s jedinou mezerou (výpadek)
    DEV.logtext = mock_server.make_log(20, stray=False)
    page.reload()
    page.wait_for_function("document.querySelector('#devId').textContent.length > 0", timeout=8000)
    settle(page, 800)
    page.click("#rangeSeg button[data-range='all']")
    settle(page, 400)
    st = page.evaluate("Plot.all[0].stat")
    check("vše: souvislá čára s jedinou mezerou", 1 <= st[0]["segs"] <= 3 and st[0]["pts"] > 100, st)
    page.screenshot(path=os.path.join(OUT, "03b_dash_all_clean.png"), full_page=True)
    page.click("#rangeSeg button[data-range='7d']")
    settle(page, 400)
    page.screenshot(path=os.path.join(OUT, "02b_dash_7d_clean.png"))
    box = page.locator("#chHum").bounding_box()
    page.mouse.move(box["x"] + box["width"] * 0.55, box["y"] + box["height"] * 0.5)
    settle(page, 300)
    page.screenshot(path=os.path.join(OUT, "02c_dash_7d_tooltip.png"))
    page.mouse.move(5, 5)
    DEV.logtext = mock_server.make_log(20)

    # ------------------------------------------------------------ zalévání
    print("== Zalévání (detekce v záznamech)")
    page.reload()
    page.wait_for_function("document.querySelector('#devId').textContent.length > 0", timeout=8000)
    settle(page, 900)
    page.click("#rangeSeg button[data-range='30d']")
    settle(page, 400)
    nev = page.locator("#wList .ev").count()
    check("souhrn: počet zalití je číslo > 0", text(page, "#wCount").isdigit() and int(text(page, "#wCount")) > 0, text(page, "#wCount"))
    check("souhrn: naposledy zalito", text(page, "#wLast").startswith("před"), text(page, "#wLast"))
    check("souhrn: nejdéle bez zalití", text(page, "#wGap") not in ("", "--"), text(page, "#wGap"))
    check("souhrn: min / Ø / max", text(page, "#wMinMax").count("/") == 2, text(page, "#wMinMax"))
    check("seznam zalití (nejvýš 6)", 1 <= nev <= 6, nev)
    check("seznam: nárůst v %", "+" in text(page, "#wList .ev .delta"), text(page, "#wList"))
    check("graf: pásy zalití", page.evaluate("Plot.all[0].markN") > 0, page.evaluate("Plot.all[0].markN"))
    check("legenda: zalití", "Zalití" in text(page, "#lgHum"), text(page, "#lgHum"))
    # detekce v JS: počet událostí odpovídá skokům v simulovaném logu (z <24 % na ~90 %)
    jumps = 0
    prev = None
    for ln in DEV.logtext.splitlines():
        prt = ln.split()
        h = None
        if len(prt) > 2 and prt[2].endswith("%") and prt[2][:-1].isdigit():
            h = int(prt[2][:-1])                       # formát do v6.12
        elif len(prt) > 1 and re.match(r"^\d{10}$", prt[0]) and prt[1].isdigit():
            h = int(prt[1])                            # krátký formát (od v6.13)
        if h is not None:
            if prev is not None and h - prev > 30:
                jumps += 1
            prev = h
    nall = page.evaluate("(()=>{const b=[...document.querySelectorAll('#rangeSeg button')].find(x=>x.dataset.range==='all');b.click();return +document.querySelector('#wCount').textContent})()")
    check("detekce: všechna zalití v logu nalezena", nall == jumps, "%s vs %s" % (nall, jumps))
    page.screenshot(path=os.path.join(OUT, "02d_dash_water.png"), full_page=True)
    # odpor půdy v kΩ na přehledu, technické údaje jen po zapnutí v Servisu
    check("přehled: odpor půdy v kΩ", text(page, "#hOhm").endswith("kΩ") or text(page, "#hOhm").endswith("MΩ"), text(page, "#hOhm"))
    check("přehled: bez teploty", "°C" not in text(page, "#v-dash .hero") and "°C" not in text(page, "#footer"), text(page, "#hSub"))
    check("přehled: odpor malým písmem v okénku měřáku", page.evaluate("getComputedStyle(document.querySelector('#hOhm')).fontSize") == "11px"
          and page.locator(".gauge-val #hOhm").count() == 1)
    check("přehled: bez technických údajů a bez grafu ADC", "ADC" not in text(page, "#hSub") and page.locator("#seriesSeg").count() == 0, text(page, "#hSub"))
    page.click("#tabs a[data-r='admin']")
    open_secs(page)
    settle(page, 400)
    page.check("#fTech")
    settle(page, 1500)
    page.click("#tabs a[data-r='dash']")
    settle(page, 2600)
    check("Servis: technické údaje zapnuty", "ADC" in text(page, "#hSub"), text(page, "#hSub"))
    check("graf vlhkosti %: smíšený log (starý i krátký formát) kreslí", page.evaluate("Plot.all[0].o.axes[0].max") == 100
          and canvas_ink(page, "#chHum") > 3000)
    page.click("#rangeSeg button[data-range='24h']")
    settle(page, 300)

    check("přehled: bez tlačítka Změřit teď (měří se samo)", page.locator("#btnMeasure").count() == 0
          and "obnovuje se samo každých 15 s" in text(page, "#hSub"), text(page, "#hSub"))

    # varování
    DEV.vbat = 2.95
    wait_text(page, "#batTxt", lambda v: v.startswith("2.95"))
    settle(page, 300)
    check("slabá baterie: banner", "Slabá baterie" in text(page, "#banners"), text(page, "#banners"))
    check("slabá baterie: úroveň 1", page.get_attribute("#bat", "data-lvl") == "1")
    DEV.vbat = 2.85
    wait_text(page, "#batTxt", lambda v: v.startswith("2.85"))
    settle(page, 300)
    check("kritická baterie: banner", "téměř vybitá" in text(page, "#banners"))
    DEV.vbat = 3.27
    DEV.flags = 2
    wait_text(page, "#batTxt", lambda v: v.startswith("3.27"))
    settle(page, 300)
    check("příznak naprázdno: banner", "naprázdno" in text(page, "#banners"))
    DEV.flags = 8
    settle(page, 2500)
    check("příznak rušení: banner", "Nestabilní" in text(page, "#banners"))
    DEV.flags = 0
    settle(page, 2500)

    DEV.clock_set = False
    settle(page, 2500)
    check("hodiny nenastavené: banner s tlačítkem", "nejsou nastavené" in text(page, "#banners") and page.locator("#banners button").count() == 1)
    page.screenshot(path=os.path.join(OUT, "04_dash_banner.png"))
    page.click("#banners button")
    settle(page, 2800)
    check("po nastavení času banner zmizí", "nejsou nastavené" not in text(page, "#banners"), text(page, "#banners"))
    check("zařízení dostalo čas z telefonu", DEV.clock_set is True and abs(DEV.clock_offset) < 10, DEV.clock_offset)

    # ---------------------------------------------------------------- nastavení
    print("== Nastavení")
    page.click("#tabs a[data-r='settings']")
    settle(page, 500)
    check("formulář: interval z nastavení zařízení", page.input_value("#fInt") == "30", page.input_value("#fInt"))
    check("interval: srozumitelné volby", page.eval_on_selector_all("#fInt option", "o => o.map(x => x.value)") == ["0", "10", "15", "30", "60", "120"]
          and "doporučeno" in page.inner_text("#fInt"), page.inner_text("#fInt"))
    check("nápověda intervalu: kdy bude další záznam", "Další záznam asi v" in text(page, "#intHint"), text(page, "#intHint"))
    check("Nastavení: bez tlačítek Uložit", page.locator("#v-settings button[type=submit]").count() == 1)  # jen „Přidat místo“
    check("Nastavení: doba běhu stránky je v Servisu", not page.is_visible("#fWeb"))
    page.select_option("#fInt", "60")
    settle(page, 3200)
    check("interval uložen hned po výběru", DEV.cfg["int"] == 60, DEV.cfg)
    hh = text(page, "#intHint")
    check("nápověda: další záznam na celou hodinu", hh.endswith(":00."), hh)
    check("potvrzení (toast)", "Uloženo" in toast_text(page), toast_text(page))
    DEV.cfg["int"] = 45  # dřív nastavená jiná hodnota: musí se objevit jako volba
    settle(page, 2600)
    check("interval mimo nabídku se ukáže", page.input_value("#fInt") == "45" and "45 min" in page.inner_text("#fInt"), page.input_value("#fInt"))
    check("kalibrace: kroky a stav", "zatím ne" in text(page, "#calStep1") and "Nezkalibrováno" in text(page, "#cDet")
          and "kalibrace místa" in text(page, "#calTitle").lower(), (text(page, "#calStep1"), text(page, "#cDet")))
    check("hodiny: stav jednou větou", "jdou správně" in text(page, "#clockState") or "liší" in text(page, "#clockState"), text(page, "#clockState"))
    check("pruh Kdy zalévat: ukazatel aktuální vlhkosti", page.is_visible("#thrNow") and "teď" in text(page, "#thrNowTxt"), text(page, "#thrNowTxt"))
    check("nastavení: bez odborných karet", not page.is_visible("#wireCard") and not page.is_visible("#formProbe")
          and not page.is_visible("#tabs a[data-r='files']"))
    page.fill("#fDry", "25")
    page.fill("#fWet", "80")
    page.press("#fWet", "Tab")
    settle(page, 3600)
    check("prahy uloženy samy (po opuštění pole)", DEV.cfg["dry"] == 25 and DEV.cfg["wet"] == 80, DEV.cfg)
    w = page.evaluate("document.querySelector('#thrZd').getBoundingClientRect().width / document.querySelector('#thrBar').getBoundingClientRect().width")
    check("pruh: zóna Zalít odpovídá 25 %", abs(w - 0.25) < 0.02, w)
    page.fill("#fWet", "20")
    page.press("#fWet", "Tab")
    settle(page, 1500)
    check("prahy: Mokro pod Zalít se neuloží", DEV.cfg["wet"] == 80 and page.input_value("#fWet") == "80" and "Mokro" in toast_text(page),
          (DEV.cfg["wet"], toast_text(page)))
    check("kalibrace: přehled v nastavení", text(page, "#cPct").isdigit() or text(page, "#cPct") == "--", text(page, "#cPct"))
    page.screenshot(path=os.path.join(OUT, "05_settings_cz.png"), full_page=True)

    # ------------------------------------------------------------------ místa
    print("== Místa (kalibrační profily)")
    rows = page.locator("#locList .locrow")
    check("místa: jedno výchozí, sonda je tady", rows.count() == 1 and DEV.cfg["locs"] == ["Místo 1"]
          and "Sonda je tady" in rows.nth(0).inner_text())
    check("místa: jediné místo nejde smazat", rows.nth(0).locator("[data-act=del]").count() == 0)
    check("místa: nezkalibrované místo je vidět", "nezkalibrováno" in rows.nth(0).inner_text(), rows.nth(0).inner_text())
    check("místa: formulář nového místa zavřený", not page.is_visible("#locNew"))
    page.click("#btnLocAdd")
    settle(page, 200)
    check("místa: nové místo – prázdný název a vysvětlení", page.input_value("#fLocNew") == "" and "Místo 1" in text(page, "#locNewHint"))
    page.click("#locNew button[type=submit]")
    settle(page, 300)
    check("místa: prázdný název se neodešle", DEV.cfg["locs"] == ["Místo 1"])
    page.fill("#fLocNew", "Lípa u školy")
    page.click("#locNew button[type=submit]")
    settle(page, 2500)
    check("místa: nové místo přidáno a vybráno", DEV.cfg["locs"] == ["Místo 1", "Lípa u školy"] and DEV.cfg["loc"] == 1, DEV.cfg["locs"])
    check("místa: seznam ukazuje 2 místa, sonda na novém", rows.count() == 2 and "Sonda je tady" in rows.nth(1).inner_text()
          and not page.is_visible("#locNew"))
    rows.nth(1).locator("[data-act=ren]").click()
    settle(page, 200)
    check("místa: přejmenování předvyplní název", page.input_value("#fLocRen") == "Lípa u školy")
    page.fill("#fLocRen", "Lípa – sever")
    page.click("#locList form button[type=submit]")
    settle(page, 2500)
    check("místa: přejmenováno", DEV.cfg["locs"][1] == "Lípa – sever" and page.locator("#fLocRen").count() == 0, DEV.cfg["locs"])
    # přesun sondy klepnutím na řádek (s potvrzením) a zpět
    rows.nth(0).locator(".locname").click()
    settle(page, 2500)
    check("místa: klepnutí přesune sondu", DEV.cfg["loc"] == 0 and "Sonda je tady" in rows.nth(0).inner_text(), DEV.cfg["loc"])
    rows.nth(1).locator(".locname").click()
    settle(page, 2500)
    check("místa: zpět na druhé místo", DEV.cfg["loc"] == 1, DEV.cfg["loc"])
    page.screenshot(path=os.path.join(OUT, "05c_places.png"), full_page=True)
    # záznamy druhého místa: graf ukazuje jen ty
    lines = DEV.logtext.splitlines(True)
    half = len(lines) // 2
    DEV.logtext = "".join(lines[:half]) + "".join(
        ln.replace("\r\n", " p2\r\n") if (ln[:2] == "20" and "%" in ln) or re.match(r"^\d{10} ", ln) else ln for ln in lines[half:])
    page.click("#tabs a[data-r='dash']")
    page.reload()
    page.wait_for_function("document.querySelector('#devId').textContent.length > 0", timeout=8000)
    settle(page, 1200)
    check("přehled: výběr místa viditelný", page.is_visible("#dashLoc") and page.input_value("#dashLoc") == "1")
    page.click("#rangeSeg button[data-range='7d']")
    settle(page, 400)
    pts2 = page.evaluate("Plot.all[0].stat")[0]["pts"]
    page.select_option("#dashLoc", "0")
    settle(page, 2800)
    check("přehled: přepnutí místa", DEV.cfg["loc"] == 0, DEV.cfg["loc"])
    st1 = page.evaluate("Plot.all[0].stat")
    pts1 = st1[0]["pts"] if st1 else 0
    check("graf: jen záznamy vybraného místa (staré místo 1 za 7 dní nic)", pts1 == 0 and pts2 > 100, (pts1, pts2))
    check("graf: prázdné místo hlásí 'žádné záznamy'", page.is_visible("#chHumMsg") and len(text(page, "#chHumMsg")) > 0)
    page.screenshot(path=os.path.join(OUT, "05d_dash_place.png"))
    page.click("#tabs a[data-r='logs']")
    settle(page, 1200)
    with page.expect_download() as dl:
        page.click("#btnCsv")
    csvp = open(dl.value.path(), encoding="utf-8-sig").read().splitlines()
    hdr = csvp[0].split(";")
    check("CSV: sloupec místa", "misto" in hdr, csvp[0])
    if "misto" in hdr:
        mi = hdr.index("misto")
        names = set(r.split(";")[mi].strip('"') for r in csvp[1:])
        check("CSV: názvy míst", {"Místo 1", "Lípa – sever"} <= names, names)
    page.click("#tabs a[data-r='settings']")
    settle(page, 500)
    page.locator("#locList .locrow").nth(1).locator("[data-act=del]").click()
    settle(page, 2500)
    check("místa: smazáno (potvrzeno dialogem)", DEV.cfg["locs"] == ["Místo 1"] and DEV.cfg["loc"] == 0, DEV.cfg["locs"])
    check("místa: záznamy smazaného místa nepřešly k jinému (p2 -> p0)", " p2" not in DEV.logtext and " p0" in DEV.logtext,
          DEV.logtext[-200:])
    DEV.logtext = mock_server.make_log(20)

    # ------------------------------------------------------------------ Servis
    print("== Servis")
    page.click("#tabs a[data-r='admin']")
    open_secs(page)
    settle(page, 600)
    svc = text(page, "#svcList")
    check("Servis: stav sondy (baterie, hodiny, sonda, kalibrace, záznam, firmware)",
          all(x in svc for x in ("Baterie", "Hodiny", "Sonda", "Kalibrace", "Záznam", "Firmware")), svc)
    check("Servis: neověřené zapojení má tlačítko Opravit", page.locator("#svcList button[data-go=secProbe]").count() == 1, svc)
    check("Servis: shrnutí sekcí", "GPIO32" in text(page, "#sumProbe") and "krátký pulz" in text(page, "#sumProbe")
          and "8 %" in text(page, "#sumWater") and "normální vysílač" in text(page, "#sumNet"),
          (text(page, "#sumProbe"), text(page, "#sumWater"), text(page, "#sumNet")))
    # (výjimka: heslo Wi-Fi od v7.0 se ukládá tlačítkem, protože zařízení restartuje)
    check("Servis: bez tlačítek Uložit u voleb", page.locator("#formNet button:not(#btnPw), #formPm button, #formRet button, #formWire button, #formAdv button").count() == 0)
    page.evaluate("document.querySelector('#secProbe').open = false")
    page.click("#svcList button[data-go=secProbe]")
    settle(page, 500)
    check("Servis: Opravit otevře sekci Sonda", page.evaluate("document.querySelector('#secProbe').open") and page.is_visible("#btnWire"))
    check("Servis: obnovování živé hodnoty výchozí 15 s", page.input_value("#fLive") == "15" and "obnova 15 s" in text(page, "#sumProbe"),
          text(page, "#sumProbe"))
    page.select_option("#fLive", "30")
    settle(page, 3000)
    check("Servis: obnovování 30 s uloženo", DEV.cfg["live"] == 30 and "obnova 30 s" in text(page, "#sumProbe"), DEV.cfg["live"])
    page.select_option("#fLive", "15")
    settle(page, 3000)
    check("Servis: po připojení výchozí nic", page.input_value("#fCport") == "0")
    for v in ("2", "1", "0"):
        page.select_option("#fCport", v)
        settle(page, 3000)
        check("Servis: po připojení = %s" % v, DEV.cfg["cport"] == int(v), DEV.cfg["cport"])
    # vysílač Wi-Fi (v6.5)
    check("Servis: vysílač výchozí nízký", page.input_value("#fTxp") == "1")
    page.select_option("#fTxp", "0")
    settle(page, 3000)
    check("Servis: vysílač normální uložen", DEV.cfg["txp"] == 0, DEV.cfg["txp"])
    page.select_option("#fTxp", "1")
    settle(page, 3000)
    check("Servis: vysílač zpět nízký", DEV.cfg["txp"] == 1, DEV.cfg["txp"])
    # heslo Wi-Fi (v7.0): výchozí otevřená síť, krátké heslo se neodešle, platné se uloží, pak zase zrušit
    check("Servis: heslo výchozí vypnuté", not page.is_checked("#fPwOn") and page.is_hidden("#pwRow"))
    page.check("#fPwOn")
    check("Servis: heslo – pole se ukáže", page.is_visible("#fPw"))
    page.fill("#fPw", "kratke")
    page.click("#btnPw")
    settle(page, 800)
    check("Servis: krátké heslo se neodešle", DEV.cfg["pw"] == 0, DEV.cfg["pw"])
    page.fill("#fPw", "tajne-heslo")
    page.click("#btnPw")
    settle(page, 2500)
    check("Servis: heslo uloženo", DEV.cfg["pw"] == 1 and getattr(DEV, "wifipw", "") == "tajne-heslo", DEV.cfg["pw"])
    check("Servis: stav hesla", "chráněná" in page.inner_text("#pwState"), page.inner_text("#pwState"))
    page.uncheck("#fPwOn")
    page.click("#btnPw")
    settle(page, 2500)
    check("Servis: heslo zrušeno", DEV.cfg["pw"] == 0, DEV.cfg["pw"])
    # spotřeba (v6.5): odhad z čítačů
    txt = page.inner_text("#pwrRows")
    check("Servis: spotřeba ukazuje probuzení/záznamy", "146 / 144" in txt, txt)
    check("Servis: spotřeba ukazuje odchylku časovače", "+4.1 %" in txt, txt)
    check("Servis: spotřeba ukazuje odhad výdrže", "mAh" in txt and "dní" in txt, txt)
    page.screenshot(path=os.path.join(OUT, "05a_admin.png"), full_page=True)
    # ruční zápis (přenosný logger, v6.10)
    print("== Ruční zápis")
    log_before = DEV.logtext
    check("ruční zápis: výchozí vypnutý", not page.is_checked("#fMan") and "vypnutý" in text(page, "#sumMan"))
    page.check("#fMan")
    settle(page, 3000)
    check("ruční zápis: zapnut v Servisu", DEV.cfg["man"] == 1 and "automatické číslo" in text(page, "#sumMan"), DEV.cfg["man"])
    page.click("#tabs a[data-r='dash']")
    settle(page, 2600)
    check("ruční zápis: karta na Přehledu", page.is_visible("#manCard") and page.is_visible("#btnMan") and not page.is_visible("#fManNote"))
    check("ruční zápis: ukáže, jak se zápis pojmenuje", "Bod 1" in text(page, "#manAuto"), text(page, "#manAuto"))
    page.click("#btnMan")
    settle(page, 3200)
    check("ruční zápis: řádek v logu se značkou M a číslem", "M # Bod 1\r\n" in DEV.logtext, DEV.logtext[-120:])
    check("ruční zápis: potvrzení", "Zapsáno" in toast_text(page), toast_text(page))
    page.click("#btnMan")
    settle(page, 3200)
    check("ruční zápis: další číslo", "M # Bod 2\r\n" in DEV.logtext and "Bod 3" in text(page, "#manAuto"), text(page, "#manAuto"))
    check("ruční zápis: seznam posledních zápisů", page.locator("#manList .manrow").count() == 2 and "Bod 2" in text(page, "#manList"))
    DEV.cfg["mnote"] = 1
    settle(page, 2600)
    check("ruční zápis: vlastní poznámka – pole se ukáže", page.is_visible("#fManNote"))
    page.fill("#fManNote", "Lípa 7")
    page.click("#btnMan")
    settle(page, 3200)
    check("ruční zápis: vlastní poznámka zapsána", "M # Lípa 7\r\n" in DEV.logtext, DEV.logtext[-80:])
    check("ruční zápis: předvyplní další číslo", page.input_value("#fManNote") == "Lípa 8", page.input_value("#fManNote"))
    page.click("#tabs a[data-r='logs']")
    settle(page, 1500)
    check("Záznamy: tabulka ručních měření", page.is_visible("#manLogCard") and page.locator("#manTbl tr").count() == 4
          and "Lípa 7" in text(page, "#manTbl"), text(page, "#manTbl"))
    page.click("#tabs a[data-r='admin']")
    open_secs(page)
    page.uncheck("#fMan")
    settle(page, 3000)
    DEV.cfg["mnote"] = 2
    page.click("#tabs a[data-r='dash']")
    settle(page, 2600)
    check("ruční zápis: vypnutý = bez karty", DEV.cfg["man"] == 0 and not page.is_visible("#manCard"))
    DEV.logtext = log_before
    page.click("#tabs a[data-r='admin']")
    open_secs(page)
    settle(page, 400)
    # šetřící režim (v6.5): upozornění na přehledu
    DEV.pwr["save"] = True
    DEV.pwr["int"] = 60
    page.click("#tabs a[data-r='dash']")
    settle(page, 2600)
    bt = page.inner_text("#banners") if page.locator("#banners").count() else page.inner_text("body")
    check("Přehled: upozornění na šetřící režim", "Šetřící režim" in bt and "60 min" in bt, bt[:300])
    DEV.pwr["save"] = False
    page.click("#tabs a[data-r='admin']")
    open_secs(page)
    settle(page, 600)
    # místa a kalibrace: úprava, export, import
    print("== Servis: místa a kalibrace")
    import json as _json
    check("místa: řádek za každé místo", page.locator("#locsRows .lrow").count() == len(DEV.cfg["locs"]) == 1)
    thr0 = [DEV.cfg["dry"], DEV.cfg["wet"]]  # nové místo převezme prahy vybraného
    page.click("#btnLocsAdd")
    rows = page.locator("#locsRows .lrow")
    check("místa: přidán řádek", rows.count() == 2 and rows.nth(1).locator("[data-k=name]").input_value() == "Místo 2")
    rows.nth(1).locator("[data-k=name]").fill("Buk – sever")
    rows.nth(1).locator("[data-k=gw]").fill("2,5")
    rows.nth(1).locator("[data-k=gd]").fill("2,4")
    page.click("#btnLocsSave")
    settle(page, 600)
    check("místa: neplatná kalibrace se neodešle", len(DEV.cfg["locs"]) == 1 and "neplatné" in toast_text(page)
          and "bad" in (rows.nth(1).get_attribute("class") or ""), toast_text(page))
    rows.nth(1).locator("[data-k=gd]").fill("0,08")
    rows.nth(0).locator("[data-k=dry]").fill("28")
    page.click("#btnLocsSave")
    settle(page, 3000)
    check("místa: uloženo vše najednou", DEV.cfg["locs"] == ["Místo 1", "Buk – sever"] and DEV.cfg["locd"][1] == thr0 + [0.08, 2.5]
          and DEV.cfg["loc"] == 0 and DEV.cfg["dry"] == 28, (DEV.cfg["locs"], DEV.cfg["locd"]))
    check("místa: seznam v Nastavení zná nové místo", page.locator("#locList .locrow").count() == 2)
    with page.expect_download() as dl:
        page.click("#btnLocsExp")
    exp = _json.loads(open(dl.value.path(), encoding="utf-8").read())
    check("export: formát a data", exp.get("format") == "arbolok-mista" and dl.value.suggested_filename.startswith("mista_0123456_")
          and exp["mista"][1] == {"nazev": "Buk – sever", "zalevat_pod": thr0[0], "mokro_nad": thr0[1], "g_sucho": 0.08, "g_mokro": 2.5}, exp)
    exp["mista"].append({"nazev": "Dub", "zalevat_pod": 20, "mokro_nad": 70, "g_sucho": 0.1, "g_mokro": 4})
    exp["mista"][0]["g_mokro"] = 3.5
    page.set_input_files("#fLocsImp", files=[{"name": "m.json", "mimeType": "application/json",
                                              "buffer": _json.dumps(exp, ensure_ascii=False).encode("utf-8")}])
    settle(page, 500)
    check("import: načteno do formuláře, ještě neuloženo", page.locator("#locsRows .lrow").count() == 3 and len(DEV.cfg["locs"]) == 2
          and "Načteno míst: 3" in toast_text(page), toast_text(page))
    settle(page, 2500)
    check("import: rozpracovaný formulář nepřepíše polling", page.locator("#locsRows .lrow").count() == 3)
    page.screenshot(path=os.path.join(OUT, "05f_places_admin.png"), full_page=True)
    page.click("#btnLocsSave")
    settle(page, 3000)
    check("import: uloženo", DEV.cfg["locs"] == ["Místo 1", "Buk – sever", "Dub"] and DEV.cfg["gw"] == 3.5, (DEV.cfg["locs"], DEV.cfg["gw"]))
    bad = _json.dumps({"mista": [{"nazev": "X", "zalevat_pod": 80, "mokro_nad": 20, "g_sucho": 1, "g_mokro": 2}]}).encode()
    page.set_input_files("#fLocsImp", files=[{"name": "b.json", "mimeType": "application/json", "buffer": bad}])
    settle(page, 500)
    check("import: neplatný soubor odmítnut", "neplatné" in toast_text(page) and page.locator("#locsRows .lrow").count() == 3, toast_text(page))
    page.set_input_files("#fLocsImp", files=[{"name": "c.json", "mimeType": "application/json", "buffer": b"nesmysl"}])
    settle(page, 500)
    check("import: nečitelný soubor", "nepodařilo přečíst" in toast_text(page), toast_text(page))
    # zpět jedno místo (zbytek testů s tím počítá)
    while page.locator("#locsRows .lrow").count() > 1:
        page.locator("#locsRows .lrow-x").last.click()
    r0 = page.locator("#locsRows .lrow").first
    for k, v in (("name", "Místo 1"), ("dry", str(thr0[0])), ("wet", str(thr0[1])), ("gd", "0.05"), ("gw", "3")):
        r0.locator("[data-k=%s]" % k).fill(v)
    page.click("#btnLocsSave")
    settle(page, 3000)
    check("místa: zpět jedno", DEV.cfg["locs"] == ["Místo 1"], DEV.cfg["locs"])
    check("Servis: bez nastavení parametrů pulzu", page.locator("#formProbe, #fMaxms, #fEarly").count() == 0)
    check("Servis: způsob měření výchozí krátký pulz", page.input_value("#fPmode") == "2")
    check("Servis: bez zálohy sondy", page.locator("#bakCard, #btnBakExp").count() == 0)
    check("Servis: jen dva způsoby měření", page.eval_on_selector_all("#fPmode option", "o => o.map(x => x.value)") == ["2", "1"])
    check("Servis: druhá elektroda jen GND/GPIO14, BIAS bez GPIO25", page.eval_on_selector_all("#fRet option", "o => o.map(x => x.value)") == ["0", "14"]
          and page.eval_on_selector_all("#fBias option", "o => o.map(x => x.value)") == ["0", "32", "26"])
    page.select_option("#fPmode", "1")
    settle(page, 3000)
    check("Servis: dlouhý pulz pro starší sondy uložen", DEV.cfg["pmode"] == 1, DEV.cfg["pmode"])
    page.select_option("#fPmode", "2")
    settle(page, 3000)
    check("Servis: zpět krátký pulz", DEV.cfg["pmode"] == 2, DEV.cfg["pmode"])
    page.fill("#fWjmp", "15")
    page.fill("#fWwin", "12")
    page.click("#formWdet button[type=submit]")
    settle(page, 3000)
    check("vyhodnocení zalití: uloženo", DEV.cfg["wjmp"] == 15 and DEV.cfg["wwin"] == 12, DEV.cfg)
    DEV.cfg["wjmp"] = 8
    DEV.cfg["wwin"] = 6
    check("druhá elektroda: výchozí GND", page.input_value("#fRet") == "0")
    page.select_option("#fRet", "14")
    settle(page, 3000)
    check("druhá elektroda: uloženo", DEV.cfg["ret"] == 14, DEV.cfg)
    check("nepřepojená elektroda: varování u sondy", "Obrácená polarita" in text(page, "#lWarn"), text(page, "#lWarn"))
    DEV.real_ret = 14
    page.select_option("#fRet", "14")
    settle(page, 3000)
    check("přepojená elektroda: bez varování, fáze v detailu", not page.is_visible("#lWarn") and "poměr obou" in text(page, "#lDet"),
          text(page, "#lDet"))
    page.screenshot(path=os.path.join(OUT, "05b_settings_expert.png"), full_page=True)
    page.select_option("#fRet", "0")
    settle(page, 3000)
    check("druhá elektroda: zpět na GND", DEV.cfg["ret"] == 0 and DEV.flags & 32 == 0, DEV.cfg)
    DEV.real_ret = 0

    # kalibrace (tlačítka v Nastavení, detail a reset v Servisu)
    page.click("#tabs a[data-r='settings']")
    DEV.raw = 3300.0
    page.wait_for_timeout(2500)
    page.click("#btnDry")
    settle(page, 3000)
    check("kalibrace sucho uložena", abs(DEV.cfg["gd"] - (4095 - 3300) / 3300) < 0.01, DEV.cfg["gd"])
    check("kalibrace sucho: toast", "sucho" in toast_text(page).lower(), toast_text(page))
    DEV.raw = 1500.0
    page.wait_for_timeout(2500)
    DEV.fail_next = "cal"
    page.click("#btnWet")
    settle(page, 3000)
    check("chyba kalibrace: červený toast", "není platná" in toast_text(page) and "err" in page.get_attribute("#toast", "class"), toast_text(page))
    page.click("#btnWet")
    settle(page, 3000)
    check("kalibrace mokro uložena", abs(DEV.cfg["gw"] - (4095 - 1500) / 1500) < 0.01, DEV.cfg["gw"])
    settle(page, 600)
    check("kalibrace: oba kroky hotové, stav Zkalibrováno", "hotovo" in text(page, "#calStep1") and "hotovo" in text(page, "#calStep2")
          and "Zkalibrováno" in text(page, "#cDet"), (text(page, "#calStep2"), text(page, "#cDet")))
    check("Servis: bez karty kalibrace", page.locator("#calCard, #btnOpen, #calNow").count() == 0)
    page.click("#btnCalReset")
    settle(page, 3000)
    check("výchozí kalibrace (v Nastavení, s potvrzením)", DEV.cfg["gd"] == 0.05 and DEV.cfg["gw"] == 3.0)
    page.click("#tabs a[data-r='admin']")
    open_secs(page)
    settle(page, 400)
    check("Servis: odpor zeminy v detailu sondy", "kΩ" in text(page, "#lDet") or "MΩ" in text(page, "#lDet"), text(page, "#lDet"))

    # profil
    mock_server.PROF_KIND[0] = "ok"
    page.click("#btnProf")
    settle(page, 3500)
    check("profil: graf viditelný", page.is_visible("#profBox"))
    check("profil: graf kreslí", canvas_ink(page, "#chProf") > 800, canvas_ink(page, "#chProf"))
    check("profil ok: verdikt zelený", "good" in page.get_attribute("#profVerdict", "class"), text(page, "#profVerdict"))
    mock_server.PROF_KIND[0] = "rise"
    page.click("#btnProf")
    settle(page, 3500)
    check("profil stoupající: varování", "stoupá" in text(page, "#profVerdict"), text(page, "#profVerdict"))
    check("profil stoupající: bez poplachu (krátký pulz), vysvětlení", "začátku pulzu" in text(page, "#profVerdict")
          and "bad" not in page.get_attribute("#profVerdict", "class"), text(page, "#profVerdict"))
    page.screenshot(path=os.path.join(OUT, "06_settings_profile.png"), full_page=True)

    # pokročilé
    page.select_option("#fScale", "0")
    page.check("#fVexc")
    settle(page, 3200)
    check("pokročilé uloženo", DEV.cfg["scale"] == 0 and DEV.cfg["vexc"] is True, DEV.cfg)
    check("pokročilé: bez nastavení pull-upu", page.locator("#fRpull").count() == 0)
    page.select_option("#fScale", "1")
    page.uncheck("#fVexc")
    settle(page, 3200)
    page.fill("#fVbat", "3.30")
    page.click("#formVbat button[type=submit]")
    settle(page, 3200)
    check("kalibrace baterie", abs(DEV.vbat - 3.30) < 1e-6 and "kalibrován" in toast_text(page), toast_text(page))
    page.screenshot(path=os.path.join(OUT, "07_settings_adv.png"), full_page=True)

    # ----------------------------------------------------------- zapojení sondy
    print("== Zapojení sondy")
    # Scénář z provozu: deska má BIAS na GPIO26, sestavení buzí GPIO32 -> uzel visí, čítač ~0 a "zkrat"
    DEV.real_bias = 26
    DEV.cfg["bias"] = 0
    DEV.cfg["biasUsed"] = 32
    DEV.wire = dict(have=False, res=0, pin=0, n=0, p=[], lo=[], hi=[])
    DEV.flags = 4
    page.click("#tabs a[data-r='dash']")
    settle(page, 2800)
    check("chyba sondy: místo procent '--'", text(page, "#hVal") == "--", text(page, "#hVal"))
    check("chyba sondy: ručka na dorazu pod nulou", float(page.evaluate("document.querySelector('#needle').dataset.deg")) < -120)
    check("neověřené zapojení: stav 'Sonda neměří'", text(page, "#hState") == "Sonda neměří", text(page, "#hState"))
    check("neověřené zapojení: banner bez slova zkrat, odkazuje na zapojení", "zapojení" in text(page, "#banners")
          and "zkrat" not in text(page, "#banners").lower(), text(page, "#banners"))
    check("chyba sondy: banner má tlačítko k zapojení", page.locator("#banners button").count() == 1 and "Zapojení sondy" in text(page, "#banners button"))
    page.screenshot(path=os.path.join(OUT, "17_dash_fault.png"))
    page.evaluate("document.querySelector('#secProbe').open = false")
    page.click("#banners button")
    settle(page, 700)
    check("tlačítko v banneru otevře Servis", page.evaluate("location.hash") == "#/admin", page.evaluate("location.hash"))
    check("karta zapojení je vidět", page.is_visible("#wireCard"))
    check("karta: pin ze sestavení, nepotvrzený", "GPIO32" in text(page, "#wirePin") and "nepotvrzeno" in text(page, "#wirePin"), text(page, "#wirePin"))
    check("karta: výběr pinu je 'Automaticky'", page.input_value("#fBias") == "0", page.input_value("#fBias"))
    check("varování u sondy říká totéž", "zapojení" in text(page, "#lWarn"), text(page, "#lWarn"))
    check("u chyby se ukazuje ADC, ne procenta", page.text_content("#cPct") == "--" and "ADC" in text(page, "#lDet"), page.text_content("#cPct") + " | " + text(page, "#lDet"))
    gd0 = DEV.cfg["gd"]
    page.click("#tabs a[data-r='settings']")
    settle(page, 400)
    page.click("#btnDry")
    settle(page, 3000)
    check("kalibrace při chybě sondy: červená hláška bez slova zkrat", "přelitá" in toast_text(page) and "zkrat" not in toast_text(page)
          and "err" in page.get_attribute("#toast", "class"), toast_text(page))
    check("kalibrace při chybě sondy se neuložila", DEV.cfg["gd"] == gd0)
    page.click("#tabs a[data-r='admin']")
    open_secs(page)
    settle(page, 400)

    page.click("#btnWire")
    settle(page, 3200)
    check("test zapojení: nalezeno GPIO26", "Nalezeno: BIAS je na GPIO26" in text(page, "#wireRes"), text(page, "#wireRes"))
    check("test zapojení: zelený verdikt", "good" in page.get_attribute("#wireRes", "class"))
    check("test zapojení: řádky kandidátů", "GPIO32: 2 → 5 · bez odezvy" in text(page, "#wireRes") and
          "GPIO26: 2 → 4090 · uzel reaguje" in text(page, "#wireRes") and "GPIO25" in text(page, "#wireRes"), text(page, "#wireRes"))
    check("test zapojení: pin uložen na zařízení", DEV.cfg["bias"] == 26 and DEV.cfg["biasUsed"] == 26, DEV.cfg)
    check("test zapojení: potvrzení (toast)", "Zapojení nalezeno" in toast_text(page), toast_text(page))
    check("karta: nový pin bez 'nepotvrzeno'", "GPIO26" in text(page, "#wirePin") and "nepotvrzeno" not in text(page, "#wirePin"), text(page, "#wirePin"))
    check("výběr pinu ukazuje GPIO26", page.input_value("#fBias") == "26", page.input_value("#fBias"))
    check("po nalezení zmizí varování u sondy", not page.is_visible("#lWarn"))
    page.screenshot(path=os.path.join(OUT, "18_wiring_found.png"), full_page=True)
    page.click("#tabs a[data-r='dash']")
    settle(page, 2800)
    check("po nalezení se zase ukazuje vlhkost", text(page, "#hVal").isdigit(), text(page, "#hVal"))
    check("po nalezení zmizí banner chyby", "zapojení" not in text(page, "#banners"), text(page, "#banners"))
    # ověřené zapojení a skoro nulový odpor = přelitá půda (žádná „chyba“, žádný „zkrat“)
    DEV.flags = 4
    settle(page, 2800)
    check("přelitá půda: stav 'Přelito'", text(page, "#hState") == "Přelito", text(page, "#hState"))
    check("přelitá půda: ručka na straně Mokro", float(page.evaluate("document.querySelector('#needle').dataset.deg")) > 115)
    check("přelitá půda: klidný pruh bez tlačítka a bez slova zkrat", "přelitá" in text(page, "#banners")
          and page.locator("#banners button").count() == 0 and "zkrat" not in text(page, "#banners").lower(), text(page, "#banners"))
    DEV.flags = 0
    settle(page, 2800)

    # ruční volba špatného pinu: uzel nereaguje (jiná chyba než zkrat), zpět na "Automaticky"
    page.click("#tabs a[data-r='admin']")
    open_secs(page)
    settle(page, 500)
    page.select_option("#fBias", "32")
    settle(page, 3200)
    check("ruční pin uložen", DEV.cfg["bias"] == 32 and DEV.cfg["biasUsed"] == 32, DEV.cfg)
    page.click("#tabs a[data-r='dash']")
    settle(page, 2800)
    check("špatný pin: banner 'nereaguje na buzení'", "nereaguje na buzení" in text(page, "#banners"), text(page, "#banners"))
    check("špatný pin: '--' místo procent", text(page, "#hVal") == "--")
    page.click("#tabs a[data-r='admin']")
    open_secs(page)
    settle(page, 500)
    page.select_option("#fBias", "0")
    settle(page, 3200)
    check("Automaticky: pin se rozpoznal a uložil", DEV.cfg["bias"] == 26 and DEV.cfg["biasUsed"] == 26, DEV.cfg)

    # nic nereaguje (např. zkrat sondy, jiný pin ADC)
    DEV.real_bias = None
    DEV.flags = 4
    page.click("#btnWire")
    settle(page, 3200)
    check("nic nereaguje: červený verdikt s radou", "bad" in page.get_attribute("#wireRes", "class") and "GPIO35" in text(page, "#wireRes"), text(page, "#wireRes"))
    check("nic nereaguje: chybová hláška", "Žádný pin nereaguje" in toast_text(page) and "err" in page.get_attribute("#toast", "class"), toast_text(page))
    check("nic nereaguje: žádný pin nenalezen", all(" · bez odezvy" in ln for ln in text(page, "#wireRes").split("\n")[1:]), text(page, "#wireRes"))
    DEV.real_bias = 26
    DEV.cfg["biasUsed"] = 26
    DEV.flags = 0
    settle(page, 2500)

    # jazyky
    print("== Jazyky")
    page.click("#tabs a[data-r='settings']")
    settle(page, 400)
    page.click("#langs button[data-lang='en']")
    settle(page, 500)
    check("EN: záložky", text(page, "#tabs a[data-r='dash']") == "Overview")
    check("EN: místa přeložena", "place" in page.text_content("#locCard h2").lower(), page.text_content("#locCard h2"))
    check("EN: karta zapojení přeložena", page.text_content("#wireCard h3").lower() == "probe wiring" and page.text_content("#btnWire").strip() == "Find wiring", text(page, "#wireCard h3"))
    check("EN: nastaveno na zařízení", DEV.cfg["lang"] == "en")
    page.screenshot(path=os.path.join(OUT, "08_settings_en.png"))
    page.click("#langs button[data-lang='ua']")
    settle(page, 500)
    check("UA: záložky", text(page, "#tabs a[data-r='dash']") == "Огляд")
    check("UA: karta zapojení přeložena", page.text_content("#wireCard h3").lower() == "підключення зонда" and page.text_content("#btnWire").strip() == "Знайти підключення", text(page, "#wireCard h3"))
    check("UA: lang atribut", page.get_attribute("html", "lang") == "uk")
    page.screenshot(path=os.path.join(OUT, "09_settings_ua.png"))
    page.click("#tabs a[data-r='dash']")
    settle(page, 700)
    check("UA: hlavní stránka", text(page, "#hState") in ("Полити!", "Усе гаразд", "Волого"), text(page, "#hState"))
    page.screenshot(path=os.path.join(OUT, "10_dash_ua.png"))
    page.click("#tabs a[data-r='settings']")
    page.click("#langs button[data-lang='cz']")
    settle(page, 3200)  # počkat, až se projeví i polling
    check("CZ zpět (stav nepřepsán starou hodnotou)", text(page, "#tabs a[data-r='dash']") == "Přehled", text(page, "#tabs a[data-r='dash']"))

    # ------------------------------------------------------------------ záznamy
    # ------------------------------------------------------------ stránka nahrávání firmwaru
    print("== Stránka /update")
    up = ctx.new_page()
    up.goto(BASE + "update")
    up.wait_for_function("document.querySelector('#curVer').textContent !== '--'", timeout=5000)
    check("update: běžící verze ze stavu", up.inner_text("#curVer") == DEV.fw, up.inner_text("#curVer"))
    check("update: tlačítko bez souboru vypnuté", up.is_disabled("#go"))

    def put(name, data):
        up.set_input_files("#file", files=[{"name": name, "mimeType": "application/octet-stream", "buffer": data}])
        up.wait_for_timeout(400)

    put("littlefs.bin", b"\x00" * 4096)
    check("update: littlefs.bin odmítnut", up.locator("#checks li.bad").count() >= 1 and up.is_disabled("#go"), up.inner_text("#checks"))
    put("c3.bin", fake_bin(chip=5))
    check("update: obraz pro ESP32-C3 odmítnut", "ESP32-C3" in up.inner_text("#checks") and up.is_disabled("#go"))
    put("poskozeny.bin", fake_bin(corrupt=True))
    check("update: poškozený (XOR) odmítnut", "součet" in up.inner_text("#checks") and up.is_disabled("#go"))
    put("velky.bin", fake_bin() + bytes(1306624))
    check("update: přes limit OTA odmítnut", "OTA" in up.inner_text("#checks .bad") and up.is_disabled("#go"))
    put("v3.bin", fake_bin(safe=False))
    check("update: bez nouzového režimu jen varování + potvrzení", up.locator("#checks li.bad").count() == 0
          and up.is_visible("#forceRow") and up.is_disabled("#go"))
    up.check("#force")
    check("update: po potvrzení lze nahrát", up.is_enabled("#go"))
    put("arbolog_v6.4.bin", fake_bin("v6.4"))
    check("update: platný soubor: verze ze souboru", "v6.4" in up.inner_text("#checks") and up.locator("#checks li.bad").count() == 0,
          up.inner_text("#checks"))
    check("update: platný soubor: lze nahrát bez potvrzení", up.is_enabled("#go") and not up.is_visible("#forceRow"))
    up.screenshot(path=os.path.join(OUT, "08_update_ready.png"), full_page=True)
    up.click("#go")
    up.wait_for_function("document.querySelector('#msg').textContent.indexOf('v6.4') >= 0", timeout=30000)
    check("update: soubor došel na zařízení", DEV.ota_body.find(b"v6.4") >= 0)
    check("update: po restartu hlásí novou verzi a odkaz", "v6.4-mock" in up.inner_text("#msg") and up.locator("#msg a").count() == 1,
          up.inner_text("#msg"))
    check("update: průběh 100 %", up.inner_text("#progP") == "100 %")
    up.screenshot(path=os.path.join(OUT, "08_update_done.png"), full_page=True)
    up.set_viewport_size({"width": 320, "height": 700})
    check("update: 320 px bez přetečení", up.evaluate("document.documentElement.scrollWidth <= 320"))
    up.close()
    DEV.fw = "v6.16-mock"
    # hlavní stránka v jiném panelu mezitím zkoušela stav během "restartu" zařízení: to jsou očekávané výpadky
    errors[:] = [e for e in errors if "ERR_EMPTY_RESPONSE" not in e and "ERR_CONNECTION" not in e]
    print("== Rozcestník /portal")
    for uaname, ua, want in (("iPhone", "Mozilla/5.0 (iPhone; CPU iPhone OS 18_0 like Mac OS X) AppleWebKit/605.1.15", "Hotovo"),
                             ("Android", "Mozilla/5.0 (Linux; Android 15; Pixel 8) AppleWebKit/537.36 Chrome/130 Mobile", "Zavřete")):
        pc = browser.new_context(viewport={"width": 360, "height": 640}, user_agent=ua)
        pp = pc.new_page()
        pp.goto(BASE + "portal")
        pp.wait_for_function("document.querySelector('#sub').textContent.indexOf('0123456') >= 0", timeout=5000)
        check("portal %s: ID zařízení" % uaname, "0123456" in pp.inner_text("#sub"))
        check("portal %s: krok 2 skrytý" % uaname, not pp.is_visible("#next"))
        DEV.portal_ok = False
        pp.click("#ok")
        pp.wait_for_timeout(500)
        check("portal %s: Hotovo potvrzeno zařízení" % uaname, DEV.portal_ok)
        check("portal %s: návod pro platformu" % uaname, want in pp.inner_text("#s1"), pp.inner_text("#s1"))
        check("portal %s: adresa" % uaname, pp.inner_text("#addr") == "10.10.10.10")
        check("portal %s: bez přetečení" % uaname, pp.evaluate("document.documentElement.scrollWidth <= 360"))
        if uaname == "iPhone":
            check("portal iPhone: tip arbolok.local", "arbolok.local" in pp.inner_text("#alt"))
            pp.screenshot(path=os.path.join(OUT, "09_portal_iphone.png"), full_page=True)
        pp.click("a.btn")
        pp.wait_for_function("document.querySelector('#devId') && document.querySelector('#devId').textContent.length > 0", timeout=8000)
        check("portal %s: odkaz otevře přehled" % uaname, pp.url.rstrip("/") == BASE.rstrip("/"))
        pc.close()

    print("== Záznamy")
    page.click("#tabs a[data-r='logs']")
    settle(page, 1500)
    with page.expect_download() as dl:
        page.click("#btnCsv")
    csv = open(dl.value.path(), encoding="utf-8-sig").read().splitlines()
    check("CSV: hlavička", csv[0].startswith("cas;misto;vlhkost_pct;teplota_c;baterie_v;adc"), csv[0])
    check("CSV: řádky jako záznamy", len(csv) > 100, len(csv))
    check("CSV: desetinná čárka a ADC", "," in csv[10] and csv[10].split(";")[5].isdigit(), csv[10])
    zi = csv[0].split(";").index("zaliti")
    check("CSV: označená zalití", sum(1 for r in csv[1:] if r.split(";")[zi] == "1") > 0)
    check("CSV: sloupce ručního zápisu", csv[0].endswith(";rucne;poznamka"), csv[0])
    check("CSV: název podle ID", dl.value.suggested_filename == "0123456.csv", dl.value.suggested_filename)
    watch_toasts(page)
    page.click("#tabs a[data-r='logs']")
    settle(page, 1500)
    check("záznamy: kolik paměti a dní zbývá", "paměti" in text(page, "#logCap") and "dní" in text(page, "#logCap"), text(page, "#logCap"))
    check("záznamy: po dnech, první den otevřený", page.locator("#logDays details.day").count() >= 3
          and page.evaluate("document.querySelector('#logDays details.day').open"), page.locator("#logDays details.day").count())
    check("záznamy: řádek čas, vlhkost, teplota, baterie", page.locator("#logDays details.day[open] table tr").count() > 2
          and "°" in text(page, "#logDays details.day[open]") and " V" in text(page, "#logDays details.day[open]")
          and page.input_value("#logLoc") == str(DEV.cfg["loc"] + 1))
    check("záznamy: ikona zalití s vysvětlivkou", "zalito" in text(page, "#logLegend"), text(page, "#logLegend"))
    check("záznamy: surový log schovaný", not page.is_visible("#logBox"))
    page.click("#logRaw summary")
    settle(page, 300)
    lines = text(page, "#logBox").count("\n") + 1
    check("log: zobrazeno posledních ~400 řádků", 300 <= lines <= 401, lines)
    check("log: tlačítko 'zobrazit vše'", page.is_visible("#btnLogAll"))
    page.click("#btnLogAll")
    settle(page, 400)
    lines2 = text(page, "#logBox").count("\n") + 1
    check("log: zobrazit vše", lines2 > lines, (lines, lines2))
    with page.expect_download() as dl:
        page.click("#btnTxt")
    txt = open(dl.value.path(), encoding="utf-8").read().splitlines()
    check("Stáhnout .txt: čitelné řádky s jednotkami i z krátkého formátu",
          len(txt) > 100 and all(re.match(r"^\d{4}-\d\d-\d\d \d\d:\d\d (--|\d+)% ", x) for x in txt) and txt[-1].endswith(" p1")
          and dl.value.suggested_filename == "0123456.txt", txt[-1] if txt else "")
    page.screenshot(path=os.path.join(OUT, "11_logs.png"))
    check("záznamy: bez tlačítka mazání", not page.is_visible("#btnLogClear"))
    page.click("#tabs a[data-r='admin']")
    open_secs(page)
    settle(page, 400)
    page.click("#btnLogClear")
    settle(page, 700)
    page.click("#tabs a[data-r='logs']")
    settle(page, 900)
    check("log: smazán", text(page, "#logBox") == "(prázdné)", text(page, "#logBox")[:50])
    page.click("#tabs a[data-r='dash']")
    settle(page, 800)
    check("po smazání logu graf hlásí prázdno", "Zatím žádné záznamy" in page.inner_text("#chHumMsg"), page.inner_text("#chHumMsg"))
    page.screenshot(path=os.path.join(OUT, "12_dash_empty_log.png"))
    # prázdný / neexistující log = 200 s prázdným tělem, ne chyba: žádná červená hláška o komunikaci
    # (nový přístroj, záznam vypnutý, nebo právě smazaný log)
    tt = pop_toasts(page)
    check("prázdný log nevyvolá chybovou hlášku", not any("komunikace" in x or "zaneprázdn" in x for x in tt), tt)
    DEV.logtext = mock_server.make_log(20)

    # --- zařízení právě ředí log: načtení dostane 503 busy, stránka potichu zkusí znovu
    print("== Záznamy: zařízení zaneprázdněné")
    pop_toasts(page)
    DEV.fail_next = "log-busy"
    page.click("#tabs a[data-r='logs']")
    settle(page, 900)
    check("log busy: žádná hláška při prvním pokusu", pop_toasts(page) == [])
    settle(page, 2600)
    n_lines = text(page, "#logBox").count("\n") + 1
    check("log busy: po druhém pokusu je log načten", n_lines >= 20 and DEV.fail_next is None, n_lines)
    check("log busy: ani potom žádná hláška", pop_toasts(page) == [])
    # mazání logu při busy: chyba je vidět a log zůstává
    DEV.fail_next = "clear-busy"
    page.click("#tabs a[data-r='admin']")
    open_secs(page)
    settle(page, 400)
    page.click("#btnLogClear")
    settle(page, 800)
    tt = pop_toasts(page)
    page.click("#tabs a[data-r='logs']")
    settle(page, 900)
    check("smazání logu při busy ukáže 'zaneprázdněné'", any("zaneprázdn" in x for x in tt), tt)
    check("po busy zůstal log zobrazen", text(page, "#logBox").count("\n") + 1 >= 20 and len(DEV.logtext) > 100)
    errors[:] = [e for e in errors if not any(("status of %d" % c) in e for c in (503, 409, 507))]

    print("== Období od–do a zpráva do PDF")
    import datetime as _dt2
    DEV.logtext = mock_server.make_log(20)
    page.click("#tabs a[data-r='dash']")
    page.reload()
    page.wait_for_function("document.querySelector('#devId').textContent.length > 0", timeout=8000)
    settle(page, 2000)
    def _day(ln):  # datum záznamu v obou formátech (do v6.12 "2026-11-09 …", od v6.13 "2611091300 …")
        if ln[:2] == "20" and ln[4:5] == "-":
            return ln[:10]
        if re.match(r"^\d{10} ", ln):
            return "20%s-%s-%s" % (ln[0:2], ln[2:4], ln[4:6])
        return None
    last = max(d for d in (_day(ln) for ln in DEV.logtext.splitlines()) if d)
    d_to = _dt2.date.fromisoformat(last) - _dt2.timedelta(days=3)
    d_from = d_to - _dt2.timedelta(days=4)
    page.click("#rangeSeg button[data-range='custom']")
    settle(page, 300)
    check("od–do: pole se ukážou", page.is_visible("#rFrom") and page.is_visible("#rTo"))
    page.fill("#rFrom", d_from.isoformat()); page.dispatch_event("#rFrom", "change")
    page.fill("#rTo", d_to.isoformat()); page.dispatch_event("#rTo", "change")
    settle(page, 400)
    o = page.evaluate("[Plot.all[0].o.xmin, Plot.all[0].o.xmax]")
    exp0 = int(_dt2.datetime(d_from.year, d_from.month, d_from.day, tzinfo=_dt2.timezone.utc).timestamp() * 1000)
    check("od–do: graf přesně za zvolené dny", o[0] == exp0 and o[1] == exp0 + 5 * 86400000, (o, exp0))
    check("od–do: souhrn zalévání za období", d_from.strftime("%-d. %-m.") in text(page, "#wRange"), text(page, "#wRange"))
    page.screenshot(path=os.path.join(OUT, "02e_dash_custom.png"))
    page.reload()
    page.wait_for_function("document.querySelector('#devId').textContent.length > 0", timeout=8000)
    settle(page, 1500)
    check("od–do: volba si pamatuje", page.input_value("#rFrom") == d_from.isoformat() and page.input_value("#rTo") == d_to.isoformat())
    page.click("#rangeSeg button[data-range='24h']")
    page.click("#tabs a[data-r='logs']")
    settle(page, 1200)
    check("zpráva: výchozí období vyplněné", page.input_value("#repFrom") != "" and page.input_value("#repTo") == last,
          (page.input_value("#repFrom"), page.input_value("#repTo")))
    page.evaluate("() => { window.__printed = 0; window.print = () => { window.__printed++; }; }")
    page.fill("#repFrom", "2020-01-01"); page.fill("#repTo", "2020-01-05")
    page.click("#btnRep")
    settle(page, 300)
    check("zpráva: prázdné období ohlásí", "nejsou žádné záznamy" in toast_text(page) and page.evaluate("window.__printed") == 0, toast_text(page))
    page.fill("#repFrom", (_dt2.date.fromisoformat(last) - _dt2.timedelta(days=9)).isoformat()); page.fill("#repTo", last)
    page.click("#btnRep")
    settle(page, 500)
    rep = page.text_content("#report")
    check("zpráva: otevřen tisk", page.evaluate("window.__printed") == 1)
    check("zpráva: obsah (místo, souhrn, zalití, dny)", "Zpráva o vlhkosti půdy" in rep and "Místo 1" in rep and "Zalití v období" in rep
          and "Tabulka po dnech" in rep and page.locator("#report table.days tbody tr").count() == 10, rep[:300])
    check("zpráva: grafy jako obrázky", page.locator("#report img.rep-chart").count() == 2 and
          (page.get_attribute("#report img.rep-chart", "src") or "").startswith("data:image/png"))
    page.evaluate("document.body.classList.add('printing'); document.querySelector('#report').hidden = false")
    page.emulate_media(media="print")
    page.pdf(path=os.path.join(OUT, "report.pdf"), format="A4", print_background=True)
    page.emulate_media(media="screen")
    page.evaluate("document.body.classList.remove('printing'); document.querySelector('#report').hidden = true")
    check("zpráva: PDF vzniklo", os.path.getsize(os.path.join(OUT, "report.pdf")) > 20000)

    print("== Vybitá baterie (hibernace)")
    import time as _t
    import datetime as _dt
    lines = [ln for ln in mock_server.make_log(20).splitlines(True) if ln[:2] == "20" and " p" not in ln]
    lines.sort()
    cut = len(lines) - 60  # posledních ~60 záznamů "chybí": hibernace, pak zase měření
    t_h = _dt.datetime.strptime(lines[cut - 1][:16], "%Y-%m-%d %H:%M") + _dt.timedelta(minutes=1)
    hib = t_h.strftime("%Y-%m-%d %H:%M") + " --% 9.5C 2.68V p1 ! H\r\n"
    DEV.logtext = "".join(lines[:cut]) + hib + "".join(lines[cut + 40:])
    page.click("#tabs a[data-r='dash']")
    page.reload()
    page.wait_for_function("document.querySelector('#devId').textContent.length > 0", timeout=8000)
    settle(page, 1500)
    page.click("#rangeSeg button[data-range='7d']")
    settle(page, 600)
    check("hibernace: pás v grafu s legendou", "Vybitá baterie" in text(page, "#lgHum"), text(page, "#lgHum"))
    check("hibernace: info pruh, že měření stálo", "kvůli vybité baterii" in text(page, "#banners"), text(page, "#banners")[:200])
    page.screenshot(path=os.path.join(OUT, "05e_hibernation.png"), full_page=True)
    DEV.logtext = "".join(lines[:cut]) + hib
    page.reload()
    page.wait_for_function("document.querySelector('#devId').textContent.length > 0", timeout=8000)
    settle(page, 4000)
    check("hibernace: měření stojí (červený pruh)", "Měření stojí od" in text(page, "#banners"), text(page, "#banners")[:200])
    page.click("#tabs a[data-r='logs']")
    settle(page, 1000)
    with page.expect_download() as dl:
        page.click("#btnCsv")
    csvh = open(dl.value.path(), encoding="utf-8-sig").read().splitlines()
    vi = csvh[0].split(";").index("vybita_baterie") if "vybita_baterie" in csvh[0] else -1
    check("CSV: sloupec vybita_baterie", vi >= 0 and sum(1 for r in csvh[1:] if r.split(";")[vi] == "1") == 1, csvh[0])
    DEV.logtext = mock_server.make_log(20)
    page.reload()
    settle(page, 1500)

    print("== Staré soubory")
    check("soubory: záložka i API pryč", page.locator("#tabs a[data-r='files']").count() == 0 and page.locator("#fileList").count() == 0)

    # --------------------------------------------------- tmavý režim a úzký displej
    print("== Vzhled")
    ctx2 = browser.new_context(viewport={"width": 390, "height": 844}, device_scale_factor=2, has_touch=True,
                               locale="cs-CZ", timezone_id="Europe/Prague", color_scheme="dark")
    p2 = ctx2.new_page()
    p2.on("pageerror", lambda e: errors.append("pageerror(dark): " + str(e)))
    p2.on("console", lambda m: errors.append("console(dark): " + m.text) if m.type == "error" else None)
    p2.goto(BASE)
    p2.wait_for_function("document.querySelector('#devId').textContent.length > 0", timeout=8000)
    settle(p2, 900)
    p2.screenshot(path=os.path.join(OUT, "14_dash_dark.png"), full_page=True)
    check("tmavý režim: graf kreslí", canvas_ink(p2, "#chHum") > 3000)
    bg = p2.evaluate("getComputedStyle(document.body).backgroundColor")
    check("tmavý režim: tmavé pozadí", bg.startswith("rgb(17") or bg.startswith("rgb(1"), bg)
    p2.close()
    ctx2.close()

    ctx3 = browser.new_context(viewport={"width": 320, "height": 640}, device_scale_factor=2, has_touch=True,
                               locale="cs-CZ", timezone_id="Europe/Prague")
    p3 = ctx3.new_page()
    p3.on("pageerror", lambda e: errors.append("pageerror(320): " + str(e)))
    p3.goto(BASE)
    p3.wait_for_function("document.querySelector('#devId').textContent.length > 0", timeout=8000)
    settle(p3, 800)
    for r in ("dash", "settings", "logs", "admin"):
        p3.click("#tabs a[data-r='%s']" % r)
        settle(p3, 600)
        check("320 px: bez přetečení (%s)" % r, p3.evaluate("document.documentElement.scrollWidth <= window.innerWidth + 1"),
              p3.evaluate("document.documentElement.scrollWidth"))
    p3.click("#tabs a[data-r='dash']")
    settle(p3, 500)
    p3.screenshot(path=os.path.join(OUT, "15_dash_320.png"))
    p3.close()
    ctx3.close()

    check("žádné chyby v konzoli ani výjimky (před zkouškou výpadku)", not errors, errors[:5])

    # ------------------------------------------------------------------ ztráta spojení
    print("== Ztráta spojení")
    page.click("#tabs a[data-r='dash']")
    settle(page, 500)
    DEV.sleeping = True
    page.wait_for_function("!document.querySelector('#offline').hidden", timeout=20000)
    check("offline banner po uspání", "uspalo" in text(page, "#offline"), text(page, "#offline"))
    page.screenshot(path=os.path.join(OUT, "16_offline.png"))
    DEV.sleeping = False
    page.wait_for_function("document.querySelector('#offline').hidden", timeout=20000)
    check("spojení obnoveno automaticky", True)

    browser.close()

bad = [n for n, ok in results if not ok]
print("\n== UI: %d kontrol, %d selhalo ==" % (len(results), len(bad)))
for n in bad:
    print("   FAIL:", n)
sys.exit(1 if bad else 0)
