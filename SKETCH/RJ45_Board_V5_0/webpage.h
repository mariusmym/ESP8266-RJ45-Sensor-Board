#pragma once
#include <Arduino.h>

// Dashboard page served at "/". Stored in flash (PROGMEM), no external files/CDNs,
// so it works even if the LAN has no internet. Data comes from /api/data and /api/history,
// settings go to POST /api/config, /api/forget and /api/device.
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
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(240px,1fr));gap:12px}
.card{position:relative;background:var(--bgs);border:1px solid var(--bg2);border-radius:4px;padding:14px;transition:border-color .15s}
.card::before{content:"";position:absolute;left:-1px;right:-1px;top:-1px;height:2px;background:var(--aqua);
opacity:0;transition:opacity .15s;border-radius:4px 4px 0 0}
.card:hover{border-color:var(--bg3)}.card:hover::before{opacity:1}
.card.alarm{border-color:var(--red)}.card.alarm::before{background:var(--red);opacity:1}
.card.dim{opacity:.45}
.head{display:flex;justify-content:space-between;align-items:center;gap:8px}
.name{color:var(--fg0);font-weight:900;font-size:13px;letter-spacing:.06em;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.tag{font-size:10px;font-weight:700;padding:2px 6px;border-radius:2px;text-transform:uppercase;letter-spacing:.08em;
background:var(--bg1);color:var(--fg4);flex:none}
.tag.ok{background:var(--green);color:var(--bgh)}
.tag.bad{background:var(--red);color:var(--fg0)}
.tag.ack{background:var(--orange);color:var(--bgh)}
.temp{font-size:40px;font-weight:700;color:var(--fg0);margin:10px 0 6px;letter-spacing:-.03em;font-variant-numeric:tabular-nums}
.temp small{font-size:15px;color:var(--fg4);font-weight:400}
.temp .rh{font-size:20px;color:var(--blue);margin-left:10px}
.tr{font-size:16px;margin-left:6px}.tr.up{color:var(--orange)}.tr.dn{color:var(--blue)}
.meta{font-size:11px;color:var(--fg4);display:grid;grid-template-columns:auto 1fr;gap:3px 10px}
.meta span:nth-child(even){color:var(--fg2);text-align:right;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.swatch{display:inline-block;width:10px;height:3px;margin-right:8px;vertical-align:middle;flex:none}
.banner{display:none;align-items:center;justify-content:space-between;gap:12px;margin-top:20px;padding:12px 14px;
background:rgba(251,73,52,.12);border:1px solid var(--red);border-radius:4px;color:var(--fg0);font-weight:700}
.banner.on{display:flex;animation:pulse 1.2s infinite}
@keyframes pulse{50%{background:rgba(251,73,52,.25)}}
button{font:inherit;cursor:pointer;border-radius:4px;transition:all .15s}
.btn{font-weight:700;font-size:12px;text-transform:uppercase;letter-spacing:.05em;background:var(--green);color:var(--bgh);
border:0;padding:7px 14px}.btn:hover{background:var(--aqua)}
.btn.sm{font-size:11px;padding:6px 10px}
.label.row{display:flex;align-items:center;justify-content:space-between;gap:12px}
.btn.dl{font-size:11px;padding:5px 10px;letter-spacing:.08em}
.btn.dl:disabled{background:var(--bg1);color:var(--gray);cursor:default}
.dlg{display:flex;gap:6px}
.btn.danger{background:var(--bg1);color:var(--red);border:1px solid var(--red)}.btn.danger:hover{background:var(--red);color:var(--fg0)}
.box{background:var(--bgs);border:1px solid var(--bg2);border-radius:4px;padding:14px}
canvas{width:100%;height:280px;display:block}
.legend{display:flex;flex-wrap:wrap;gap:8px;margin-top:12px}
.legend button{font-size:11px;background:var(--bg1);color:var(--fg);border:1px solid var(--bg2);padding:3px 9px;border-radius:2px}
.legend button:hover{border-color:var(--bg3)}.legend button.off{opacity:.35}
details.panel{margin-top:24px}
details.panel>summary{list-style:none;cursor:pointer;user-select:none;display:flex;align-items:center;gap:8px;
font-size:11px;letter-spacing:.18em;text-transform:uppercase;color:var(--fg4);margin-bottom:10px;transition:color .15s}
details.panel>summary::-webkit-details-marker{display:none}
details.panel>summary::before{content:"\25B8";color:var(--aqua);display:inline-block;transition:transform .15s}
details.panel[open]>summary::before{transform:rotate(90deg)}
details.panel>summary:hover{color:var(--fg2)}
details.panel .cnt{color:var(--gray);letter-spacing:.04em;text-transform:none}
.cfg{display:grid;gap:8px 10px;align-items:center;font-size:12px}
.cfg.al{grid-template-columns:minmax(150px,1.4fr) 70px 100px 100px auto}
.cfg.lb{grid-template-columns:minmax(170px,1fr) minmax(140px,1.4fr) 90px auto}
.cfg.dv{grid-template-columns:minmax(150px,auto) 1fr;max-width:640px}
.cfg.dv .note{font-size:11px;color:var(--fg4);line-height:1.5}
.cfg.dv .note b{color:var(--yellow);font-weight:700}
.cfg .h{font-size:10px;letter-spacing:.15em;text-transform:uppercase;color:var(--fg4)}
.cfg .who{display:flex;align-items:center;min-width:0;color:var(--fg0);font-weight:700}
.cfg .who small{color:var(--fg4);font-weight:400;margin-left:6px;white-space:nowrap}
.cfg .acts{display:flex;gap:6px;justify-content:flex-end}
.cfg .sep{grid-column:1/-1;height:1px;background:var(--bg1)}
select,input[type=text],input[type=number]{font:inherit;font-size:12px;background:var(--bgh);color:var(--fg);border:1px solid var(--bg2);
border-radius:4px;padding:6px 8px;width:100%;transition:border-color .15s}
select:focus,input[type=text]:focus,input[type=number]:focus{outline:none;border-color:var(--aqua)}
input::placeholder{color:var(--gray)}
.chk{display:flex;align-items:center;gap:6px;cursor:pointer;color:var(--fg2)}
.chk input{accent-color:var(--green);width:15px;height:15px}
.mobl{display:none}
.msg{font-size:11px;color:var(--fg4);margin-top:12px;min-height:14px}
.msg.ok{color:var(--green)}.msg.err{color:var(--red)}
footer{margin-top:24px;font-size:11px;color:var(--gray);display:flex;justify-content:space-between;flex-wrap:wrap;gap:8px}
@media(max-width:720px){
 .cfg.al{grid-template-columns:auto 1fr 1fr;align-items:end}
 .cfg.lb{grid-template-columns:1fr}
 .cfg.al .who,.cfg.al .acts{grid-column:1/-1}
 .cfg.al .chk{padding-bottom:7px}
 .cfg .h{display:none}
 .cfg .who,.cfg .lbl{grid-column:1/-1}
 .cfg .acts{grid-column:1/-1;justify-content:flex-start}
 .mobl{display:block;font-size:10px;color:var(--fg4);letter-spacing:.1em;text-transform:uppercase;margin-bottom:3px}
}
</style></head><body><div class="wrap">
<header>
 <div><h1>RJ45 Board</h1><div class="sub">plug &amp; play sensors &middot; <span id="ip">&mdash;</span></div></div>
 <div class="stat">
  <span><span class="dot off" id="link"></span><b id="clock">--:--:--</b></span>
  <span>WiFi <b id="rssi">--</b></span>
  <span>Up <b id="up">--</b></span>
 </div>
</header>
<div class="banner" id="banner"><span id="bannerText"></span><button class="btn" id="ackBtn">Silence</button></div>
<div class="label">Sensors</div>
<div class="grid" id="grid"></div>
<div class="label row"><span>History &middot; last 12 h &middot; 2 min samples</span>
<span class="dlg"><button class="btn dl" id="csvBtn" title="Last 12 h from RAM, one row every 2 min">&darr; 12 h</button><button class="btn dl" id="logBtn" disabled>&darr; Log</button></span></div>
<div class="box"><canvas id="chart"></canvas><div class="legend" id="legend"></div></div>
<details class="panel" id="pAl"><summary>Alarms <span class="cnt" id="alCnt"></span></summary>
<div class="box"><div id="cfgAl"></div><div class="msg" id="msgAl"></div></div></details>
<details class="panel" id="pLb"><summary>Sensors <span class="cnt" id="lbCnt"></span></summary>
<div class="box"><div id="cfgLb"></div><div class="msg" id="msgLb"></div></div></details>
<details class="panel" id="pDv"><summary>Device <span class="cnt" id="dvCnt"></span></summary>
<div class="box"><div id="cfgDv"></div><div class="msg" id="msgDv"></div></div></details>
<footer><span id="fw">&nbsp;</span><span id="upd">connecting&hellip;</span></footer>
</div>
<script>
const COLORS=['#8ec07c','#fabd2f','#83a598','#d3869b','#fe8019','#b8bb26','#bdae93','#fb4934'];
const $=id=>document.getElementById(id);
const hidden=new Set();
let hist=null,data=null,legendKey='',alKey='',lbKey='',dvKey='';

const fmt=(v,d=1)=>v==null?'--.-':v.toFixed(d);
const pad=n=>String(n).padStart(2,'0');
const esc=s=>String(s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
function dur(s){const d=Math.floor(s/86400);return (d?d+'d ':'')+pad(Math.floor(s/3600)%24)+'h '+pad(Math.floor(s/60)%60)+'m';}
const visible=()=>data?data.sensors.filter(s=>s.visible):[];

function sensorCard(s){
  const alarm=s.al.state!=='NONE';
  let tag='tag',st;
  if(alarm){st=s.al.state;tag+=s.al.ack?' ack':' bad';}
  else if(!s.present)st='offline';
  else if(!s.valid){st=s.tcOpen?'TC open':'error';tag+=' bad';}
  else{st='ok';tag+=' ok';}
  const tr=s.trend>0.2?'<span class="tr up">&#9650;</span>':s.trend<-0.2?'<span class="tr dn">&#9660;</span>':'';
  const lim=s.al.on?fmt(s.al.lo)+' &hellip; '+fmt(s.al.hi)+' &deg;C':'off';
  const rh=s.h!=null?`<span class="rh">${fmt(s.h)}<small> %RH</small></span>`:'';
  let rows=`<span>min</span><span>${fmt(s.min)}</span><span>max</span><span>${fmt(s.max)}</span>`;
  if(s.dp!=null)rows+=`<span>dew point</span><span>${fmt(s.dp)} &deg;C</span>`;
  if(s.p!=null)rows+=`<span>pressure</span><span>${fmt(s.p)} hPa</span>`;
  rows+=`<span>alarm</span><span>${lim}</span>`;
  if(s.off)rows+=`<span>offset</span><span>${s.off>0?'+':''}${fmt(s.off)} &deg;C</span>`;
  rows+=`<span>errors</span><span title="last bad value: ${s.lastBad??'none'}">${s.reads?s.errors+' / '+s.reads+(s.lastBad!=null?' ('+s.lastBad+')':''):'&mdash;'}</span>`;
  rows+=`<span>dropouts</span><span title="times the sensor went LOST since boot">${s.drops??0}</span>`;
  if(s.kind==='ds')rows+=`<span>search misses</span><span title="bus searches that missed it while it kept reading fine (harmless)">${s.miss??0}</span>`;
  const src=s.kind==='ds'?s.src+(s.parasite?' &middot; parasite':''):s.src;
  rows+=`<span>${esc(s.model)}</span><span title="${esc(src)}">${src}</span>`;
  return `<div class="card${alarm?' alarm':''}${s.present?'':' dim'}">
   <div class="head"><span class="name"><i class="swatch" style="background:${COLORS[s.id-1]}"></i>${esc(s.name)}</span><span class="${tag}">${st}</span></div>
   <div class="temp">${s.valid?fmt(s.t):'--.-'}<small> &deg;C</small>${tr}${rh}</div>
   <div class="meta">${rows}</div></div>`;
}

function render(d){
  data=d;
  $('ip').textContent=d.ip;
  $('clock').textContent=d.synced?d.time:'no NTP';
  $('rssi').textContent=d.rssi+' dBm';
  $('up').textContent=dur(d.uptime);
  $('fw').textContent='fw v'+d.fw+' · heap '+Math.round(d.heap/1024)+' KB · I2C '+(d.i2c.length?d.i2c.join(', '):'none')+
    ' · 1-wire '+d.dsOnBus+' found'+(d.dev.spi?' · SPI port: MAX6675':'')+(d.log&&d.log.ok?' · log '+span(d.log):' · log off');
  logStatus(d);
  $('upd').textContent=d.dev.restart?'restarting…':'updated '+new Date().toLocaleTimeString();
  const vis=visible();
  $('grid').innerHTML=vis.length?vis.map(sensorCard).join(''):
    '<div class="card"><div class="name">No sensors yet</div><div class="meta" style="display:block;margin-top:8px">Plug in a DS18B20, an I2C sensor (SHT3x, SHT4x, AHT20, BME280, BMP280) or a MAX6675. It appears here within 30 s.</div></div>';

  const open=vis.filter(s=>s.al.state!=='NONE'&&!s.al.ack);
  $('banner').classList.toggle('on',open.length>0);
  $('bannerText').textContent=open.map(s=>s.name+' '+s.al.state+(s.valid?' '+fmt(s.t)+' °C':'')).join('  ·  ');
  document.title=(open.length?'⚠ ALARM · ':'')+'RJ45 Board';
  buildLegend();
  buildCfg();
}

function buildLegend(){
  const vis=visible(),key=vis.map(s=>s.id+s.name).join('|');
  if(key===legendKey)return;
  legendKey=key;
  $('legend').innerHTML=vis.map(s=>{const i=s.id-1;
    return `<button data-i="${i}" class="${hidden.has(i)?'off':''}"><i class="swatch" style="background:${COLORS[i]}"></i>${esc(s.name)}</button>`;}).join('');
  $('legend').querySelectorAll('button').forEach(b=>b.onclick=()=>{
    const i=+b.dataset.i;hidden.has(i)?hidden.delete(i):hidden.add(i);b.classList.toggle('off');draw();
  });
  draw();
}

// Panels: each is rebuilt only when its saved values or the sensor list
// change, and never while you're typing in it
function buildCfg(){
  const vis=visible();
  const on=vis.filter(s=>s.al.on).length,named=vis.filter(s=>s.label).length;
  $('alCnt').textContent='· '+on+' of '+vis.length+' on';
  $('lbCnt').textContent='· '+vis.length+' of '+data.slots+' slots used'+(named?', '+named+' named':'');
  $('dvCnt').textContent='· '+(data.dev.spi?'SPI port mode':'buttons + buzzer');
  buildAl(vis);buildLb(vis);buildDv();
}

function who(s,full){
  const i=s.id-1;
  let sub='';
  if(full)sub=`<small title="${esc(s.model+' · '+s.src)}">${esc(s.kind==='ds'?'…'+s.src.slice(-6):s.src)}${s.present?'':' &middot; offline'}</small>`;
  else if(!s.present)sub='<small>offline</small>';
  return `<span class="who"><i class="swatch" style="background:${COLORS[i]}"></i>${esc(full?s.def:s.name)}${sub}</span>`;
}

function enterSaves(box,fn){
  box.querySelectorAll('input[type=text],input[type=number]').forEach(inp=>inp.onkeydown=e=>{
    if(e.key==='Enter'){e.preventDefault();fn(+inp.dataset.i);}
  });
}

function buildAl(vis){
  const box=$('cfgAl');
  const key=vis.map(s=>[s.id,s.present,s.name,s.al.on,s.al.lo,s.al.hi].join(',')).join('|');
  if(key===alKey||box.contains(document.activeElement))return;
  alKey=key;
  if(!vis.length){box.innerHTML='<span class="msg">No sensors yet.</span>';return;}
  let h=`<div class="cfg al"><span class="h">sensor</span><span class="h">alarm</span>
   <span class="h">low &le; &deg;C</span><span class="h">high &ge; &deg;C</span><span></span>`;
  vis.forEach((s,n)=>{
    const i=s.id-1;
    if(n)h+='<span class="sep"></span>';
    h+=`${who(s,false)}
     <label class="chk"><input type="checkbox" id="on${i}"${s.al.on?' checked':''}>on</label>
     <span><span class="mobl">low &deg;C</span><input type="number" id="lo${i}" data-i="${i}" step="0.5" min="${s.tmin}" max="${s.tmax}" value="${s.al.lo}"></span>
     <span><span class="mobl">high &deg;C</span><input type="number" id="hi${i}" data-i="${i}" step="0.5" min="${s.tmin}" max="${s.tmax}" value="${s.al.hi}"></span>
     <span class="acts"><button class="btn sm" data-al="${i}">Save</button></span>`;
  });
  box.innerHTML=h+'</div>';
  box.querySelectorAll('[data-al]').forEach(b=>b.onclick=()=>saveAl(+b.dataset.al));
  enterSaves(box,saveAl);
  if(!$('msgAl').textContent)msg('msgAl','HIGH must be at least '+data.gap+' °C above LOW. A missing sensor raises a LOST alarm. Saved to flash.');
}

function buildLb(vis){
  const box=$('cfgLb');
  const key=vis.map(s=>[s.id,s.present,s.label,s.off,s.src].join(',')).join('|');
  if(key===lbKey||box.contains(document.activeElement))return;
  lbKey=key;
  if(!vis.length){box.innerHTML='<span class="msg">No sensors yet.</span>';return;}
  let h=`<div class="cfg lb"><span class="h">sensor</span><span class="h">label</span><span class="h">offset &deg;C</span><span></span>`;
  vis.forEach((s,n)=>{
    const i=s.id-1;
    if(n)h+='<span class="sep"></span>';
    h+=`${who(s,true)}
     <span class="lbl"><span class="mobl">label</span><input type="text" id="lb${i}" data-i="${i}" maxlength="11" value="${esc(s.label)}" placeholder="${esc(s.name===s.label?s.def:s.name)}"></span>
     <span class="lbl"><span class="mobl">offset &deg;C</span><input type="number" id="of${i}" data-i="${i}" step="0.1" min="-20" max="20" value="${s.off}"></span>
     <span class="acts"><button class="btn sm" data-lb="${i}">Save</button>${!s.present?`<button class="btn sm danger" data-forget="${i}" title="Free this sensor's slot, label and alarm">Forget</button>`:''}</span>`;
  });
  box.innerHTML=h+'</div>';
  box.querySelectorAll('[data-lb]').forEach(b=>b.onclick=()=>saveLb(+b.dataset.lb));
  box.querySelectorAll('[data-forget]').forEach(b=>b.onclick=()=>forget(+b.dataset.forget));
  enterSaves(box,saveLb);
  if(!$('msgLb').textContent)msg('msgLb','Sensors are found on their own and keep their slot. Label: up to 11 characters (letters, digits, space - _ . # / ( ) + :), shown on the OLED too, empty = default name. Offset corrects the temperature (humidity follows). Forget frees the slot of a sensor that is gone.');
}

function buildDv(){
  const box=$('cfgDv'),d=data.dev;
  const key=[d.sound,d.beep,d.off,d.contrast,d.spi].join(',');
  if(key===dvKey||box.contains(document.activeElement))return;
  dvKey=key;
  const opt=(v,t,sel)=>`<option value="${v}"${sel?' selected':''}>${t}</option>`;
  box.innerHTML=`<div class="cfg dv">
   <span class="h">alarm sound</span><label class="chk"><input type="checkbox" id="dvSound"${d.sound?' checked':''}>buzzer on alarms</label>
   <span class="h">key beep</span><label class="chk"><input type="checkbox" id="dvBeep"${d.beep?' checked':''}>click on button press</label>
   <span class="h">screen off</span><select id="dvOff">${d.offMin.map((m,i)=>opt(i,m?m+' min':'never',i===d.off)).join('')}</select>
   <span class="h">contrast</span><select id="dvCon">${[1,2,3,4,5,6,7,8].map(c=>opt(c,c+' / 8',c===d.contrast)).join('')}</select>
   <span class="h">IO13 / IO14</span><select id="dvSpi">${opt(0,'Button B + buzzer',!d.spi)}${opt(1,'SPI port: MAX6675 on RJ5 / SPI header',d.spi)}</select>
   <span></span><span class="note">SPI port mode needs jumpers <b>U8</b> (button B) and <b>U9</b> (buzzer) <b>removed</b>: IO14 becomes SCK and IO13 the MAX6675 data line. Button A then cycles the screens, alarms and settings are set here. Changing it restarts the board.</span>
   <span></span><span class="acts" style="justify-content:flex-start"><button class="btn sm" id="dvSave">Save</button></span></div>`;
  $('dvSave').onclick=saveDv;
}

function msg(id,t,cls){const m=$(id);m.textContent=t;m.className='msg'+(cls?' '+cls:'');}

async function post(url,params){
  const r=await fetch(url,{method:'POST',body:new URLSearchParams(params)});
  if(!r.ok)throw new Error(await r.text()||('HTTP '+r.status));
}

function done(){if(document.activeElement)document.activeElement.blur();alKey=lbKey=legendKey='';poll();}

async function saveAl(i){
  const s=data.sensors[i],lo=parseFloat($('lo'+i).value),hi=parseFloat($('hi'+i).value);
  if(isNaN(lo)||isNaN(hi))return msg('msgAl','Low and high must be numbers','err');
  if(lo<s.tmin||hi>s.tmax)return msg('msgAl',s.model+': limits must be between '+s.tmin+' and '+s.tmax+' °C','err');
  if(hi<lo+data.gap)return msg('msgAl','HIGH must be at least '+data.gap+' °C above LOW','err');
  msg('msgAl','saving…');
  try{
    await post('/api/config',{ch:i,on:$('on'+i).checked?1:0,lo,hi});
    msg('msgAl','Saved alarm for '+s.name+' ✓','ok');done();
  }catch(e){msg('msgAl',e.message,'err');}
}

async function saveLb(i){
  const label=$('lb'+i).value.trim(),off=parseFloat($('of'+i).value||'0');
  if(isNaN(off)||Math.abs(off)>20)return msg('msgLb','Offset must be between -20 and 20 °C','err');
  msg('msgLb','saving…');
  try{
    await post('/api/config',{ch:i,label,off});
    msg('msgLb','Saved '+(label||data.sensors[i].def)+' ✓','ok');done();
  }catch(e){msg('msgLb',e.message,'err');}
}

async function forget(i){
  const s=data.sensors[i];
  if(!confirm('Forget '+s.name+' ('+s.model+', '+s.src+')?\nIts slot, label, offset and alarm are removed. If it is plugged in again it comes back as a new sensor.'))return;
  try{await post('/api/forget',{ch:i});msg('msgLb','Forgot '+s.name,'ok');done();loadHist();}
  catch(e){msg('msgLb',e.message,'err');}
}

async function saveDv(){
  const spi=+$('dvSpi').value;
  if(spi!==data.dev.spi&&!confirm(spi?'Switch IO13/IO14 to the SPI port?\n\nRemove jumpers U8 (button B) and U9 (buzzer) first. The board restarts.':'Switch IO13/IO14 back to button B + buzzer?\n\nUnplug the SPI sensor, then put jumpers U8 and U9 back. The board restarts.'))return;
  msg('msgDv','saving…');
  try{
    const r=await fetch('/api/device',{method:'POST',body:new URLSearchParams({sound:$('dvSound').checked?1:0,beep:$('dvBeep').checked?1:0,
      off:$('dvOff').value,contrast:$('dvCon').value,spi})});
    if(!r.ok)throw new Error(await r.text()||('HTTP '+r.status));
    const j=await r.json();
    msg('msgDv',j.restart?'Saved ✓ restarting, back in ~10 s…':'Saved ✓','ok');
    dvKey='';if(!j.restart)done();
  }catch(e){msg('msgDv',e.message,'err');}
}

// panels start closed; the browser remembers which ones you left open
['pAl','pLb','pDv'].forEach(id=>{
  const d=$(id);
  try{d.open=localStorage.getItem('rj_'+id)==='1';}catch(e){}
  d.addEventListener('toggle',()=>{try{localStorage.setItem('rj_'+id,d.open?'1':'0');}catch(e){}});
});

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

function shown(i){return !hidden.has(i)&&data&&data.sensors[i]&&data.sensors[i].visible;}

function draw(){
  const c=$('chart'),dpr=window.devicePixelRatio||1,W=c.clientWidth,H=c.clientHeight;
  c.width=W*dpr;c.height=H*dpr;
  const g=c.getContext('2d');g.scale(dpr,dpr);g.clearRect(0,0,W,H);
  g.font="11px 'JetBrains Mono','SF Mono',monospace";
  const n=hist&&hist.series.length?hist.series[0].length:0;
  if(n<2){g.fillStyle='#928374';g.fillText('collecting data… (one point every 2 min)',8,20);return;}
  const step=hist.interval*1000,tEnd=hist.t-hist.age*1000,tStart=tEnd-(n-1)*step;
  let lo=Infinity,hi=-Infinity;
  hist.series.forEach((s,i)=>{if(!shown(i))return;s.forEach(v=>{if(v!=null){lo=Math.min(lo,v/10);hi=Math.max(hi,v/10);}});});
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
    if(!shown(i))return;
    g.strokeStyle=COLORS[i];g.beginPath();let pen=false;
    s.forEach((v,k)=>{
      if(v==null){pen=false;return;}
      const px=x(tStart+k*step),py=y(v/10);
      pen?g.lineTo(px,py):g.moveTo(px,py);pen=true;
    });
    g.stroke();
  });
}

// Long-term flash log: the board streams the CSV itself
function span(l){
  if(!l.recs)return 'empty';
  const h=(l.newest-l.oldest)/3600+l.every/60;
  return h>=48?(h/24).toFixed(1)+' d':Math.round(h)+' h';
}
function logStatus(d){
  const b=$('logBtn'),l=d.log;
  if(b.dataset.busy)return;
  if(!l||!l.ok){b.disabled=true;b.innerHTML='&darr; Log';
    b.title='Log not available: pick a Flash Size with FS in Tools and reflash';return;}
  b.disabled=!l.recs;
  b.innerHTML='&darr; Log '+(l.recs?span(l):'');
  b.title=l.recs?(l.every+'-min averages · '+l.recs+' rows · from '+new Date(l.oldest*1000).toLocaleString()+
    ' · keeps '+Math.round(l.keepMin/24)+'–'+Math.round(l.keepMax/24)+' days')
    :'First row is written at the next '+l.every+'-minute mark';
}
$('logBtn').onclick=()=>{
  const b=$('logBtn'),comma=(1.5).toLocaleString().indexOf(',')>=0;
  const a=document.createElement('a');
  a.href='/api/log.csv?eu='+(comma?1:0);a.download='';
  document.body.appendChild(a);a.click();a.remove();
  b.dataset.busy=1;b.textContent='✓ downloading';
  setTimeout(()=>{delete b.dataset.busy;if(data)logStatus(data);},2500);
};

// CSV export of the stored history (built in the browser from /api/history).
// Uses ; and decimal comma when the browser is set to a comma-decimal locale
// (e.g. Romanian), so Excel opens it straight into columns.
async function downloadCsv(){
  const b=$('csvBtn');b.disabled=true;b.textContent='…';
  try{
    const [h,d]=await Promise.all([fetch('/api/history',{cache:'no-store'}).then(r=>r.json()),
                                   fetch('/api/data',{cache:'no-store'}).then(r=>r.json())]);
    const now=Date.now(),cols=d.sensors.filter(s=>s.visible);
    const n=h.series.length?h.series[0].length:0;
    if(!n)throw new Error('no data yet');
    const comma=(1.5).toLocaleString().indexOf(',')>=0,sep=comma?';':',';
    const num=v=>{const t=(v/10).toFixed(1);return comma?t.replace('.',','):t;};
    const q=t=>/[;,"\n]/.test(t)?'"'+t.replace(/"/g,'""')+'"':t;
    const step=h.interval*1000,tEnd=now-h.age*1000,tStart=tEnd-(n-1)*step;
    const ts=t=>{const x=new Date(t);return x.getFullYear()+'-'+pad(x.getMonth()+1)+'-'+pad(x.getDate())+' '+pad(x.getHours())+':'+pad(x.getMinutes());};
    const lines=[['Time'].concat(cols.map(s=>s.name+' (°C)')).map(q).join(sep)];
    for(let k=0;k<n;k++){
      const row=cols.map(s=>{const v=h.series[s.id-1][k];return v==null?'':num(v);});
      if(row.every(c=>c===''))continue;
      lines.push([ts(tStart+k*step)].concat(row).join(sep));
    }
    const blob=new Blob(['\ufeff'+lines.join('\r\n')+'\r\n'],{type:'text/csv;charset=utf-8'});
    const a=document.createElement('a'),f=new Date(now);
    a.href=URL.createObjectURL(blob);
    a.download='rj45-board_'+f.getFullYear()+'-'+pad(f.getMonth()+1)+'-'+pad(f.getDate())+'_'+pad(f.getHours())+pad(f.getMinutes())+'.csv';
    document.body.appendChild(a);a.click();a.remove();
    setTimeout(()=>URL.revokeObjectURL(a.href),1000);
    b.textContent='✓ '+(lines.length-1)+' rows';
  }catch(e){b.textContent='✗ '+e.message;}
  setTimeout(()=>{b.disabled=false;b.innerHTML='&darr; 12 h';},2000);
}
$('csvBtn').onclick=downloadCsv;

$('ackBtn').onclick=async()=>{try{await fetch('/api/ack',{method:'POST'});}catch(e){}poll();};
window.addEventListener('resize',draw);
poll();loadHist();
setInterval(poll,2000);
setInterval(loadHist,60000);
</script></body></html>)rawliteral";
