#!/usr/bin/env python3
# Výpis velikosti firmwaru z linkerové mapy (firmware.map): kolik zabírá, kolik zbývá do limitu OTA,
# co z toho jsou vaše zdrojáky, knihovny, jádro Arduino a hotové knihovny ESP-IDF, a jestli se kód
# nepřeložil BEZ optimalizace (to se stalo s platformou espressif32 8.0.0, viz platformio.ini).
#
#   python3 tools/size_report.py                                  (mapa výchozího prostředí)
#   python3 tools/size_report.py cesta/k/firmware.map [--top 20]
#
# Mapu vyrábí PlatformIO při každém sestavení: .pio/build/<prostředí>/firmware.map (vzniká i tehdy, když
# sestavení nakonec selže na velikosti). Skript se spouští sám před kontrolou velikosti (tools/pio_post.py),
# takže se výpis ukáže i u firmwaru, který se nevejde. Jen standardní knihovna Pythonu.
#
# "Velikost" je počítaná stejně jako ji hlásí PlatformIO u ESP32:
#     .iram0.text + .iram0.vectors + .dram0.data + .flash.text + .flash.rodata
# (ověřeno na mapách sestavení verze 3 a 4 – shoduje se na bajt s výpisem "Flash: ... used N bytes").
import collections
import io
import os
import re
import sys

SLOT_BYTES = 0x140000                      # oddíl app0/app1 ve výchozí tabulce oddílů: 1 310 720 B
OTA_MAX_BYTES = (SLOT_BYTES - 0x1000) & ~0xFFF   # co přijme OTA stránka /update: 1 306 624 B (jako tools/check_bin.py)
WARN_HEADROOM_BYTES = 64 * 1024            # pod touto rezervou skript varuje, že je toho málo

SIZE_SECTIONS = (".iram0.vectors", ".iram0.text", ".dram0.data", ".flash.text", ".flash.rodata")
CODE_SECTIONS = (".iram0.vectors", ".iram0.text", ".flash.text")
RAM_SECTIONS = (".dram0.data", ".dram0.bss")
WANTED = set(SIZE_SECTIONS) | set(RAM_SECTIONS) | {".flash.appdesc"}

# Pomocné šablony standardní knihovny, které optimalizující překladač vždy vloží do volajícího kódu.
# Že jsou v hotovém firmwaru jako samostatné funkce, je spolehlivý znak překladu bez optimalizace (-O0):
# v sestavení verze 3 (s -Os) jich bylo 0, v prvním sestavení verze 4 (bez optimalizace) přes 200.
_UNOPT_RE = re.compile(r"^\.text\._ZSt(?:4move|7forward)")
UNOPT_WARN_COUNT = 10

_OUT_RE = re.compile(r"^(\.\S+)\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)")
_FULL_RE = re.compile(r"^ (\.\S+)\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s+(\S.*)$")
_NAME_RE = re.compile(r"^ (\.\S+)\s*$")
_CONT_RE = re.compile(r"^\s{10,}(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s+(\S.*)$")


USAGE = ("použití: python3 tools/size_report.py [cesta/k/firmware.map] [--top N]\n"
         "  Bez argumentu čte .pio/build/esp32doit-devkit-v1/firmware.map (vzniká při každém `pio run`).\n"
         "  Vypíše velikost jako PlatformIO, rezervu do limitu OTA, největší položky a jestli se kód nepřeložil bez optimalizace.\n"
         "  Návratový kód: 0 = vejde se, 1 = nevejde se, 2 = mapu nešlo přečíst.")


class MapError(Exception):
    pass


