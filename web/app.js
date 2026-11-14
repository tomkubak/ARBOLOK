(function () {
'use strict';

// ============================================================================
//  Pomocné
// ============================================================================
const $ = (s, r) => (r || document).querySelector(s);
const $$ = (s, r) => Array.from((r || document).querySelectorAll(s));
const pad2 = (n) => (n < 10 ? '0' : '') + n;
const HTMLLANG = { cz: 'cs', en: 'en', ua: 'uk' };
let lang = 'cz';

function t(k, v) {
  let s = (I18N[lang] && I18N[lang][k]) || I18N.en[k] || I18N.cz[k] || k;
  if (v) for (const x in v) s = s.split('{' + x + '}').join(v[x]);
  return s;
}
// Text chyby z kódu API; neznámý kód (např. "origin", "buf") se ukáže jako obecná chyba komunikace.
function terr(code) { const k = 'e_' + (code || 'http'); return I18N.cz[k] ? t(k) : t('e_http'); }
function lsGet(k) { try { return localStorage.getItem(k); } catch (e) { return null; } }
function lsSet(k, v) { try { localStorage.setItem(k, v); } catch (e) { /* soukromé okno apod. */ } }
function esc(s) { return String(s).replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c])); }
function css(n) { return getComputedStyle(document.documentElement).getPropertyValue(n).trim(); }
function fmtClock(sec) {
  const d = new Date(sec * 1000);
  return pad2(d.getUTCHours()) + ':' + pad2(d.getUTCMinutes()) + ':' + pad2(d.getUTCSeconds());
}
function fmtDate(sec) {
  const d = new Date(sec * 1000);
  return d.getUTCDate() + '.' + (d.getUTCMonth() + 1) + '.' + d.getUTCFullYear();
}
function fmtOhm(kohm) {
  if (!isFinite(kohm)) return '--';
  if (kohm >= 1000) return (kohm / 1000).toFixed(kohm >= 10000 ? 0 : 1) + ' MΩ';
  if (kohm >= 100) return kohm.toFixed(0) + ' kΩ';
  return kohm.toFixed(kohm >= 10 ? 1 : 2) + ' kΩ';
}

// ============================================================================
//  Komunikace se zařízením
// ============================================================================
async function api(path, opt) {
  opt = opt || {};
  const ctl = new AbortController();
  const to = setTimeout(() => ctl.abort(), opt.timeout || 7000);
  try {
    return await fetch(path, { method: opt.method || 'GET', cache: 'no-store', signal: ctl.signal });
  } finally { clearTimeout(to); }
}
async function getJSON(path) {
  const r = await api(path);
  if (!r.ok) throw new Error('HTTP ' + r.status);
  return r.json();
}
async function post(path, params) {
  const q = params ? '?' + new URLSearchParams(params).toString() : '';
  const r = await api(path + q, { method: 'POST' });
  let j = null;
  try { j = await r.json(); } catch (e) { /* bez těla */ }
  return { ok: r.ok, status: r.status, json: j };
}

// ============================================================================
//  Stav aplikace
// ============================================================================
let S = null;            // poslední /api/status
let sAt = 0;             // Date.now() příjmu
let fails = 0;
let seqSeen = null;
let pending = null;
let langHold = 0;
let route = 'dash';
// Servis jen s PINem (proti náhodnému zásahu kolemjdoucích, ne skutečné zabezpečení: API zůstává otevřené).
// Odemčení platí do zavření / obnovení stránky.
const SVC_PIN = '0852';
let svcOk = false;
let logRecs = null;
let manRecs = [];  // ruční zápisy (M), v grafech nejsou
let logText = null;
let logAt = 0;
let range = lsGet('range') || '24h';
const ser = 'pct';  // graf ADC od v6.13 není (log už surový čítač nenese)
let tech = lsGet('tech') === '1';  // Servis: technické údaje na přehledu (jen v tomto prohlížeči)
let formDirty = { pw: false, pm: false, ops: false, adv: false, wire: false, ret: false, wdet: false, thr: false, net: false, web: false, man: false, live: false };
let waterEv = [];        // zalití nalezená v celém logu (detectWater)
let lastBanners = '';
let profLoaded = false;
const plots = {};

// ============================================================================
//  Jazyk
// ============================================================================
function applyI18n() {
  document.documentElement.lang = HTMLLANG[lang] || 'cs';
  $$('[data-i]').forEach((el) => { el.textContent = t(el.dataset.i); });
  $$('#langs button').forEach((b) => b.classList.toggle('on', b.dataset.lang === lang));
  lastBanners = '';
  if (S) { renderAll(); }
  if (route === 'logs') renderLogText();
  if (logRecs) renderCharts();
  if (profLoaded) renderProfile();
}
function setLang(l, fromUser) {
  if (!I18N[l]) l = 'cz';
  lang = l;
  lsSet('lang', l);
  if (fromUser) langHold = Date.now() + 4000;
  applyI18n();
}

// ============================================================================
//  Toast, tlačítka
// ============================================================================
let toastTimer = 0;
function toast(msg, err) {
  const el = $('#toast');
  el.textContent = msg;
  el.className = 'toast show' + (err ? ' err' : '');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { el.className = 'toast' + (err ? ' err' : ''); }, err ? 5000 : 2600);
}
function setBusy(btn, on) {
  if (!btn) return;
  btn.classList.toggle('busy', !!on);
  btn.disabled = !!on;
}
function clearPending() {
  if (pending) { setBusy(pending.btn, false); }
  pending = null;
  $$('#v-settings .btn, #v-admin .btn, #btnMan, #btnExtend').forEach((b) => { if (!b.classList.contains('busy')) b.disabled = false; });
}
async function runCmd(btn, path, params, opts) {
  // Předchozí příkaz ještě čeká na výsledek (např. právě se ukládá volba): počkat, nezahazovat (nejvýš 15 s).
  for (let i = 0; i < 150 && pending; i++) await new Promise((r) => setTimeout(r, 100));
  if (pending) return false;
  pending = { btn: btn, at: Date.now() };
  setBusy(btn, true);
  let res;
  try { res = await post(path, params); } catch (e) { clearPending(); toast(t('e_http'), true); return false; }
  if (!res.ok) {
    clearPending();
    const code = res.json && res.json.err;
    toast(terr(code), true);
    return false;
  }
  if (opts && opts.noWait) clearPending();
  return true;
}

// ============================================================================
//  Odpovědi na příkazy (msg/seq ze stavu)
// ============================================================================
function handleMsg(code) {
  if (pending) clearPending();
  if (!code) return;
  if (code.indexOf('err:') === 0) { toast(terr(code.slice(4)), true); return; }
  const k = code.indexOf('ok:') === 0 ? code.slice(3) : 'ok';
  if (k === 'measure' || k === 'lang') return;
  toast(t('m_' + k), false);
  if (k === 'profile') loadProfile();
  if (k === 'manual') loadLog(true);
  if (k === 'locs' || k === 'loc') { locsDirty = false; locsSig = ''; }
  if (k === 'settings' || k === 'loc' || k === 'locs') { Object.keys(formDirty).forEach((f) => { formDirty[f] = false; }); locUi.edit = -1; locUi.adding = false; locsImported = false; fillSettings(true); locSig = ''; renderLoc(); renderLocs(); }
  if (k === 'factory') { fillSettings(true); }
}

// ============================================================================
//  Hlavní polling stavu
// ============================================================================
async function poll() {
  let delay = 2000;
  try {
    S = await getJSON('/api/status');
    sAt = Date.now();
    fails = 0;
    $('#offline').hidden = true;
    onStatus();
    if (S.busy) delay = 700;
  } catch (e) {
    fails++;
    if (fails >= 2) { const o = $('#offline'); o.textContent = t('offline'); o.hidden = false; }
    delay = 3000;
  }
  if (pending && Date.now() - pending.at > 9000) clearPending();
  setTimeout(poll, delay);
}

function onStatus() {
  if (S.cfg.lang !== lang && Date.now() > langHold) setLang(S.cfg.lang, false);
  if (seqSeen === null) seqSeen = S.seq;
  else if (S.seq !== seqSeen) { seqSeen = S.seq; handleMsg(S.msg); }
  renderAll();
  if (route === 'admin' && S.prof.valid && !profLoaded) loadProfile();
}

// ----------------------------------------------------------------------------
//  Ruční zápis (přenosný logger, od v6.10)
// ----------------------------------------------------------------------------
// Další automatické číslo: největší „Bod N“ v ručních zápisech + 1.
function nextPoint() {
  const pre = t('man_pt'), rx = new RegExp('^' + pre.replace(/[.*+?^${}()|[\]\\]/g, '\\$&') + ' (\\d+)$');
  let max = 0;
  manRecs.forEach((r) => { const m = rx.exec(r.note || ''); if (m && +m[1] > max) max = +m[1]; });
  return pre + ' ' + (max + 1);
}
// Vlastní poznámka končící číslem -> stejný text s dalším číslem („Lípa 7“ -> „Lípa 8“).
function bumpNote(s) {
  const m = /^(.*?)(\d+)$/.exec(s);
  return m ? m[1] + (+m[2] + 1) : s;
}
function manRow(r) {
  return '<div class="manrow"><span class="mt">' + esc(fmtWhen(r.t)) + '</span><span class="mh">' + (r.hum === null ? '--' : r.hum + ' %') +
    '</span><span class="mn">' + esc(r.note || '') + (S.cfg.locs && S.cfg.locs.length > 1 ? ' <span class="ml">' + esc(locName(r.loc - 1)) + '</span>' : '') +
    '</span></div>';
}
function renderMan() {
  if (!S) return;
  const c = S.cfg, on = !!c.man;
  $('#manCard').hidden = !on;
  if (on) {
    const mode = c.mnote === undefined ? 2 : c.mnote;
    $('#manNoteRow').hidden = mode !== 1;
    if (mode === 1 && !$('#fManNote').value && document.activeElement !== $('#fManNote')) $('#fManNote').value = lsGet('man_note') || '';
    $('#manAuto').textContent = mode === 2 ? t('man_auto', { n: nextPoint() }) : (mode === 1 ? t('man_note_h') : t('man_plain'));
    $('#manCount').textContent = manRecs.length ? t('man_count', { n: manRecs.length }) : '';
    const last = manRecs.slice(-5).reverse();
    $('#manList').innerHTML = last.length ? last.map(manRow).join('') : '<div class="hint">' + esc(t('man_none')) + '</div>';
  }
  // Záznamy: tabulka všech ručních měření (nejnovější nahoře)
  $('#manLogCard').hidden = !manRecs.length;
  if (manRecs.length) {
    const list = manRecs.slice(-300).reverse();
    $('#manLogInfo').textContent = t('man_count', { n: manRecs.length });
    $('#manTbl').innerHTML = '<tr><th>' + esc(t('man_c_time')) + '</th><th>' + esc(t('man_c_hum')) + '</th><th>' + esc(t('man_c_note')) +
      '</th><th>' + esc(t('man_c_loc')) + '</th></tr>' + list.map((r) => '<tr><td>' + esc(fmtWhen(r.t)) + '</td><td>' +
      (r.hum === null ? '--' : r.hum + ' %') + '</td><td class="n">' + esc(r.note || '') + '</td><td>' + esc(locName(r.loc - 1)) + '</td></tr>').join('');
  }
}

function renderAll() {
  renderHeader();
  renderBanners();
  renderHero();
  renderSession();
  renderLoc();
  renderLocs();
  renderMan();
  reportFill();
  if (route === 'settings') { fillSettings(false); renderCal(); renderFooter(); if (!formDirty.ops) updIntHint(); }
  if (route === 'admin') { fillSettings(false); renderWire(); renderCal(); renderPower(); renderSvc(); }
  if (logRecs && route === 'dash') renderChartLines();
}

function devNow() { return S && S.clock.ok ? S.clock.unix + (Date.now() - sAt) / 1000 : null; }

function renderHeader() {
  $('#devId').textContent = S.id;
  const d = devNow();
  $('#clock').textContent = d === null ? '--:--:--' : fmtClock(d);
  const bat = $('#bat'), v = S.bat.v;
  let lvl = S.bat.lvl;
  if (v === null) lvl = 9;
  bat.dataset.lvl = lvl;
  const pct = S.bat.pct === null || S.bat.pct === undefined ? 0 : S.bat.pct;
  $('#batFill').style.width = (v === null ? 0 : Math.max(6, pct)) + '%';
  $('#batTxt').textContent = v === null ? '--' : v.toFixed(2) + ' V';
}

function renderBanners() {
  const list = [];
  const d = devNow();
  if (!S.clock.set || !S.clock.ok) list.push({ k: 'clk', c: 'warn', i: '🕒', x: t('clock_unset'), a: 'clock_fix' });
  else if (d !== null) {
    const now = new Date();
    const phone = now.getTime() / 1000 - now.getTimezoneOffset() * 60;
    const diff = Math.round(Math.abs(d - phone) / 60);
    if (diff >= 3) list.push({ k: 'clk2', c: 'info', i: '🕒', x: t('clock_diff', { n: diff }), a: 'clock_fix' });
  }
  if (S.bat.lvl === 2) list.push({ k: 'bat2', c: 'bad', i: '🪫', x: t('bat_crit', { v: S.bat.v === null ? '--' : S.bat.v.toFixed(2) }) });
  else if (S.bat.lvl === 1) list.push({ k: 'bat1', c: 'warn', i: '🔋', x: t('bat_low', { v: S.bat.v === null ? '--' : S.bat.v.toFixed(2) }) });
  if (S.pwr && S.pwr.save) list.push({ k: 'save', c: 'warn', i: '🔋', x: t('save_on', { n: S.pwr.int, m: S.cfg.int }) });
  const hs = hibSpans(), lastH = hs.length ? hs[hs.length - 1] : null;
  if (lastH && lastH.open) list.push({ k: 'hib', c: 'bad', i: '🪫', x: t('hib_now', { d: fmtWhen(lastH.t0) }) });
  else if (lastH && devNow() !== null && devNow() * 1000 - lastH.t1 < 7 * 86400e3)
    list.push({ k: 'hib2', c: 'info', i: '🔋', x: t('hib_was', { a: fmtWhen(lastH.t0), b: fmtWhen(lastH.t1) }) });
  if (S.logErr) list.push({ k: 'log', c: 'bad', i: '⚠️', x: t('log_err') });
  const f = S.m.have ? S.m.flags : 0;
  if (f & 2) list.push({ k: 'f2', c: 'warn', i: '🌱', x: t('f_open') });
  if ((f & 4) && isFlood(f)) list.push({ k: 'f4', c: 'info', i: '💧', x: t('f_short') });
  else if (f & 4) list.push({ k: 'f4u', c: 'warn', i: '🔌', x: t('f_short_unc'), a: 'wire_go' });
  if (f & 16) list.push({ k: 'f16', c: 'bad', i: '🔌', x: t('f_noresp'), a: 'wire_go' });
  if (f & 8) list.push({ k: 'f8', c: 'warn', i: '📶', x: t('f_noisy') });
  if (f & 32) list.push({ k: 'f32', c: 'warn', i: '🔁', x: t('f_retbad'), a: 'wire_go' });
  if ((f & 1) && !(f & 2)) list.push({ k: 'f1', c: 'info', i: '〰️', x: t('f_unsettled') });
  const sig = lang + '|' + list.map((b) => b.k + ':' + b.x).join('|');
  if (sig === lastBanners) return;
  lastBanners = sig;
  $('#banners').innerHTML = list.map((b) =>
    '<div class="banner ' + b.c + '"><span class="ic">' + b.i + '</span><div class="tx">' + esc(b.x) +
    (b.a ? '<button data-act="' + b.a + '">' + esc(t(b.a)) + '</button>' : '') + '</div></div>').join('');
}

