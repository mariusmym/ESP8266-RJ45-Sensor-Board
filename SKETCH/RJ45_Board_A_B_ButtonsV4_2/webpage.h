#pragma once
#include <Arduino.h>

// Dashboard page served at "/". Stored in flash (PROGMEM), no external files/CDNs,
// so it works even if the LAN has no internet. Data comes from /api/data and /api/history.
static const char INDEX_HTML[] PROGMEM = R"rawliteral(<!doctype html>
<html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>RJ45 Board</title>
<style>
:root{--bgh:#1d2021;--bg:#282828;--bgs:#32302f;--bg1:#3c3836;--bg2:#504945;--bg3:#665c54;
--fg0:#fbf1c7;--fg:#ebdbb2;--fg2:#d5c4a1;--fg3:#bdae93;--fg4:#a89984;--gray:#928374;
--green:#b8bb26;--aqua:#8ec07c;--yellow:#fabd2f;--orange:#fe8019;--red:#fb4934;--blue:#83a598;--purple:#d3869b}
*{box-sizing:border-box;margin:0;padding:0}
body{background:var(--bgh) radial-gradient(ellipse at top,rgba(142,192,124,.07),transparent 60%) no-repeat;
color:var(--fg);font:14px 'JetBrains Mono','SF Mono','Cascadia Code',monospace;min-height:100vh;padding:24px 16px}
.wrap{max-width:1100px;margin:0 auto}
header{display:flex;flex-wrap:wrap;align-items:flex-end;justify-content:space-between;gap:12px}
h1{color:var(--fg0);font-weight:900;text-transform:uppercase;letter-spacing:-.03em;font-size:28px;
text-shadow:0 0 14px rgba(142,192,124,.4)}
.sub{color:var(--fg4);font-size:11px;letter-spacing:.15em;text-transform:uppercase;margin-top:4px}
.stat{display:flex;gap:18px;flex-wrap:wrap;font-size:12px;color:var(--fg4)}
.stat b{color:var(--fg0)}
.dot{display:inline-block;width:8px;height:8px;border-radius:50%;background:var(--green);margin-right:6px;
vertical-align:middle;box-shadow:0 0 6px var(--green)}
.dot.off{background:var(--red);box-shadow:0 0 6px var(--red)}
.label{font-size:11px;letter-spacing:.18em;text-transform:uppercase;color:var(--fg4);margin:24px 0 10px}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(200px,1fr));gap:12px}
.card{position:relative;background:var(--bgs);border:1px solid var(--bg2);border-radius:4px;padding:14px;transition:border-color .15s}
.card::before{content:"";position:absolute;left:-1px;right:-1px;top:-1px;height:2px;background:var(--aqua);
opacity:0;transition:opacity .15s;border-radius:4px 4px 0 0}
.card:hover{border-color:var(--bg3)}.card:hover::before{opacity:1}
.card.alarm{border-color:var(--red)}.card.alarm::before{background:var(--red);opacity:1}
.card.dim{opacity:.45}
.head{display:flex;justify-content:space-between;align-items:center}
.name{color:var(--fg0);font-weight:900;font-size:13px;letter-spacing:.06em}
.tag{font-size:10px;font-weight:700;padding:2px 6px;border-radius:2px;text-transform:uppercase;letter-spacing:.08em;
background:var(--bg1);color:var(--fg4)}
.tag.ok{background:var(--green);color:var(--bgh)}
.tag.bad{background:var(--red);color:var(--fg0)}
.tag.ack{background:var(--orange);color:var(--bgh)}
.temp{font-size:40px;font-weight:700;color:var(--fg0);margin:10px 0 6px;letter-spacing:-.03em;font-variant-numeric:tabular-nums}
.temp small{font-size:15px;color:var(--fg4);font-weight:400}
.tr{font-size:16px;margin-left:6px}.tr.up{color:var(--orange)}.tr.dn{color:var(--blue)}
.meta{font-size:11px;color:var(--fg4);display:grid;grid-template-columns:auto 1fr;gap:3px 10px}
.meta span:nth-child(even){color:var(--fg2);text-align:right;overflow:hidden;text-overflow:ellipsis}
.swatch{display:inline-block;width:10px;height:3px;margin-right:8px;vertical-align:middle}
.banner{display:none;align-items:center;justify-content:space-between;gap:12px;margin-top:20px;padding:12px 14px;
background:rgba(251,73,52,.12);border:1px solid var(--red);border-radius:4px;color:var(--fg0);font-weight:700}
.banner.on{display:flex;animation:pulse 1.2s infinite}
@keyframes pulse{50%{background:rgba(251,73,52,.25)}}
button{font:inherit;cursor:pointer;border-radius:4px;transition:all .15s}
.btn{font-weight:700;font-size:12px;text-transform:uppercase;letter-spacing:.05em;background:var(--green);color:var(--bgh);
border:0;padding:7px 14px}.btn:hover{background:var(--aqua)}
.chartbox{background:var(--bgs);border:1px solid var(--bg2);border-radius:4px;padding:14px}
canvas{width:100%;height:280px;display:block}
.legend{display:flex;flex-wrap:wrap;gap:8px;margin-top:12px}
.legend button{font-size:11px;background:var(--bg1);color:var(--fg);border:1px solid var(--bg2);padding:3px 9px;border-radius:2px}
.legend button:hover{border-color:var(--bg3)}.legend button.off{opacity:.35}
footer{margin-top:24px;font-size:11px;color:var(--gray);display:flex;justify-content:space-between;flex-wrap:wrap;gap:8px}
</style></head><body><div class="wrap">
<header>
 <div><h1>RJ45 Board</h1><div class="sub">temperature monitor &middot; <span id="ip">&mdash;</span></div></div>
 <div class="stat">
  <span><span class="dot off" id="link"></span><b id="clock">--:--:--</b></span>
  <span>WiFi <b id="rssi">--</b></span>
  <span>Up <b id="up">--</b></span>
 </div>
