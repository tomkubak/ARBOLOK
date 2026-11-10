#!/usr/bin/env python3
# Test tools/size_report.py (výpis velikosti z linkerové mapy) a jeho zapojení v tools/pio_post.py.
# Mapy jsou syntetické, ve stejném formátu jako výstup GNU ld u ESP32 (sloučené řetězce na jedné adrese,
# dlouhé názvy sekcí na samostatném řádku, vyřazené sekce před mapou, cesty s lomítky i zpětnými lomítky).
# Je-li zadána ARBOLOG_REAL_MAP (a ARBOLOG_REAL_MAP_SIZE = velikost, kterou hlásí PlatformIO), zkouší se i skutečná mapa.
#   python3 -I tests_host/test_size_report.py
import contextlib
import io
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.join(HERE, "..", "tools")
sys.path.insert(0, TOOLS)
import size_report as sr  # noqa: E402

FAILS = []
CHECKS = 0


def check(cond, what):
    global CHECKS
    CHECKS += 1
    if not cond:
        FAILS.append(what)
        print("  SELHALO:", what)


PIO = ".pio/build/esp32doit-devkit-v1"
IDF = "/Users/x/.platformio/packages/framework-arduinoespressif32-esp32-libs/lib"
TC = ("/Users/x/.platformio/packages/toolchain-xtensa-esp-elf/bin/../lib/gcc/xtensa-esp-elf/14.2.0/"
      "../../../../xtensa-esp-elf/lib/esp32")


class MapBuilder:
    """Skládá mapu po sekcích; adresy a velikosti si počítá sám, takže očekávané součty jsou nezávislé na parseru."""

    def __init__(self):
        self.sections = []

    def section(self, name, start, items):
        """items: [(jméno vstupní sekce, velikost, objekt, 'one'|'cont')], po sobě jdoucí adresy."""
        out = [name.ljust(15) + " 0x%08x %10s" % (start, "0xHDR")]
        addr = start
        for it in items:
            iname, size, obj = it[0], it[1], it[2]
            form = it[3] if len(it) > 3 else "one"
            if form == "one":
                out.append(" %-15s 0x%08x %9s %s" % (iname, addr, hex(size), obj))
            else:
                out.append(" " + iname)
                out.append(" " * 16 + "0x%08x %9s %s" % (addr, hex(size), obj))
                out.append(" " * 32 + "0x%x (size before relaxing)" % (size + 8))
            addr += size
        out[0] = name.ljust(15) + " 0x%08x %10s" % (start, hex(addr - start))
        self.sections.append("\n".join(out))
        return addr - start

    def text(self):
        return "\n".join(["Discarded input sections", "",
                          " .text._ZSt4moveIRtEONSt16remove_referenceIT_E4typeEOS2_",
                          "                0x00000000       0x14 %s/src/discarded.cpp.o" % PIO, "",
                          "Linker script and memory map", "",
                          "                0x40080000                        _iram_start = 0x40080000"] + self.sections)