// ----------------------------------------------------------------------------
//  Měřidlo
// ----------------------------------------------------------------------------
// Analogový ručkový měřák (jako polní vlhkoměr): ciferník 240° (0 % vlevo dole, 100 % vpravo dole), barevné pásmo
// sucho / optimum / mokro podle nastavení, rudá ručka s mosaznou osou. Ciferník se kreslí jen při změně prahů,
// jazyka nebo motivu; ručka se jen otáčí (CSS přechod s malým dokmitem jako u skutečného přístroje).
const GC = { x: 120, y: 118, r: 100 };
function gAng(p) { return (-120 + 2.4 * p) * Math.PI / 180; }  // 0 = nahoru, po směru hodin
function gPt(p, r) { const a = gAng(p); return [GC.x + r * Math.sin(a), GC.y - r * Math.cos(a)]; }
function arcPath(p0, p1, r) {
  const a = gPt(p0, r), b = gPt(p1, r);
  const large = (p1 - p0) * 2.4 > 180 ? 1 : 0;
  return 'M' + a[0].toFixed(2) + ' ' + a[1].toFixed(2) + ' A' + r + ' ' + r + ' 0 ' + large + ' 1 ' + b[0].toFixed(2) + ' ' + b[1].toFixed(2);
}
let gaugeSig = '';
function drawFace(dry, wet) {
  const sig = [dry, wet, lang, css('--dial'), css('--warn')].join('|');
  if (sig === gaugeSig) return;
  gaugeSig = sig;
  const ink = css('--dial-ink'), warn = css('--warn'), ok = css('--ok'), wat = css('--water'), brass = css('--brass');
  const f = (n) => n.toFixed(2);
  let s = '<defs><radialGradient id="gFaceG" cx="50%" cy="40%" r="65%"><stop offset="0" stop-color="' + css('--dial') +
    '"/><stop offset="1" stop-color="' + css('--dial2') + '"/></radialGradient></defs>';
  // rámeček a ciferník
  s += '<circle cx="120" cy="118" r="114" fill="' + brass + '"/>';
  s += '<circle cx="120" cy="118" r="110" fill="' + css('--brass2') + '" opacity=".55"/>';
  s += '<circle cx="120" cy="118" r="107" fill="url(#gFaceG)"/>';
  // barevné pásmo
  const band = (a, b, col) => (b > a ? '<path d="' + arcPath(a, b, 92) + '" fill="none" stroke="' + col + '" stroke-width="7" opacity=".85"/>' : '');
  s += band(0, dry, warn) + band(dry, wet, ok) + band(wet, 100, wat);
  // stupnice
  for (let p = 0; p <= 100; p += 2) {
    const major = p % 10 === 0, mid = p % 10 === 5;
    const r1 = 87, r2 = major ? 73 : (mid ? 78 : 82);
    const a = gPt(p, r1), b = gPt(p, r2);
    s += '<line x1="' + f(a[0]) + '" y1="' + f(a[1]) + '" x2="' + f(b[0]) + '" y2="' + f(b[1]) + '" stroke="' + ink +
      '" stroke-width="' + (major ? 2.2 : 1) + '" stroke-linecap="round"/>';
    if (major) {
      const q = gPt(p, 61);
      s += '<text x="' + f(q[0]) + '" y="' + f(q[1] + 4.5) + '" text-anchor="middle" font-family="Georgia,Times New Roman,serif" font-size="' +
        (p % 50 === 0 ? 14 : 12) + '" font-weight="' + (p % 50 === 0 ? 700 : 400) + '" fill="' + ink + '">' + p + '</text>';
    }
  }
  // nápisy na ciferníku
  const cap = (x, y, txt, size, op) => '<text x="' + x + '" y="' + y + '" text-anchor="middle" font-family="Georgia,Times New Roman,serif" font-size="' +
    size + '" letter-spacing="1.6" fill="' + ink + '" opacity="' + op + '">' + esc(txt) + '</text>';
  s += cap(120, 155, t('g_title').toUpperCase(), 7, 0.7).replace('letter-spacing="1.6"', 'letter-spacing="1.1"');
  const lab = (x, txt, col) => '<text x="' + x + '" y="167" text-anchor="middle" font-family="Georgia,serif" font-size="8" letter-spacing="1" fill="' +
    col + '">' + esc(txt) + '</text>';
  s += lab(70, t('g_dry').toUpperCase(), warn) + lab(170, t('g_wet').toUpperCase(), wat);
  // lístek pod osou (decentní značka)
  s += '<path d="M120 143 C115.5 138.5 115.5 132.5 120 128 C124.5 132.5 124.5 138.5 120 143 Z" fill="' + ok + '" opacity=".5"/>';
  $('#gFace').innerHTML = s;
  // ručka (kreslí se jednou, pak se jen otáčí)
  const nd = css('--needle');
  $('#needle').innerHTML =
    '<path d="M117.6 136 L119 32 L120 26 L121 32 L122.4 136 Z" fill="' + nd + '"/>' +
    '<circle cx="120" cy="138" r="5.5" fill="' + nd + '"/>' +
    '<circle cx="120" cy="118" r="9" fill="' + brass + '" stroke="' + css('--brass2') + '" stroke-width="1.5"/>' +
    '<circle cx="120" cy="118" r="3" fill="' + css('--brass2') + '"/>';
}
function drawGauge(pct, dry, wet, have) {
  drawFace(dry, wet);
  // bez hodnoty leží ručka na dorazu pod nulou (jako vypnutý přístroj)
  const p = have ? Math.max(-2, Math.min(102, pct)) : -5;
  const deg = -120 + 2.4 * p;
  const n = $('#needle');
  n.style.transform = 'rotate(' + deg.toFixed(1) + 'deg)';
  n.dataset.deg = deg.toFixed(1);
}

// Odpor půdy z indexu vodivosti g = R_pull / R_půdy (R_pull z Pokročilých, jinak 10 kΩ jako na desce).
function soilOhm(m, short) {
  if (!m || !m.have || m.pct === null) return '--';
  if ((m.flags & 2) || !(m.g > 0.0005)) return short ? '∞ kΩ' : t('ohm_open');
  const rp = 10;  // pull-up děliče na desce: vždy 10 kΩ
  return fmtOhm(rp / m.g);
}

// Místa: výběr na přehledu i v Nastavení (při jediném místě jen jméno).
function locName(i) {
  if (i < 0) return t('loc_deleted');  // záznamy smazaného místa (p0, od v6.7)
  const n = S && S.cfg.locs ? S.cfg.locs[i] : ''; return n || (t('loc_t') + ' ' + (i + 1));
}
let locSig = '';
// Stav úprav v kartě Místa (Nastavení): přejmenovává se místo locUi.edit (-1 = nic), je otevřený formulář nového místa.
const locUi = { edit: -1, adding: false };
const LOC_MAX = 8;
function renderLoc() {
  if (!S || !S.cfg.locs) return;
  const c = S.cfg, sig = c.loc + '|' + c.locs.join('\u0001') + '|' + JSON.stringify(c.locd || []) + '|' + lang + '|' +
    (logRecs ? logRecs.length : -1) + '|' + locUi.edit + '|' + locUi.adding;
  const one = c.locs.length <= 1;
  $('#dashLoc').hidden = one;
  $('#dashLocName').hidden = !one;
  $('#dashLocName').textContent = locName(c.loc);
  if (sig === locSig) return;
  locSig = sig;
  $('#dashLoc').innerHTML = c.locs.map((n, i) => '<option value="' + i + '">' + esc(locName(i)) + '</option>').join('');
  $('#dashLoc').value = String(c.loc);
  renderLocList();
  if (logRecs) renderCharts();
}
// Kalibrace místa d = [dry, wet, gDry, gWet]: krok je hotový, když se bod liší od výchozí hodnoty (config.h
// DEF_CAL_G_DRY / DEF_CAL_G_WET). Zkalibrované je místo až s OBĚMA kroky; jen jeden = napůl (procenta pořád nesedí).
function calSteps(d) {
  const dry = Math.abs(d[2] - 0.05) > 1e-4, wet = Math.abs(d[3] - 3.0) > 1e-4;
  return { dry: dry, wet: wet, all: dry && wet, any: dry || wet };
}
// Seznam míst: kde je sonda, kalibrace, práh, záznamy; klepnutím na jiné místo se sonda přesune.
function renderLocList() {
  const c = S.cfg, box = $('#locList'), n = c.locs.length;
  const focusedEdit = document.activeElement && document.activeElement.id === 'fLocRen';
  const keep = focusedEdit ? $('#fLocRen').value : null;
  box.innerHTML = c.locs.map((_, i) => {
    const on = i === c.loc, d = (c.locd || [])[i] || [c.dry, c.wet, c.gd, c.gw];
    const cs = calSteps(d);
    const meta = [cs.all ? esc(t('loc_cal_ok')) : '<span class="warn">' + esc(t(cs.any ? 'loc_cal_half' : 'loc_cal_def')) + '</span>',
      esc(t('loc_thr', { n: d[0] }))];
    if (logRecs) {
      const recs = logRecs.filter((r) => r.loc === i + 1 && !r.q);
      meta.push(esc(recs.length ? t('loc_recs', { n: recs.length, d: fmtWhen(recs[recs.length - 1].t) }) : t('loc_norecs')));
    }
    let h = '<div class="locrow' + (on ? ' on' : '') + '" role="radio" aria-checked="' + on + '" tabindex="0" data-i="' + i + '">' +
      '<span class="locdot" aria-hidden="true"></span><span class="locname">' + esc(locName(i)) + '</span>' +
      (on ? '<span class="locbadge">' + esc(t('loc_here')) + '</span>' : '<span></span>') +
      '<div class="locmeta">' + meta.join(' · ') + '</div>';
    if (locUi.edit === i) {
      h += '<form class="locedit" data-act="rensave" autocomplete="off">' +
        '<input type="text" id="fLocRen" maxlength="24" autocomplete="off" aria-label="' + esc(t('loc_name')) + '" value="' + esc(c.locs[i] || '') + '">' +
        '<div class="grid2"><button class="btn primary" type="submit">' + esc(t('save')) + '</button>' +
        '<button class="btn ghost" type="button" data-act="rencancel">' + esc(t('cancel')) + '</button></div></form>';
    } else {
      h += '<div class="locacts"><button class="btn ghost" type="button" data-act="ren">' + esc(t('loc_ren')) + '</button>' +
        (n > 1 ? '<button class="btn danger" type="button" data-act="del">' + esc(t('loc_del')) + '</button>' : '') + '</div>';
    }
    return h + '</div>';
  }).join('');
  if (locUi.edit >= 0 && $('#fLocRen')) {
    if (keep !== null) $('#fLocRen').value = keep;
    if (focusedEdit || keep === null) $('#fLocRen').focus();
  }
  const full = n >= LOC_MAX;
  $('#btnLocAdd').hidden = locUi.adding || full;
  $('#locNew').hidden = !locUi.adding || full;
  $('#locMax').hidden = !full;
  $('#locNewHint').textContent = t('loc_add_hint', { n: locName(c.loc) });
}

// Uzel ≈ 0 V (příznak 4) při OVĚŘENÉM zapojení (pin BIAS uložený) = sonda měří skoro nulový odpor: přelitá půda
// (nebo se dotýkají hroty). Bez ověřeného zapojení to může být i špatný pin – pak se nabídne kontrola zapojení.
function isFlood(f) { return !!(S && S.cfg.bias && (f & 4) && !(f & 16)); }

function renderHero() {
  const m = S.m, have = m.have && m.pct !== null;
  const fault = m.have && m.pct === null;  // uzel ≈ 0 V / nereaguje: zařízení žádné procento neposílá
  const flood = fault && isFlood(m.flags);  // ověřené zapojení a skoro nulový odpor: půda je přelitá vodou
  const pct = have ? Math.round(m.pct) : 0;
  $('#hVal').textContent = have ? pct : '--';
  drawGauge(have ? m.pct : (flood ? 102 : 0), S.cfg.dry, S.cfg.wet, have || flood);
  const st = $('#hState');
  if (flood) { st.textContent = t('st_flood'); st.dataset.k = 'wet'; }
  else if (!have) { st.textContent = t(fault ? 'st_fault' : 'no_data'); st.dataset.k = fault ? 'bad' : ''; }
  else if (pct < S.cfg.dry) { st.textContent = t('st_dry'); st.dataset.k = 'dry'; }
  else if (pct > S.cfg.wet) { st.textContent = t('st_wet'); st.dataset.k = 'wet'; }
  else { st.textContent = t('st_ok'); st.dataset.k = 'ok'; }
  let sub = '';
  if (m.have) {
    const age = m.age + Math.round((Date.now() - sAt) / 1000);
    sub = (age < 90 ? t('ago', { n: age }) : t('ago_m', { n: Math.round(age / 60) })) + ' · ' + t('auto_upd', { n: S.cfg.live || 15 });  // měří se samo (Servis → Sonda a měření)
    if (tech) sub += ' · ADC ' + Math.round(m.raw) + ' · g ' + m.g.toFixed(2);
  }
  $('#hSub').textContent = sub;
  // odpor půdy: jen doplňkově, malým šedým písmem v okénku měřáku
  const ohm = soilOhm(m, true);
  $('#hOhm').textContent = ohm === '--' ? '' : ohm;
  $('#hOhm').title = t('ohm_t');
}

function renderSession() {
  const left = Math.max(0, S.sess.left - Math.round((Date.now() - sAt) / 1000));
  const total = Math.max(1, S.sess.total);
  $('#sessTxt').textContent = t('sess_left', { t: Math.floor(left / 60) + ':' + pad2(left % 60) });
  $('#sessFill').style.width = Math.min(100, left / total * 100) + '%';
  $('#sessbar').classList.toggle('low', left < 30);
}

