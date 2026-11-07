// The WiFi web app. The car's starter page (app_page.h) sets window.EMOCAR_APP to the
// folder this file came from and loads it, so the page runs on the car's own origin:
// /status and /anim/<n> are plain same-origin fetches. The GIF → .qgif converter is
// server/qgif compiled to WebAssembly (qgif.wasm), the same code as the PC server.
'use strict';
(() => {

const BASE = window.EMOCAR_APP || new URL('.', document.currentScript.src).href;

// Index = enum CarState in the firmware = the car's slot number. Append only.
const STATES = [
  { name: 'NORMAL', emoji: '\u{1F642}' },
  { name: 'FAST',   emoji: '\u{1F62E}' },
  { name: 'DIZZY',  emoji: '\u{1F974}' },
  { name: 'HAPPY',  emoji: '\u{1F970}' },
];
const $ = id => document.getElementById(id);

let car = null;            // last /status
let wasmReady = false;
const players = [];        // one per state card
const shownBytes = [];     // per slot: the size the card is showing, to spot changes
const slotAnims = [];      // per slot: the decoded animation the card plays
const slotRaw = [];        // per slot: its .qgif bytes, the editor's source until a file is picked

// ---------------- loading ----------------
function loadScript(src) {
  return new Promise((ok, bad) => {
    const s = document.createElement('script');
    s.src = src;
    s.onload = ok;
    s.onerror = () => bad(new Error(src));
    document.head.appendChild(s);
  });
}

async function startWasm() {
  await loadScript(BASE + 'wasm_exec.js');
  const go = new Go();
  const url = BASE + 'qgif.wasm';
  let inst;
  try {
    inst = (await WebAssembly.instantiateStreaming(fetch(url), go.importObject)).instance;
  } catch {   // wrong MIME type on some servers: fall back to bytes
    const buf = await (await fetch(url)).arrayBuffer();
    inst = (await WebAssembly.instantiate(buf, go.importObject)).instance;
  }
  go.run(inst);
  for (let i = 0; i < 50 && typeof window.qgifConvert !== 'function'; i++)
    await new Promise(r => setTimeout(r, 20));
  if (typeof window.qgifConvert !== 'function') throw new Error('qgif.wasm did not start');
}

// ---------------- drawing ----------------
// a = qgifDecode() result: {w, h, format, delays, frames}. Pixels as the display shows them.
function paint(canvas, a, fi) {
  const { w, h, format } = a, bm = a.frames[fi];
  if (canvas.width !== w || canvas.height !== h) { canvas.width = w; canvas.height = h; }
  const ctx = canvas.getContext('2d');
  const img = ctx.createImageData(w, h), d = img.data;
  for (let y = 0, p = 0; y < h; y++) {
    for (let x = 0; x < w; x++, p += 4) {
      let r, g, b;
      if (format === 'mono1') {                       // bit set = dark, MSB = leftmost
        r = g = b = bm[y * ((w + 7) >> 3) + (x >> 3)] & (0x80 >> (x & 7)) ? 0 : 255;
      } else if (format === 'gray4') {                // high nibble = leftmost
        const v = bm[y * ((w + 1) >> 1) + (x >> 1)];
        r = g = b = ((x & 1) ? v & 15 : v >> 4) * 17;
      } else {                                        // rgb565, big-endian
        const i = (y * w + x) * 2, v = bm[i] << 8 | bm[i + 1];
        r = (v >> 11) * 255 / 31; g = (v >> 5 & 63) * 255 / 63; b = (v & 31) * 255 / 31;
      }
      d[p] = r; d[p + 1] = g; d[p + 2] = b; d[p + 3] = 255;
    }
  }
  ctx.putImageData(img, 0, 0);
}

function player(canvas) {
  let anim = null, i = 0, t = null;
  const step = () => {
    paint(canvas, anim, i);
    t = setTimeout(step, Math.max(20, anim.delays[i] || 100));
    i = (i + 1) % anim.frames.length;
  };
  return {
    set(a) {
      clearTimeout(t); anim = a; i = 0;
      if (a && a.frames.length) step();
    },
  };
}

// ---------------- page ----------------
function buildPage() {
  document.body.innerHTML = `
<main>
  <header><h1>emocar</h1><span id="lang"></span></header>
  <div id="carline"><span id="dot"></span><span id="cartext">looking for the car…</span></div>
  <div class="hint" id="wasmline">loading the converter…</div>

  <h2>On the car</h2>
  <div class="states" id="states"></div>
  <p class="hint">Tap a state to change its face</p>
</main>

<div id="tune">
  <div id="tunebox">
    <div class="row"><b id="tname"></b>
      <label id="pick">Choose GIF<input type="file" id="file" hidden></label></div>
    <div id="tinfo"></div>
    <div class="oled"><canvas id="tcv"></canvas></div>
    <div id="knobs">
      <div class="row mono gifonly"><label>threshold</label>
        <input type="range" id="kthr" min="1" max="254" value="128"><span id="kthrv">128</span></div>
      <div class="row"><span class="mono"><label>invert</label><input type="checkbox" id="kinv"></span>
        <span class="mono gifonly"><label class="tight">dither</label>
        <select id="kdither">
          <option value="none">none</option><option value="bayer">bayer</option>
          <option value="floyd">floyd</option>
        </select></span>
        <label class="tight">scale</label>
        <select id="kscale">
          <option value="fit">fit</option><option value="fit_width">fit_width</option>
          <option value="fit_height">fit_height</option><option value="stretch">stretch</option>
        </select></div>
      <div class="row"><label>zoom</label>
        <input type="range" id="kzoom" min="50" max="400" step="5" value="100"><span id="kzoomv">100%</span></div>
      <div class="row"><label>pan</label>
        <span class="axis">x</span> <input type="number" id="kpanx" value="0" step="1">
        <span class="axis">y</span> <input type="number" id="kpany" value="0" step="1">
        <button id="kreset" type="button">reset</button></div>
      <div class="row hint">drag the preview to pan (or type x/y: + is right/down) · scroll to zoom · double-click resets</div>
    </div>
    <div class="row">
      <button class="primary" id="apply" disabled>Send to car</button>
      <button id="close">Close</button>
      <span id="msg"></span>
    </div>
  </div>
</div>`;

  $('states').innerHTML = STATES.map((s, i) => `
    <div class="card" id="card${i}">
      <h3>${s.emoji} ${s.name}</h3>
      <div class="oled"><canvas id="cv${i}"></canvas></div>
      <div class="tag" id="tag${i}">–</div>
    </div>`).join('');
  STATES.forEach((_, i) => {
    players[i] = player($('cv' + i));
    $('card' + i).onclick = () => openTune(i);
  });
  wireTune();
}

// ---------------- car ----------------
async function poll() {
  try {
    const r = await fetch('/status', { cache: 'no-store' });
    if (!r.ok) throw new Error(r.status);
    const first = !car;
    car = await r.json();
    if (first) {
      document.documentElement.style.setProperty('--oled-ar', `${car.w} / ${car.h}`);
      $('kpanx').min = -2 * car.w; $('kpanx').max = 2 * car.w;
      $('kpany').min = -2 * car.h; $('kpany').max = 2 * car.h;
    }
    $('dot').classList.add('on');
    $('cartext').textContent = tr('{host} — fw {fw}, state {state}',
                                  { host: location.hostname, fw: car.fw, state: car.state });
    car.slots.forEach((sl, i) => {
      if (!$('tag' + i)) return;
      $('tag' + i).textContent = sl.file
        ? tr('{frames} frames · {kb} KB', { frames: sl.frames, kb: (sl.bytes / 1024).toFixed(1) })
        : tr('built-in');
      $('card' + i).classList.toggle('live', String(car.state).toUpperCase() === STATES[i].name);
      const size = sl.file ? sl.bytes : 0;
      if (wasmReady && shownBytes[i] !== size) loadSlot(i);   // changed elsewhere (USB, another tab)
    });
  } catch {
    $('dot').classList.remove('on');
    $('cartext').textContent = tr('car not answering — is it on this WiFi?');
  }
}

const slotLoads = [];
function loadSlot(i) {
  if (slotLoads[i]) return slotLoads[i];
  const sl = car && car.slots[i];
  shownBytes[i] = sl && sl.file ? sl.bytes : 0;
  slotLoads[i] = (async () => {
    try {
      const r = await fetch(`/anim/${i}`, { cache: 'no-store' });
      if (!r.ok) throw new Error(await r.text());
      const raw = new Uint8Array(await r.arrayBuffer());
      const a = qgifDecode(raw);
      if (a.error) throw new Error(a.error);
      slotRaw[i] = raw;
      slotAnims[i] = a;
      players[i].set(a);
    } catch (e) {
      shownBytes[i] = undefined;
      $('tag' + i).textContent = tr('failed') + ': ' + e.message;
    } finally { slotLoads[i] = null; }
  })();
  return slotLoads[i];
}

// ---------------- editor ----------------
let sel = 0;               // state being edited
// The editor's source: the state's face as the car has it (.qgif), or a picked GIF/.qgif.
// Never stored anywhere; gone with the page.
let src = null;
let srcName = '';
let srcIsGif = false;      // threshold + dither only mean something for a GIF
let dirty = false;         // Send only once something changed
let out = null;            // the .qgif the preview shows = what Send sends
let panX = 0, panY = 0;
let tplayer = null;

function setMsg(text, cls) { $('msg').textContent = text; $('msg').className = cls || ''; }

function showKnobs() {
  $('knobs').style.display = src ? '' : 'none';
  for (const el of document.querySelectorAll('#knobs .mono'))
    el.style.display = car.format === 'mono1' ? '' : 'none';
  if (!srcIsGif) for (const el of document.querySelectorAll('#knobs .gifonly')) el.style.display = 'none';
}

function resetKnobs() {
  $('kthr').value = 128; $('kthrv').textContent = '128';
  $('kinv').checked = false; $('kdither').value = 'none'; $('kscale').value = 'fit';
  $('kzoom').value = 100; $('kzoomv').textContent = '100%';
  panX = panY = 0; $('kpanx').value = 0; $('kpany').value = 0;
}

function setSource(bytes, name, isGif) {
  src = bytes; srcName = name; srcIsGif = isGif;
  resetKnobs();
  showKnobs();
}

// Every open starts from what this state plays now; a file picked for another state is dropped.
function openTune(i) {
  if (!car) return;
  sel = i;
  $('tname').textContent = `${STATES[i].emoji} ${STATES[i].name}`;
  setMsg('');
  out = null; dirty = false; $('apply').disabled = true;
  setSource(slotRaw[i] || null, tr('now on the car'), false);
  tplayer.set(slotAnims[i] || null);
  $('tinfo').textContent = slotAnims[i]
    ? tr('{name} · {frames} frames · {kb} KB',
         { name: srcName, frames: slotAnims[i].frames.length, kb: (slotRaw[i].length / 1024).toFixed(1) })
    : '';
  $('tune').classList.add('open');
}

function convertNow() {
  if (!src || !wasmReady || !car) return;
  const t0 = performance.now();
  const r = qgifConvert(src, {
    w: car.w, h: car.h, format: car.format,
    threshold: +$('kthr').value, invert: $('kinv').checked,
    scale: $('kscale').value, dither: $('kdither').value,
    zoom: +$('kzoom').value, panX, panY,
  });
  if (r.error) { out = null; $('apply').disabled = true; setMsg(r.error, 'bad'); return; }
  out = r.qgif;
  const a = qgifDecode(out);
  tplayer.set(a);
  $('tinfo').textContent = tr('{name} · {frames} frames · {kb} KB',
    { name: srcName, frames: r.frames, kb: (out.length / 1024).toFixed(1) });
  $('apply').disabled = !dirty;
  console.log(`qgif convert ${Math.round(performance.now() - t0)} ms`);
}

let debounce = null;
function knobChanged() {
  dirty = true;
  $('kthrv').textContent = $('kthr').value;
  $('kzoomv').textContent = $('kzoom').value + '%';
  clearTimeout(debounce);
  debounce = setTimeout(convertNow, 150);
}

// Pan has one setter shared by dragging, typing and reset. Panel pixels, + = right/down.
const clampPan = (v, d) => Math.min(2 * d, Math.max(-2 * d, Math.round(v) || 0));
function setPan(x, y, typed) {
  panX = clampPan(x, car.w);
  panY = clampPan(y, car.h);
  if (!typed) { $('kpanx').value = panX; $('kpany').value = panY; }
  knobChanged();
}
function resetView() { $('kzoom').value = 100; setPan(0, 0); }

function wireTune() {
  tplayer = player($('tcv'));
  $('kthr').oninput = knobChanged; $('kinv').onchange = knobChanged;
  $('kscale').onchange = knobChanged; $('kdither').onchange = knobChanged;
  $('kzoom').oninput = knobChanged;
  $('kpanx').oninput = $('kpany').oninput = () => setPan(+$('kpanx').value, +$('kpany').value, true);
  $('kpanx').onchange = $('kpany').onchange = () => { $('kpanx').value = panX; $('kpany').value = panY; };
  $('kreset').onclick = resetView;

  const cv = $('tcv');
  let drag = null;
  cv.ondblclick = resetView;
  cv.onwheel = e => {
    if (!src) return;
    e.preventDefault();
    $('kzoom').value = Math.min(400, Math.max(50, +$('kzoom').value + (e.deltaY < 0 ? 10 : -10)));
    knobChanged();
  };
  cv.onpointerdown = e => {
    if (!src) return;
    e.preventDefault();
    try { cv.setPointerCapture(e.pointerId); } catch {}
    drag = { x: e.clientX, y: e.clientY, px: panX, py: panY };
  };
  cv.onpointermove = e => {
    if (!drag) return;
    setPan(drag.px + (e.clientX - drag.x) * car.w / cv.clientWidth,
           drag.py + (e.clientY - drag.y) * car.h / cv.clientHeight);
  };
  cv.onpointerup = cv.onpointercancel = () => { drag = null; };

  $('file').onchange = async () => {
    const f = $('file').files[0];
    $('file').value = '';
    if (!f) return;
    if (!wasmReady) { setMsg(tr('loading the converter…'), 'bad'); return; }
    const bytes = new Uint8Array(await f.arrayBuffer());
    const isGif = String.fromCharCode(...bytes.subarray(0, 4)) === 'GIF8';
    setMsg('');
    // A .qgif (v2, or the legacy headerless v1) is laid out again like the car's face.
    if (!isGif && qgifDecode(bytes).error) {
      setMsg(tr('{name} is not a GIF or .qgif', { name: f.name }), 'bad');
      return;
    }
    setSource(bytes, f.name, isGif);
    dirty = true;
    convertNow();
  };

  const close = () => $('tune').classList.remove('open');
  $('close').onclick = close;
  $('tune').onclick = e => { if (e.target === $('tune')) close(); };

  $('apply').onclick = async () => {
    if (!out) return;
    $('apply').disabled = true;
    setMsg(tr('sending…'));
    try {
      const r = await fetch(`/anim/${sel}`, {
        method: 'POST', headers: { 'Content-Type': 'application/octet-stream' }, body: out,
      });
      if (!r.ok) throw new Error(await r.text());
      const j = await r.json();
      setMsg(tr('on the car — {frames} frames, {kb} KB',
                { frames: j.frames, kb: (j.bytes / 1024).toFixed(1) }), 'ok');
      await poll();
      loadSlot(sel);       // same size as before would not show as a change in /status
    } catch (e) { setMsg(e.message || tr('failed'), 'bad'); }
    $('apply').disabled = false;
  };
}

// ---------------- boot ----------------
(async () => {
  const css = document.createElement('link');
  css.rel = 'stylesheet'; css.href = BASE + 'app.css';
  document.head.appendChild(css);
  try { await loadScript(BASE + 'i18n.js'); } catch { window.tr = (s, v) => s.replace(/\{(\w+)\}/g, (m, k) => (v && k in v ? v[k] : m)); window.i18nApply = () => {}; }
  buildPage();
  i18nApply();
  await poll();
  setInterval(poll, 5000);
  try {
    await startWasm();
    wasmReady = true;
    $('wasmline').textContent = '';
    if (car) for (let i = 0; i < car.slots.length && i < STATES.length; i++) await loadSlot(i);
  } catch (e) {
    $('wasmline').textContent = tr('converter failed to load: {err}', { err: e.message });
  }
})();

})();
