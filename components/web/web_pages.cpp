#include "web_pages.h"

#include "app_config.h"

#include <cstdio>

namespace {

const char* kSharedCss = R"CSS(
:root{color:#17211a;background:#f4f0e6;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;}
*{box-sizing:border-box}
body{margin:0;padding:22px;background:radial-gradient(circle at top left,#fff7d6 0,#f4f0e6 32%,#ece0cd 100%);}
main{max-width:1060px;margin:0 auto;}
nav{display:flex;gap:10px;align-items:center;justify-content:space-between;margin-bottom:16px;flex-wrap:wrap;}
nav .links{display:flex;gap:8px;flex-wrap:wrap;}
a,.link{color:#795c20;font-weight:800;text-decoration:none}
.pill{display:inline-flex;align-items:center;gap:6px;border:1px solid #dfd3bd;background:#fffaf0;border-radius:999px;padding:9px 13px;box-shadow:0 8px 24px rgba(66,52,30,.08);}
.hero{background:#fffaf0;border:1px solid #dfd3bd;border-radius:26px;padding:24px;box-shadow:0 20px 60px rgba(66,52,30,.13);}
h1{margin:0 0 8px;font-size:32px;letter-spacing:-.03em;}
h2{margin:0 0 12px;font-size:18px;}
p{line-height:1.55;color:#5f584c;}
.grid{display:grid;grid-template-columns:1.05fr .95fr;gap:16px;margin-top:16px;}
.cards{display:grid;grid-template-columns:repeat(auto-fit,minmax(190px,1fr));gap:12px;margin-top:16px;}
.card,.panel{background:#fdf6e8;border:1px solid #eadcc5;border-radius:18px;padding:16px;}
.label{font-size:12px;text-transform:uppercase;letter-spacing:.08em;color:#7d725f;font-weight:800;}
.value{font-size:20px;font-weight:900;margin-top:6px;word-break:break-word;}
label{display:block;font-size:12px;color:#6c614f;font-weight:800;margin:12px 0 5px;}
select,input,textarea{width:100%;border:1px solid #d9c8ad;background:#fffaf0;border-radius:12px;padding:11px 12px;font:inherit;color:#17211a;}
textarea{min-height:124px;resize:vertical;font-family:ui-monospace,SFMono-Regular,Menlo,monospace;}
.two{display:grid;grid-template-columns:1fr 1fr;gap:12px;}
.three{display:grid;grid-template-columns:repeat(3,1fr);gap:12px;}
.actions{display:flex;gap:10px;flex-wrap:wrap;margin-top:16px;}
button{border:0;border-radius:14px;padding:11px 15px;background:#263525;color:#fff;font-weight:900;cursor:pointer;box-shadow:0 10px 22px rgba(38,53,37,.18);}
button.secondary{background:#efe3ce;color:#2c251a;box-shadow:none;border:1px solid #d9c8ad;}
button.warn{background:#a0522d;}
pre{white-space:pre-wrap;word-break:break-word;background:#1f261f;color:#eaf2df;border-radius:18px;padding:16px;min-height:170px;margin:0;font-size:13px;line-height:1.45;}
code{background:#efe3ce;border-radius:8px;padding:3px 7px;}
.muted{font-size:13px;color:#7d725f;}
@media(max-width:760px){body{padding:14px}.grid,.two,.three{grid-template-columns:1fr}.hero{padding:18px}h1{font-size:26px}}
)CSS";

const char* kRootScript = R"JS(
const $=id=>document.getElementById(id);
const echo=$('echo');
function show(title,data){echo.textContent=title+'\n'+(typeof data==='string'?data:JSON.stringify(data,null,2));}
function params(apply){
  const q=new URLSearchParams();
  q.set('power',$('power').value);
  q.set('mode',$('mode').value);
  q.set('temperature_f',$('temperature').value);
  q.set('fan',$('fan').value);
  q.set('vane',$('vane').value);
  q.set('wide_vane',$('wide').value);
  if(apply)q.set('apply','1');
  return q.toString();
}
function fill(s){
  const m=s.cn105.mock_state;
  $('power').value=m.power;
  $('mode').value=m.mode;
  $('temperature').value=m.target_temperature_f;
  $('fan').value=m.fan;
  $('vane').value=m.vane;
  $('wide').value=m.wide_vane;
  $('summary').textContent=`${m.power} / ${m.mode} / ${m.target_temperature_f}F / fan ${m.fan} / vane ${m.vane} / wide ${m.wide_vane}`;
  $('wifi').textContent=`${s.wifi.mode} ${s.wifi.ip} ${s.wifi.rssi} dBm`;
  $('uart').textContent=`UART${s.cn105.uart} RX=${s.cn105.rx_pin} TX=${s.cn105.tx_pin} ${s.cn105.baud} ${s.cn105.format}`;
  if(s.homekit)$('homekit').textContent=`${s.homekit.started?'已启动':'未启动'} / 已配对 ${s.homekit.paired_controllers} / ${s.homekit.setup_code}`;
}
async function statusOnly(){
  const r=await fetch('/api/status');
  const j=await r.json();
  fill(j);
  return j;
}
async function refresh(){
  const j=await statusOnly();
  fill(j);
  show('当前状态',j.cn105.mock_state);
}
async function build(apply){
  const r=await fetch('/api/cn105/mock/build-set?'+params(apply));
  const j=await r.json();
  if(j.ok)await statusOnly();
  show(apply?'已发送到 Mock':'已生成 CN105 SET payload',j);
}
window.addEventListener('load',refresh);
)JS";

const char* kDebugScript = R"JS(
const $=id=>document.getElementById(id);
const out=$('out');
function show(title,data){out.textContent=title+'\n'+(typeof data==='string'?data:JSON.stringify(data,null,2));}
async function getJson(path){
  const r=await fetch(path);
  const j=await r.json();
  show(path,j);
}
async function decodeRaw(){
  const hex=$('hex').value.trim();
  const r=await fetch('/api/cn105/decode?hex='+encodeURIComponent(hex));
  const j=await r.json();
  show('Raw decode',j);
}
async function sampleBuild(){
  const r=await fetch('/api/cn105/mock/build-set?power=ON&mode=COOL&temperature_f=77&fan=AUTO&vane=AUTO&wide_vane=%7C');
  const j=await r.json();
  show('Sample build without apply',j);
}
)JS";

}  // namespace

namespace web_pages {

bool renderRoot(char* out, size_t out_len) {
    if (out == nullptr || out_len == 0) {
        return false;
    }

    const int written = std::snprintf(
        out,
        out_len,
        R"HTML(<!doctype html>
<html lang="zh-Hans">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>%s</title>
<style>%s</style>
</head>
<body><main>
<nav><div class="pill"><strong>三菱空调桥接器</strong><span class="muted">M7 / HomeKit Mock</span></div><div class="links"><a class="pill" href="/">遥控器</a><a class="pill" href="/debug">Debug</a><a class="pill" href="/api/status">JSON</a></div></nav>
<section class="hero">
<h1>虚拟遥控器</h1>
<p>当前是真实 HomeKit + 离线 CN105 Mock：你在网页或 Home app 里改设置，都会同步到 ESP32 内存里的模拟状态；真实空调 transport 还没接上。</p>
<pre id="echo">正在读取当前状态...</pre>
<div class="grid">
<section class="panel">
<h2>遥控器</h2>
<div class="two">
<div><label>电源</label><select id="power"><option>OFF</option><option>ON</option></select></div>
<div><label>模式</label><select id="mode"><option>COOL</option><option>HEAT</option><option>DRY</option><option>FAN</option><option>AUTO</option></select></div>
</div>
<div class="three">
<div><label>目标温度 °F</label><input id="temperature" type="number" min="61" max="88" step="1" value="77"></div>
<div><label>风速</label><select id="fan"><option>AUTO</option><option>QUIET</option><option>1</option><option>2</option><option>3</option><option>4</option></select></div>
<div><label>上下风向</label><select id="vane"><option>AUTO</option><option>1</option><option>2</option><option>3</option><option>4</option><option>5</option><option>SWING</option></select></div>
</div>
<label>左右风向</label><select id="wide"><option value="|">中间 |</option><option value="&lt;&lt;">最左 &lt;&lt;</option><option value="&lt;">左 &lt;</option><option value="&gt;">右 &gt;</option><option value="&gt;&gt;">最右 &gt;&gt;</option><option value="&lt;&gt;">分散 &lt;&gt;</option><option>SWING</option><option>AIRFLOW CONTROL</option></select>
<div class="actions"><button class="secondary" onclick="refresh()">刷新状态</button><button onclick="build(false)">生成 Payload</button><button class="warn" onclick="build(true)">发送到 Mock</button></div>
</section>
<section class="panel">
<h2>当前同步状态</h2>
<div class="cards">
<div class="card"><div class="label">CN105 Mock</div><div class="value" id="summary">--</div></div>
<div class="card"><div class="label">HomeKit</div><div class="value" id="homekit">--</div></div>
<div class="card"><div class="label">Wi-Fi</div><div class="value" id="wifi">--</div></div>
<div class="card"><div class="label">UART</div><div class="value" id="uart">--</div></div>
</div>
<p class="muted">HomeKit 使用默认 HAP 端口 80；这个 WebUI 现在运行在 8080。这里生成的 payload 可以复制去 Debug 页 decode。</p>
</section>
</div>
</section>
</main><script>%s</script></body></html>)HTML",
        app_config::kDeviceName,
        kSharedCss,
        kRootScript);

    return written > 0 && static_cast<size_t>(written) < out_len;
}

bool renderDebug(char* out, size_t out_len) {
    if (out == nullptr || out_len == 0) {
        return false;
    }

    const int written = std::snprintf(
        out,
        out_len,
        R"HTML(<!doctype html>
<html lang="zh-Hans">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>%s Debug</title>
<style>%s</style>
</head>
<body><main>
<nav><div class="pill"><strong>Debug Console</strong><span class="muted">raw decode / API</span></div><div class="links"><a class="pill" href="/">遥控器</a><a class="pill" href="/debug">Debug</a><a class="pill" href="/api/status">JSON</a></div></nav>
<section class="hero">
<h1>调试工具</h1>
<p>这里放不会影响真实空调的离线工具：状态 API、Mock payload build、raw packet decode。</p>
<pre id="out">选择一个操作...</pre>
<div class="grid">
<section class="panel">
<h2>Raw Packet Decode</h2>
<textarea id="hex">FC 41 01 30 10 01 1F 01 01 03 00 00 00 00 00 00 00 00 03 B2 00 A4</textarea>
<div class="actions"><button onclick="decodeRaw()">Decode</button><button class="secondary" onclick="sampleBuild()">生成 77F 示例 SET</button></div>
</section>
<section class="panel">
<h2>API 快捷入口</h2>
<div class="actions"><button class="secondary" onclick="getJson('/api/health')">Health</button><button class="secondary" onclick="getJson('/api/status')">Status</button><button class="secondary" onclick="getJson('/api/cn105/mock/status')">Mock State</button></div>
<p class="muted">如果页面行为奇怪，先看这些 API 是否还是 200，再看串口日志。</p>
</section>
</div>
</section>
</main><script>%s</script></body></html>)HTML",
        app_config::kDeviceName,
        kSharedCss,
        kDebugScript);

    return written > 0 && static_cast<size_t>(written) < out_len;
}

}  // namespace web_pages