def parse_map(text):
    """Vrátí (hlavičky výstupních sekcí, efektivní bajty podle (sekce, objekt), počet nevložených pomocných šablon).

    hlavičky:  {sekce: (adresa, velikost)}
    efektivní bajty: {(sekce, objekt): bajty}; každá skupina vstupních sekcí na téže adrese se přičte celá
        tomu členu, který deklaruje největší velikost (GNU ld slučuje řetězce z mnoha souborů a celý blok
        přiřadí prvnímu z nich; ostatní mají jen velikost "před sloučením"). Součet tak vychází přesně
        na velikost výstupní sekce. Texty (rodata) jsou po souborech jen orientační, kód přesný.
    """
    lines = text.splitlines()
    start = None
    for i, line in enumerate(lines):
        if line.startswith("Linker script and memory map"):
            start = i
            break
    if start is None:
        raise MapError("v souboru není 'Linker script and memory map' (není to mapa z linkeru GNU ld)")

    headers = {}
    entries = collections.defaultdict(list)    # sekce -> [(adresa, velikost, objekt, jméno sekce)]
    cur = None
    pend = None
    unopt = 0
    for line in lines[start + 1:]:
        m = _OUT_RE.match(line)
        if m:
            cur = m.group(1)
            pend = None
            if cur in WANTED:
                headers[cur] = (int(m.group(2), 16), int(m.group(3), 16))
            continue
        if cur not in WANTED:
            continue
        m = _FULL_RE.match(line)
        if m:
            name, addr, size, obj = m.groups()
            pend = None
            if obj.startswith("0x"):
                continue
            entries[cur].append((int(addr, 16), int(size, 16), obj.strip(), name))
            if int(size, 16) and cur == ".flash.text" and _UNOPT_RE.match(name) and _is_pio_object(obj):
                unopt += 1
            continue
        m = _NAME_RE.match(line)
        if m:
            pend = m.group(1)
            continue
        m = _CONT_RE.match(line)
        if m and pend:
            addr, size, obj = m.groups()
            entries[cur].append((int(addr, 16), int(size, 16), obj.strip(), pend))
            if int(size, 16) and cur == ".flash.text" and _UNOPT_RE.match(pend) and _is_pio_object(obj):
                unopt += 1
            pend = None

    eff = collections.defaultdict(int)
    for sec, es in entries.items():
        lo, size = headers.get(sec, (0, 0))
        end = lo + size
        groups = collections.defaultdict(list)
        for e in es:
            if lo <= e[0] < end:
                groups[e[0]].append(e)
        addrs = sorted(groups)
        for i, a in enumerate(addrs):
            nxt = addrs[i + 1] if i + 1 < len(addrs) else end
            best = max(groups[a], key=lambda e: e[1])
            eff[(sec, best[2])] += nxt - a
    return headers, dict(eff), unopt


def _norm(obj):
    return obj.replace("\\", "/")


def _is_pio_object(obj):
    return "/.pio/build/" in _norm(obj) or _norm(obj).startswith(".pio/build/")


def origin_of(obj):
    o = _norm(obj)
    if _is_pio_object(o):
        return "PIO"           # přeloženo na vašem počítači: vaše src/, knihovny z lib_deps, jádro Arduino
    if "toolchain" in o or "/xtensa-esp" in o:
        return "TOOLCHAIN"     # libc, libstdc++, libgcc
    return "IDF"               # hotové knihovny ESP-IDF z balíčku esp32-libs


def group_of(obj):
    o = _norm(obj)
    m = re.search(r"/(lib[^/]+\.a)\(", o)
    if m:
        name = m.group(1)[3:-2]                  # libWire.a -> Wire, libAsyncTCP@3.4.9.a -> AsyncTCP@3.4.9
        return name
    m = re.search(r"/src/([^/(]+\.o)", o)
    if m and _is_pio_object(o):
        return "src/" + m.group(1)
    base = o.rsplit("/", 1)[-1]
    base = base.split("(")[0]
    if base.startswith("lib") and base.endswith(".a"):
        return base[3:-2]
    return base or o


def summarize(text, limit=OTA_MAX_BYTES):
    """Spočítá všechna čísla, která skript vypisuje (kvůli testům odděleno od výpisu)."""
    headers, eff, unopt = parse_map(text)
    sizes = {k: headers[k][1] for k in headers}
    missing = [k for k in (".flash.text", ".flash.rodata") if k not in sizes]
    if missing:
        raise MapError("v mapě chybí sekce %s (není to mapa pro ESP32?)" % ", ".join(missing))
    program = sum(sizes.get(k, 0) for k in SIZE_SECTIONS)
    ram = sum(sizes.get(k, 0) for k in RAM_SECTIONS)
    code = sum(sizes.get(k, 0) for k in CODE_SECTIONS)
    groups = collections.defaultdict(lambda: {"code": 0, "rodata": 0, "data": 0, "origin": ""})
    by_origin = collections.defaultdict(lambda: {"code": 0, "rodata": 0, "data": 0})
    for (sec, obj), val in eff.items():
        g = groups[group_of(obj)]
        g["origin"] = g["origin"] or origin_of(obj)
        o = by_origin[origin_of(obj)]
        if sec in CODE_SECTIONS:
            g["code"] += val
            o["code"] += val
        elif sec == ".flash.rodata":
            g["rodata"] += val
            o["rodata"] += val
        elif sec == ".dram0.data":
            g["data"] += val
            o["data"] += val
    return {
        "program": program, "ram": ram, "code": code,
        "rodata": sizes.get(".flash.rodata", 0), "data": sizes.get(".dram0.data", 0),
        "slot": SLOT_BYTES, "limit": limit, "headroom": limit - program,
        "groups": dict(groups), "by_origin": dict(by_origin), "unopt": unopt,
    }


def _kb(n):
    return "{:,}".format(n).replace(",", " ")


