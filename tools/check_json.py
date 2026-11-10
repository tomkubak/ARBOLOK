#!/usr/bin/env python3
"""Zkontroluje, že JSON vzorky z host testů jsou platné a mají klíče, které čeká web/app.js.

Použití:  python3 -I tools/check_json.py <adresář_se_vzorky>
"""
import json
import pathlib
import sys

STATUS_KEYS = {
    "fw", "id", "board", "up", "heap", "clock", "bat", "m", "temp", "busy", "msg", "seq",
    "sess", "fs", "logErr", "ota", "cfg", "prof", "wire",
}
CFG_KEYS = {"lang", "web", "int", "dry", "wet", "gd", "gw", "open", "scale", "vexc", "pmode", "rpull", "vcorr",
            "bias", "biasUsed", "ret", "maxms", "eps", "pdis", "lpul", "cdel", "wjmp", "wwin", "cport", "early", "loc", "locs", "locd"}
M_KEYS = {"have", "pct", "raw", "rawRaw", "g", "ms", "settled", "flags", "age", "rawB", "ph"}
WIRE_KEYS = {"have", "res", "pin", "n", "p", "lo", "hi"}


def fail(msg):
    print("  FAIL", msg)
    sys.exit(1)


def main(argv):
    d = pathlib.Path(argv[1])
    files = sorted(d.glob("*.json"))
    if not files:
        fail("žádné vzorky v " + str(d))
    for f in files:
        try:
            data = json.loads(f.read_text(encoding="utf-8"))
        except Exception as e:  # noqa: BLE001
            fail("%s: neplatný JSON (%s)" % (f.name, e))
        if f.name.startswith("status_"):
            missing = STATUS_KEYS - set(data)
            if missing:
                fail("%s: chybí klíče %s" % (f.name, sorted(missing)))
            miss2 = CFG_KEYS - set(data["cfg"])
            if miss2:
                fail("%s: chybí cfg klíče %s" % (f.name, sorted(miss2)))
            miss4 = M_KEYS - set(data["m"])
            if miss4:
                fail("%s: chybí m klíče %s" % (f.name, sorted(miss4)))
            w = data["wire"]
            miss3 = WIRE_KEYS - set(w)
            if miss3:
                fail("%s: chybí wire klíče %s" % (f.name, sorted(miss3)))
            if not (len(w["p"]) == len(w["lo"]) == len(w["hi"]) == w["n"]):
                fail("%s: délky polí wire nesedí" % f.name)
        if f.name.startswith("profile"):
            if len(data["t"]) != data["n"] or len(data["v"]) != data["n"]:
                fail("%s: délky polí nesedí" % f.name)
        print("  ok  ", f.name)
    print("JSON: %d souborů v pořádku" % len(files))


if __name__ == "__main__":
    main(sys.argv)
