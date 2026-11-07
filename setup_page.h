#pragma once

// setup_page.h — the WiFi setup page, served by the car on its hotspot (net.cpp).
// The phone has no internet here: no fonts, scripts or images from outside.

const char SETUP_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>emocar setup</title>
<style>
:root{--bg:#EDF1F2;--card:#fff;--ink:#0E1518;--ink2:#46595F;--rule:#CEDADE;--acc:#00849E;--accs:#DCF0F4;
--ok:#1A6138;--okb:#E2F2E8;--bad:#8E2119;--badb:#FBE7E5;--work:#075E71;--workb:#DCF0F4}
@media (prefers-color-scheme:dark){:root{--bg:#090F12;--card:#111A1E;--ink:#E6EEF1;--ink2:#9CAFB7;--rule:#223036;
--acc:#3FD3EF;--accs:#0E2F38;--ok:#7CD3A2;--okb:#0C2418;--bad:#F79289;--badb:#2E100C;--work:#8CDCEC;--workb:#0E2F38}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--ink);font:16px/1.5 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
main{max-width:28rem;margin:0 auto;padding:1.2rem 1rem 3rem;display:flex;flex-direction:column;gap:1rem}
h1{font-size:1.4rem;margin:0}
p{margin:0;color:var(--ink2)}
.card{background:var(--card);border:1px solid var(--rule);border-radius:10px;padding:1rem;display:flex;flex-direction:column;gap:.6rem}
.row{display:flex;justify-content:space-between;align-items:center;gap:.5rem}
#list{display:flex;flex-direction:column;gap:.4rem}
.net{display:flex;justify-content:space-between;gap:.6rem;width:100%;text-align:left;font:inherit;color:var(--ink);
background:var(--bg);border:1px solid var(--rule);border-radius:8px;padding:.65rem .8rem;cursor:pointer}
.net span:first-child{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.net.on{border-color:var(--acc);background:var(--accs)}
.meta{color:var(--ink2);white-space:nowrap;font-size:.9rem}
label{font-size:.85rem;font-weight:600}
input[type=text],input[type=password]{width:100%;font:inherit;color:var(--ink);background:var(--bg);
border:1px solid var(--rule);border-radius:8px;padding:.65rem .8rem}
input:focus,button:focus-visible{outline:2px solid var(--acc);outline-offset:1px}
.show{display:flex;align-items:center;gap:.4rem;font-size:.9rem;color:var(--ink2);font-weight:400}
.go{font:inherit;font-weight:600;color:#fff;background:var(--acc);border:0;border-radius:8px;padding:.8rem;cursor:pointer}
.go:disabled{opacity:.5}
.link{font:inherit;font-size:.9rem;color:var(--acc);background:none;border:0;padding:0;cursor:pointer}
#msg{display:none;border-radius:8px;padding:.8rem;border-left:3px solid}
#msg.ok{display:block;background:var(--okb);color:var(--ok)}
#msg.bad{display:block;background:var(--badb);color:var(--bad)}
#msg.work{display:block;background:var(--workb);color:var(--work)}
small{color:var(--ink2)}
</style>
</head>
<body>
<main>
  <h1>Connect the car to your Wi-Fi</h1>
  <p>Pick your Wi-Fi and type its password. The car only works with 2.4&nbsp;GHz Wi-Fi.</p>

  <div class="card">
    <div class="row"><span id="scanmsg">Looking for Wi-Fi…</span><button class="link" id="again" type="button">Scan again</button></div>
    <div id="list"></div>
  </div>

  <form class="card" id="form">
    <label for="ssid">Wi-Fi name</label>
    <input type="text" id="ssid" maxlength="32" autocomplete="off" autocapitalize="none" autocorrect="off" spellcheck="false" required>
    <label for="pass">Password</label>
    <input type="password" id="pass" maxlength="64" autocomplete="off" autocapitalize="none" autocorrect="off" spellcheck="false">
    <label class="show"><input type="checkbox" id="showpw"> Show password</label>
    <button class="go" id="go" type="submit">Connect</button>
  </form>

  <div id="msg"></div>
  <small>Leave the password empty only for an open Wi-Fi.</small>
</main>
<script>
const $ = id => document.getElementById(id);
const sleep = ms => new Promise(f => setTimeout(f, ms));
const LOST = 'Your phone left the setup hotspot. That is normal while the car joins your Wi-Fi. ' +
  'Check the car\u2019s screen: it shows its IP when it worked. If it did not, scan the QR again and retry.';

function say(text, kind) { const m = $('msg'); m.textContent = text; m.className = kind; }
function bars(r) { return r > -55 ? '▂▄▆█' : r > -67 ? '▂▄▆' : r > -78 ? '▂▄' : '▂'; }

async function scan(again) {
  $('list').textContent = '';
  $('scanmsg').textContent = 'Looking for Wi-Fi…';
  try {
    for (;;) {
      const j = await (await fetch('/scan' + (again ? '?again=1' : ''))).json();
      again = false;
      if (!j.busy) return show(j.nets);
      await sleep(700);
    }
  } catch (e) {
    $('scanmsg').textContent = 'Could not scan. Type the name below.';
  }
}

function show(nets) {
  const best = {};
  nets.forEach(n => { if (n.ssid && (!best[n.ssid] || best[n.ssid].rssi < n.rssi)) best[n.ssid] = n; });
  const list = Object.values(best).sort((a, b) => b.rssi - a.rssi);
  $('scanmsg').textContent = list.length ? 'Tap your Wi-Fi:' : 'No Wi-Fi found. Type the name below.';
  list.forEach(n => {
    const b = document.createElement('button');
    b.type = 'button';
    b.className = 'net';
    const name = document.createElement('span');
    name.textContent = n.ssid;
    const meta = document.createElement('span');
    meta.className = 'meta';
    meta.textContent = (n.open ? 'open ' : '🔒 ') + bars(n.rssi);
    b.append(name, meta);
    b.onclick = () => {
      document.querySelectorAll('.net').forEach(x => x.classList.remove('on'));
      b.classList.add('on');
      $('ssid').value = n.ssid;
      $('pass').value = '';
      if (!n.open) $('pass').focus();
    };
    $('list').append(b);
  });
}

function busy(on) { $('go').disabled = on; $('ssid').disabled = on; $('pass').disabled = on; }

async function watch(ssid) {
  let misses = 0;
  for (;;) {
    await sleep(1000);
    let j;
    try { j = await (await fetch('/join')).json(); misses = 0; }
    catch (e) { if (++misses === 3) say(LOST, 'work'); continue; }
    if (j.state === 'ok') {
      say('Done! The car is on “' + ssid + '” (IP ' + j.ip + '). The setup hotspot closes in a few seconds ' +
          'and your phone goes back to your own Wi-Fi. You can close this page.', 'ok');
      return;
    }
    if (j.state === 'fail') {
      const why = {
        notfound: 'Can’t find “' + ssid + '”. Check the name, and that it is a 2.4 GHz Wi-Fi.',
        refused: '“' + ssid + '” refused the password. Check it and try again.',
        timeout: 'No answer from “' + ssid + '”. Wrong password, a 5 GHz-only Wi-Fi, or too far away.'
      };
      say(why[j.why] || why.timeout, 'bad');
      busy(false);
      return;
    }
  }
}

$('form').onsubmit = async e => {
  e.preventDefault();
  const ssid = $('ssid').value;
  const body = new URLSearchParams({ ssid, pass: $('pass').value });
  busy(true);
  say('Connecting to “' + ssid + '”… this takes up to 15 seconds.', 'work');
  try {
    const r = await fetch('/save', { method: 'POST', body });
    const j = await r.json();
    if (!r.ok) { say(j.error, 'bad'); busy(false); return; }
  } catch (e) {
    say(LOST, 'work');
  }
  watch(ssid);
};

$('showpw').onchange = e => { $('pass').type = e.target.checked ? 'text' : 'password'; };
$('again').onclick = () => scan(true);
scan(false);
</script>
</body>
</html>
)HTML";