def build(helpers=0, win=False, big_text=0):
    """Vrací (text mapy, očekávané hodnoty)."""
    b = MapBuilder()
    pio = PIO.replace("/", "\\") if win else PIO
    sep = "\\" if win else "/"
    exp = {}
    v = b.section(".iram0.vectors", 0x40080000, [(".iram0.vectors", 0x404, IDF + "/libesp_system.a(vectors.S.obj)")])
    t0 = b.section(".iram0.text", 0x40080404, [
        (".iram1.0", 0x100, IDF + "/libesp_system.a(panic.c.obj)"),
        (".iram1.1", 0x180, IDF + "/libfreertos.a(tasks.c.obj)"),
        (".iram1.2", 0x40, "%s%slib0a1%slibWire.a(Wire.cpp.o)" % (pio, sep, sep))])
    d = b.section(".dram0.data", 0x3ffbdb60, [(".data.x", 0x80, IDF + "/liblwip.a(sockets.c.obj)")])
    bss = b.section(".dram0.bss", 0x3ffc3ac8, [(".bss.y", 0x400, IDF + "/liblwip.a(sockets.c.obj)")])
    # rodata: na adrese 0x3f400120 slité řetězce (tři členy; celý blok 0x180 je u prvního), potom dvě další položky
    start = 0x3f400120
    ro_lines = [
        ".flash.rodata".ljust(15) + " 0x%08x %10s" % (start, hex(0x180 + 0x60 + 0x40)),
        " .rodata.str1.1 0x%08x %9s %s/libesp_driver_gpio.a(gpio.c.obj)" % (start, hex(0x180), IDF),
        "                 0x40 (size before relaxing)",
        " .rodata.str1.1 0x%08x %9s %s%ssrc%smain.cpp.o" % (start, hex(0x40), pio, sep, sep),
        " .rodata.str1.1 0x%08x %9s %s%slib0a1%slibWire.a(Wire.cpp.o)" % (start, hex(0x20), pio, sep, sep),
        " .rodata.web    0x%08x %9s %s%ssrc%swebui.cpp.o" % (start + 0x180, hex(0x60), pio, sep, sep),
        " .rodata.z      0x%08x %9s %s/libc.a(lib_a-printf.o)" % (start + 0x1e0, hex(0x40), TC)]
    b.sections.append("\n".join(ro_lines))
    ro = 0x180 + 0x60 + 0x40
    items = [
        (".literal._Z5setupv", 0x20, "%s%ssrc%smain.cpp.o" % (pio, sep, sep)),
        (".text._Z5setupv", 0x300, "%s%ssrc%smain.cpp.o" % (pio, sep, sep), "cont"),
        (".text._ZN7TwoWire5beginEv", 0x200, "%s%slib0a1%slibWire.a(Wire.cpp.o)" % (pio, sep, sep), "cont"),
        (".text.ieee80211_input", 0x400, IDF + "/libnet80211.a(ieee80211_input.c.obj)"),
        (".text.memcpy", 0x100, TC + "/libc.a(lib_a-memcpy.o)"),
        (".text._ZNSt8__detail4wowEv", 0x1a0, "%s%ssrc%swebui.cpp.o" % (pio, sep, sep), "cont"),
    ]
    for i in range(helpers):
        nm = ".text._ZSt4moveIRSt8functionIFvvEEEONSt16remove_referenceIT_E4typeEOS5_%d" % i if i % 2 else \
             ".text._ZSt7forwardIRKSsEOT_RNSt16remove_referenceIS2_E4typeE%d" % i
        items.append((nm, 0x10, "%s%ssrc%swebui.cpp.o" % (pio, sep, sep), "cont"))
    if big_text:
        items.append((".text.big", big_text, IDF + "/libnet80211.a(big.c.obj)"))
    # položka s nulovou velikostí (po relaxaci) se nesmí počítat jako nevložená šablona
    items.append((".text._ZSt4moveIRiEONSt16remove_referenceIT_E4typeEOS2_", 0x0, "%s%ssrc%smain.cpp.o" % (pio, sep, sep), "cont"))
    f = b.section(".flash.text", 0x400d0020, items)
    exp.update(vectors=v, iram=t0, data=d, bss=bss, ro=ro, flash=f,
               program=v + t0 + d + f + ro, code=v + t0 + f, ram=d + bss, helpers=helpers,
               main=0x20 + 0x300, wire=0x40 + 0x200, webui=0x1a0 + helpers * 0x10, net80211=0x400 + big_text)
    return b.text() + "\n", exp


def with_stdout(fn, *a, **kw):
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        r = fn(*a, **kw)
    return r, buf.getvalue()