// ============================================================================
//  Záznamy: parsování, grafy
// ============================================================================
const LOGRX = /^(\d{4})-(\d\d)-(\d\d) (\d\d):(\d\d)\s+(--|\d+)%\s+(--|-?\d+(?:\.\d+)?)C\s+(--|\d+(?:\.\d+)?)V(.*)$/;
// krátký formát (od v6.13): RRMMDDhhmm vlhkost teplota napětí(setiny V)
const LOGRX2 = /^(\d\d)(\d\d)(\d\d)(\d\d)(\d\d) (-|\d+) (-|-?\d+(?:\.\d+)?) (-|\d+)(.*)$/;
function parseLog(txt) {
  const recs = [];
  for (const line of txt.split('\n')) {
    const ln = line.trim();
    let m = LOGRX.exec(ln);
    if (!m) {  // krátký formát: převést na skupiny jako u starého (rok, …, vlhkost, teplota, napětí ve V)
      const k = LOGRX2.exec(ln);
      if (!k) continue;
      m = [ln, String(2000 + +k[1]), k[2], k[3], k[4], k[5], k[6] === '-' ? '--' : k[6], k[7] === '-' ? '--' : k[7],
        k[8] === '-' ? '--' : String(+k[8] / 100), k[9]];
    }
    const y = +m[1];
    if (y < 2024 || y > 2099) continue;
    let rest = m[9] || '', note = '';
    const hi = rest.indexOf(' # ');  // poznámka ručního zápisu (od v6.10) – v ní se nic nehledá
    if (hi >= 0) { note = rest.slice(hi + 3).trim(); rest = rest.slice(0, hi); }
    const fl = /(?:^|\s)F(?:\s|$)/.test(rest);  // půda přelitá vodou (od v7.0): v logu je "-", zobrazí se 100 %
    const r = {
      t: Date.UTC(y, +m[2] - 1, +m[3], +m[4], +m[5]),
      hum: m[6] === '--' ? (fl ? 100 : null) : +m[6],
      tmp: m[7] === '--' ? null : +m[7],
      v: m[8] === '--' ? null : +m[8],
      q: /(?:^|\s)\?(?:\s|$)/.test(rest),
      raw: null, st: null, u: false, lb: /(?:^|\s)!(?:\s|$)/.test(rest),
      hib: /(?:^|\s)H(?:\s|$)/.test(rest),  // odtud měření stojí: vybitý článek (hibernace)
      fl: fl,
      man: /(?:^|\s)M(?:\s|$)/.test(rest), note: note  // ruční zápis a jeho poznámka
    };
    const mr = /(?:^|\s)r(\d+)/.exec(rest);
    if (mr) r.raw = +mr[1];
    const ms = /(?:^|\s)s(\d+)(u?)/.exec(rest);
    if (ms) { r.st = +ms[1]; r.u = ms[2] === 'u'; }
    const mp = /(?:^|\s)p(\d+)/.exec(rest);
    r.loc = mp ? +mp[1] : 1;  // záznamy bez čísla místa patří k prvnímu místu
    recs.push(r);
  }
  recs.sort((a, b) => a.t - b.t);
  return recs;
}

// Log se drží i v prohlížeči (localStorage) a ze zařízení se stahují jen nové řádky: velký log
// (stovky kB) by se jinak přes Wi-Fi zařízení tahal při každém otevření několik vteřin.
// Platnost hlídá verze logu (S.fs.gen, hlavička X-Log-Gen) – mění se při každém přepsání logu.
// Bez uloženého logu se nejdřív stáhne jen jeho konec (poslední dny) a zbytek dorazí na pozadí.
const LOG_TAIL_BYTES = 65536;
function logCacheKey() { return 'arb_log_' + (S && S.id ? S.id : ''); }
function logCacheGet() {
  try {
    const c = JSON.parse(lsGet(logCacheKey()) || 'null');
    return c && typeof c.text === 'string' && typeof c.size === 'number' ? c : null;
  } catch (e) { return null; }
}
function logCachePut(gen, size, text) {
  try { localStorage.setItem(logCacheKey(), JSON.stringify({ gen: gen, size: size, text: text })); }
  catch (e) { try { localStorage.removeItem(logCacheKey()); } catch (e2) { /* plné úložiště – nevadí */ } }
}
async function fetchLog(query) {
  const r = await api('/api/log' + query, { timeout: 20000 });
  if (!r.ok) {
    let code = '';
    try { code = (await r.json()).err || ''; } catch (e) { /* tělo není JSON */ }
    const err = new Error('HTTP ' + r.status);
    err.code = code;
    throw err;
  }
  const h = (n) => +(r.headers.get(n) || 0);
  return { text: await r.text(), size: h('X-Log-Size'), from: h('X-Log-From'), gen: h('X-Log-Gen') };
}
function useLog(text) {
  logText = text;
  const all = parseLog(text);
  logRecs = all.filter((x) => !x.man);
  manRecs = all.filter((x) => x.man);
  logAt = Date.now();
  reportFill();
}
function renderLogViews() {
  renderCharts();
  renderMan();
  if (route === 'logs') renderLogText();
}
let logLoading = false;
async function loadLog(force, retried) {
  if (!force && logRecs && Date.now() - logAt < 30000) return;
  if (!S) { setTimeout(() => loadLog(force, retried), 400); return; }  // id zařízení a verze logu jsou ve stavu
  if (logLoading && !retried) return;
  logLoading = true;
  try {
    const c = logCacheGet();
    let done = false;
    if (c && c.gen === S.fs.gen) {
      if (!logRecs) { useLog(c.text); renderLogViews(); }      // hned z prohlížeče
      const d = await fetchLog('?from=' + c.size);              // jen nové řádky
      if (d.gen === c.gen) {
        const text = d.text ? c.text + d.text : c.text;
        if (d.text) logCachePut(d.gen, d.size, text);
        useLog(text);
        done = true;
      }
    } else if (!logRecs) {
      const t = await fetchLog('?tail=' + LOG_TAIL_BYTES);      // rychlý první náhled
      useLog(t.text);
      renderLogViews();
      done = t.from === 0;                                       // malý log přišel celý
      if (done) logCachePut(t.gen, t.size, t.text);
    }
    if (!done) {
      const d = await fetchLog('');
      logCachePut(d.gen, d.size, d.text);
      useLog(d.text);
    }
  } catch (e) {
    logLoading = false;
    // "stale": log byl mezitím přepsán (uložený je delší) -> celý znovu
    if (e && e.code === 'stale' && !retried) {
      try { localStorage.removeItem(logCacheKey()); } catch (e2) { /* nic */ }
      return loadLog(true, true);
    }
    // "busy": zařízení právě ředí log (trvá pár vteřin) -> jednou potichu zkusit znovu
    if (e && e.code === 'busy' && !retried) {
      setTimeout(() => loadLog(true, true), 2500);
      return;
    }
    toast(terr(e && e.code), true);
    logText = logText || '';
    logRecs = logRecs || [];
  }
  logLoading = false;
  renderLogViews();
}

// osa o 'div' dílcích s "pěknou" velikostí kroku, která pokrývá <lo, hi>
function alignedAxis(lo, hi, div) {
  const steps = [0.5, 1, 2, 2.5, 5, 10, 20, 25, 50, 100];
  for (const s of steps) {
    const min = Math.floor(lo / s) * s;
    if (min + div * s >= hi) return { min: min, max: min + div * s, step: s };
  }
  const s = Math.ceil((hi - lo) / div);
  return { min: Math.floor(lo), max: Math.floor(lo) + div * s, step: s };
}

function anchorTime(recs) {
  const last = recs.length ? recs[recs.length - 1].t : 0;
  const d = devNow();
  if (d !== null && S.clock.set) return Math.max(last, d * 1000);
  if (last) return last;
  const n = new Date();
  return n.getTime() - n.getTimezoneOffset() * 60000;
}

function windowFor(recs) {
  const a = anchorTime(recs);
  const H = 3600e3, D = 24 * H;
  let x0;
  if (range === '24h') x0 = a - D;
  else if (range === '7d') x0 = a - 7 * D;
  else if (range === '14d') x0 = a - 14 * D;
  else if (range === '30d') x0 = a - 30 * D;
  else if (range === 'custom') {
    customDefaults(recs);
    const w = dayRange(rFrom, rTo);
    if (w) return w;
    x0 = a - 7 * D;
  } else x0 = recs.length ? recs[0].t : a - D;
  let x1 = a;
  if (x1 - x0 < H) x1 = x0 + H;
  return [x0, x1];
}

function renderChartLines() { /* hranice se mění jen při změně nastavení */
  const sig = S.cfg.dry + '|' + S.cfg.wet + '|' + S.cfg.wjmp + '|' + S.cfg.wwin;
  if (renderChartLines.sig !== sig) { renderChartLines.sig = sig; renderCharts(); }
}

// Zalití = nárůst vlhkosti aspoň o 'jump' % proti minimu z posledních 'winH' hodin. Událost trvá od toho minima
// po vrchol (dokud vlhkost roste); další se hledá až po vrcholu. Záznamy s pochybným časem (?) a bez % se vynechají.
function detectWater(recs, jump, winH) {
  const pts = recs.filter((r) => r.hum !== null && !r.q);
  const W = winH * 3600e3, ev = [];
  let lastEnd = -Infinity;
  for (let k = 1; k < pts.length; k++) {
    let jmin = -1;
    for (let j = k - 1; j >= 0 && pts[k].t - pts[j].t <= W; j--) {
      if (pts[j].t < lastEnd) break;
      if (jmin < 0 || pts[j].hum < pts[jmin].hum) jmin = j;
    }
    if (jmin < 0 || pts[k].hum - pts[jmin].hum < jump) continue;
    let p = k;
    while (p + 1 < pts.length && pts[p + 1].hum >= pts[p].hum && pts[p + 1].t - pts[p].t <= W) p++;
    ev.push({ t0: pts[jmin].t, t1: pts[p].t, from: pts[jmin].hum, to: pts[p].hum });
    lastEnd = pts[p].t;
    k = p;
  }
  return ev;
}

function fmtWhen(ms) {  // čas záznamu (lokální čas zařízení uložený jako UTC); rok jen když není letošní
  const d = new Date(ms), y = new Date(anchorTime(logRecs || [])).getUTCFullYear();
  return d.getUTCDate() + '. ' + (d.getUTCMonth() + 1) + '.' + (d.getUTCFullYear() !== y ? ' ' + d.getUTCFullYear() : '') + ' ' +
    pad2(d.getUTCHours()) + ':' + pad2(d.getUTCMinutes());
}
function fmtDur(ms) {
  const h = ms / 3600e3;
  return h < 48 ? t('w_hours', { n: Math.round(h) }) : t('w_days', { n: (h / 24).toFixed(h < 240 ? 1 : 0) });
}

function renderWater(recs, x0, x1) {
  const c = S ? S.cfg : { dry: 30, wjmp: 8, wwin: 6 };
  const evIn = waterEv.filter((e) => e.t1 >= x0 && e.t0 <= x1);
  const now = anchorTime(logRecs || []);
  const last = waterEv.length ? waterEv[waterEv.length - 1] : null;
  const set = (id, v) => { $(id).textContent = v; };
  $('#wRange').textContent = recs.length ? t('w_period', { a: fmtWhen(x0), b: fmtWhen(x1) }) : '';
  if (last) {
    const ago = now - last.t1;
    set('#wLast', ago < 48 * 3600e3 ? t('w_ago_h', { n: Math.max(0, Math.round(ago / 3600e3)) }) : t('w_ago_d', { n: Math.round(ago / 86400e3) }));
    set('#wLastSub', fmtWhen(last.t1) + ' · ' + Math.round(last.from) + ' → ' + Math.round(last.to) + ' %');
  } else { set('#wLast', t('w_never')); set('#wLastSub', ''); }
  set('#wCount', recs.length ? String(evIn.length) : '--');
  set('#wCountSub', '');
  // nejdelší úsek bez zalití v zobrazeném období (od začátku dat / konce zalití po začátek dalšího / konec období)
  const hp = recs.filter((r) => r.hum !== null);
  if (hp.length) {
    // úseky dat delší výpadek (> 24 h bez záznamu) přeruší: chybějící data nejsou "bez zalití"
    let prev = hp[0].t, gap = 0, gs = prev, ei = 0;
    const cand = (a, b) => { if (b - a > gap) { gap = b - a; gs = a; } };
    for (let i = 0; i < hp.length; i++) {
      if (i && hp[i].t - hp[i - 1].t > 24 * 3600e3) { cand(prev, hp[i - 1].t); prev = hp[i].t; }
      while (ei < evIn.length && evIn[ei].t0 <= hp[i].t) {
        if (evIn[ei].t0 >= prev) cand(prev, evIn[ei].t0);
        prev = Math.max(prev, evIn[ei].t1);
        ei++;
      }
    }
    const lastT = hp[hp.length - 1].t;
    cand(prev, now - lastT <= 24 * 3600e3 ? Math.max(lastT, Math.min(x1, now)) : lastT);
    set('#wGap', fmtDur(Math.max(0, gap)));
    set('#wGapSub', t('w_period', { a: fmtWhen(gs), b: fmtWhen(gs + gap) }));
    const v = hp.map((r) => r.hum);
    const avg = v.reduce((a, b) => a + b, 0) / v.length;
    set('#wMinMax', Math.round(Math.min.apply(null, v)) + ' / ' + Math.round(avg) + ' / ' + Math.round(Math.max.apply(null, v)));
    // doba pod prahem "zalévat" (součet intervalů mezi záznamy, jejichž začátek je pod prahem; mezery nad 3 h se nepočítají)
    let dryMs = 0;
    for (let i = 0; i + 1 < hp.length; i++) {
      const dt = hp[i + 1].t - hp[i].t;
      if (hp[i].hum < c.dry && dt <= 3 * 3600e3) dryMs += dt;
    }
    set('#wDrySub', t('w_dry_sub', { p: c.dry, h: Math.round(dryMs / 3600e3) }));
  } else { ['#wGap', '#wMinMax'].forEach((id) => set(id, '--')); ['#wGapSub', '#wDrySub'].forEach((id) => set(id, '')); }
  const MAXE = 6, list = evIn.slice().reverse();
  let h = list.slice(0, MAXE).map((e) =>
    '<div class="ev"><span class="drop" aria-hidden="true">💧</span><span class="when">' + esc(fmtWhen(e.t0)) +
    '<small>' + esc(fmtDur(e.t1 - e.t0)) + ' · ' + Math.round(e.from) + ' → ' + Math.round(e.to) + ' %</small></span>' +
    '<span class="delta">+' + Math.round(e.to - e.from) + ' %</span></div>').join('');
  if (list.length > MAXE) h += '<div class="muted">' + esc(t('w_more', { n: list.length - MAXE })) + '</div>';
  $('#wList').innerHTML = h;
}

// Úseky, kdy měření stálo kvůli vybitému článku: od záznamu "H" do dalšího záznamu (nebo do teď).
function hibSpans() {
  const recs = (logRecs || []).filter((r) => !r.q), out = [];
  for (let i = 0; i < recs.length; i++) {
    if (!recs[i].hib) continue;
    let j = i + 1;
    while (j < recs.length && recs[j].hib) j++;
    const d = devNow();
    out.push({ t0: recs[i].t, t1: j < recs.length ? recs[j].t : (d !== null ? d * 1000 : recs[i].t), open: j >= recs.length });
    i = j - 1;
  }
  return out;
}