</header>
<div class="banner" id="banner"><span id="bannerText"></span><button class="btn" id="ackBtn">Silence</button></div>
<div class="label">Sensors</div>
<div class="grid" id="grid"></div>
<div class="label">History &middot; last 12 h &middot; 2 min samples</div>
<div class="chartbox"><canvas id="chart"></canvas><div class="legend" id="legend"></div></div>
<footer><span id="fw">&nbsp;</span><span id="upd">connecting&hellip;</span></footer>
</div>
<script>
const COLORS=['#8ec07c','#fabd2f','#83a598','#d3869b','#fe8019','#bdae93'];
const $=id=>document.getElementById(id);
const hidden=new Set();
let hist=null,data=null;

const fmt=(v,d=1)=>v==null?'--.-':v.toFixed(d);
const pad=n=>String(n).padStart(2,'0');
function dur(s){const d=Math.floor(s/86400);return (d?d+'d ':'')+pad(Math.floor(s/3600)%24)+'h '+pad(Math.floor(s/60)%60)+'m';}

function sensorCard(s){
  const alarm=s.al.state!=='NONE';
  let tag='tag',st;
  if(alarm){st=s.al.state;tag+=s.al.ack?' ack':' bad';}
  else if(!s.present)st='offline';
  else if(!s.valid){st='error';tag+=' bad';}
  else{st='ok';tag+=' ok';}
  const tr=s.trend>0.2?'<span class="tr up">&#9650;</span>':s.trend<-0.2?'<span class="tr dn">&#9660;</span>':'';
  const lim=s.al.on?fmt(s.al.lo)+' &hellip; '+fmt(s.al.hi)+' &deg;C':'off';
  return `<div class="card${alarm?' alarm':''}${s.present?'':' dim'}">
   <div class="head"><span class="name"><i class="swatch" style="background:${COLORS[s.id-1]}"></i>T${s.id}</span><span class="${tag}">${st}</span></div>
   <div class="temp">${s.valid?fmt(s.t):'--.-'}<small> &deg;C</small>${tr}</div>
   <div class="meta"><span>min</span><span>${fmt(s.min)}</span><span>max</span><span>${fmt(s.max)}</span>
   <span>alarm</span><span>${lim}</span>
   <span>errors</span><span title="last bad raw value: ${s.lastBad??'none'}">${s.present?s.errors+' / '+s.reads+(s.lastBad!=null?' ('+s.lastBad+')':''):'&mdash;'}</span>
   <span>rom</span><span title="${s.addr||''}">${s.addr||'&mdash;'}</span></div></div>`;
}

function ambientCard(a){
  let rows=`<span>humidity</span><span>${fmt(a.h)} %RH</span>`;
  if(a.p!=null)rows+=`<span>pressure</span><span>${fmt(a.p,0)} hPa</span>`;
  rows+=`<span>sensor</span><span>${a.type}</span>`;
  return `<div class="card"><div class="head"><span class="name"><i class="swatch" style="background:${COLORS[5]}"></i>AMBIENT</span>
   <span class="tag${a.t!=null?' ok':' bad'}">${a.t!=null?'ok':'error'}</span></div>
   <div class="temp">${fmt(a.t)}<small> &deg;C</small></div><div class="meta">${rows}</div></div>`;
}