# ---------------------------------------------------------------- jednotkové testy
text, e = build()
s = sr.summarize(text)
check(s["program"] == e["program"], "velikost = součet pěti sekcí (%d vs %d)" % (s["program"], e["program"]))
check(s["code"] == e["code"] and s["rodata"] == e["ro"] and s["data"] == e["data"], "kód / rodata / data")
check(s["ram"] == e["ram"], "RAM = data + bss (%d vs %d)" % (s["ram"], e["ram"]))
check(s["headroom"] == sr.OTA_MAX_BYTES - e["program"], "rezerva do limitu OTA")
check(sr.OTA_MAX_BYTES == 1306624 and sr.SLOT_BYTES == 1310720, "limity odpovídají tools/check_bin.py a tabulce oddílů")
g = s["groups"]
check(g["src/main.cpp.o"]["code"] == e["main"], "kód src/main.cpp.o (%r)" % g.get("src/main.cpp.o"))
check(g["Wire"]["code"] == e["wire"] and g["Wire"]["origin"] == "PIO", "knihovna Wire je PIO, kód %r" % g.get("Wire"))
check(g["net80211"]["origin"] == "IDF" and g["net80211"]["code"] == e["net80211"], "net80211 je IDF")
check(g["c"]["origin"] == "TOOLCHAIN" and g["c"]["code"] == 0x100, "libc je TOOLCHAIN")
check(sum(x["code"] for x in g.values()) == e["code"], "součet kódu po skupinách = celkový kód (nic se neztratilo ani nezdvojilo)")
check(sum(x["rodata"] for x in g.values()) == e["ro"], "součet textů po skupinách = celkové rodata (sloučené řetězce se nezdvojí)")
check(g["esp_driver_gpio"]["rodata"] >= 0x180, "sloučený blok řetězců patří prvnímu členu skupiny na adrese (chování GNU ld)")
check(s["unopt"] == 0, "bez nevložených šablon: 0 (%d)" % s["unopt"])
rep = "\n".join(sr.format_report(s))
check("optimalizace je patrně zapnutá" in rep and "BEZ OPTIMALIZACE" not in rep, "výpis pro optimalizovaný překlad")
check("VERDIKT: vejde se s rezervou" in rep, "verdikt: s rezervou")
check("%.1f %%" % (100.0 * e["program"] / sr.SLOT_BYTES) in rep, "procento slotu ve výpisu")

# nevložené pomocné šablony std::move / std::forward => varování
text_u, e_u = build(helpers=12)
su = sr.summarize(text_u)
check(su["unopt"] == 12, "12 nevložených šablon se napočítá (%d); vyřazené a nulové se nepočítají" % su["unopt"])
repu = "\n".join(sr.format_report(su))
check("BEZ OPTIMALIZACE" in repu and "-Os" in repu and "12" in repu, "varování před překladem bez optimalizace")
text_9, _ = build(helpers=9)
check("BEZ OPTIMALIZACE" not in "\n".join(sr.format_report(sr.summarize(text_9))), "9 šablon (těsně pod prahem 10) se nevaruje")
text_10, _ = build(helpers=10)
check("BEZ OPTIMALIZACE" in "\n".join(sr.format_report(sr.summarize(text_10))), "10 šablon (práh) už se varuje")
# šablony z hotových knihoven (IDF, překladač) se do počtu nezahrnují (nejsou z vašeho překladu)
text_idf = text.replace("%s/libnet80211.a(ieee80211_input.c.obj)" % IDF, "x").replace(
    ".text.ieee80211_input", ".text._ZSt4moveIiEOT_RNSt16remove_referenceIS0_E4typeE")
check(sr.summarize(text_idf)["unopt"] == 0, "šablona v hotové knihovně IDF se nepočítá")

# zpětná lomítka (Windows)
text_w, e_w = build(helpers=11, win=True)
sw = sr.summarize(text_w)
check(sw["program"] == e_w["program"] and sw["unopt"] == 11, "cesty se zpětnými lomítky: velikost i počet šablon")
check(sw["groups"]["src/main.cpp.o"]["origin"] == "PIO" and sw["groups"]["Wire"]["origin"] == "PIO",
      "cesty se zpětnými lomítky: původ PIO a názvy skupin (%r)" % sorted(sw["groups"]))

# překročení limitu / malá rezerva
over = sr.summarize(text, limit=e["program"] - 1)
ro_ = "\n".join(sr.format_report(over))
check(over["headroom"] == -1 and "PŘEKROČENO" in ro_ and "NEVEJDE SE" in ro_, "o bajt přes limit: NEVEJDE SE")
exact = sr.summarize(text, limit=e["program"])
check(exact["headroom"] == 0 and "NEVEJDE SE" not in "\n".join(sr.format_report(exact)), "přesně na limitu: vejde se")
low = sr.summarize(text, limit=e["program"] + 1000)
check("malou rezervou" in "\n".join(sr.format_report(low)), "malá rezerva se označí")