function renderCharts() {
  $$('#rangeSeg button').forEach((b) => b.classList.toggle('on', b.dataset.range === range));
  syncRangeUi();
  const msgH = $('#chHumMsg'), msgB = $('#chBatMsg');
  const curLoc = S && S.cfg.locs ? S.cfg.loc + 1 : 1;
  const recsAll = (logRecs || []).filter((r) => !r.q && r.loc === curLoc);  // jen vybrané místo
  if (!plots.hum) plots.hum = new Plot($('#chHum'));
  if (!plots.bat) plots.bat = new Plot($('#chBat'));
  if (logRecs === null) { msgH.textContent = t('loading'); msgB.textContent = ''; }
  const empty = !recsAll.length;
  const [x0, x1] = windowFor(recsAll);
  const recs = recsAll.filter((r) => r.t >= x0 && r.t <= x1);
  const dry = S ? S.cfg.dry : 30, wet = S ? S.cfg.wet : 85;
  const water = css('--water'), warn = css('--warn'), ok = css('--ok'), soil = css('--soil');
  waterEv = detectWater(recsAll, S ? S.cfg.wjmp : 8, S ? S.cfg.wwin : 6);
  const marks = waterEv.filter((e) => e.t1 >= x0 && e.t0 <= x1).map((e) => ({ x0: e.t0, x1: e.t1, color: water }));
  const bad = css('--bad');
  const hibM = hibSpans().filter((h) => h.t1 >= x0 && h.t0 <= x1).map((h) => ({ x0: h.t0, x1: h.t1, color: bad, drop: false }));

  // vlhkost + teplota
  const hum = [], tmp = [], bat = [];
  for (const r of recs) {
    if (ser === 'raw') { if (r.raw !== null) hum.push([r.t, r.raw]); } else if (r.hum !== null) hum.push([r.t, r.hum]);
    if (r.tmp !== null) tmp.push([r.t, r.tmp]);
  }
  for (const r of (logRecs || [])) if (!r.q && r.v !== null && r.t >= x0 && r.t <= x1) bat.push([r.t, r.v]);  // baterie: všechna místa
  let tAx = { min: 0, max: 20, step: 5 };
  if (tmp.length) {
    tAx = alignedAxis(Math.min.apply(null, tmp.map((p) => p[1])) - 0.5, Math.max.apply(null, tmp.map((p) => p[1])) + 0.5, 4);
  }
  const series = ser === 'raw'
    ? [{ name: t('raw_l'), color: water, pts: hum, axis: 0, w: 2, fmt: (v) => String(Math.round(v)) }]
    : [{ name: t('hum'), color: water, pts: hum, axis: 0, fill: true, w: 2.2, fmt: (v) => Math.round(v) + ' %' }];
  const axes = ser === 'raw'
    ? [{ min: 0, max: 4096, ticks: [0, 1024, 2048, 3072, 4096], fmt: (v) => String(v) }, null]
    : [{ min: 0, max: 100, ticks: [0, 25, 50, 75, 100], fmt: (v) => v + '%' }, null];
  if (tmp.length) {
    series.push({ name: t('temp'), color: soil, pts: tmp, axis: 1, w: 1.6, fmt: (v) => v.toFixed(1) + ' °C' });
    const tk = [];
    for (let i = 0; i <= 4; i++) tk.push(tAx.min + i * tAx.step);
    axes[1] = { min: tAx.min, max: tAx.max, ticks: tk, color: soil, fmt: (v) => (tAx.step % 1 ? v.toFixed(1) : Math.round(v)) + '°' };
  }
  plots.hum.set({
    xmin: x0, xmax: x1, axes: axes, series: series, marks: marks.concat(hibM),
    lines: ser === 'raw' ? [] : [{ axis: 0, y: dry, color: warn }, { axis: 0, y: wet, color: water }]
  });
  $('#lgHum').innerHTML =
    '<span><i style="background:' + water + '"></i>' + esc(t(ser === 'raw' ? 'raw_l' : 'hum')) + '</span>' +
    (marks.length ? '<span><i style="background:' + water + ';opacity:.35"></i>' + esc(t('water_ev')) + '</span>' : '') +
    (hibM.length ? '<span><i style="background:' + bad + ';opacity:.35"></i>' + esc(t('hib_l')) + '</span>' : '') +
    (tmp.length ? '<span><i style="background:' + soil + '"></i>' + esc(t('temp')) + '</span>' : '') +
    '<span><i style="background:' + warn + '"></i>' + esc(t('thr_dry_l')) + ' ' + dry + ' %</span>';
  msgH.textContent = empty ? t('no_log') : (!recs.length ? t('no_range') : '');
  renderWater(recs, x0, x1);

  // baterie
  let vmin = 2.9, vmax = 3.5;
  if (bat.length) {
    vmin = Math.min.apply(null, bat.map((p) => p[1])); vmax = Math.max.apply(null, bat.map((p) => p[1]));
    vmin = Math.floor((vmin - 0.05) * 20) / 20; vmax = Math.ceil((vmax + 0.05) * 20) / 20;
    if (vmax - vmin < 0.2) vmax = vmin + 0.2;
  }
  plots.bat.set({
    xmin: x0, xmax: x1,
    axes: [{ min: vmin, max: vmax, fmt: (v) => v.toFixed(2) }, null],
    series: [{ name: t('vbat'), color: ok, pts: bat, axis: 0, w: 2, fmt: (v) => v.toFixed(2) + ' V' }], marks: hibM,
    lines: [{ axis: 0, y: 3.0, color: warn }]
  });
  msgB.textContent = empty ? '' : (!bat.length ? t('no_range') : '');
}

// ============================================================================
//  Nastavení
// ============================================================================
function fillSettings(force) {
  if (!S) return;
  const c = S.cfg;
  if (force || !formDirty.web) { $('#fWeb').value = c.web; $('#webVal').textContent = c.web; }
  if (force || !formDirty.live) fillLive(c.live || 15);
  if (force || !formDirty.man) { $('#fMan').checked = !!c.man; $('#fManMode').value = String(c.mnote === undefined ? 2 : c.mnote); $('#fManExt').checked = c.mext !== 0; }
  if (force || !formDirty.ops) { fillIntervals(c.int); updIntHint(); }
  if (force || !formDirty.thr) { $('#fDry').value = c.dry; $('#fWet').value = c.wet; }
  if (force || !formDirty.net) { $('#fCport').value = String(c.cport); $('#fTxp').value = String(c.txp || 0); $('#fTech').checked = tech; }
  if (force || !formDirty.pw) { $('#fPwOn').checked = c.pw === 1; $('#pwRow').hidden = c.pw !== 1; }
  $('#pwState').textContent = t(c.pw === 1 ? 'pw_st1' : (c.pw === 2 ? 'pw_st2' : 'pw_st0'));
  if (force || !formDirty.adv) {
    $('#fScale').value = c.scale;
    $('#fVexc').checked = !!c.vexc;
  }
  if (force || !formDirty.wire) $('#fBias').value = String(c.bias);
  if (force || !formDirty.ret) $('#fRet').value = String(c.ret);
  if (force || !formDirty.pm) $('#fPmode').value = String(c.pmode);
  if (force || !formDirty.wdet) { $('#fWjmp').value = c.wjmp; $('#fWwin').value = c.wwin; }
}
// Obnovování živé hodnoty (Servis): běžné volby + aktuální hodnota, když je jiná.
const LIVE_CHOICES = [5, 10, 15, 30, 60, 120];
function fillLive(cur) {
  const sel = $('#fLive'), list = LIVE_CHOICES.indexOf(cur) >= 0 ? LIVE_CHOICES : LIVE_CHOICES.concat([cur]).sort((a, b) => a - b);
  const label = (n) => (n < 60 ? t('live_s', { n: n }) : t('live_m', { n: n / 60 })) + (n === 15 ? ' ' + t('int_rec') : '');
  const sig = list.join(',') + lang;
  if (sel.dataset.sig !== sig) {
    sel.innerHTML = list.map((n) => '<option value="' + n + '">' + esc(label(n)) + '</option>').join('');
    sel.dataset.sig = sig;
  }
  sel.value = String(cur);
}
// Interval: běžné volby + aktuální hodnota, když je jiná (nastavená dřív číslem nebo přes API).
const INT_CHOICES = [0, 10, 15, 30, 60, 120];
function fillIntervals(cur) {
  const sel = $('#fInt'), list = INT_CHOICES.indexOf(cur) >= 0 ? INT_CHOICES : INT_CHOICES.concat([cur]).sort((a, b) => a - b);
  const label = (n) => n === 0 ? t('int_off') : (n === 60 ? t('int_h1') : (n === 120 ? t('int_h2') : t('int_opt', { n: n }))) +
    (n === 30 ? ' ' + t('int_rec') : '');
  const sig = list.join(',') + lang;
  if (sel.dataset.sig !== sig) {
    sel.innerHTML = list.map((n) => '<option value="' + n + '">' + esc(label(n)) + '</option>').join('');
    sel.dataset.sig = sig;
  }
  sel.value = String(cur);
}
function updIntHint() {
  const n = parseInt($('#fInt').value, 10) || 0, d = devNow();
  let h = t('interval_off');
  if (n > 0) {
    const p = n * 60 * (S && S.pwr && S.pwr.save ? 2 : 1);
    h = d === null ? t('interval_on', { n: n }) : t('int_next', { t: fmtClock(Math.ceil((d + 1) / p) * p).slice(0, 5) });
  }
  $('#intHint').textContent = h;
}

function renderWire() {
  const w = S.wire, c = S.cfg;
  $('#wirePin').textContent = 'GPIO' + c.biasUsed + (c.bias ? '' : ' (' + t('wire_unconf') + ')');
  const box = $('#wireRes');
  if (!w.have) { box.hidden = true; return; }
  let h = '<div class="wv">' + esc(w.res === 1 ? t('wire_found', { p: w.pin }) : (w.res === 2 ? t('wire_amb') : t('wire_none'))) + '</div>';
  for (let i = 0; i < w.n; i++) {
    const reacts = w.hi[i] - w.lo[i] >= 80;
    h += '<div class="wr">GPIO' + w.p[i] + ': ' + Math.round(w.lo[i]) + ' → ' + Math.round(w.hi[i]) + ' · ' + esc(t(reacts ? 'wire_resp' : 'wire_noresp')) + '</div>';
  }
  box.className = 'verdict ' + (w.res === 1 ? 'good' : 'bad');
  box.innerHTML = h;
  box.hidden = false;
}

function renderCal() {
  const m = S.m, have = m.have && m.pct !== null;
  let det = m.have ? esc(t('live_det', { raw: Math.round(m.raw), g: m.g.toFixed(3), ms: m.ms })) : '';
  if (have && m.g > 0.0005) det += '<br>' + esc(t('live_rp', { r: fmtOhm(10 / m.g) }));
  if (m.have && S.cfg.ret) det += '<br>' + esc(t('live_ph', { p: t(['ph_a', 'ph_b', 'ph_r'][m.ph] || 'ph_a'), b: m.rawB === null ? '--' : Math.round(m.rawB) }));
  $('#lDet').innerHTML = det;
  $('#cPct').textContent = have ? Math.round(m.pct) : '--';
  renderCalSteps(have ? m.pct : null, m.have && !have);
  const f = m.have ? m.flags : 0, w = [];
  if (f & 2) w.push(t('f_open'));
  if (f & 4) w.push(t(isFlood(f) ? 'f_short' : 'f_short_unc'));
  if (f & 16) w.push(t('f_noresp'));
  if (f & 8) w.push(t('f_noisy'));
  if (f & 1) w.push(t('f_unsettled'));
  if (f & 32) w.push(t('f_retbad'));
  const box = $('#lWarn');
  box.hidden = !w.length;
  box.textContent = w.join(' ');
  const d = devNow();
  $('#cDev').textContent = d === null ? '--' : fmtDate(d) + ' ' + fmtClock(d);
  const n = new Date();
  $('#cPhone').textContent = n.getDate() + '.' + (n.getMonth() + 1) + '.' + n.getFullYear() + ' ' + pad2(n.getHours()) + ':' + pad2(n.getMinutes()) + ':' + pad2(n.getSeconds());
  // hodiny jednou větou; tlačítko je výrazné, jen když hodiny nejdou správně
  let cs = 'ok', ct = t('clock_ok');
  if (!S.clock.set || !S.clock.ok || d === null) { cs = 'warn'; ct = t('clock_unset'); }
  else {
    const diff = Math.round(Math.abs(d - (n.getTime() / 1000 - n.getTimezoneOffset() * 60)) / 60);
    if (diff >= 2) { cs = 'warn'; ct = t('clock_diff', { n: diff }); }
  }
  const st = $('#clockState');
  st.className = 'clockstate ' + cs;
  st.textContent = (cs === 'ok' ? '✓ ' : '! ') + ct;
  $('#btnTime').className = 'btn' + (cs === 'ok' ? ' ghost' : ' primary');
}

// Kalibrace vybraného místa (Nastavení): který krok je hotový (bod se liší od výchozí hodnoty), stav jednou větou,
// a pruh „Kdy zalévat“ s ukazatelem aktuální vlhkosti.
function renderCalSteps(pct, fault) {
  const c = S.cfg, d = (c.locd || [])[c.loc] || [c.dry, c.wet, c.gd, c.gw];
  const cs = calSteps(d), dryOk = cs.dry, wetOk = cs.wet;
  $('#calTitle').textContent = t('cal_title', { n: locName(c.loc) });
  const steps = [['#calStep1', '#calChip1', dryOk], ['#calStep2', '#calChip2', wetOk]];
  steps.forEach(([li, chip, ok]) => { $(li).classList.toggle('done', ok); $(chip).textContent = t(ok ? 'cal_done' : 'cal_todo'); });
  const st = $('#cDet');
  if (fault) { st.className = 'calstate warn'; st.textContent = t('st_fault'); }
  else if (dryOk && wetOk) { st.className = 'calstate ok'; st.textContent = '✓ ' + t('cal_state_ok'); }
  else { st.className = 'calstate warn'; st.textContent = t(dryOk || wetOk ? 'cal_state_half' : 'cal_state_def'); }
  // pruh: šířky podle hranic (i rozpracovaných ve formuláři)
  const dry = Math.max(0, Math.min(99, parseInt($('#fDry').value, 10) || 0));
  const wet = Math.max(dry + 1, Math.min(100, parseInt($('#fWet').value, 10) || 100));
  $('#thrZd').style.flex = '0 0 ' + dry + '%';
  $('#thrZo').style.flex = '0 0 ' + (wet - dry) + '%';
  $('#thrZw').style.flex = '1 1 ' + (100 - wet) + '%';
  const now = $('#thrNow');
  now.hidden = pct === null;
  if (pct !== null) now.style.left = Math.max(9, Math.min(91, pct)) + '%';  // u kraje se text nesmí useknout
  $('#thrNowTxt').textContent = pct === null ? '' : t('thr_now', { n: Math.round(pct) });
}

