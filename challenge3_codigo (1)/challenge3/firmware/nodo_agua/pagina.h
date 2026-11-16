/**
 * @file pagina.h
 * @brief Tablero de control local servido por el ESP32.
 *
 * No usa librerías externas (CDN) porque la WLAN de las autoridades puede no
 * tener Internet: las gráficas se dibujan con <canvas> a mano.
 * Consulta /api/estado cada 2 s y /api/historia cada 10 s.
 */
#pragma once

const char PAGINA_HTML[] PROGMEM = R"rawliteral(<!doctype html>
<html lang="es"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Tablero Agua - Sabana Centro</title>
<style>
:root{--bg:#0f1720;--card:#18222e;--tx:#e6edf3;--mu:#8b98a5;--ok:#2ea043;--pr:#d29922;--al:#f0883e;--cr:#f85149;--ac:#58a6ff}
*{box-sizing:border-box}body{margin:0;font-family:system-ui,sans-serif;background:var(--bg);color:var(--tx)}
header{display:flex;justify-content:space-between;align-items:center;padding:12px 16px;background:var(--card)}
h1{font-size:18px;margin:0}main{padding:16px;max-width:1100px;margin:auto}
#banner{padding:14px 16px;border-radius:10px;font-weight:600;font-size:18px;margin-bottom:12px}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:10px}
.card{background:var(--card);border-radius:10px;padding:12px}
.k{color:var(--mu);font-size:12px}.v{font-size:24px;font-weight:600;margin-top:4px}
canvas{width:100%;height:160px;background:var(--card);border-radius:10px;margin-top:10px}
button{background:var(--ac);color:#000;border:0;border-radius:8px;padding:10px 14px;font-weight:600;cursor:pointer;margin:4px 4px 0 0}
button.sec{background:#30363d;color:var(--tx)}
#ia{margin-top:12px}small{color:var(--mu)}
</style></head><body>
<header><h1>Monitoreo hídrico · <span id="node">-</span></h1>
<div><button class="sec" onclick="sonidoOn()">Activar sonido</button>
<button class="sec" onclick="salir()">Salir</button></div></header>
<main>
<div id="banner">Cargando...</div>
<button onclick="silenciar()">Silenciar alarma física</button>
<small id="sil"></small>
<div class="grid" style="margin-top:12px">
<div class="card"><div class="k">Nivel del tanque</div><div class="v" id="nivel">-</div></div>
<div class="card"><div class="k">Tasa de cambio</div><div class="v" id="tasa">-</div></div>
<div class="card"><div class="k">Caudal</div><div class="v" id="caudal">-</div></div>
<div class="card"><div class="k">Volumen acumulado</div><div class="v" id="vol">-</div></div>
<div class="card"><div class="k">Temperatura</div><div class="v" id="temp">-</div></div>
<div class="card"><div class="k">Humedad relativa</div><div class="v" id="hum">-</div></div>
<div class="card"><div class="k">Presión</div><div class="v" id="pres">-</div></div>
<div class="card"><div class="k">Radiación solar</div><div class="v" id="rad">-</div></div>
<div class="card"><div class="k">Evaporación potencial</div><div class="v" id="evap">-</div></div>
</div>
<div class="card" id="ia"><div class="k">Recomendación del módulo de IA</div><div id="iatxt">-</div></div>
<canvas id="cNivel"></canvas><canvas id="cCaudal"></canvas><canvas id="cClima"></canvas>
<small>Histórico de los últimos 20 min (un punto cada 10 s). <span id="estado"></span></small>
</main>
<script>
const COL=['#2ea043','#d29922','#f0883e','#f85149'];
let ultimaAlerta=-1,audio=null;
const f=(v,d,u)=>v===null||v===undefined||isNaN(v)?'-':v.toFixed(d)+u;
function sonidoOn(){audio=new (window.AudioContext||window.webkitAudioContext)();beep(200);}
function beep(ms){if(!audio)return;const o=audio.createOscillator();o.frequency.value=880;
o.connect(audio.destination);o.start();setTimeout(()=>o.stop(),ms);}
function notificar(t){beep(600);if(navigator.vibrate)navigator.vibrate([300,100,300]);
if(window.Notification&&Notification.permission==='granted')new Notification('Alerta hídrica',{body:t});
document.title='⚠ '+t;}
async function silenciar(){await fetch('/api/silenciar',{method:'POST'});estado();}
function salir(){fetch('/logout').finally(()=>location.href='/logout');}
async function estado(){
 try{const r=await fetch('/api/estado');const d=await r.json();
 node.textContent=d.node;
 nivel.textContent=f(d.nivel_pct,1,' %');tasa.textContent=f(d.tasa_pct_h,1,' %/h');
 caudal.textContent=f(d.caudal_lpm,2,' L/min');vol.textContent=f(d.volumen_l,1,' L');
 temp.textContent=f(d.temp_c,1,' °C');hum.textContent=f(d.hum_pct,0,' %');
 pres.textContent=f(d.pres_hpa,0,' hPa');rad.textContent=f(d.rad_wm2,0,' W/m²');
 evap.textContent=f(d.evap_mmh,2,' mm/h');iatxt.textContent=d.ia;
 banner.style.background=COL[d.alerta];banner.style.color=d.alerta?'#000':'#fff';
 banner.textContent=d.nombre+' · puntaje '+d.puntaje+' · '+d.causas;
 sil.textContent=d.silenciada?' Alarma sonora silenciada':'';
 document.getElementById('estado').textContent='MQTT: '+(d.mqtt?'conectado':'sin conexión')+' · uptime '+d.uptime_s+' s';
 if(d.alerta>ultimaAlerta&&d.alerta>=2)notificar(d.nombre+': '+d.causas);
 if(d.alerta<2)document.title='Tablero Agua - Sabana Centro';
 ultimaAlerta=d.alerta;}catch(e){banner.textContent='Sin conexión con el nodo';banner.style.background='#30363d';}
}
function graf(id,t,series,titulo){
 const c=document.getElementById(id),W=c.width=c.clientWidth*devicePixelRatio,H=c.height=160*devicePixelRatio,x=c.getContext('2d');
 x.scale(devicePixelRatio,devicePixelRatio);const w=W/devicePixelRatio,h=160,p=34;
 x.fillStyle='#8b98a5';x.font='12px system-ui';x.fillText(titulo,p,14);
 if(!t.length)return;const t0=Math.min(...t),t1=Math.max(0,...t);
 series.forEach((s,k)=>{const v=s.d.filter(a=>a!==null);if(!v.length)return;
  let lo=Math.min(...v),hi=Math.max(...v);if(hi-lo<1e-3){hi+=1;lo-=1;}
  x.strokeStyle=s.c;x.lineWidth=2;x.beginPath();let first=true;
  s.d.forEach((a,i)=>{if(a===null)return;const X=p+(t[i]-t0)/(t1-t0||1)*(w-p-8),Y=h-18-(a-lo)/(hi-lo)*(h-44);
   first?x.moveTo(X,Y):x.lineTo(X,Y);first=false;});x.stroke();
  x.fillStyle=s.c;x.fillText(s.n+' ('+lo.toFixed(1)+'–'+hi.toFixed(1)+')',p+k*150,h-4);});
}
async function historia(){
 try{const d=await(await fetch('/api/historia')).json();
 graf('cNivel',d.t,[{d:d.nivel,c:'#58a6ff',n:'Nivel %'}],'Nivel del tanque');
 graf('cCaudal',d.t,[{d:d.caudal,c:'#3fb950',n:'Caudal L/min'}],'Caudal');
 graf('cClima',d.t,[{d:d.temp,c:'#f0883e',n:'Temp °C'},{d:d.hum,c:'#a5d6ff',n:'HR %'},{d:d.evap,c:'#d2a8ff',n:'Ep mm/h'}],'Clima y evaporación');
 }catch(e){}
}
if(window.Notification&&Notification.permission==='default')Notification.requestPermission();
estado();historia();setInterval(estado,2000);setInterval(historia,10000);
</script></body></html>)rawliteral";