def format_report(s, top=12):
    out = []
    add = out.append
    pct = 100.0 * s["program"] / s["slot"]
    add("Velikost firmwaru (jako ji počítá PlatformIO): %s B = %.1f %% oddílu %s B" % (_kb(s["program"]), pct, _kb(s["slot"])))
    if s["headroom"] >= 0:
        add("Rezerva do limitu OTA stránky (%s B): %s B (%.1f %%)" % (_kb(s["limit"]), _kb(s["headroom"]), 100.0 * s["headroom"] / s["limit"]))
    else:
        add("PŘEKROČENO: firmware je o %s B větší než to, co přijme OTA stránka (%s B)" % (_kb(-s["headroom"]), _kb(s["limit"])))
    add("  kód %s B, konstanty a texty %s B, inicializovaná data %s B; RAM (data + bss) %s B" % (
        _kb(s["code"]), _kb(s["rodata"]), _kb(s["data"]), _kb(s["ram"])))
    po = s["by_origin"]
    names = (("PIO", "přeloženo u vás (src/, knihovny z platformio.ini, jádro Arduino)"),
             ("IDF", "hotové knihovny ESP-IDF (Wi-Fi, TCP/IP, FreeRTOS …)"),
             ("TOOLCHAIN", "knihovny překladače (libc, libstdc++, libgcc)"))
    add("  kód podle původu:")
    for key, label in names:
        if key in po:
            add("    %-52s %9s B" % (label, _kb(po[key]["code"])))
    add("  největší položky podle velikosti kódu (texty se po souborech nevypisují: linker slučuje řetězce z celého"
        " firmwaru a přičte je jednomu souboru):")
    ranked = sorted(s["groups"].items(), key=lambda kv: -kv[1]["code"])
    shown = 0
    for name, g in ranked:
        if g["code"] <= 0:
            continue
        add("    %-36s %9s B   %s" % (name[:36], _kb(g["code"]), g["origin"]))
        shown += 1
        if shown >= top:
            break
    # verdikt o překladu
    if s["unopt"] >= UNOPT_WARN_COUNT:
        add("POZOR: kód z vašeho překladu vypadá přeložený BEZ OPTIMALIZACE (nalezeno %d nevložených pomocných šablon "
            "std::move/std::forward; u optimalizovaného překladu jich je 0). Zkontrolujte v platformio.ini řádek "
            "-Os v build_flags; bez něj je firmware o stovky kB větší." % s["unopt"])
    else:
        add("Překlad: optimalizace je patrně zapnutá (nevložených pomocných šablon std::move/forward: %d)." % s["unopt"])
    if s["headroom"] < 0:
        add("VERDIKT: NEVEJDE SE – přes OTA by se nenahrálo. Pošlete mi tento výpis a soubor firmware.map.")
    elif s["headroom"] < WARN_HEADROOM_BYTES:
        add("VERDIKT: vejde se, ale s malou rezervou (pod %s B). Funguje, další úpravy ale nejspíš ne." % _kb(WARN_HEADROOM_BYTES))
    else:
        add("VERDIKT: vejde se s rezervou.")
    return out


def _say(text, out=None):
    out = out or sys.stdout
    try:
        out.write(text + "\n")
    except Exception:  # noqa: BLE001  (terminál bez UTF-8: ASCII náhrada)
        try:
            out.write(text.encode("ascii", "replace").decode("ascii") + "\n")
        except Exception:  # noqa: BLE001
            pass


def report(map_path, out=None, top=12, limit=OTA_MAX_BYTES):
    """Vypíše zprávu o velikosti. Vrací True/False (vejde se do limitu OTA) nebo None, když mapu nešlo přečíst.
    Nikdy nevyvolá výjimku (volá se z PlatformIO a nesmí sestavení shodit)."""
    try:
        with io.open(map_path, "r", encoding="utf-8", errors="replace") as fh:
            text = fh.read()
        s = summarize(text, limit)
        lines = format_report(s, top)
    except Exception as exc:  # noqa: BLE001
        _say("(Výpis velikosti z %s se nepodařil: %s)" % (map_path, exc), out)
        return None
    _say("", out)
    _say("================ VELIKOST FIRMWARU ================", out)
    for line in lines:
        _say(line, out)
    _say("===================================================", out)
    return s["headroom"] >= 0


def default_map_path(proj):
    return os.path.join(proj, ".pio", "build", "esp32doit-devkit-v1", "firmware.map")


def main(argv):
    top = 12
    path = None
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "--top" and i + 1 < len(argv):
            top = int(argv[i + 1])
            i += 2
            continue
        if a in ("-h", "--help"):
            _say(USAGE)
            return 0
        path = a
        i += 1
    if path is None:
        path = default_map_path(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
    if not os.path.isfile(path):
        _say("Soubor %s neexistuje. Nejdřív spusťte `pio run` (mapa vzniká při sestavení)." % path)
        return 2
    ok = report(path, top=top)
    if ok is None:
        return 2
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
