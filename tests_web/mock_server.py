#!/usr/bin/env python3
"""Simulované zařízení ARBOLOK pro zkoušení webového rozhraní bez hardwaru.

Implementuje stejné HTTP API jako firmware (viz docs/API.md): stav, měření, kalibrace,
nastavení, log, profil napětí, soubory. Slouží i jako "kontraktní test" pro web/app.js.

    python3 tests_web/mock_server.py [--port 8099] [--html cesta/k/index.html]
"""
import argparse
import json
import math
import os
import random
import re
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

LOCK = threading.Lock()


class Device:
    def __init__(self):
        self.t0 = time.time()
        self.clock_offset = 0.0     # posun hodin zařízení proti "lokálnímu" času telefonu (s)
        self.clock_set = True
        self.clock_ok = True
        self.seq = 1
        self.msg = "ok"
        self.busy_until = 0.0
        self.session_total = 240
        self.session_start = time.time()
        self.cfg = dict(lang="cz", web=4, int=30, dry=30, wet=85, gd=0.05, gw=3.0, open=4095, scale=1,
                        vexc=True, pmode=2, rpull=10.0, vcorr=0.97, bias=0, biasUsed=32, ret=0,
                        maxms=300, eps=5, pdis=30, lpul=1, cdel=150, wjmp=8, wwin=6, cport=0, txp=1, pw=0, man=0, mnote=2, mext=1, live=15, early=1000, loc=0, locs=["Místo 1"], locd=[[30, 85, 0.05, 3.0]])
        self.real_ret = 0           # pin, na který je druhá elektroda na "desce" opravdu přepojená (0 = GND)
        self.real_bias = 32         # pin, který na "desce" opravdu buzí dělič (None = žádný z kandidátů)
        self.wire = dict(have=False, res=0, pin=0, n=0, p=[], lo=[], hi=[])
        self.vbat = 3.27
        self.temp = 18.4
        self.raw = 2870.0
        self.flags = 0
        self.have = True
        self.meas_at = time.time()
        self.prof_valid = False
        self.fail_next = None       # např. "cal" -> příští kalibrace skončí chybou
        self.files = {"log.txt": None, "chart.js": b"x" * 208521, "style.css": b"y" * 3152}
        self.logtext = make_log(20)
        self.loggen = 7  # verze obsahu logu (firmware: logGeneration) – mění se při přepsání, ne při připsání
        self.sleeping = False
        self.fw = "v6.16-mock"
        # čítače spotřeby (v6.5): ~3 dny provozu po 30 min, 2 webové relace
        self.pwr = dict(save=False, int=30, since=self.dev_unix() - 3 * 86400, boots=146, logs=144,
                        awake=146 * 450, web=420, sleep=3 * 86400 - 500, k=1.0412, cap=2000)
        self.portal_ok = False
        self.ota_body = b""          # poslední nahraný firmware (POST /update)
        self.reboot_until = 0.0      # do kdy "se restartuje" (neodpovídá)

    # přečíslování míst v logu (logfmt::remapPlaceLine): mp[staré číslo] = nové, 0 = smazané místo
    def remap_log(self, mp):
        out = []
        for ln in self.logtext.splitlines(True):
            if not ln[:1].isdigit():
                out.append(ln)
                continue
            data = ln.split(" # ")[0]
            m = re.search(r" p(\d+)(?=[ \r\n]|$)", data)
            old = int(m.group(1)) if m else 1
            if old not in mp or mp[old] == old:
                out.append(ln)
            elif m:
                out.append(ln[:m.start(1)] + str(mp[old]) + ln[m.end(1):])
            else:
                body = data.rstrip("\r\n")
                out.append(body + " p%d" % mp[old] + ln[len(body):])
        self.logtext = "".join(out)
        self.loggen += 1

    # "lokální" unix čas zařízení
    def dev_unix(self):
        n = time.time()
        local = n - time.timezone  # lokální čas jako "unix" (jako RTC)
        return int(local + self.clock_offset)

    def g(self):
        r = max(self.raw, 10.0)
        return max((self.cfg["open"] - r) / r, 1e-3)

    def pct(self):
        c = self.cfg
        if c["scale"] == 0:
            return max(0.0, min(100.0, 100.0 - self.raw / 4095.0 * 100.0))
        g = self.g()
        x = (math.log(g) - math.log(c["gd"])) / (math.log(c["gw"]) - math.log(c["gd"]))
        return max(0.0, min(100.0, x * 100.0))

    def post_msg(self, m):
        self.msg = m
        self.seq += 1

    def level(self):
        v = self.vbat
        return 2 if v < 2.9 else (1 if v < 3.0 else 0)

    def status(self):
        c = self.cfg  # jako locSyncFromSettings: pracovní kopie vybraného místa -> tabulka míst
        while len(c["locd"]) < len(c["locs"]):
            c["locd"].append([c["dry"], c["wet"], 0.05, 3.0])
        del c["locd"][len(c["locs"]):]
        c["locd"][c["loc"]] = [c["dry"], c["wet"], c["gd"], c["gw"]]
        left = max(0, int(self.session_total - (time.time() - self.session_start)))
        return {
            "fw": self.fw, "id": "0123456", "board": 1, "up": int(time.time() - self.t0), "heap": 183456,
            "clock": {"ok": self.clock_ok, "unix": self.dev_unix(), "set": self.clock_set},
            "bat": {"v": self.vbat, "pct": 55, "lvl": self.level()},
            "m": {"have": self.have, "pct": round(self.pct(), 1) if self.have and not (self.flags & 20) else None,
                  "raw": self.raw, "rawRaw": self.raw,
                  "g": round(self.g(), 4), "ms": 12, "settled": True, "flags": self.flags,
                  "age": int(time.time() - self.meas_at),
                  "rawB": (4095 - self.raw) if self.cfg["ret"] else None, "ph": 2 if self.cfg["ret"] and not (self.flags & 32) else 0},
            "temp": self.temp, "busy": time.time() < self.busy_until, "msg": self.msg, "seq": self.seq,
            "sess": {"left": left, "total": self.session_total, "max": 1800},
            "fs": {"total": 1441792, "used": 380000, "log": len(self.logtext.encode()), "lmax": 573440, "gen": self.loggen},
            "logErr": False, "ota": False,
            "cfg": self.cfg, "prof": {"valid": self.prof_valid, "n": 30 if self.prof_valid else 0},
            "pwr": self.pwr,
            "wire": self.wire,
        }


