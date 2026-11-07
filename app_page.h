#pragma once

// app_page.h — the starter page the car serves at / on the home WiFi (net.cpp).
// It only loads the web app from GitHub Pages, the build made for this firmware's tag,
// so the app runs on the car's own origin: no CORS, no mixed content.
// Dev: open http://<car>/?app=http://<pc>:8080/app/ once — the browser remembers it,
// and ?app= with nothing after it goes back to Pages.

#include "config.h"

static const char APP_HTML[] PROGMEM =
"<!doctype html><html><head><meta charset=utf-8>"
"<meta name=viewport content='width=device-width,initial-scale=1'><title>emocar</title></head>"
"<body style='margin:0;background:#101216;color:#d6dae2;font:14px/1.5 system-ui,sans-serif'>"
"<p id=boot style='padding:16px'>emocar " FW_VERSION " &middot; loading the app&hellip;</p>"
"<script>"
"var b='https://sangnnbs.github.io/emocar/app/" FW_VERSION "/';"
"try{var q=new URLSearchParams(location.search).get('app');"
"if(q!==null){q?localStorage.setItem('emocar.app',q):localStorage.removeItem('emocar.app')}"
"b=localStorage.getItem('emocar.app')||b}catch(e){}"
"if(!/\\/$/.test(b))b+='/';"
"window.EMOCAR_APP=b;"
"var s=document.createElement('script');s.src=b+'app.js';"
"s.onerror=function(){document.getElementById('boot').textContent="
"'Could not load the app from '+b+' \\u2014 is this phone or PC online?'};"
"document.head.appendChild(s);"
"</script></body></html>";