# ---------------------------------------------------------------- report() a main() se nikdy nesmí zhroutit
with tempfile.TemporaryDirectory() as tmp:
    good = os.path.join(tmp, "firmware.map")
    with open(good, "w", encoding="utf-8") as fh:
        fh.write(text_u)
    r, out = with_stdout(sr.report, good)
    check(r is True and "VELIKOST FIRMWARU" in out and "BEZ OPTIMALIZACE" in out, "report(): dobrá mapa -> True a výpis")
    r, out = with_stdout(sr.report, os.path.join(tmp, "neni.map"))
    check(r is None and "nepodařil" in out, "report(): chybějící soubor -> None, bez výjimky")
    junk = os.path.join(tmp, "junk.map")
    with open(junk, "wb") as fh:
        fh.write(b"\xff\xfe\x00 nesmysl \x80\x81\n" * 50)
    r, out = with_stdout(sr.report, junk)
    check(r is None and "nepodařil" in out, "report(): binární smetí -> None, bez výjimky")
    nosec = os.path.join(tmp, "nosec.map")
    with open(nosec, "w", encoding="utf-8") as fh:
        fh.write("Linker script and memory map\n\n.foo 0x0 0x10\n")
    r, out = with_stdout(sr.report, nosec)
    check(r is None and "chybí sekce" in out, "report(): mapa bez .flash.text -> None s vysvětlením (%r)" % out)
    empty = os.path.join(tmp, "empty.map")
    open(empty, "w").close()
    r, out = with_stdout(sr.report, empty)
    check(r is None, "report(): prázdný soubor -> None")

    over_txt, _ = build(big_text=0x150000)
    bigp = os.path.join(tmp, "big.map")
    with open(bigp, "w", encoding="utf-8") as fh:
        fh.write(over_txt)
    rc, out = with_stdout(sr.main, [bigp])
    check(rc == 1 and "NEVEJDE SE" in out, "main(): příliš velký firmware -> kód 1")
    rc, out = with_stdout(sr.main, [good])
    check(rc == 0, "main(): vejde se -> kód 0")
    rc, out = with_stdout(sr.main, [os.path.join(tmp, "neni.map")])
    check(rc == 2 and "pio run" in out, "main(): chybějící mapa -> kód 2 a rada")
    rc, out = with_stdout(sr.main, ["--help"])
    check(rc == 0 and "použití" in out and "firmware.map" in out, "main(): --help vypíše použití")
    rc, out = with_stdout(sr.main, [good, "--top", "3"])
    check(rc == 0 and out.count(" B   ") <= 3 + 3, "main(): --top omezuje počet položek")

    # výpis nesmí spadnout na terminálu bez UTF-8
    class AsciiOnly(io.StringIO):
        def write(self, s):
            s.encode("ascii")
            return super().write(s)

    ao = AsciiOnly()
    r = sr.report(good, out=ao)
    check(r is True and "VELIKOST" in ao.getvalue(), "report(): terminál jen s ASCII -> náhradní výpis, bez pádu")

    # ------------------------------------------------------------ zapojení do PlatformIO (pio_post.py) s atrapou prostředí
    class FakeEnv:
        def __init__(self, proj, build_dir, broken=False):
            self.proj, self.build_dir, self.broken = proj, build_dir, broken
            self.pre, self.post = [], []

        def __getitem__(self, k):
            return {"PROJECT_DIR": self.proj}[k]

        def subst(self, s):
            if self.broken:
                raise RuntimeError("atrapa: subst selhalo")
            return s.replace("$BUILD_DIR", self.build_dir).replace("${PROGNAME}", "firmware")

        def AddPreAction(self, target, fn):  # noqa: N802
            if self.broken == "pre":
                raise RuntimeError("atrapa: cíl neexistuje")
            self.pre.append((target, fn))

        def AddPostAction(self, target, fn):  # noqa: N802
            self.post.append((target, fn))

    def load_post(env):
        src = open(os.path.join(TOOLS, "pio_post.py"), encoding="utf-8").read()
        glb = {"__name__": "pio_post_test", "Import": lambda name: None, "env": env}
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            exec(compile(src, "pio_post.py", "exec"), glb)  # noqa: S102
        return glb, buf.getvalue()

    bdir = os.path.join(tmp, "build")
    os.makedirs(bdir)
    with open(os.path.join(bdir, "firmware.map"), "w", encoding="utf-8") as fh:
        fh.write(text)
    proj = os.path.join(HERE, "..")
    fe = FakeEnv(proj, bdir)
    glb, out = load_post(fe)
    check(out == "", "pio_post: při načtení nic nevypisuje (%r)" % out)
    check([t for t, _ in fe.pre] == ["checkprogsize"], "pio_post: výpis velikosti je zapojen PŘED kontrolu PlatformIO (checkprogsize)")
    check([t for t, _ in fe.post] == ["$BUILD_DIR/${PROGNAME}.bin"] * 2, "pio_post: po sestavení se spouští výpis velikosti (záloha) a kontrola .bin")
    size_cb = fe.pre[0][1]
    _, out1 = with_stdout(size_cb, target=["checkprogsize"], source=[], env=fe)
    check("VELIKOST FIRMWARU" in out1 and "vejde se s rezervou" in out1, "pio_post: výpis velikosti z mapy v adresáři sestavení")
    _, out2 = with_stdout(fe.post[0][1], target=["x.bin"], source=[], env=fe)
    check(out2 == "", "pio_post: velikost se nevypisuje podruhé")
    _, out3 = with_stdout(fe.post[1][1], target=[os.path.join(bdir, "nic.bin")], source=[], env=fe)
    check(out3 == "", "pio_post: chybějící .bin se tiše přeskočí")
    garbage = os.path.join(bdir, "firmware.bin")
    with open(garbage, "wb") as fh:
        fh.write(b"toto neni obraz ESP32" * 20)
    _, out4 = with_stdout(fe.post[1][1], target=[garbage], source=[], env=fe)
    check("KONTROLA" in out4 and "NENAHRÁVEJTE" in out4, "pio_post: nesmyslný .bin = výslovné NENAHRÁVEJTE (%r)" % out4[:200])
    # mapa chybí: bez výpisu a bez výjimky
    fe2 = FakeEnv(proj, os.path.join(tmp, "prazdno"))
    glb2, _ = load_post(fe2)
    _, out5 = with_stdout(fe2.pre[0][1], target=[], source=[], env=fe2)
    check(out5 == "", "pio_post: chybí mapa -> ticho")
    # rozbité prostředí: žádná výjimka do PlatformIO
    fe3 = FakeEnv(proj, bdir, broken=True)
    glb3, _ = load_post(fe3)
    try:
        _, out6 = with_stdout(fe3.pre[0][1], target=[], source=[], env=fe3)
        check("selhal" in out6, "pio_post: rozbité prostředí -> jen POZOR, ne výjimka (%r)" % out6)
    except Exception as exc:  # noqa: BLE001
        check(False, "pio_post: výjimka z funkce volané PlatformIO: %r" % exc)
    # zapojení před checkprogsize se nepodařilo: sestavení pokračuje, výpis zůstane po sestavení
    fe4 = FakeEnv(proj, bdir, broken="pre")
    try:
        glb4, out7 = load_post(fe4)
        check("nepodařilo zapojit" in out7 and len(fe4.post) == 2, "pio_post: nepodařené zapojení před kontrolu = POZOR, záloha zůstane (%r)" % out7)
    except Exception as exc:  # noqa: BLE001
        check(False, "pio_post: načtení skončilo výjimkou: %r" % exc)

# ---------------------------------------------------------------- skutečná mapa (volitelně)
real = os.environ.get("ARBOLOG_REAL_MAP")
if real:
    with open(real, encoding="utf-8", errors="replace") as fh:
        rs = sr.summarize(fh.read())
    want = os.environ.get("ARBOLOG_REAL_MAP_SIZE")
    if want:
        check(rs["program"] == int(want), "skutečná mapa %s: velikost %d == %s hlášeno PlatformIO" % (real, rs["program"], want))
    check(sum(x["code"] for x in rs["groups"].values()) == rs["code"], "skutečná mapa: součet kódu po skupinách sedí")
    print("  skutečná mapa: %s -> %d B, nevložených šablon %d" % (real, rs["program"], rs["unopt"]))

print("test_size_report: %d kontrol, %d selhalo" % (CHECKS, len(FAILS)))
if FAILS:
    sys.exit(1)
print("test_size_report: OK, %d kontrol" % CHECKS)