def make_log(days, interval_min=30, stray=True):
    rnd = random.Random(3)
    now = int(time.time() - time.timezone)
    start = now - days * 86400
    lines = []
    hum = 70.0
    t = (start // 60) * 60
    while t < now:
        # zalévání a vysychání
        hum -= 0.35 + rnd.random() * 0.15
        if hum < 24:
            hum = 88 + rnd.random() * 4
        tm = time.gmtime(t)
        hod = tm.tm_hour + tm.tm_min / 60
        temp = 16 + 6 * math.sin((hod - 9) / 24 * 2 * math.pi) + rnd.random() * 0.4
        vb = 3.33 - 0.000012 * ((t - start) / 60) + rnd.random() * 0.004
        # mezera v datech (výpadek)
        if not (days > 10 and (now - t) > 6 * 86400 and (now - t) < 6.4 * 86400):
            if (now - t) > 2 * 86400:  # starší řádky ve formátu do v6.12
                lines.append("%04d-%02d-%02d %02d:%02d %d%% %.1fC %.2fV r%d s%d\r\n" % (
                    tm.tm_year, tm.tm_mon, tm.tm_mday, tm.tm_hour, tm.tm_min, round(hum), temp, vb,
                    int(4095 / (1 + 0.05 * math.exp(hum / 100 * math.log(60)))), 8 + rnd.randint(0, 6)))
            else:  # poslední 2 dny krátkým formátem (od v6.13)
                lines.append("%02d%02d%02d%02d%02d %d %d %d\r\n" % (
                    tm.tm_year % 100, tm.tm_mon, tm.tm_mday, tm.tm_hour, tm.tm_min, round(hum), round(temp), round(vb * 100)))
        t += interval_min * 60
    # pár řádků ve starém formátu (v3), rozbitý řádek a řádek s nesprávnými hodinami
    if stray:
        lines.insert(5, "2025-11-01 10:00 45% 21.0C 3.25V\r\n")
    lines.append("garbage line\r\n")
    return "".join(lines)


DEV = Device()
HTML_PATH = None


def profile_curve(kind):
    ts, vs = [], []
    t = 0.3
    while t < 300:
        ts.append(round(t, 2))
        if kind == "rise":
            v = 3000 + 700 * (1 - math.exp(-t / 60)) - 800 * math.exp(-t / 0.7)
        elif kind == "fall":
            v = 2500 - 500 * (1 - math.exp(-t / 80)) - 700 * math.exp(-t / 0.7)
        else:
            v = 2870 - 900 * math.exp(-t / 1.2) + random.uniform(-3, 3)
        vs.append(round(v, 1))
        t *= 1.35
    return ts, vs


PROF_KIND = ["ok"]


class H(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        pass

    def _send(self, code, body, ctype="application/json", extra=None):
        if isinstance(body, (dict, list)):
            body = json.dumps(body)
        if isinstance(body, str):
            body = body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(body)

    def _q(self):
        u = urlparse(self.path)
        return u.path, {k: v[0] for k, v in parse_qs(u.query).items()}

    def do_GET(self):
        path, q = self._q()
        with LOCK:
            if DEV.sleeping:
                self.close_connection = True
                return
            if time.time() < DEV.reboot_until:
                self.close_connection = True
                return
            if path == "/":
                with open(HTML_PATH, "rb") as f:
                    self._send(200, f.read(), "text/html; charset=utf-8")
            elif path == "/api/status":
                self._send(200, DEV.status())
            elif path in ("/api/log", "/log.txt"):
                if DEV.fail_next == "log-busy":  # zařízení právě ředí log: web čeká max. 0,5 s a odpoví busy
                    DEV.fail_next = None
                    return self._send(503, {"ok": False, "err": "busy"})
                # log, který neexistuje (nebo byl smazán), je pro stránku prázdný: 200 + prázdné tělo
                # ?from=N jen bajty od N (N > velikost -> 409 stale), ?tail=N posledních ~N bajtů od začátku řádku
                data = DEV.logtext.encode()
                start = int(q.get("from", 0) or 0)
                if start > len(data):
                    return self._send(409, {"ok": False, "err": "stale"})
                tail = int(q.get("tail", 0) or 0)
                if tail and len(data) - start > tail:
                    start = len(data) - tail
                    nl = data.find(b"\n", start)
                    start = len(data) if nl < 0 else nl + 1
                self._send(200, data[start:], "text/plain; charset=utf-8",
                           {"X-Log-Size": str(len(data)), "X-Log-From": str(start), "X-Log-Gen": str(DEV.loggen)})
            elif path == "/api/log/download":
                self._send(200, DEV.logtext, "text/plain", {"Content-Disposition": 'attachment; filename="0123456.txt"'})
            elif path == "/api/profile":
                if not DEV.prof_valid:
                    self._send(200, {"n": 0, "t": [], "v": []})
                else:
                    ts, vs = profile_curve(PROF_KIND[0])
                    self._send(200, {"n": len(ts), "t": ts, "v": vs})
            elif path == "/api/files":
                out = []
                for n, d in DEV.files.items():
                    out.append({"n": n, "s": len(DEV.logtext) if d is None else len(d)})
                self._send(200, out)
            elif path == "/api/file":
                n = q.get("name", "")
                if n in DEV.files:
                    d = DEV.logtext.encode() if DEV.files[n] is None else DEV.files[n]
                    self._send(200, d, "application/octet-stream")
                else:
                    self._send(404, {"err": "notfound"})
            elif path in ("/apple-touch-icon.png", "/icon-192.png"):
                name = "apple-touch-icon.png" if path.startswith("/apple") else "icon-192.png"
                with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "web", "icons", name), "rb") as f:
                    self._send(200, f.read(), "image/png")
            elif path == "/manifest.webmanifest":
                self._send(200, {"name": "Arbolok", "short_name": "Arbolok", "start_url": "/", "display": "standalone",
                                 "icons": [{"src": "/icon-192.png", "sizes": "192x192", "type": "image/png"}]},
                           "application/manifest+json")
            elif path == "/portal":
                with open(os.path.join(os.path.dirname(HTML_PATH), "portal.html"), "rb") as f:
                    self._send(200, f.read(), "text/html; charset=utf-8")
            elif path == "/portal/ok":
                DEV.portal_ok = True
                self._send(204, b"", "text/plain")
            elif path == "/update":
                with open(os.path.join(os.path.dirname(HTML_PATH), "update.html"), "rb") as f:
                    self._send(200, f.read(), "text/html; charset=utf-8")
            else:
                self._send(404, {"err": "notfound"})

    def do_POST(self):
        path, q = self._q()
        n = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(n) if n else b""
        if path == "/update":
            with LOCK:
                if q.get("name") != "firmware":
                    return self._send(400, "missing name", "text/plain")
                DEV.ota_body = body
                m = re.search(rb"\0(v(?:[4-9]|[1-9][0-9])\.\d+)\0", body)
                ok = b"\xe9" in body[:400]
                self._send(200, '<meta content="15;URL=/"http-equiv=refresh>Update Success! Rebooting...' if ok
                           else "Update error: Wrong Magic Byte", "text/html")
                if ok:
                    DEV.reboot_until = time.time() + 4
                    DEV.fw = (m.group(1).decode() if m else "v?") + "-mock"
            return
        with LOCK:
            if DEV.sleeping:
                self.close_connection = True
                return
            c = DEV.cfg
            DEV.busy_until = time.time() + 0.6
            if path == "/api/measure":
                DEV.raw = max(200.0, min(4000.0, DEV.raw + random.uniform(-6, 6)))
                DEV.meas_at = time.time()
                DEV.post_msg("ok:measure")
                self._send(200, {"ok": True})
            elif path == "/api/cal":
                w = q.get("which")
                if DEV.fail_next == "cal":
                    DEV.fail_next = None
                    DEV.post_msg("err:cal")
                elif DEV.flags & 20 and w in ("dry", "wet"):  # zkrat / uzel nereaguje: kalibrovat nelze
                    DEV.post_msg("err:short")
                elif w == "dry":
                    c["gd"] = round(DEV.g(), 4)
                    DEV.post_msg("ok:dry")
                elif w == "wet":
                    c["gw"] = round(DEV.g(), 4)
                    DEV.post_msg("ok:wet")
                elif w == "reset":
                    c.update(gd=0.05, gw=3.0, open=4095)
                    DEV.post_msg("ok:reset")
                else:
                    return self._send(400, {"ok": False, "err": "param"})
                self._send(200, {"ok": True})
            elif path == "/api/settings":
                changed = False
                try:
                    if "lang" in q:
                        if q["lang"] not in ("cz", "en", "ua"):
                            raise ValueError
                        c["lang"] = q["lang"]
                    for k, lo, hi in (("web", 1, 30), ("int", 0, 120), ("dry", 0, 99), ("wet", 1, 100), ("pmode", 1, 2),
                                      ("scale", 0, 1), ("wjmp", 1, 50), ("wwin", 1, 48)):  # parametry pulzu jsou pevné
                        if k in q:
                            v = int(q[k])
                            if not lo <= v <= hi:
                                raise ValueError
                            c[k] = v
                            changed = True
                    if "cport" in q:
                        if q["cport"] not in ("0", "1", "2"):
                            raise ValueError
                        c["cport"] = int(q["cport"])
                        changed = True
                    if "live" in q:
                        if not q["live"].isdigit() or not 5 <= int(q["live"]) <= 120:
                            raise ValueError
                        c["live"] = int(q["live"])
                        changed = True
                    for k, hi in (("man", 1), ("mnote", 2), ("mext", 1)):
                        if k in q:
                            if not q[k].isdigit() or int(q[k]) > hi:
                                raise ValueError
                            c[k] = int(q[k])
                            changed = True
                    if "txp" in q:
                        if q["txp"] not in ("0", "1"):
                            raise ValueError
                        c["txp"] = int(q["txp"])
                        changed = True
                    if "vcorr" in q:
                        v = float(q["vcorr"])
                        if not 0.8 <= v <= 1.2:
                            raise ValueError
                        c["vcorr"] = v
                        changed = True
                    if "vexc" in q:
                        c["vexc"] = q["vexc"] == "1"
                        changed = True
                    if "ret" in q:
                        rp = int(q["ret"])
                        if rp not in (0, 14):
                            raise ValueError
                        c["ret"] = rp
                        DEV.flags = (DEV.flags & ~32) | (32 if rp and rp != DEV.real_ret else 0)
                        changed = True
                    if "bias" in q:
                        b = int(q["bias"])
                        if b not in (0, 32, 26):
                            raise ValueError
                        c["bias"] = b
                        c["biasUsed"] = b or 32
                        if b == 0 and DEV.real_bias in (32, 26):  # "Automaticky": rozpoznání hned
                            c["bias"] = c["biasUsed"] = DEV.real_bias
                        DEV.flags = 0 if c["biasUsed"] == DEV.real_bias else 16
                        changed = True
                except ValueError:
                    return self._send(400, {"ok": False, "err": "param"})
                DEV.post_msg("ok:settings" if changed or "lang" not in q else "ok:lang")
                self._send(200, {"ok": True})
            elif path == "/api/calvbat":
                try:
                    v = float(q.get("v", "x").replace(",", "."))
                except ValueError:
                    return self._send(400, {"ok": False, "err": "param"})
                if not 2.0 <= v <= 4.5:
                    DEV.post_msg("err:vbat")
                else:
                    DEV.vbat = v
                    c["vcorr"] = round(v / 3.3, 4)
                    DEV.post_msg("ok:vbat")
                self._send(200, {"ok": True})
            elif path == "/api/time":
                try:
                    vals = [int(q[k]) for k in ("y", "mo", "d", "h", "mi", "s")]
                except (KeyError, ValueError):
                    return self._send(400, {"ok": False, "err": "param"})
                import calendar
                phone_local = calendar.timegm(vals + [0, 0, 0])
                DEV.clock_offset = phone_local - (time.time() - time.timezone)
                DEV.clock_set = True
                DEV.post_msg("ok:time")
                self._send(200, {"ok": True})
            elif path == "/api/manual":
                if not DEV.cfg["man"]:
                    DEV.post_msg("err:manoff")
                else:
                    t = time.gmtime(DEV.dev_unix())
                    note = re.sub(r'[\x00-\x1f;"\\<>]', '', q.get("note", "")).strip()[:24]
                    hum = round(max(0, min(100, 100 * (4095 - DEV.raw) / 4095)))
                    ln = "%02d%02d%02d%02d%02d %d %d %d%s M%s\r\n" % (
                        t.tm_year % 100, t.tm_mon, t.tm_mday, t.tm_hour, t.tm_min, hum, round(DEV.temp), round(DEV.vbat * 100),
                        (" p%d" % (DEV.cfg["loc"] + 1)) if DEV.cfg["loc"] else "", (" # " + note) if note else "")
                    DEV.logtext += ln
                    DEV.post_msg("ok:manual")
                self._send(200, {"ok": True})
            elif path == "/api/loc":
                op = q.get("op")
                c = DEV.cfg
                c = DEV.cfg
                locs = c["locs"]
                name = (q.get("name") or "").strip()[:24]
                DEV.status()  # srovnat tabulku s pracovní kopií
                if op == "sel" and q.get("i", "").isdigit() and int(q["i"]) < len(locs):
                    c["loc"] = int(q["i"]); d = c["locd"][c["loc"]]
                    c.update(dry=d[0], wet=d[1], gd=d[2], gw=d[3]); DEV.post_msg("ok:loc")
                elif op == "new" and name and len(locs) < 8:
                    locs.append(name); c["locd"].append([c["dry"], c["wet"], 0.05, 3.0]); c["loc"] = len(locs) - 1
                    c.update(gd=0.05, gw=3.0); DEV.post_msg("ok:loc")
                elif op == "ren" and name and q.get("i", "").isdigit() and int(q["i"]) < len(locs):
                    locs[int(q["i"])] = name; DEV.post_msg("ok:loc")
                elif op == "del" and q.get("i", "").isdigit() and int(q["i"]) < len(locs) and len(locs) > 1:
                    i = int(q["i"])
                    DEV.remap_log({k + 1: (k + 1 if k < i else (0 if k == i else k)) for k in range(len(locs))})
                    locs.pop(i); c["locd"].pop(i)
                    if c["loc"] == i:
                        c["loc"] = 0; d = c["locd"][0]; c.update(dry=d[0], wet=d[1], gd=d[2], gw=d[3])
                    elif c["loc"] > i: c["loc"] -= 1
                    DEV.post_msg("ok:loc")
                elif op in ("sel", "new", "ren", "del"):
                    DEV.post_msg("err:loc")
                else:
                    return self._send(400, {"err": "param"})
                self._send(200, {"ok": True})
            elif path == "/api/locs":
                c = DEV.cfg
                try:
                    n = int(q.get("n", ""))
                    assert 1 <= n <= 8
                    rows = []
                    for i in range(n):
                        nm = (q.get("n%d" % i) or "").strip()[:24]
                        d, w = int(q["d%d" % i]), int(q["w%d" % i])
                        gd, gw = float(q["gd%d" % i]), float(q["gw%d" % i])
                        assert nm and 0 <= d < w <= 100 and gd > 0 and gw > gd * 1.2 and gw < 1000
                        rows.append((nm, [d, w, gd, gw]))
                    a = int(q.get("a", "0"))
                    assert 0 <= a < n
                except (ValueError, KeyError, AssertionError):
                    return self._send(400, {"err": "param"})
                if any(("o%d" % i) in q for i in range(n)):  # přečíslování záznamů (v6.7)
                    old = len(c["locs"])
                    mp = {k: 0 for k in range(1, old + 1)}
                    for j in range(n):
                        o = int(q.get("o%d" % j, "-1"))
                        if 0 <= o < old and mp[o + 1] == 0:
                            mp[o + 1] = j + 1
                    DEV.remap_log(mp)
                c["locs"] = [r[0] for r in rows]
                c["locd"] = [r[1] for r in rows]
                c["loc"] = a
                d = c["locd"][a]
                c.update(dry=d[0], wet=d[1], gd=d[2], gw=d[3])
                DEV.post_msg("ok:locs")
                self._send(200, {"ok": True})
            elif path == "/api/wiring":
                pins = [32, 26, 25]
                found = DEV.real_bias in pins
                DEV.wire = dict(have=True, res=1 if found else 0, pin=DEV.real_bias if found else 0, n=3, p=pins,
                                lo=[2.0, 2.0, 2.0], hi=[4090.0 if p == DEV.real_bias else 5.0 for p in pins])
                if found:
                    c["bias"] = c["biasUsed"] = DEV.real_bias
                    DEV.flags = 0
                    DEV.post_msg("ok:wiring")
                else:
                    DEV.post_msg("err:wiring")
                self._send(200, {"ok": True})
            elif path == "/api/profile/capture":
                DEV.prof_valid = True
                DEV.post_msg("ok:profile")
                self._send(200, {"ok": True})
            elif path == "/api/extend":
                DEV.session_total = min(1800, DEV.session_total + 300)
                DEV.post_msg("ok:extend")
                self._send(200, {"ok": True})
            elif path in ("/api/restart", "/api/factory"):
                self._send(200, {"ok": True})
            elif path == "/api/wifipw":
                pw = parse_qs(urlparse(self.path).query, keep_blank_values=True).get("pw", [None])[0]  # "" = zrušit heslo
                if pw is None or not (pw == "" or (8 <= len(pw) <= 63 and all(0x20 <= ord(ch) <= 0x7e for ch in pw))):
                    return self._send(400, {"ok": False, "err": "pw"})
                DEV.wifipw = pw
                DEV.cfg["pw"] = 1 if pw else 0
                self._send(200, {"ok": True})
            elif path == "/api/log/clear":
                if DEV.fail_next == "clear-busy":
                    DEV.fail_next = None
                    return self._send(409, {"ok": False, "err": "busy"})
                DEV.logtext = ""
                DEV.loggen += 1
                self._send(200, {"ok": True})
            elif path == "/api/files/delete":
                if DEV.fail_next == "del":
                    DEV.fail_next = None
                    return self._send(409, {"ok": False, "err": "busy"})
                if DEV.fail_next == "del-unknown":
                    DEV.fail_next = None
                    return self._send(403, {"ok": False, "err": "origin"})
                DEV.files.pop(q.get("name", ""), None)
                self._send(200, {"ok": True})
            elif path == "/upload":
                if DEV.fail_next == "up-busy":
                    DEV.fail_next = None
                    return self._send(409, {"ok": False, "err": "busy"})
                if DEV.fail_next == "up-space":
                    DEV.fail_next = None
                    return self._send(507, {"ok": False, "err": "fs"})
                DEV.files["uploaded.txt"] = b"data"
                self._send(200, {"ok": True})
            else:
                self._send(404, {"err": "notfound"})


def serve(port, html):
    global HTML_PATH
    HTML_PATH = html
    srv = ThreadingHTTPServer(("127.0.0.1", port), H)
    srv.daemon_threads = True
    t = threading.Thread(target=srv.serve_forever, daemon=True)
    t.start()
    return srv


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8099)
    ap.add_argument("--html", default="/tmp/arbolog_build/index.html")
    a = ap.parse_args()
    srv = serve(a.port, a.html)
    print("mock na http://127.0.0.1:%d/" % a.port)
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        pass
