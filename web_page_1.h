// Coco Emote — control page served by the keychain itself (captive portal)
#pragma once

const char INDEX_HTML[] PROGMEM = R"COCO(<!doctype html>
<html lang="en"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>Coco Emote</title>
<style>
:root{--bg:#fff7fb;--card:#ffffff;--ink:#2a2233;--muted:#8a7f96;--accent:#ff5fa2;--accent2:#7c5cff;--line:#f0e3ee;--eye:#2a2233}
@media (prefers-color-scheme:dark){:root{--bg:#141019;--card:#1f1927;--ink:#f5eefa;--muted:#a597b3;--line:#2e2538;--eye:#7fe7ff}}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
body{margin:0;background:var(--bg);color:var(--ink);font:16px/1.4 system-ui,-apple-system,Segoe UI,Roboto,sans-serif;padding:20px 16px 32px}
.wrap{max-width:440px;margin:0 auto}
header{display:flex;align-items:center;gap:14px;margin-bottom:18px}
.logo{width:58px;height:40px;border-radius:14px;background:#000;display:flex;align-items:center;justify-content:center;gap:7px}
.logo i{width:17px;height:17px;border-radius:5px;background:#7fe7ff;animation:blink 4s infinite}
@keyframes blink{0%,46%,50%,100%{transform:scaleY(1)}48%{transform:scaleY(.1)}}
h1{font-size:22px;margin:0;letter-spacing:.2px}
.sub{color:var(--muted);font-size:13px}
.dot{display:inline-block;width:8px;height:8px;border-radius:50%;background:#3ccf7a;margin-right:6px;vertical-align:1px}
.dot.off{background:#e05656}
h2{font-size:13px;text-transform:uppercase;letter-spacing:1px;color:var(--muted);margin:22px 4px 10px}
.grid{display:grid;grid-template-columns:repeat(3,1fr);gap:10px}
button{font:inherit;color:inherit;border:1.5px solid var(--line);background:var(--card);border-radius:18px;padding:14px 6px;cursor:pointer;transition:transform .12s,border-color .2s,box-shadow .2s}
button:active{transform:scale(.95)}
.mood .e{font-size:30px;display:block;line-height:1.1}
.mood .n{font-size:13px;color:var(--muted)}
.mood.on{border-color:var(--accent);box-shadow:0 6px 18px -8px var(--accent)}
.mood.on .n{color:var(--accent);font-weight:600}
.row{display:grid;grid-template-columns:repeat(4,1fr);gap:10px}
.act{font-size:14px;padding:12px 4px}
.card{background:var(--card);border:1.5px solid var(--line);border-radius:18px;padding:14px 16px}
input[type=range]{width:100%;accent-color:var(--accent2);margin:6px 0 0}
.sleep{width:100%;margin-top:22px;background:transparent;color:var(--muted)}
footer{text-align:center;color:var(--muted);font-size:12px;margin-top:20px}
</style></head>
<body><div class="wrap">
<header>
  <div class="logo"><i></i><i></i></div>
  <div><h1>Coco Emote</h1><div class="sub"><span id="dot" class="dot"></span><span id="name">connecting…</span></div></div>
</header>

<h2>Mood</h2>
<div class="grid" id="moods"></div>

<h2>Do something cute</h2>
<div class="row">
  <button class="act" onclick="act('blink')">😑<br>Blink</button>
  <button class="act" onclick="act('wink')">😉<br>Wink</button>
  <button class="act" onclick="act('hop')">🐇<br>Hop</button>
  <button class="act" onclick="act('look')">👀<br>Look</button>
</div>

<h2>Brightness</h2>
<div class="card"><input id="br" type="range" min="10" max="255" onchange="bright(this.value)"></div>

<button class="sleep" onclick="if(confirm('Put Coco to sleep? Turn it off and on again to wake it.'))post('/api/sleep')">💤 Put to sleep</button>
<footer>WiFi turns itself off after 3 min of no use · turn Coco off and on to get this page back</footer>
</div>
<script>
const M=[["idle","😌","Chill"],["happy","😊","Happy"],["sad","🥺","Sad"],["angry","😠","Angry"],["sleepy","😴","Sleepy"],["love","😍","Love"],["surprised","😮","Surprised"]];
const g=document.getElementById('moods');
M.forEach(([k,e,n])=>{const b=document.createElement('button');b.className='mood';b.id='m_'+k;
b.innerHTML='<span class="e">'+e+'</span><span class="n">'+n+'</span>';b.onclick=()=>post('/api/mood?m='+k);g.appendChild(b);});
let dragging=false;const br=document.getElementById('br');
br.addEventListener('pointerdown',()=>dragging=true);br.addEventListener('pointerup',()=>dragging=false);
function render(s){document.getElementById('name').textContent=s.name;document.getElementById('dot').className='dot';
M.forEach(([k])=>document.getElementById('m_'+k).classList.toggle('on',k===s.mood));if(!dragging)br.value=s.bright;}
function off(){document.getElementById('dot').className='dot off';document.getElementById('name').textContent='not connected';}
function post(u){return fetch(u,{method:'POST'}).then(r=>r.json()).then(render).catch(off);}
function act(a){post('/api/action?a='+a);}
function bright(v){post('/api/bright?v='+v);}
function poll(){fetch('/api/state').then(r=>r.json()).then(render).catch(off);}
poll();setInterval(poll,4000);
</script></body></html>)COCO";
