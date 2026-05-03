#pragma once

#include <string>

namespace sensorstream {

// The entire dashboard is a single HTML page served at GET /
// Embedded as a C++11 raw string literal — no escaping needed for quotes,
// backslashes, or any other HTML/JS characters
// Raw string delimiter SSDASH is arbitrary — chosen to be unique enough
// that it will never appear inside the HTML content itself
inline const std::string DASHBOARD_HTML = R"SSDASH(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>SensorStream</title>
<script src="https://cdnjs.cloudflare.com/ajax/libs/Chart.js/4.4.1/chart.umd.min.js"></script>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{background:#0a0e1a;color:#c9d1d9;font-family:'Courier New',monospace;min-height:100vh}
header{background:#161b22;border-bottom:1px solid #21262d;padding:16px 24px;display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:12px}
h1{font-size:1.4rem;color:#58a6ff;letter-spacing:2px}
.subtitle{font-size:0.7rem;color:#8b949e;margin-top:2px}
.status{display:flex;gap:20px}
.metric{text-align:center}
.metric-val{font-size:1.2rem;font-weight:bold;color:#3fb950}
.metric-label{color:#8b949e;font-size:0.65rem;text-transform:uppercase;letter-spacing:1px}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(300px,1fr));gap:16px;padding:20px}
.card{background:#161b22;border:1px solid #21262d;border-radius:8px;padding:16px;transition:border-color 0.3s}
.card-header{display:flex;justify-content:space-between;align-items:flex-start;margin-bottom:10px}
.sensor-type{font-size:0.75rem;font-weight:bold;color:#58a6ff}
.sensor-id{font-size:0.7rem;color:#8b949e}
.badge{display:inline-block;padding:2px 8px;border-radius:4px;font-size:0.65rem;font-weight:bold;text-transform:uppercase;letter-spacing:1px}
.badge-normal{background:#1a3a1e;color:#3fb950;border:1px solid #2ea043}
.badge-anomaly{background:#3a1a1a;color:#f85149;border:1px solid #da3633}
.sensor-value{font-size:2rem;font-weight:bold;color:#e6edf3;margin-bottom:2px}
.sensor-unit{font-size:0.7rem;color:#8b949e;margin-bottom:6px}
.sensor-stats{font-size:0.72rem;color:#8b949e;margin-bottom:10px}
.chart-wrap{position:relative;height:80px}
.anomaly-section{padding:0 20px 20px}
.anomaly-section h2{font-size:0.85rem;color:#8b949e;text-transform:uppercase;letter-spacing:2px;margin-bottom:10px;border-bottom:1px solid #21262d;padding-bottom:8px}
.anomaly-item{background:#161b22;border:1px solid #21262d;border-left:3px solid #da3633;border-radius:4px;padding:8px 12px;font-size:0.72rem;display:flex;justify-content:space-between;align-items:center;gap:8px;flex-wrap:wrap;margin-bottom:6px}
.a-sensor{color:#58a6ff;font-weight:bold}
.a-value{color:#f85149;font-weight:bold}
.a-z{color:#8b949e}
.empty{color:#484f58;font-size:0.8rem;padding:8px 0}
footer{text-align:center;padding:16px;color:#484f58;font-size:0.7rem;border-top:1px solid #21262d}
footer a{color:#58a6ff;text-decoration:none}
</style>
</head>
<body>
<header>
  <div>
    <h1>&#9889; SENSORSTREAM</h1>
    <div class="subtitle">Real-Time Industrial Sensor Processing Engine &mdash; C++20</div>
  </div>
  <div class="status">
    <div class="metric"><div class="metric-val" id="m-proc">0</div><div class="metric-label">Processed</div></div>
    <div class="metric"><div class="metric-val" id="m-anom" style="color:#f85149">0</div><div class="metric-label">Anomalies</div></div>
    <div class="metric"><div class="metric-val" id="m-drop" style="color:#d29922">0</div><div class="metric-label">Dropped</div></div>
    <div class="metric"><div class="metric-val" id="m-buf">0%</div><div class="metric-label">Buffer</div></div>
  </div>
</header>

<div class="grid" id="sensor-grid"></div>

<div class="anomaly-section">
  <h2>&#9888; Anomaly Log</h2>
  <div id="anomaly-list"><div class="empty">No anomalies detected yet...</div></div>
</div>

<footer>
  SensorStream &mdash; Satvik Pandey &bull;
  <a href="https://github.com/SatvikSPandey" target="_blank">github.com/SatvikSPandey</a> &bull;
  <a href="https://satvikspandey.netlify.app" target="_blank">satvikspandey.netlify.app</a>
</footer>

<script>
const charts={}, histories={};
const UNITS={TEMPERATURE:'degC',PRESSURE:'bar',VIBRATION:'mm/s',FLOW_RATE:'L/min',HUMIDITY:'%RH'};
const COLORS={TEMPERATURE:'#ff7b54',PRESSURE:'#58a6ff',VIBRATION:'#3fb950',FLOW_RATE:'#d2a8ff',HUMIDITY:'#79c0ff'};

function ensureCard(s){
  if(document.getElementById('card-'+s.sensor_id)) return;
  const color=COLORS[s.type]||'#58a6ff';
  const div=document.createElement('div');
  div.className='card'; div.id='card-'+s.sensor_id;
  div.innerHTML=`
    <div class="card-header">
      <div><div class="sensor-type">${s.type}</div><div class="sensor-id">${s.sensor_id}</div></div>
      <span class="badge badge-normal" id="badge-${s.sensor_id}">NORMAL</span>
    </div>
    <div class="sensor-value" id="val-${s.sensor_id}">--</div>
    <div class="sensor-unit">${UNITS[s.type]||''}</div>
    <div class="sensor-stats" id="stats-${s.sensor_id}">mean: -- &nbsp; &sigma;: --</div>
    <div class="chart-wrap"><canvas id="chart-${s.sensor_id}"></canvas></div>`;
  document.getElementById('sensor-grid').appendChild(div);
  histories[s.sensor_id]=[];
  const ctx=document.getElementById('chart-'+s.sensor_id).getContext('2d');
  charts[s.sensor_id]=new Chart(ctx,{
    type:'line',
    data:{labels:[],datasets:[{data:[],borderColor:color,borderWidth:1.5,pointRadius:0,fill:true,backgroundColor:color+'18',tension:0.3}]},
    options:{animation:false,plugins:{legend:{display:false}},scales:{x:{display:false},y:{display:true,ticks:{color:'#484f58',font:{size:9}},grid:{color:'#21262d'}}},responsive:true,maintainAspectRatio:false}
  });
}

function updateCard(s){
  ensureCard(s);
  const v=parseFloat(s.value).toFixed(3);
  document.getElementById('val-'+s.sensor_id).textContent=v;
  document.getElementById('stats-'+s.sensor_id).innerHTML=
    'mean: '+parseFloat(s.mean).toFixed(3)+'&nbsp;&nbsp;&sigma;: '+parseFloat(s.stddev).toFixed(3);
  const badge=document.getElementById('badge-'+s.sensor_id);
  const card=document.getElementById('card-'+s.sensor_id);
  if(s.is_anomaly){
    badge.textContent='ANOMALY'; badge.className='badge badge-anomaly';
    card.style.borderColor='#da3633';
  } else {
    badge.textContent='NORMAL'; badge.className='badge badge-normal';
    card.style.borderColor='#21262d';
  }
  const h=histories[s.sensor_id];
  h.push(parseFloat(v));
  if(h.length>60) h.shift();
  const c=charts[s.sensor_id];
  c.data.labels=h.map((_,i)=>i);
  c.data.datasets[0].data=h;
  c.update('none');
}

async function poll(){
  try{
    const r=await fetch('/api/sensors');
    if(r.ok)(await r.json()).forEach(updateCard);
  }catch(e){}
  try{
    const r=await fetch('/api/metrics');
    if(r.ok){
      const m=await r.json();
      document.getElementById('m-proc').textContent=m.readings_processed.toLocaleString();
      document.getElementById('m-anom').textContent=m.anomalies_detected.toLocaleString();
      document.getElementById('m-drop').textContent=m.readings_dropped.toLocaleString();
      document.getElementById('m-buf').textContent=m.buffer_utilization_pct.toFixed(1)+'%';
    }
  }catch(e){}
  try{
    const r=await fetch('/api/anomalies?limit=15');
    if(r.ok){
      const anomalies=await r.json();
      const list=document.getElementById('anomaly-list');
      if(!anomalies.length){list.innerHTML='<div class="empty">No anomalies detected yet...</div>';return;}
      list.innerHTML=[...anomalies].reverse().map(a=>
        `<div class="anomaly-item">
          <span class="a-sensor">${a.sensor_id}</span>
          <span>${a.type}</span>
          <span class="a-value">${parseFloat(a.value).toFixed(3)} ${UNITS[a.type]||''}</span>
          <span class="a-z">Z=${parseFloat(a.z_score).toFixed(2)}</span>
         </div>`
      ).join('');
    }
  }catch(e){}
}

poll();
setInterval(poll,1000);
</script>
</body>
</html>
)SSDASH";

} // namespace sensorstream