function render(d){
  data=d;
  $('ip').textContent=d.ip;
  $('clock').textContent=d.synced?d.time:'no NTP';
  $('rssi').textContent=d.rssi+' dBm';
  $('up').textContent=dur(d.uptime);
  $('fw').textContent='fw v'+d.fw+' · heap '+Math.round(d.heap/1024)+' KB · sound '+(d.alarmSound?'on':'off')+' · 1-wire '+(d.parasite?'PARASITE':'3-wire');
  $('upd').textContent='updated '+new Date().toLocaleTimeString();
  let html=d.sensors.map(sensorCard).join('');
  if(d.ambient)html+=ambientCard(d.ambient);
  $('grid').innerHTML=html;

  const open=d.sensors.filter(s=>s.al.state!=='NONE'&&!s.al.ack);
  $('banner').classList.toggle('on',open.length>0);
  $('bannerText').textContent=open.map(s=>'T'+s.id+' '+s.al.state+(s.valid?' '+fmt(s.t)+' °C':'')).join('  ·  ');
  document.title=(open.length?'⚠ ALARM · ':'')+'RJ45 Board';
  if(!$('legend').children.length)buildLegend();
}

function buildLegend(){
  const names=['T1','T2','T3','T4','T5'];
  if(data&&data.ambient)names.push('Ambient');
  $('legend').innerHTML=names.map((n,i)=>`<button data-i="${i}"><i class="swatch" style="background:${COLORS[i]}"></i>${n}</button>`).join('');
  $('legend').querySelectorAll('button').forEach(b=>b.onclick=()=>{
    const i=+b.dataset.i;hidden.has(i)?hidden.delete(i):hidden.add(i);b.classList.toggle('off');draw();
  });
}

async function poll(){
  try{
    const r=await fetch('/api/data',{cache:'no-store'});
    render(await r.json());
    $('link').className='dot';
  }catch(e){
    $('link').className='dot off';
    $('upd').textContent='connection lost — retrying';
  }
}

async function loadHist(){
  try{const r=await fetch('/api/history',{cache:'no-store'});hist=await r.json();hist.t=Date.now();draw();}catch(e){}
}

function draw(){
  const c=$('chart'),dpr=window.devicePixelRatio||1,W=c.clientWidth,H=c.clientHeight;
  c.width=W*dpr;c.height=H*dpr;
  const g=c.getContext('2d');g.scale(dpr,dpr);g.clearRect(0,0,W,H);
  g.font="11px 'JetBrains Mono','SF Mono',monospace";
  const n=hist&&hist.series.length?hist.series[0].length:0;
  if(n<2){g.fillStyle='#928374';g.fillText('collecting data… (one point every 2 min)',8,20);return;}
  const step=hist.interval*1000,tEnd=hist.t-hist.age*1000,tStart=tEnd-(n-1)*step;
  let lo=Infinity,hi=-Infinity;
  hist.series.forEach((s,i)=>{if(hidden.has(i))return;s.forEach(v=>{if(v!=null){lo=Math.min(lo,v/10);hi=Math.max(hi,v/10);}});});
  if(lo===Infinity){lo=0;hi=30;}
  if(hi-lo<2){const m=(hi+lo)/2;lo=m-1;hi=m+1;}
  const p=(hi-lo)*.08;lo-=p;hi+=p;
  const L=46,R=10,T=8,B=22;
  const x=t=>L+(W-L-R)*(t-tStart)/(tEnd-tStart),y=v=>T+(H-T-B)*(1-(v-lo)/(hi-lo));
  g.lineWidth=1;g.strokeStyle='#3c3836';g.fillStyle='#a89984';
  for(let i=0;i<=4;i++){
    const v=lo+(hi-lo)*i/4,yy=Math.round(y(v))+.5;
    g.beginPath();g.moveTo(L,yy);g.lineTo(W-R,yy);g.stroke();g.fillText(v.toFixed(1),2,yy+4);
  }
  const span=tEnd-tStart,steps=[9e5,18e5,36e5,72e5,108e5];
  const ts=steps.find(s=>span/s<=6)||108e5;
  for(let t=Math.ceil(tStart/ts)*ts;t<=tEnd;t+=ts){
    const xx=Math.round(x(t))+.5,dt=new Date(t),lab=pad(dt.getHours())+':'+pad(dt.getMinutes());
    g.beginPath();g.moveTo(xx,T);g.lineTo(xx,H-B);g.stroke();
    g.fillText(lab,xx-g.measureText(lab).width/2,H-6);
  }
  g.lineWidth=1.75;g.lineJoin='round';
  hist.series.forEach((s,i)=>{
    if(hidden.has(i))return;
    g.strokeStyle=COLORS[i];g.beginPath();let pen=false;
    s.forEach((v,k)=>{
      if(v==null){pen=false;return;}
      const px=x(tStart+k*step),py=y(v/10);
      pen?g.lineTo(px,py):g.moveTo(px,py);pen=true;
    });
    g.stroke();
  });
}

$('ackBtn').onclick=async()=>{try{await fetch('/api/ack',{method:'POST'});}catch(e){}poll();};
window.addEventListener('resize',draw);
poll();loadHist();
setInterval(poll,2000);
setInterval(loadHist,60000);
</script></body></html>)rawliteral";
