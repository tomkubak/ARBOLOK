#!/usr/bin/env python3
"""Sestaví webové rozhraní do jediného komprimovaného souboru a uloží ho jako C++ pole.

    web/index.html + web/app.css + web/i18n.js + web/plot.js + web/app.js
        -> (slepit, zmenšit) -> gzip -> src/web_assets.h   (INDEX_HTML_GZ)
    web/update.html (samostatná stránka nahrávání firmwaru, /update) -> UPDATE_HTML_GZ
    web/portal.html (rozcestník po připojení k Wi-Fi, /portal)         -> PORTAL_HTML_GZ
    web/icons/apple-touch-icon.png, icon-192.png (ikona na plochu)     -> ICON_180_PNG, ICON_192_PNG
      (PNG jsou vyrenderované z web/icons/icon.svg; nový render: python3 tools/make_icons.py)

Spouští se automaticky před každým sestavením z PlatformIO (tools/pio_pre.py),
ručně:   python3 tools/gen_web_assets.py            (zapíše src/web_assets.h)
         python3 tools/gen_web_assets.py --html X   (uloží i nekomprimovanou stránku do X)
         python3 tools/gen_web_assets.py --check    (jen ověří, že web_assets.h je aktuální)

Výstup je deterministický (gzip s mtime = 0), takže se hlavička nemění, dokud se nezmění zdroje.
"""
import gzip
import hashlib
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WEB = os.path.join(ROOT, "web")
OUT = os.path.join(ROOT, "src", "web_assets.h")


def read(name):
    with open(os.path.join(WEB, name), "r", encoding="utf-8") as f:
        return f.read()


def min_css(s):
    s = re.sub(r"/\*.*?\*/", "", s, flags=re.S)
    s = re.sub(r"\s+", " ", s)
    s = re.sub(r"\s*([{};,])\s*", r"\1", s)
    return s.strip()


def min_js(s):
    if "`" in s:
        raise SystemExit("JS obsahuje zpětné apostrofy (template literal) – zmenšování by mohlo rozbít mezery; upravte min_js()")
    out = []
    for line in s.splitlines():
        t = line.strip()
        if not t or t.startswith("//"):
            continue
        out.append(t)
    return "\n".join(out)


def min_html(s):
    s = re.sub(r"<!--(?!\[).*?-->", "", s, flags=re.S)
    lines = [ln.strip() for ln in s.splitlines()]
    return "\n".join(ln for ln in lines if ln)


def build_html():
    html = read("index.html")
    css = min_css(read("app.css"))
    js = "\n".join(min_js(read(n)) for n in ("i18n.js", "plot.js", "app.js"))
    if "/*@CSS@*/" not in html or "/*@JS@*/" not in html:
        raise SystemExit("index.html nemá značky /*@CSS@*/ a /*@JS@*/")
    html = html.replace("/*@CSS@*/", css, 1)   # str.replace nevyhodnocuje zpětná lomítka v JS
    html = html.replace("/*@JS@*/", js, 1)
    return min_html_keep_script(html)


def min_html_keep_script(html):
    # zmenšit jen část mimo <script>, JS už je zmenšený
    parts = re.split(r"(<script>.*?</script>)", html, flags=re.S)
    return "\n".join(p if p.startswith("<script>") else min_html(p) for p in parts if p.strip())


def build_update_html(name="update.html"):
    html = read(name)
    html = re.sub(r"<style>(.*?)</style>", lambda m: "<style>" + min_css(m.group(1)) + "</style>", html, flags=re.S)
    html = re.sub(r"<script>(.*?)</script>", lambda m: "<script>" + min_js(m.group(1)) + "</script>", html, flags=re.S)
    return min_html_keep_script(html)


def arr(gz):
    return "\n".join("  " + ",".join("0x%02x" % b for b in gz[i:i + 16]) + "," for i in range(0, len(gz), 16))


def read_bin(*parts):
    with open(os.path.join(WEB, *parts), "rb") as f:
        return f.read()


ICONS = [read_bin("icons", "apple-touch-icon.png"), read_bin("icons", "icon-192.png")]