// Odhad spotřeby z čítačů zařízení (Servis). Proudy jsou PŘEDPOKLAD (viz pwr_help), ne měření.
const PWR_MA = { awake: 30, web: 100, sleep: 0.15 };
function renderPower() {
  const p = S.pwr, box = $('#pwrRows');
  if (!p) { box.innerHTML = ''; return; }
  const row = (label, v) => '<div class="kv"><span>' + esc(label) + '</span><b>' + esc(v) + '</b></div>';
  const awakeS = p.awake / 1000;
  let h = '';
  h += row(t('pwr_since'), p.since ? fmtDate(p.since) + ' ' + fmtClock(p.since).slice(0, 5) : '--');
  h += row(t('pwr_wakes'), p.boots + ' / ' + p.logs);
  h += row(t('pwr_awake'), fmtSecs(awakeS));
  h += row(t('pwr_web'), fmtSecs(p.web));
  h += row(t('pwr_drift'), p.k === null ? t('pwr_drift_none') : ((p.k - 1) * 100 >= 0 ? '+' : '') + ((p.k - 1) * 100).toFixed(1) + ' %');
  const pe = pwrEstimate(p);
  if (!pe) h += '<div class="hint">' + esc(t('pwr_few')) + '</div>';
  else {
    h += row(t('pwr_day'), pe.perDay.toFixed(2) + ' mAh · ' + t('pwr_share', { n: Math.round(pe.webShare) }));
    h += row(t('pwr_life', { c: p.cap }), t('pwr_days', { n: pe.days === null ? '--' : pe.days }));
  }
  if (p.save) h += '<div class="warnbox">' + esc(t('pwr_save', { n: p.int })) + '</div>';
  box.innerHTML = h;
}
function fmtSecs(s) {
  s = Math.round(s);
  if (s < 120) return s + ' s';
  if (s < 7200) return Math.round(s / 60) + ' min';
  return (s / 3600).toFixed(1) + ' h';
}

// Otevře sekci Servisu (a Servis, je-li třeba) a posune se na ni.
function openSec(id) {
  if (location.hash !== '#/admin') location.hash = '#/admin';
  const d = $('#' + id);
  if (!d) return;
  d.open = true;
  setTimeout(() => d.scrollIntoView({ block: 'start', behavior: 'smooth' }), 80);
}
// Odhad spotřeby (mAh/den, dní výdrže) z čítačů; null = málo dat.
function pwrEstimate(p) {
  if (!p) return null;
  const awakeS = p.awake / 1000, total = awakeS + p.web + p.sleep;
  if (total < 3600) return null;
  const mAh = (awakeS * PWR_MA.awake + p.web * PWR_MA.web + p.sleep * PWR_MA.sleep) / 3600;
  const perDay = mAh * 86400 / total;
  return { perDay: perDay, days: perDay > 0 ? Math.round(p.cap / perDay) : null, webShare: mAh > 0 ? (p.web * PWR_MA.web / 3600) / mAh * 100 : 0 };
}
// Servis: karta Stav sondy a jednořádkové shrnutí v záhlaví každé sekce.
function renderSvc() {
  if (!S) return;
  const c = S.cfg, rows = [];
  const add = (st, lbl, val, go, act) => rows.push({ st: st, lbl: lbl, val: val, go: go, act: act });
  const bv = S.bat.v === null ? '--' : S.bat.v.toFixed(2) + ' V' + (S.bat.pct !== null && S.bat.pct !== undefined ? ' (' + S.bat.pct + ' %)' : '');
  const pe = pwrEstimate(S.pwr), saving = !!(S.pwr && S.pwr.save);
  add(S.bat.lvl === 2 ? 'bad' : (S.bat.lvl === 1 || saving ? 'warn' : 'ok'), t('svc_bat'),
    bv + (saving ? ' · ' + t('svc_save') : '') + (pe && pe.days ? ' · ' + t('svc_days', { n: pe.days }) : ''), null, null);
  const clk = S.clock.set && S.clock.ok;
  add(clk ? 'ok' : 'warn', t('svc_clock'), t(clk ? 'svc_clock_ok' : 'svc_clock_bad'), clk ? null : 'clock', 'svc_set');
  const f = S.m.have ? S.m.flags : 0, fault = !!((f & 4) || (f & 16));
  add(fault ? 'bad' : (c.bias ? 'ok' : 'warn'), t('svc_probe'),
    fault ? t('svc_probe_bad') : t(c.bias ? 'svc_probe_ok' : 'svc_probe_unc', { p: c.biasUsed }), fault || !c.bias ? 'secProbe' : null, 'svc_fix');
  const d = (c.locd || [])[c.loc] || [c.dry, c.wet, c.gd, c.gw];
  const cs = calSteps(d);
  add(cs.all ? 'ok' : 'warn', t('svc_cal'), t(cs.all ? 'svc_cal_ok' : (cs.any ? 'svc_cal_half' : 'svc_cal_bad'), { n: locName(c.loc) }),
    cs.all ? null : 'settings', 'svc_set');
  const free = S.fs.total ? Math.round((1 - S.fs.used / S.fs.total) * 100) : null;
  add(S.logErr ? 'bad' : (c.int ? 'ok' : 'warn'), t('svc_log'),
    (c.int ? t('svc_log_on', { n: S.pwr && S.pwr.int ? S.pwr.int : c.int }) : t('svc_log_off')) + (free !== null ? ' · ' + t('svc_free', { n: free }) : ''),
    c.int ? null : 'settings', 'svc_set');
  add('info', t('svc_fw'), S.fw + ' · ID ' + S.id, null, null);
  const sig = JSON.stringify(rows) + lang;
  if (sig !== renderSvc.sig) {
    renderSvc.sig = sig;
    $('#svcList').innerHTML = rows.map((r) => '<div class="svcrow ' + r.st + '"><span class="svcico" aria-hidden="true">' +
      ({ ok: '✓', warn: '!', bad: '!', info: 'i' })[r.st] + '</span><div><div class="svclbl">' + esc(r.lbl) + '</div><div class="svcval">' +
      esc(r.val) + '</div></div>' + (r.go ? '<button class="btn" type="button" data-go="' + r.go + '">' + esc(t(r.act)) + '</button>' : '<span></span>') + '</div>').join('');
  }
  // shrnutí v záhlaví sekcí
  const sum = (id, txt, warn) => { const e = $('#' + id); e.textContent = txt; e.classList.toggle('warn', !!warn); };
  sum('sumProbe', (fault ? t('svc_probe_bad') : 'GPIO' + c.biasUsed) + ' · ' + t(c.pmode === 1 ? 'pm_s1' : 'pm_s2') +
    ' · ' + t('sum_live', { n: c.live || 15 }) +
    (c.ret ? ' · GPIO' + c.ret : ''), fault);
  sum('sumLocs', t('sum_locs', { n: c.locs ? c.locs.length : 1 }));
  sum('sumWater', t('sum_water', { j: c.wjmp, h: c.wwin }));
  sum('sumNet', t(c.txp ? 'sum_txp1' : 'sum_txp0') + ' · ' + t('sum_cp' + c.cport) + ' · ' + t(c.pw ? 'sum_pw1' : 'sum_pw0'));
  sum('sumBat', bv + (pe ? ' · ' + pe.perDay.toFixed(1) + ' mAh/' + t('sum_day') : ''), S.bat.lvl > 0 || saving);
  sum('sumMaint', t('fw') + ' ' + S.fw);
  sum('sumMan', c.man ? t('sum_man_on', { m: t('mn' + (c.mnote === undefined ? 2 : c.mnote) + '_s') }) : t('sum_man_off'));
}

function renderFooter() {
  $('#footer').innerHTML = esc(t('fw')) + ' ' + esc(S.fw) + ' · ID ' + esc(S.id) + '<br>' +
    esc(t('free_heap')) + ' ' + Math.round(S.heap / 1024) + ' kB';
}

// ----------------------------------------------------------------------------
//  Diagnostický profil
// ----------------------------------------------------------------------------
let prof = null;
async function loadProfile() {
  try {
    prof = await getJSON('/api/profile');
    profLoaded = true;
    renderProfile();
  } catch (e) { /* tichá chyba, zkusí se při dalším snímku */ }
}
function interp(ts, vs, x) {
  if (!ts.length) return null;
  if (x <= ts[0]) return vs[0];
  for (let i = 1; i < ts.length; i++) {
    if (ts[i] >= x) {
      const f = (x - ts[i - 1]) / (ts[i] - ts[i - 1] || 1);
      return vs[i - 1] + f * (vs[i] - vs[i - 1]);
    }
  }
  return vs[vs.length - 1];
}
function renderProfile() {
  const box = $('#profBox');
  if (!prof || !prof.n) { box.hidden = true; return; }
  box.hidden = false;
  if (!plots.prof) plots.prof = new Plot($('#chProf'));
  const pts = prof.t.map((x, i) => [x, prof.v[i]]);
  const vs = prof.v;
  const lo = Math.max(0, Math.floor((Math.min.apply(null, vs) - 80) / 100) * 100);
  const hi = Math.min(4300, Math.ceil((Math.max.apply(null, vs) + 80) / 100) * 100);
  plots.prof.set({
    logx: true, xmin: 0.3, xmax: 300, xticks: [1, 3, 10, 30, 100, 300],
    xfmt: (v) => String(v), xfull: (x) => x.toFixed(x < 10 ? 2 : 0) + ' ms',
    axes: [{ min: lo, max: hi, fmt: (v) => Math.round(v) }, null],
    series: [{ name: 'ADC', color: css('--water'), pts: pts, axis: 0, w: 2, fmt: (v) => Math.round(v) }],
    lines: []
  });
  const v10 = interp(prof.t, prof.v, 10), v250 = interp(prof.t, prof.v, 250);
  const el = $('#profVerdict');
  if (v10 === null || v250 === null || v250 < 1) { el.textContent = t('prof_none'); el.className = 'verdict'; return; }
  const d = (v250 - v10) / v250 * 100;
  const ad = Math.abs(d).toFixed(1);
  if (Math.abs(d) < 1.5) { el.textContent = t('prof_ok', { d: ad }); el.className = 'verdict good'; }
  else if (d > 0 && S && S.cfg.pmode === 2) { el.textContent = t('prof_rise_ok', { d: ad, us: S.cfg.early }); el.className = 'verdict good'; }
  else if (d > 0) { el.textContent = t('prof_rise', { d: ad }); el.className = 'verdict ' + (d > 6 ? 'bad' : 'mid'); }
  else { el.textContent = t('prof_fall', { d: ad }); el.className = 'verdict ' + (d < -6 ? 'bad' : 'mid'); }
}

// ============================================================================
//  Záznamy (textový výpis)
// ============================================================================
let logShowAll = false;
let logDaysShown = 14;  // kolik dní tabulka ukazuje (tlačítko Zobrazit starší dny přidává)

// Značky záznamu srozumitelně: ikona + vysvětlivka (legenda pod tabulkou).
const LOG_FLAGS = [['w', '💧', 'lf_w'], ['man', '✋', 'lf_man'], ['fl', '🌊', 'lf_fl'], ['err', '⚠️', 'lf_err'], ['lb', '🔋', 'lf_lb'], ['q', '🕒', 'lf_q'], ['hib', '💤', 'lf_hib']];
function recFlags(r, waterSet) {
  const f = [];
  if (waterSet.has(r.loc + '|' + r.t)) f.push('w');
  if (r.man) f.push('man');
  if (r.fl) f.push('fl');
  else if (r.hum === null && !r.hib) f.push('err');
  if (r.lb) f.push('lb');
  if (r.q) f.push('q');
  if (r.hib) f.push('hib');
  return f;
}
// Kolik místa log zabírá a jak dlouho ještě vydrží v plném rozlišení (pak starší záznamy řídnou, nic se nemaže).
function renderLogCap() {
  if (!S || !S.fs) return;
  const used = S.fs.log || 0, max = S.fs.lmax || 573440, n = (logRecs ? logRecs.length : 0) + manRecs.length;
  const pct = Math.min(100, Math.round(used / max * 100));
  const bpr = n > 20 ? used / n : 46, per = S.pwr && S.pwr.int ? S.pwr.int : S.cfg.int;
  let txt;
  if (!per) txt = t('cap_off', { p: pct });
  else if (used >= max * 0.98) txt = t('cap_full', { p: pct });
  else {
    const days = Math.max(0, Math.floor((max - used) / bpr / (1440 / per)));
    txt = t('cap_left', { p: pct, d: days, m: (days / 30.4).toFixed(days < 61 ? 1 : 0) });
  }
  $('#logCap').innerHTML = txt + '<div class="capbar"><i style="width:' + pct + '%"></i></div>';
}
// Záznamy po dnech (nejnovější nahoře), s průměrem/min/max dne, barevnou vlhkostí podle prahů a ikonami místo značek.
function renderLogView() {
  if (!S) return;
  renderLogCap();
  const c = S.cfg, sel = $('#logLoc');
  const opts = '<option value="0">' + esc(t('log_all')) + '</option>' + (c.locs || []).map((n, i) => '<option value="' + (i + 1) + '">' + esc(locName(i)) + '</option>').join('');
  if (sel.dataset.sig !== opts) {  // výchozí výběr: místo, kde je sonda
    const keep = sel.dataset.sig ? sel.value : String(c.loc + 1);
    sel.innerHTML = opts; sel.dataset.sig = opts;
    sel.value = +keep <= (c.locs || []).length ? keep : String(c.loc + 1);
  }
  const loc = +sel.value || 0, multi = (c.locs || []).length > 1;
  const all = (logRecs || []).concat(manRecs).filter((r) => !loc || r.loc === loc).sort((a, b) => b.t - a.t);
  const waterSet = new Set();
  new Set((logRecs || []).map((r) => r.loc)).forEach((l) => detectWater((logRecs || []).filter((r) => r.loc === l), c.wjmp, c.wwin)
    .forEach((e) => waterSet.add(l + '|' + e.t0)));
  const box = $('#logDays');
  if (logRecs === null) { box.innerHTML = '<div class="hint">' + esc(t('loading')) + '</div>'; return; }
  if (!all.length) { box.innerHTML = '<div class="hint">' + esc(t('log_none')) + '</div>'; $('#btnLogMore').hidden = true; $('#logLegend').innerHTML = ''; return; }
  const days = [];
  all.forEach((r) => { const k = Math.floor(r.t / 86400e3); if (!days.length || days[days.length - 1].k !== k) days.push({ k: k, recs: [] }); days[days.length - 1].recs.push(r); });
  const today = devNow() !== null ? Math.floor(devNow() / 86400) : null;
  const used = new Set();
  const hcls = (h) => (h === null ? '' : (h < c.dry ? 'dry' : (h > c.wet ? 'wet' : 'ok')));
  const html = days.slice(0, logDaysShown).map((d, di) => {
    const hs = d.recs.filter((r) => r.hum !== null && !r.man).map((r) => r.hum);
    const dt = new Date(d.k * 86400e3);
    const name = d.k === today ? t('today') : (d.k === today - 1 ? t('yesterday') : dt.getUTCDate() + '. ' + (dt.getUTCMonth() + 1) + '. ' + dt.getUTCFullYear());
    const sum = hs.length ? t('day_sum', { a: Math.round(hs.reduce((x, y) => x + y, 0) / hs.length), lo: Math.min(...hs), hi: Math.max(...hs), n: d.recs.length })
      : t('day_n', { n: d.recs.length });
    const rows = d.recs.map((r) => {
      const fl = recFlags(r, waterSet);
      fl.forEach((x) => used.add(x));
      const ic = fl.map((x) => { const f = LOG_FLAGS.find((y) => y[0] === x); return '<span title="' + esc(t(f[2])) + '">' + f[1] + '</span>'; }).join(' ');
      const tm = new Date(r.t);
      return '<tr><td>' + pad2(tm.getUTCHours()) + ':' + pad2(tm.getUTCMinutes()) + '</td><td class="h"><i class="dot ' + hcls(r.hum) + '"></i>' +
        (r.hum === null ? '--' : r.hum + ' %') + '</td><td>' + (r.tmp === null ? '--' : Math.round(r.tmp) + '°') + '</td><td>' +
        (r.v === null ? '--' : r.v.toFixed(2) + ' V') + '</td><td class="f">' + ic + (r.note ? ' ' + esc(r.note) : '') +
        (multi && !loc ? ' <span class="ml">' + esc(locName(r.loc - 1)) + '</span>' : '') + '</td></tr>';
    }).join('');
    return '<details class="day"' + (di === 0 ? ' open' : '') + '><summary><span class="dayt">' + esc(name) + '</span><span class="days-s">' + esc(sum) +
      '</span></summary><div class="tblwrap"><table class="dtbl"><tr><th>' + esc(t('c_time')) + '</th><th>' + esc(t('hum')) + '</th><th>' + esc(t('temp')) +
      '</th><th>' + esc(t('c_bat')) + '</th><th></th></tr>' + rows + '</table></div></details>';
  }).join('');
  box.innerHTML = html;
  $('#btnLogMore').hidden = days.length <= logDaysShown;
  $('#logLegend').innerHTML = LOG_FLAGS.filter((f) => used.has(f[0])).map((f) => '<span>' + f[1] + ' ' + esc(t(f[2])) + '</span>').join('');
}
function renderLogText() {
  const box = $('#logBox');
  if (logText === null) { box.textContent = t('loading'); return; }
  const lines = logText.split('\n').filter((l) => l.trim() !== '');
  const MAXL = 400;
  const shown = logShowAll || lines.length <= MAXL ? lines : lines.slice(lines.length - MAXL);
  box.textContent = shown.length ? shown.join('\n') : t('empty');
  $('#logInfo').textContent = t('recs', { n: lines.length });
  renderLogView();
  const b = $('#btnLogAll');
  b.hidden = logShowAll || lines.length <= MAXL;
  b.textContent = t('show_all', { n: lines.length });
  box.scrollTop = box.scrollHeight;
}

