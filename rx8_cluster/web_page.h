// RX-8 Cluster web paneli (ESP32 içinde gömülü)
#pragma once
#include <pgmspace.h>

static const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!doctype html><html lang="tr"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>RX-8 Cluster</title>
<style>
:root{--bg:#111316;--card:#1b1e23;--line:#2b3038;--fg:#e8eaed;--mut:#9aa0a6;--acc:#e5484d;--ok:#3fb950;--warn:#f0b429}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);font:15px system-ui,sans-serif}
header{padding:14px 16px;border-bottom:1px solid var(--line);display:flex;justify-content:space-between;align-items:center;gap:8px;flex-wrap:wrap}
h1{font-size:18px;margin:0}h1 b{color:var(--acc)}
main{max-width:1000px;margin:0 auto;padding:12px 16px;display:grid;gap:12px;grid-template-columns:repeat(auto-fit,minmax(300px,1fr))}
.card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:12px 14px}
.card h2{font-size:13px;letter-spacing:.06em;text-transform:uppercase;color:var(--mut);margin:0 0 10px}
.row{display:grid;grid-template-columns:110px 1fr 64px;align-items:center;gap:8px;margin:8px 0}
.row label{color:var(--mut);font-size:13px}.row output{text-align:right;font-variant-numeric:tabular-nums}
input[type=range]{width:100%;accent-color:var(--acc)}
input[type=number]{width:100%;background:#0d0f12;color:var(--fg);border:1px solid var(--line);border-radius:6px;padding:5px}
.tg{display:grid;grid-template-columns:repeat(auto-fill,minmax(130px,1fr));gap:6px}
.t{border:1px solid var(--line);background:#14171b;color:var(--fg);border-radius:8px;padding:9px 8px;font-size:13px;cursor:pointer;text-align:left}
.t.on{background:var(--acc);border-color:var(--acc);color:#fff}
.t.opt.on{background:#1f6feb;border-color:#1f6feb}
.btns{display:flex;gap:6px;flex-wrap:wrap}
.b{border:1px solid var(--line);background:#22262c;color:var(--fg);border-radius:8px;padding:8px 12px;cursor:pointer;font-size:13px}
.pill{font-size:12px;padding:3px 8px;border-radius:99px;border:1px solid var(--line)}
.pill.ok{color:var(--ok);border-color:var(--ok)}.pill.bad{color:var(--acc);border-color:var(--acc)}.pill.w{color:var(--warn);border-color:var(--warn)}
table{width:100%;border-collapse:collapse;font:12px ui-monospace,monospace}td,th{padding:3px 4px;border-bottom:1px solid var(--line);text-align:left}
.stat{display:grid;grid-template-columns:repeat(3,1fr);gap:6px;font-size:12px;color:var(--mut)}.stat b{display:block;color:var(--fg);font-size:15px}
.hint{font-size:12px;color:var(--mut);margin:6px 0 0}
[hidden]{display:none!important}
</style></head><body>
<header><h1><b>RX-8</b> Cluster Simülatörü</h1><span id="canPill" class="pill">CAN ...</span></header>
<main>
<section class="card"><h2>Göstergeler</h2>
 <div class="row"><label>Devir (rpm)</label><input type="range" id="rpm" min="0" max="9500" step="50"><output id="rpm_o"></output></div>
 <div class="row"><label>Hız (km/s)</label><input type="range" id="speed" min="0" max="300" step="1"><output id="speed_o"></output></div>
 <div class="row"><label>Hararet (ham)</label><input type="range" id="temp" min="0" max="255" step="1"><output id="temp_o"></output></div>
 <div class="row"><label>Yağ bas. (ham)</label><input type="range" id="oil" min="0" max="255" step="1"><output id="oil_o"></output></div>
 <div class="row"><label>Yakıt (%)</label><input type="range" id="fuel" min="0" max="100" step="1"><output id="fuel_o"></output></div>
 <div class="row"><label>Gaz pedalı (%)</label><input type="range" id="throttle" min="0" max="100" step="1"><output id="throttle_o"></output></div>
 <p class="hint">Hararet: ~145 ibre ortada. Yağ: 0 = düşük, 1+ = normal. Yakıt sadece direnç merdiveni bağlıysa çalışır.</p>
 <div class="btns" style="margin-top:8px">
  <button class="b" onclick="send({rpm:0,speed:0,throttle:0})">Sıfırla</button>
 </div>
</section>

<section class="card"><h2>Modlar</h2>
 <div class="tg">
  <button class="t md" data-m="3">Hoşgeldin</button>
  <button class="t md" data-m="4">Hoşçakal</button>
  <button class="t md" data-m="1">İbre süpürme</button>
  <button class="t md" data-m="7">Soğuk çalıştırma</button>
  <button class="t md" data-m="2">Sürüş ↻</button>
  <button class="t md" data-m="6">Gazlama / kesici ↻</button>
  <button class="t md" data-m="5">Işık şovu ↻</button>
 </div>
 <p class="hint">Hoşgeldin: lamba testi → ibre süpürme → dörtlü → marş (açılışta ve kontak açılınca da çalışır). ↻ olanlar durdurulana kadar döner; bitince değerler ve lambalar eski haline döner.</p>
 <div class="btns" style="margin-top:8px"><button class="b" onclick="send({demo:0})">Durdur</button></div>
</section>

<section class="card"><h2>Uyarı lambaları (CAN)</h2>
 <div class="tg">
  <button class="t" data-k="checkEngine">Check Engine</button>
  <button class="t" data-k="ceBlink">Check Eng. (yanıp sönen)</button>
  <button class="t" data-k="charge">Şarj / Akü</button>
  <button class="t" data-k="oilWarn">Yağ basıncı</button>
  <button class="t" data-k="lowCoolant">Düşük antifriz</button>
  <button class="t" data-k="abs">ABS</button>
  <button class="t" data-k="brakeWarn">Fren (EBD)</button>
  <button class="t" data-k="dscOff">DSC OFF</button>
  <button class="t" data-k="tcsBlink">TCS (yanıp sönen)</button>
  <button class="t" data-k="tcsOff">TCS kapalı</button>
 </div>
</section>

<section class="card"><h2>Kontak ve kablolu lambalar</h2>
 <div class="tg">
  <button class="t gp" data-k="turnL">◀ Sol sinyal</button>
  <button class="t gp" data-k="turnR">Sağ sinyal ▶</button>
  <button class="t gp" data-k="hazard">Dörtlü</button>
  <button class="t gp" data-k="highBeam">Uzun far</button>
  <button class="t gp" data-k="door">Kapı açık</button>
  <button class="t gp" data-k="parkBrake">El freni</button>
  <button class="t gp" data-k="brakeFluid">Fren hidroliği</button>
  <button class="t gp" data-k="seatbelt">Emniyet kemeri</button>
  <button class="t opt" data-k="ignition">Kontak (IG)</button>
 </div>
 <p class="hint" id="gpHint" hidden>Bu kartta boş pin olmadığı için kablolu lambalar (sinyal, kapı, el freni…) devre dışı. Kontak düğmesi CAN yayınını durdurup başlatır.</p>
 <div class="btns" style="margin-top:10px">
  <button class="b" onclick="send({lampTest:1})">Tüm lambalar AÇ</button>
  <button class="b" onclick="send({lampTest:0})">Tüm lambalar KAPAT</button>
 </div>
</section>

<section class="card"><h2>CAN durumu</h2>
 <div class="stat">
  <div>Durum<b id="cState">-</b></div><div>TX hata sayacı<b id="cTxErr">-</b></div><div>RX hata sayacı<b id="cRxErr">-</b></div>
  <div>Kuyruğa alınan<b id="cTxOk">-</b></div><div>Gönderilemeyen<b id="cTxFail">-</b></div><div>Alınan<b id="cRx">-</b></div>
 </div>
 <p class="hint">TX hata sayacı 128+ ve artıyorsa: cluster ACK vermiyor → besleme (1C, 1G, 1E), CAN H/L yer değişimi veya 500k hızını kontrol et.</p>
 <table><thead><tr><th>ID</th><th>Veri</th><th>Adet</th><th>ms önce</th></tr></thead><tbody id="rxTbl"></tbody></table>
</section>

<section class="card"><h2>Ayarlar / kalibrasyon</h2>
 <div class="row"><label>RPM çarpanı</label><input type="number" id="rpmFactor" step="0.01" min="1" max="10"><span></span></div>
 <div class="row"><label>0x620 byte6</label><input type="number" id="abs620" step="1" min="0" max="255"><span></span></div>
 <div class="row"><label>Yakıt ham Ω</label><input type="number" id="fuelOhm" step="1" min="-1" max="400"><output id="fuelAct"></output></div>
 <div class="row"><label>Odo sayaç</label><input type="number" id="odo" step="1" min="0" max="255"><span></span></div>
 <div class="row"><label>Odo tik/km</label><input type="number" id="odoTicks" step="1" min="1"><span></span></div>
 <div class="tg" style="margin-top:8px">
  <button class="t opt" data-k="canEnabled">CAN yayını</button>
  <button class="t opt" data-k="keepalive">PCM keep-alive</button>
  <button class="t opt" data-k="sendDSC">0x212 DSC</button>
  <button class="t opt" data-k="sendEPS">0x300 EPS</button>
  <button class="t opt" data-k="immo">Immobilizer cevabı</button>
  <button class="t opt" data-k="odoAuto">Odo otomatik (deneysel)</button>
 </div>
 <p class="hint">Yakıt ham Ω: -1 = yüzde kaydırıcısını kullan. Takometre hatalıysa RPM çarpanını 3.85 ↔ 4.00 arasında dene.</p>
 <div class="btns" style="margin-top:8px"><button class="b" onclick="if(confirm('Varsayılanlara dön?'))send({reset:1})">Varsayılanlar</button></div>
</section>
</main>
<script>
let S={},pending={},busy=false,editing=null;
const $=id=>document.getElementById(id);
function flush(){
  if(busy||!Object.keys(pending).length)return;
  const q=new URLSearchParams(pending).toString();pending={};busy=true;
  fetch('/set?'+q).catch(()=>{}).finally(()=>{busy=false;setTimeout(flush,30)});
}
function send(o){Object.assign(pending,o);flush();setTimeout(poll,150)}
['rpm','speed','temp','oil','fuel','throttle'].forEach(k=>{
  const el=$(k);
  el.addEventListener('input',()=>{editing=k;$(k+'_o').textContent=el.value;send({[k]:el.value})});
  el.addEventListener('change',()=>{editing=null});
});
['rpmFactor','abs620','fuelOhm','odo','odoTicks'].forEach(k=>{
  const el=$(k);
  el.addEventListener('focus',()=>editing=k);
  el.addEventListener('blur',()=>editing=null);
  el.addEventListener('change',()=>send({[k]:el.value}));
});
document.querySelectorAll('.t[data-k]').forEach(b=>b.addEventListener('click',()=>{
  const k=b.dataset.k;send({[k]:S[k]?0:1});S[k]=!S[k];b.classList.toggle('on',!!S[k]);
}));
document.querySelectorAll('.md').forEach(b=>b.addEventListener('click',()=>send({demo:S.demo==b.dataset.m?0:b.dataset.m})));
function render(){
  ['rpm','speed','temp','oil','fuel','throttle'].forEach(k=>{
    if(editing!==k){$(k).value=S[k];$(k+'_o').textContent=S[k]}
  });
  ['rpmFactor','abs620','fuelOhm','odo','odoTicks'].forEach(k=>{if(editing!==k)$(k).value=S[k]});
  $('fuelAct').textContent=S.fuelLadder?S.fuelActual+'Ω':'kapalı';
  document.querySelectorAll('.gp').forEach(b=>b.hidden=!S.gpioLamps);$('gpHint').hidden=!!S.gpioLamps;
  document.querySelectorAll('.t[data-k]').forEach(b=>b.classList.toggle('on',!!S[b.dataset.k]));
  document.querySelectorAll('.md').forEach(b=>b.classList.toggle('on',S.demo==b.dataset.m));
  const c=S.can||{};
  $('cState').textContent=c.state;$('cTxErr').textContent=c.txErr;$('cRxErr').textContent=c.rxErr;
  $('cTxOk').textContent=c.txOk;$('cTxFail').textContent=c.txFail;$('cRx').textContent=c.rx;
  const p=$('canPill');
  const good=c.state==='CALISIYOR'&&c.txErr<96;
  p.className='pill '+(good?'ok':(c.state==='CALISIYOR'?'w':'bad'));
  p.textContent='CAN '+(good?'OK':(c.state==='CALISIYOR'?'ACK yok':c.state));
}
function poll(){fetch('/state').then(r=>r.json()).then(j=>{S=j;render()}).catch(()=>{})}
function pollCan(){fetch('/can').then(r=>r.json()).then(a=>{
  a.sort((x,y)=>parseInt(x.id,16)-parseInt(y.id,16));
  $('rxTbl').innerHTML=a.length?a.map(e=>`<tr><td>${e.id}</td><td>${e.d}</td><td>${e.n}</td><td>${e.age}</td></tr>`).join(''):'<tr><td colspan=4>Henüz mesaj yok</td></tr>';
}).catch(()=>{})}
poll();pollCan();
(function loop(){poll();setTimeout(loop,S.demo?250:700)})();setInterval(pollCan,2000);
</script></body></html>
)rawliteral";