def to_header(gz, plain_len, ugz=b"", pgz=b""):
    h = hashlib.sha1(gz).hexdigest()[:8]
    rows = []
    for i in range(0, len(gz), 16):
        rows.append("  " + ",".join("0x%02x" % b for b in gz[i:i + 16]) + ",")
    body = "\n".join(rows)
    return (
        "// GENEROVÁNO nástrojem tools/gen_web_assets.py ze zdrojů ve web/ – ručně neupravujte.\n"
        "#pragma once\n"
        "#include <Arduino.h>\n"
        "#include <stddef.h>\n"
        "#include <stdint.h>\n\n"
        '#define WEB_ASSET_HASH "%s"\n'
        "#define INDEX_HTML_PLAIN_LEN %d\n"
        "static const size_t INDEX_HTML_GZ_LEN = %d;\n"
        "static const uint8_t INDEX_HTML_GZ[] PROGMEM = {\n%s\n};\n\n"
        "// stránka /update (nahrávání firmwaru)\n"
        "static const size_t UPDATE_HTML_GZ_LEN = %d;\n"
        "static const uint8_t UPDATE_HTML_GZ[] PROGMEM = {\n%s\n};\n\n"
        "// rozcestník po připojení k Wi-Fi (/portal)\n"
        "static const size_t PORTAL_HTML_GZ_LEN = %d;\n"
        "static const uint8_t PORTAL_HTML_GZ[] PROGMEM = {\n%s\n};\n\n"
        "// ikona aplikace (Přidat na plochu)\n"
        "static const size_t ICON_180_PNG_LEN = %d;\n"
        "static const uint8_t ICON_180_PNG[] PROGMEM = {\n%s\n};\n"
        "static const size_t ICON_192_PNG_LEN = %d;\n"
        "static const uint8_t ICON_192_PNG[] PROGMEM = {\n%s\n};\n"
    ) % (h, plain_len, len(gz), body, len(ugz), arr(ugz), len(pgz), arr(pgz),
         len(ICONS[0]), arr(ICONS[0]), len(ICONS[1]), arr(ICONS[1]))


def main(argv):
    html = build_html()
    raw = html.encode("utf-8")
    gz = gzip.compress(raw, compresslevel=9, mtime=0)
    uraw = build_update_html().encode("utf-8")
    ugz = gzip.compress(uraw, compresslevel=9, mtime=0)
    praw = build_update_html("portal.html").encode("utf-8")
    pgz = gzip.compress(praw, compresslevel=9, mtime=0)
    header = to_header(gz, len(raw), ugz, pgz)

    if "--html" in argv:
        path = argv[argv.index("--html") + 1]
        with open(path, "w", encoding="utf-8") as f:
            f.write(html)
        with open(os.path.join(os.path.dirname(os.path.abspath(path)), "update.html"), "w", encoding="utf-8") as f:
            f.write(uraw.decode("utf-8"))
        with open(os.path.join(os.path.dirname(os.path.abspath(path)), "portal.html"), "w", encoding="utf-8") as f:
            f.write(praw.decode("utf-8"))

    if "--check" in argv:
        try:
            with open(OUT, "r", encoding="utf-8") as f:
                cur = f.read()
        except OSError:
            cur = ""
        if cur != header:
            print("web_assets.h je neaktuální – spusťte: python3 tools/gen_web_assets.py")
            return 1
        print("web_assets.h je aktuální (%d B html -> %d B gzip)" % (len(raw), len(gz)))
        return 0

    old = ""
    if os.path.exists(OUT):
        with open(OUT, "r", encoding="utf-8") as f:
            old = f.read()
    if old != header:
        with open(OUT, "w", encoding="utf-8", newline="\n") as f:
            f.write(header)
        print("web_assets.h aktualizován: %d B html -> %d B gzip (+ /update %d B, /portal %d B gzip)" % (len(raw), len(gz), len(ugz), len(pgz)))
    else:
        print("web_assets.h beze změny (%d B gzip)" % len(gz))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