// ============================================================================
//  Směrování
// ============================================================================
function parseRoute() {
  const h = (location.hash || '#/').replace(/^#\/?/, '');
  return ['settings', 'logs', 'admin'].indexOf(h) >= 0 ? h : 'dash';
}
function show(r) {
  if (r === 'admin' && !svcOk) {
    const p = prompt(t('svc_pin_q'));
    if (p === null || p.trim() !== SVC_PIN) {
      if (p !== null) toast(t('svc_pin_bad'), true);
      const back = route === 'admin' ? 'dash' : route;
      history.replaceState(null, '', back === 'dash' ? '#/' : '#/' + back);
      if (route === 'admin') show('dash');
      return;
    }
    svcOk = true;
    $('#svcLock').textContent = '🔓';
  }
  route = r;
  $$('.view').forEach((v) => { v.hidden = v.id !== 'v-' + r; });
  $$('#tabs a').forEach((a) => a.classList.toggle('on', a.dataset.r === r));
  window.scrollTo(0, 0);
  if (r === 'dash') { loadLog(false); }
  if (r === 'settings') { if (S) { fillSettings(true); renderCal(); renderFooter(); } }
  if (r === 'admin') { if (S) { fillSettings(true); renderWire(); renderCal(); renderPower(); renderSvc(); if (S.prof.valid && !profLoaded) loadProfile(); } }
  if (r === 'logs') { logShowAll = false; logDaysShown = 14; renderLogText(); loadLog(true); }
  setTimeout(() => Plot.redrawAll(), 30);
}

// ============================================================================
//  Události
// ============================================================================
function bind() {
  window.addEventListener('hashchange', () => show(parseRoute()));

  $('#btnExtend').addEventListener('click', (e) => runCmd(e.currentTarget, '/api/extend'));
  $$('#rangeSeg button').forEach((b) => b.addEventListener('click', () => { range = b.dataset.range; lsSet('range', range); renderCharts(); }));
  ['#rFrom', '#rTo'].forEach((id) => $(id).addEventListener('change', () => {
    if (!dayMs($('#rFrom').value) || !dayMs($('#rTo').value)) return;
    rFrom = $('#rFrom').value; rTo = $('#rTo').value;
    if (rTo < rFrom) { const x = rFrom; rFrom = rTo; rTo = x; }
    lsSet('rfrom', rFrom); lsSet('rto', rTo);
    renderCharts();
  }));
  $('#btnRep').addEventListener('click', printReport);

  $('#banners').addEventListener('click', (e) => {
    const b = e.target.closest('button[data-act]');
    if (!b) return;
    if (b.dataset.act === 'clock_fix') setClock(b);
    else if (b.dataset.act === 'wire_go') {
      openSec('secProbe');
    }
  });

  // jazyk
  $$('#langs button').forEach((b) => b.addEventListener('click', async () => {
    setLang(b.dataset.lang, true);
    try { await post('/api/settings', { lang: b.dataset.lang }); } catch (e) { toast(t('e_http'), true); }
  }));

  // provoz
  $('#fWeb').addEventListener('input', () => { $('#webVal').textContent = $('#fWeb').value; formDirty.web = true; });
  $('#fInt').addEventListener('input', () => { formDirty.ops = true; updIntHint(); });
  ['#fDry', '#fWet'].forEach((id) => $(id).addEventListener('input', () => { formDirty.thr = true; if (S) renderCalSteps(S.m.have && S.m.pct !== null ? S.m.pct : null, false); }));

  // místa
  const locOp = (btn, op, extra) => runCmd(btn, '/api/loc', Object.assign({ op: op }, extra || {}));
  const locSel = (sel) => sel.addEventListener('change', () => { locSig = ''; locOp(null, 'sel', { i: sel.value }); });
  locSel($('#dashLoc'));
  const locRedraw = () => { locSig = ''; renderLoc(); };
  const nameOk = (v) => { const n = v.trim(); if (!n) { toast(t('loc_noname'), true); return null; } return n; };
  // přesun sondy na jiné místo (klepnutí na řádek, Enter/mezerník); hned, bez potvrzení – vrátit jde stejně snadno
  const locMove = (row) => {
    const i = +row.dataset.i;
    if (i === S.cfg.loc || pending) return;
    locOp(null, 'sel', { i: i });
  };
  $('#locList').addEventListener('click', (e) => {
    const row = e.target.closest('.locrow');
    if (!row) return;
    const i = +row.dataset.i, act = e.target.closest('[data-act]');
    if (act && act.dataset.act === 'ren') { locUi.edit = i; locUi.adding = false; locRedraw(); return; }
    if (act && act.dataset.act === 'rencancel') { locUi.edit = -1; locRedraw(); return; }
    if (act && act.dataset.act === 'del') {
      const q = i === S.cfg.loc && S.cfg.locs.length > 1
        ? t('loc_del_here_q', { n: locName(i), m: locName(i === 0 ? 1 : 0) }) : t('loc_del_q', { n: locName(i) });
      if (confirm(q)) locOp(act, 'del', { i: i });
      return;
    }
    if (e.target.closest('input, form')) return;
    locMove(row);
  });
  $('#locList').addEventListener('keydown', (e) => {
    const row = e.target.classList && e.target.classList.contains('locrow') ? e.target : null;
    if (row && (e.key === 'Enter' || e.key === ' ')) { e.preventDefault(); locMove(row); }
  });
  $('#locList').addEventListener('submit', (e) => {
    e.preventDefault();
    const n = nameOk($('#fLocRen').value);
    if (n) locOp($('button[type=submit]', e.target), 'ren', { i: locUi.edit, name: n });
  });
  $('#btnLocAdd').addEventListener('click', () => {
    locUi.adding = true; locUi.edit = -1; $('#fLocNew').value = ''; locRedraw(); $('#fLocNew').focus();
  });
  $('#btnLocNewCancel').addEventListener('click', () => { locUi.adding = false; locRedraw(); });
  $('#locNew').addEventListener('submit', (e) => {
    e.preventDefault();
    const n = nameOk($('#fLocNew').value);
    if (n) locOp($('button[type=submit]', e.currentTarget), 'new', { name: n });
  });

  // Servis: volby se ukládají hned po změně (bez tlačítka Uložit). Formulář je "rozpracovaný", dokud zařízení
  // změnu nepotvrdí (pak fillSettings), aby ho mezitím nepřepsal starší stav.
  const whenIdle = async () => { for (let i = 0; i < 150 && pending; i++) await new Promise((r) => setTimeout(r, 100)); };
  const autoSave = (sel, dirtyKey, params) => $(sel).addEventListener('change', async () => {
    formDirty[dirtyKey] = true;
    await whenIdle();
    const ok = await runCmd(null, '/api/settings', params());
    if (!ok) { formDirty[dirtyKey] = false; fillSettings(true); }
  });
  autoSave('#fInt', 'ops', () => ({ int: $('#fInt').value }));
  autoSave('#fLive', 'live', () => ({ live: $('#fLive').value }));
  autoSave('#fMan', 'man', () => ({ man: $('#fMan').checked ? 1 : 0 }));
  autoSave('#fManMode', 'man', () => ({ mnote: $('#fManMode').value }));
  autoSave('#fManExt', 'man', () => ({ mext: $('#fManExt').checked ? 1 : 0 }));
  // ruční zápis: tlačítko na Přehledu
  $('#btnMan').addEventListener('click', async (e) => {
    const btn = e.currentTarget, mode = S.cfg.mnote === undefined ? 2 : S.cfg.mnote;
    if (mode === 2 && logRecs === null) await loadLog(true);  // čísla bodů se počítají z logu
    let note = '';
    if (mode === 2) note = nextPoint();
    else if (mode === 1) note = $('#fManNote').value.trim().slice(0, 24);
    const ok = await runCmd(btn, '/api/manual', note ? { note: note } : null);
    if (ok && mode === 1 && note) { const nx = bumpNote(note); lsSet('man_note', nx); $('#fManNote').value = nx; }
  });
  autoSave('#fWeb', 'web', () => ({ web: $('#fWeb').value }));
  // prahy: obě hodnoty najednou, „Mokro“ musí být nad „Zalít“ (jinak se neuloží a vrátí se)
  const thrSave = async () => {
    const dry = parseInt($('#fDry').value, 10), wet = parseInt($('#fWet').value, 10);
    if (!(dry >= 0 && dry <= 99 && wet >= 1 && wet <= 100 && wet > dry)) {
      toast(t('thr_bad'), true); formDirty.thr = false; fillSettings(true); renderCal(); return;
    }
    if (S && dry === S.cfg.dry && wet === S.cfg.wet) { formDirty.thr = false; return; }
    formDirty.thr = true;
    await whenIdle();
    if (!(await runCmd(null, '/api/settings', { dry: dry, wet: wet }))) { formDirty.thr = false; fillSettings(true); }
  };
  ['#fDry', '#fWet'].forEach((id) => $(id).addEventListener('change', thrSave));
  autoSave('#fScale', 'adv', () => ({ scale: $('#fScale').value }));
  autoSave('#fVexc', 'adv', () => ({ vexc: $('#fVexc').checked ? 1 : 0 }));
  autoSave('#fBias', 'wire', () => ({ bias: $('#fBias').value }));
  autoSave('#fPmode', 'pm', () => ({ pmode: $('#fPmode').value }));
  autoSave('#fRet', 'ret', () => ({ ret: $('#fRet').value }));
  autoSave('#fCport', 'net', () => ({ cport: $('#fCport').value }));
  autoSave('#fTxp', 'net', () => ({ txp: $('#fTxp').value }));
  // heslo Wi-Fi (od v7.0): uloží se a zařízení se restartuje s novým heslem
  $('#fPwOn').addEventListener('change', () => {
    formDirty.pw = true;
    $('#pwRow').hidden = !$('#fPwOn').checked;
    if ($('#fPwOn').checked) $('#fPw').focus();
  });
  $('#btnPw').addEventListener('click', async (e) => {
    const on = $('#fPwOn').checked, pw = on ? $('#fPw').value : '';
    if (on && !/^[\x20-\x7e]{8,63}$/.test(pw)) { toast(t('e_pw'), true); return; }
    if (!on && !(S && S.cfg.pw === 1)) { formDirty.pw = false; return; }
    if (!confirm(on ? t('pw_q', { pw: pw }) : t('pw_off_q'))) return;
    if (await runCmd(e.currentTarget, '/api/wifipw', { pw: pw }, { noWait: true })) {
      formDirty.pw = false;
      $('#fPw').value = '';
      toast(t('m_pw'));
    }
  });
  $('#fTech').addEventListener('change', () => {
    tech = $('#fTech').checked;
    lsSet('tech', tech ? '1' : '0');
    if (logRecs) renderCharts();
    toast(t('m_settings'));
  });
  ['#formWire', '#formPm', '#formRet', '#formAdv', '#formNet', '#formThr', '#formOps', '#formMan', '#formLive'].forEach((f) => $(f).addEventListener('submit', (e) => e.preventDefault()));
  // sbalitelné sekce Servisu: co si kdo otevřel, zůstane otevřené (jen v tomto prohlížeči)
  $$('details.sec').forEach((d) => {
    if (lsGet('sec_' + d.id) === '1') d.open = true;
    d.addEventListener('toggle', () => { lsSet('sec_' + d.id, d.open ? '1' : '0'); if (d.open) Plot.redrawAll(); });
  });
  $('#svcList').addEventListener('click', (e) => {
    const b = e.target.closest('button[data-go]');
    if (!b) return;
    if (b.dataset.go === 'settings') location.hash = '#/settings';
    else if (b.dataset.go === 'clock') setClock(b);
    else openSec(b.dataset.go);
  });
  $('#formVbat').addEventListener('submit', (e) => {
    e.preventDefault();
    const v = $('#fVbat').value;
    if (!v) return;
    runCmd($('button[type=submit]', e.currentTarget), '/api/calvbat', { v: v });
  });

  // zapojení sondy (pin BIAS)
  $('#btnWire').addEventListener('click', (e) => runCmd(e.currentTarget, '/api/wiring'));

  // vyhodnocení zalití
  ['fWjmp', 'fWwin'].forEach((id) => $('#' + id).addEventListener('input', () => { formDirty.wdet = true; }));
  $('#formWdet').addEventListener('submit', (e) => {
    e.preventDefault();
    runCmd($('button[type=submit]', e.currentTarget), '/api/settings', { wjmp: $('#fWjmp').value, wwin: $('#fWwin').value });
  });

  // hodiny, kalibrace, diagnostika
  $('#btnTime').addEventListener('click', (e) => setClock(e.currentTarget));
  $('#btnDry').addEventListener('click', (e) => runCmd(e.currentTarget, '/api/cal', { which: 'dry' }));
  $('#btnWet').addEventListener('click', (e) => runCmd(e.currentTarget, '/api/cal', { which: 'wet' }));
  // Servis → místa a kalibrace
  $('#locsRows').addEventListener('input', () => { locsDirty = true; });
  $('#locsRows').addEventListener('click', (e) => {
    const x = e.target.closest('.lrow-x');
    if (!x) return;
    const list = locsRead();
    if (list.length <= 1) return;
    list.splice($$('#locsRows .lrow').indexOf(x.closest('.lrow')), 1);
    locsDirty = true;
    renderLocsTable(list);
  });
  $('#btnLocsAdd').addEventListener('click', () => {
    const list = locsRead();
    if (list.length >= 8) return;
    list.push({ nazev: t('loc_default', { n: list.length + 1 }), zalevat_pod: S.cfg.dry, mokro_nad: S.cfg.wet, g_sucho: 0.05, g_mokro: 3 });
    locsDirty = true;
    renderLocsTable(list);
  });
  $('#btnLocsSave').addEventListener('click', (e) => locsSave(e.currentTarget));
  $('#btnLocsExp').addEventListener('click', locsExport);
  $('#fLocsImp').addEventListener('change', (e) => {
    const f = e.target.files && e.target.files[0];
    if (!f) return;
    const rd = new FileReader();
    rd.onload = () => locsImport(rd.result);
    rd.onerror = () => toast(t('locs_e_file'), true);
    rd.readAsText(f);
    e.target.value = '';
  });

  $('#btnCalReset').addEventListener('click', (e) => {
    if (!confirm(t('cal_reset_q', { n: locName(S.cfg.loc) }))) return;
    runCmd(e.currentTarget, '/api/cal', { which: 'reset' });
  });
  $('#btnProf').addEventListener('click', (e) => runCmd(e.currentTarget, '/api/profile/capture'));
  $('#btnRestart').addEventListener('click', async (e) => { if (await runCmd(e.currentTarget, '/api/restart', null, { noWait: true })) toast(t('m_ok')); });
  $('#btnFactory').addEventListener('click', (e) => {
    if (confirm(t('confirm_factory'))) runCmd(e.currentTarget, '/api/factory', null, { noWait: true });
  });

  // záznamy
  $('#btnCsv').addEventListener('click', () => downloadCsv());
  $('#btnTxt').addEventListener('click', () => downloadTxt());
  $('#btnLogAll').addEventListener('click', () => { logShowAll = true; renderLogText(); });
  $('#btnLogMore').addEventListener('click', () => { logDaysShown += 30; renderLogView(); });
  $('#logLoc').addEventListener('change', () => { logDaysShown = 14; renderLogView(); });
  $('#btnLogClear').addEventListener('click', async (e) => {
    if (!confirm(t('confirm_clear'))) return;
    const r = await runCmd(e.currentTarget, '/api/log/clear', null, { noWait: true });
    if (r) { logText = ''; logRecs = []; manRecs = []; renderLogText(); renderCharts(); renderMan(); toast(t('m_ok')); }
  });

}

// ============================================================================
//  Servis → Místa a kalibrace: úprava všech míst, export / import (JSON)
// ============================================================================
let locsDirty = false, locsSig = '', locsImported = false;
const LOCS_FMT = 'arbolok-mista';

function locsFromCfg() {
  const c = S.cfg, d = c.locd || [];
  return (c.locs || []).map((n, i) => ({ nazev: n, zalevat_pod: d[i] ? d[i][0] : c.dry, mokro_nad: d[i] ? d[i][1] : c.wet,
    g_sucho: d[i] ? d[i][2] : c.gd, g_mokro: d[i] ? d[i][3] : c.gw, orig: i }));
}

function locsRowHtml(l, i) {
  const f = (k, v, step) => '<label class="row"><span>' + esc(t('locs_' + k)) + '</span><input type="text" inputmode="decimal" data-k="' + k +
    '" value="' + esc(v) + '"' + (step ? ' data-g="1"' : '') + '></label>';
  return '<div class="lrow" data-orig="' + (l.orig === undefined ? '' : l.orig) + '">' +
    '<div class="lrow-h"><input type="text" data-k="name" maxlength="24" value="' + esc(l.nazev) + '" aria-label="' + esc(t('loc_name')) + '">' +
    '<button type="button" class="btn ghost lrow-x" aria-label="' + esc(t('loc_del')) + '">×</button></div>' +
    '<div class="grid4">' + f('dry', l.zalevat_pod) + f('wet', l.mokro_nad) + f('gd', l.g_sucho, 1) + f('gw', l.g_mokro, 1) + '</div></div>';
}

function renderLocsTable(list) {
  const box = $('#locsRows');
  box.innerHTML = list.map(locsRowHtml).join('');
  $$('.lrow-x', box).forEach((b) => { b.disabled = list.length <= 1; });
  $('#btnLocsAdd').disabled = list.length >= 8;
}

function renderLocs() {
  if (!S || !S.cfg.locs || locsDirty) return;
  const sig = JSON.stringify([S.cfg.locs, S.cfg.locd, lang]);
  if (sig === locsSig) return;
  locsSig = sig;
  renderLocsTable(locsFromCfg());
}

function locsRead() {
  return $$('#locsRows .lrow').map((r) => {
    const v = (k) => $('[data-k="' + k + '"]', r).value.trim();
    const num = (k) => Number(v(k).replace(',', '.'));
    return { nazev: v('name'), zalevat_pod: num('dry'), mokro_nad: num('wet'), g_sucho: num('gd'), g_mokro: num('gw'),
      orig: r.dataset.orig === '' ? undefined : Number(r.dataset.orig) };
  });
}

// Stejná pravidla jako ve firmwaru (loc::valid). Vrací null, nebo text chyby.
function locsCheck(list) {
  if (!Array.isArray(list) || list.length < 1 || list.length > 8) return t('locs_e_n');
  for (let i = 0; i < list.length; i++) {
    const l = list[i], n = (x) => typeof x === 'number' && isFinite(x);
    const bad = !l || !String(l.nazev || '').trim() || !n(l.zalevat_pod) || !n(l.mokro_nad) || !n(l.g_sucho) || !n(l.g_mokro) ||
      l.zalevat_pod < 0 || l.zalevat_pod > 99 || l.mokro_nad > 100 || l.mokro_nad <= l.zalevat_pod || Math.round(l.zalevat_pod) !== l.zalevat_pod ||
      Math.round(l.mokro_nad) !== l.mokro_nad || !(l.g_sucho > 0) || !(l.g_mokro > l.g_sucho * 1.2) || !(l.g_mokro < 1000);
    if (bad) return t('locs_e_row', { n: i + 1, name: l && l.nazev ? String(l.nazev) : '?' });
  }
  return null;
}

function locsMark(list) {
  $$('#locsRows .lrow').forEach((r, i) => r.classList.toggle('bad', !!locsCheck([list[i]])));
}

function locsSave(btn) {
  const list = locsRead();
  const err = locsCheck(list);
  locsMark(list);
  if (err) { toast(err, true); return; }
  const p = { n: list.length };
  let a = 0;
  list.forEach((l, i) => {
    p['n' + i] = l.nazev; p['d' + i] = l.zalevat_pod; p['w' + i] = l.mokro_nad; p['gd' + i] = l.g_sucho; p['gw' + i] = l.g_mokro;
    // původní místo řádku: podle toho zařízení přečísluje záznamy v logu (nový řádek -1; import ze souboru bez o<i>)
    if (!locsImported) p['o' + i] = l.orig === undefined ? -1 : l.orig;
    if (l.orig === S.cfg.loc) a = i;
  });
  p.a = a;
  runCmd(btn, '/api/locs', p);
}

function locsExport() {
  const data = { format: LOCS_FMT, verze: 1, zarizeni: S ? S.id : '', datum: new Date().toISOString().slice(0, 10),
    mista: locsFromCfg().map((l) => ({ nazev: l.nazev, zalevat_pod: l.zalevat_pod, mokro_nad: l.mokro_nad, g_sucho: l.g_sucho, g_mokro: l.g_mokro })) };
  const blob = new Blob([JSON.stringify(data, null, 2) + '\n'], { type: 'application/json' });
  const a = document.createElement('a');
  a.href = URL.createObjectURL(blob);
  a.download = 'mista_' + (S ? S.id : 'arbolok') + '_' + data.datum + '.json';
  document.body.appendChild(a);
  a.click();
  setTimeout(() => { URL.revokeObjectURL(a.href); a.remove(); }, 1000);
}

function locsImport(text) {
  let d;
  try { d = JSON.parse(String(text).replace(/^﻿/, '')); } catch (e) { toast(t('locs_e_file'), true); return; }
  const list = Array.isArray(d) ? d : (d && Array.isArray(d.mista) ? d.mista : null);
  if (!list) { toast(t('locs_e_file'), true); return; }
  const norm = list.map((l) => ({ nazev: String((l && l.nazev) || '').trim().slice(0, 24), zalevat_pod: Number(l && l.zalevat_pod),
    mokro_nad: Number(l && l.mokro_nad), g_sucho: Number(l && l.g_sucho), g_mokro: Number(l && l.g_mokro) }));
  const err = locsCheck(norm);
  if (err) { toast(err, true); return; }
  locsDirty = true;
  locsImported = true;  // místa ze souboru nahradí stávající podle pořadí (záznamy se nepřečíslují)
  renderLocsTable(norm);
  toast(t('locs_loaded', { n: norm.length }), false);
}


// ============================================================================
//  Období od–do (Přehled) a zpráva do PDF (Záznamy → tisk → Uložit jako PDF)
// ============================================================================
// Časy záznamů jsou lokální čas zařízení uložený jako UTC: datum z <input type=date> se proto bere v UTC.
function dayMs(s) { const m = /^(\d{4})-(\d{2})-(\d{2})$/.exec(s || ''); return m ? Date.UTC(+m[1], +m[2] - 1, +m[3]) : null; }
function dayStr(ms) { const d = new Date(ms); return d.getUTCFullYear() + '-' + pad2(d.getUTCMonth() + 1) + '-' + pad2(d.getUTCDate()); }
function dayRange(a, b) {  // [od 00:00, do 24:00), prohozené meze se srovnají
  let x0 = dayMs(a), x1 = dayMs(b);
  if (x0 === null || x1 === null) return null;
  if (x1 < x0) { const t0 = x0; x0 = x1; x1 = t0; }
  return [x0, x1 + 86400e3];
}
let rFrom = lsGet('rfrom') || '', rTo = lsGet('rto') || '';
function customDefaults(recs) {
  if (dayMs(rFrom) !== null && dayMs(rTo) !== null) return;
  const a = anchorTime(recs);
  rTo = dayStr(a);
  rFrom = dayStr(a - 6 * 86400e3);
}
function syncRangeUi() {
  const on = range === 'custom';
  $('#rangeCustom').hidden = !on;
  if (on) {
    customDefaults((logRecs || []).filter((r) => !r.q));
    $('#rFrom').value = rFrom; $('#rTo').value = rTo;
  }
}

// Souhrn za období (stejná pravidla jako karta Zalévání na přehledu).
function periodStats(recs, evs, x0, x1, dry) {
  const hp = recs.filter((r) => r.hum !== null), out = { n: hp.length, count: evs.length, gap: 0, gapA: 0, gapB: 0, min: null, avg: null, max: null, dryH: 0 };
  if (!hp.length) return out;
  let prev = hp[0].t, ei = 0;
  const cand = (a, b) => { if (b - a > out.gap) { out.gap = b - a; out.gapA = a; out.gapB = b; } };
  for (let i = 0; i < hp.length; i++) {
    if (i && hp[i].t - hp[i - 1].t > 24 * 3600e3) { cand(prev, hp[i - 1].t); prev = hp[i].t; }
    while (ei < evs.length && evs[ei].t0 <= hp[i].t) {
      if (evs[ei].t0 >= prev) cand(prev, evs[ei].t0);
      prev = Math.max(prev, evs[ei].t1);
      ei++;
    }
  }
  cand(prev, hp[hp.length - 1].t);
  const v = hp.map((r) => r.hum);
  out.min = Math.min.apply(null, v); out.max = Math.max.apply(null, v);
  out.avg = v.reduce((a, b) => a + b, 0) / v.length;
  let ms = 0;
  for (let i = 0; i + 1 < hp.length; i++) {
    const dt = hp[i + 1].t - hp[i].t;
    if (hp[i].hum < dry && dt <= 3 * 3600e3) ms += dt;
  }
  out.dryH = ms / 3600e3;
  return out;
}

function dailyRows(recs, evs, x0, x1, dry) {
  const rows = [];
  for (let d = x0; d < x1; d += 86400e3) {
    const r = recs.filter((x) => x.t >= d && x.t < d + 86400e3);
    const st = periodStats(r, [], d, d + 86400e3, dry);
    const tp = r.filter((x) => x.tmp !== null).map((x) => x.tmp);
    rows.push({ d: d, n: st.n, min: st.min, avg: st.avg, max: st.max, dryH: st.dryH,
      tmp: tp.length ? tp.reduce((a, b) => a + b, 0) / tp.length : null, w: evs.filter((e) => e.t0 >= d && e.t0 < d + 86400e3).length });
  }
  return rows;
}

// Graf do obrázku: dočasné plátno mimo obrazovku ve světlých barvách (tisk), pak PNG.
function chartImage(opts, w, h) {
  const root = document.documentElement;
  const wasLt = root.classList.contains('lt');
  root.classList.add('lt');
  const box = document.createElement('div');
  box.style.cssText = 'position:absolute;left:-10000px;top:0;width:' + w + 'px;height:' + h + 'px;background:#fff';
  const cv = document.createElement('canvas');
  cv.style.cssText = 'width:' + w + 'px;height:' + h + 'px;display:block';
  box.appendChild(cv);
  document.body.appendChild(box);
  let url = '';
  try {
    const p = new Plot(cv);
    p.set(opts);
    const out = document.createElement('canvas');
    out.width = cv.width; out.height = cv.height;
    const c = out.getContext('2d');
    c.fillStyle = '#fff'; c.fillRect(0, 0, out.width, out.height);
    c.drawImage(cv, 0, 0);
    url = out.toDataURL('image/png');
    Plot.forget && Plot.forget(p);
  } catch (e) { url = ''; }
  box.remove();
  if (!wasLt) root.classList.remove('lt');
  return url;
}

function reportFill() {
  if (!S || !S.cfg.locs) return;
  const sel = $('#repLoc'), sig = S.cfg.locs.join('\u0001') + '|' + lang;
  if (sel.dataset.sig !== sig) {
    const keep = sel.value;
    sel.innerHTML = S.cfg.locs.map((n, i) => '<option value="' + i + '">' + esc(locName(i)) + '</option>').join('');
    sel.value = keep !== '' && +keep < S.cfg.locs.length ? keep : String(S.cfg.loc);
    sel.dataset.sig = sig;
  }
  if (!$('#repFrom').value || !$('#repTo').value) {
    const recs = (logRecs || []).filter((r) => !r.q);
    const a = anchorTime(recs);
    $('#repTo').value = dayStr(a);
    $('#repFrom').value = dayStr(Math.max(recs.length ? recs[0].t : a, a - 29 * 86400e3));
  }
}

function buildReport() {
  const li = Number($('#repLoc').value) || 0, loc = li + 1;
  const win = dayRange($('#repFrom').value, $('#repTo').value);
  if (!win) { toast(t('rep_e_dates'), true); return false; }
  const [x0, x1] = win;
  if (x1 - x0 > 400 * 86400e3) { toast(t('rep_e_long'), true); return false; }
  const all = (logRecs || []).filter((r) => !r.q && r.loc === loc);
  const recs = all.filter((r) => r.t >= x0 && r.t < x1);
  if (!recs.length) { toast(t('rep_e_empty'), true); return false; }
  const c = S.cfg, d = (c.locd && c.locd[li]) || [c.dry, c.wet];
  const dry = d[0], wet = d[1];
  const evs = detectWater(all, c.wjmp, c.wwin).filter((e) => e.t0 >= x0 && e.t0 < x1);
  const hib = hibSpans().filter((h) => h.t1 >= x0 && h.t0 < x1);
  const st = periodStats(recs, evs, x0, x1, dry);
  const water = '#2a78c8', soil = '#8a5a35', warn = '#b86f00', bad = '#bf3b2e', okc = '#2d7a4d';
  const hum = recs.filter((r) => r.hum !== null).map((r) => [r.t, r.hum]);
  const tmp = recs.filter((r) => r.tmp !== null).map((r) => [r.t, r.tmp]);
  const series = [{ name: t('hum'), color: water, pts: hum, axis: 0, fill: true, w: 2, fmt: (v) => Math.round(v) + ' %' }];
  const axes = [{ min: 0, max: 100, ticks: [0, 25, 50, 75, 100], fmt: (v) => v + '%' }, null];
  if (tmp.length) {
    const tAx = alignedAxis(Math.min.apply(null, tmp.map((p) => p[1])) - 0.5, Math.max.apply(null, tmp.map((p) => p[1])) + 0.5, 4);
    const tk = [];
    for (let i = 0; i <= 4; i++) tk.push(tAx.min + i * tAx.step);
    series.push({ name: t('temp'), color: soil, pts: tmp, axis: 1, w: 1.3, fmt: (v) => v.toFixed(1) + ' °C' });
    axes[1] = { min: tAx.min, max: tAx.max, ticks: tk, color: soil, fmt: (v) => (tAx.step % 1 ? v.toFixed(1) : Math.round(v)) + '°' };
  }
  const marks = evs.map((e) => ({ x0: e.t0, x1: e.t1, color: water })).concat(hib.map((h) => ({ x0: h.t0, x1: h.t1, color: bad, drop: false })));
  const img1 = chartImage({ xmin: x0, xmax: x1, axes: axes, series: series, marks: marks,
    lines: [{ axis: 0, y: dry, color: warn }, { axis: 0, y: wet, color: water }] }, 720, 280);
  const bat = (logRecs || []).filter((r) => !r.q && r.v !== null && r.t >= x0 && r.t < x1).map((r) => [r.t, r.v]);
  let img2 = '';
  if (bat.length) {
    let vmin = Math.min.apply(null, bat.map((p) => p[1])), vmax = Math.max.apply(null, bat.map((p) => p[1]));
    vmin = Math.floor((vmin - 0.05) * 20) / 20; vmax = Math.ceil((vmax + 0.05) * 20) / 20;
    if (vmax - vmin < 0.2) vmax = vmin + 0.2;
    img2 = chartImage({ xmin: x0, xmax: x1, axes: [{ min: vmin, max: vmax, fmt: (v) => v.toFixed(2) }, null],
      series: [{ name: t('vbat'), color: okc, pts: bat, axis: 0, w: 1.6 }], lines: [{ axis: 0, y: 3.0, color: warn }],
      marks: hib.map((h) => ({ x0: h.t0, x1: h.t1, color: bad, drop: false })) }, 720, 130);
  }
  const f0 = (v) => (v === null ? '–' : String(Math.round(v)));
  const dT = (ms) => { const x = new Date(ms); return x.getUTCDate() + '. ' + (x.getUTCMonth() + 1) + '. ' + x.getUTCFullYear(); };
  const dTm = (ms) => { const x = new Date(ms); return dT(ms) + ' ' + pad2(x.getUTCHours()) + ':' + pad2(x.getUTCMinutes()); };
  const now = devNow();
  const kv = (k, v) => '<tr><th>' + esc(k) + '</th><td>' + v + '</td></tr>';
  let h = '<header><div class="rep-brand">ARBOLOK</div><h1>' + esc(t('rep_h1')) + '</h1></header>' +
    '<table class="rep-kv">' +
    kv(t('loc_t'), '<b>' + esc(locName(li)) + '</b>') +
    kv(t('rep_period'), esc(dT(x0) + ' – ' + dT(x1 - 86400e3))) +
    kv(t('rep_dev'), esc(S.id) + ' · ' + esc(S.fw)) +
    kv(t('rep_made'), esc(now !== null ? dTm(now * 1000) : dTm(Date.now() - new Date().getTimezoneOffset() * 60000))) +
    kv(t('rep_thr'), esc(t('rep_thr_v', { d: dry, w: wet }))) + '</table>';
  h += '<h2>' + esc(t('rep_sum')) + '</h2><table class="rep-kv">' +
    kv(t('w_count'), '<b>' + st.count + '</b>') +
    kv(t('w_gap'), st.gap ? esc(fmtDur(st.gap)) + ' <small>(' + esc(dTm(st.gapA) + ' – ' + dTm(st.gapB)) + ')</small>' : '–') +
    kv(t('rep_minmax'), f0(st.min) + ' / ' + f0(st.avg) + ' / ' + f0(st.max) + ' %') +
    kv(t('rep_dry'), esc(t('rep_dry_v', { p: dry, h: Math.round(st.dryH) }))) +
    kv(t('rep_n'), String(recs.length)) + '</table>';
  if (hib.length) h += '<p class="rep-note">' + esc(t('rep_hib', { n: hib.length })) + ' ' +
    hib.map((x) => esc(dTm(x.t0) + ' – ' + dTm(x.t1))).join('; ') + '</p>';
  h += '<h2>' + esc(t('chart_hum_t')) + '</h2>' + (img1 ? '<img class="rep-chart" alt="" src="' + img1 + '">' : '') +
    '<div class="rep-lg"><span><i style="background:' + water + '"></i>' + esc(t('hum')) + '</span>' +
    (evs.length ? '<span><i style="background:' + water + ';opacity:.35"></i>' + esc(t('water_ev')) + '</span>' : '') +
    (tmp.length ? '<span><i style="background:' + soil + '"></i>' + esc(t('temp')) + '</span>' : '') +
    '<span><i style="background:' + warn + '"></i>' + esc(t('thr_dry_l')) + ' ' + dry + ' %</span></div>';
  if (img2) h += '<h2>' + esc(t('chart_bat_t')) + '</h2><img class="rep-chart small" alt="" src="' + img2 + '">';
  h += '<h2>' + esc(t('rep_ev')) + '</h2>';
  h += evs.length ? '<table class="rep-tab"><thead><tr><th>' + esc(t('rep_when')) + '</th><th>' + esc(t('rep_dur')) + '</th><th>' + esc(t('rep_change')) +
    '</th></tr></thead><tbody>' + evs.map((e) => '<tr><td>' + esc(dTm(e.t0)) + '</td><td>' + esc(fmtDur(e.t1 - e.t0)) + '</td><td>' +
    Math.round(e.from) + ' → ' + Math.round(e.to) + ' % (+' + Math.round(e.to - e.from) + ')</td></tr>').join('') + '</tbody></table>'
    : '<p>' + esc(t('rep_noev')) + '</p>';
  if ($('#repDays').checked) {
    const rows = dailyRows(recs, evs, x0, x1, dry);
    h += '<h2>' + esc(t('rep_days')) + '</h2><table class="rep-tab days"><thead><tr><th>' + esc(t('rep_day')) + '</th><th>' + esc(t('rep_min')) +
      '</th><th>' + esc(t('rep_avg')) + '</th><th>' + esc(t('rep_max')) + '</th><th>' + esc(t('rep_dryh')) + '</th><th>' + esc(t('temp')) +
      '</th><th>' + esc(t('rep_w')) + '</th></tr></thead><tbody>' +
      rows.map((r) => '<tr' + (r.n ? '' : ' class="nod"') + '><td>' + esc(dT(r.d)) + '</td><td>' + f0(r.min) + '</td><td>' + f0(r.avg) + '</td><td>' + f0(r.max) +
        '</td><td>' + (r.n ? Math.round(r.dryH) : '–') + '</td><td>' + (r.tmp === null ? '–' : r.tmp.toFixed(1)) + '</td><td>' +
        (r.w ? '💧 ' + r.w : '') + '</td></tr>').join('') + '</tbody></table>';
  }
  h += '<footer>' + esc(t('rep_foot')) + '</footer>';
  $('#report').innerHTML = h;
  return true;
}

function printReport() {
  if (!buildReport()) return;
  const rep = $('#report');
  rep.hidden = false;
  document.body.classList.add('printing');
  const done = () => { document.body.classList.remove('printing'); rep.hidden = true; window.removeEventListener('afterprint', done); };
  window.addEventListener('afterprint', done);
  setTimeout(() => { try { window.print(); } catch (e) { toast(t('rep_e_print'), true); done(); } }, 60);
}

// CSV z logu (středník = Excel v češtině otevře přímo). Sloupec "zaliti": 1 u záznamu, kde začíná zalití.
// Stáhnout .txt: čitelný text (datum, jednotky) poskládaný ze záznamů – nezávisle na tom, jak úsporně je uložený v zařízení.
function downloadTxt() {
  const recs = (logRecs || []).concat(manRecs).sort((a, b) => a.t - b.t);
  const lines = recs.map((r) => {
    const d = new Date(r.t);
    let s = d.getUTCFullYear() + '-' + pad2(d.getUTCMonth() + 1) + '-' + pad2(d.getUTCDate()) + ' ' + pad2(d.getUTCHours()) + ':' + pad2(d.getUTCMinutes()) +
      ' ' + (r.hum === null ? '--' : r.hum) + '% ' + (r.tmp === null ? '--' : String(Math.round(r.tmp * 10) / 10)) + 'C ' + (r.v === null ? '--' : r.v.toFixed(2)) + 'V';
    if (r.raw !== null) s += ' r' + r.raw;
    if (r.st !== null) s += ' s' + r.st + (r.u ? 'u' : '');
    s += ' p' + r.loc;
    if (r.q) s += ' ?';
    if (r.lb) s += ' !';
    if (r.hib) s += ' H';
    if (r.fl) s += ' F';
    if (r.man) s += ' M';
    if (r.note) s += ' # ' + r.note;
    return s;
  });
  const blob = new Blob([lines.join('\r\n') + (lines.length ? '\r\n' : '')], { type: 'text/plain;charset=utf-8' });
  const a = document.createElement('a');
  a.href = URL.createObjectURL(blob);
  a.download = (S ? S.id : 'arbolok') + '.txt';
  document.body.appendChild(a);
  a.click();
  setTimeout(() => { URL.revokeObjectURL(a.href); a.remove(); }, 1000);
}
function downloadCsv() {
  const recs = (logRecs || []).concat(manRecs).sort((a, b) => a.t - b.t);
  const starts = new Set();  // zalití se hledají zvlášť pro každé místo
  new Set(recs.map((r) => r.loc)).forEach((l) => detectWater(recs.filter((r) => r.loc === l), S ? S.cfg.wjmp : 8, S ? S.cfg.wwin : 6)
    .forEach((e) => starts.add(l + '|' + e.t0)));
  const q = (x) => '"' + String(x).replace(/"/g, '""') + '"';
  const iso = (ms) => new Date(ms).toISOString().slice(0, 16).replace('T', ' ');
  const n = (v) => (v === null ? '' : String(v).replace('.', ','));
  const rows = ['cas;misto;vlhkost_pct;teplota_c;baterie_v;adc;ustaleni_ms;neustaleno;cas_pochybny;slaba_baterie;zaliti;vybita_baterie;rucne;poznamka;prelito'];
  for (const r of recs) {
    rows.push([iso(r.t), q(locName(r.loc - 1)), n(r.hum), n(r.tmp), n(r.v), n(r.raw), n(r.st), r.u ? 1 : 0, r.q ? 1 : 0, r.lb ? 1 : 0,
      !r.man && starts.has(r.loc + '|' + r.t) ? 1 : 0, r.hib ? 1 : 0, r.man ? 1 : 0, q(r.note || ''), r.fl ? 1 : 0].join(';'));
  }
  const blob = new Blob(['\ufeff' + rows.join('\r\n') + '\r\n'], { type: 'text/csv;charset=utf-8' });
  const a = document.createElement('a');
  a.href = URL.createObjectURL(blob);
  a.download = (S ? S.id : 'arbolok') + '.csv';
  document.body.appendChild(a);
  a.click();
  setTimeout(() => { URL.revokeObjectURL(a.href); a.remove(); }, 1000);
}

async function setClock(btn) {
  const n = new Date();
  runCmd(btn, '/api/time', { y: n.getFullYear(), mo: n.getMonth() + 1, d: n.getDate(), h: n.getHours(), mi: n.getMinutes(), s: n.getSeconds() });
}

// ============================================================================
//  Start
// ============================================================================
function tick() {
  if (!S) return;
  const d = devNow();
  $('#clock').textContent = d === null ? '--:--:--' : fmtClock(d);
  renderSession();
  if (route === 'settings') renderCal();
  if (route === 'dash') renderHero();
}

function init() {
  const saved = lsGet('lang');
  lang = I18N[saved] ? saved : 'cz';
  if (['24h', '7d', '14d', '30d', 'all', 'custom'].indexOf(range) < 0) range = '24h';
  if (range === '14d') range = '7d';  // dřívější volba
  bind();
  applyI18n();
  show(parseRoute());
  poll();
  setInterval(tick, 1000);
}
if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', init); else init();
})();
