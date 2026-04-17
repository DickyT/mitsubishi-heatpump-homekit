#include "web_pages.h"

#include "app_config.h"

#include <cstdio>

namespace {

const char* kCss = R"CSS(
:root{--bg:#0f1923;--card:#1a2836;--card2:#223344;--border:#2a3f52;--text:#e0e8ef;--muted:#7a8fa0;--accent:#4fc3f7;--accent2:#81d4fa;--green:#66bb6a;--red:#ef5350;--orange:#ffa726;--radius:14px;--font:-apple-system,BlinkMacSystemFont,'SF Pro','Segoe UI',sans-serif}
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:var(--font);background:var(--bg);color:var(--text);min-height:100dvh;padding-bottom:72px}
main{max-width:640px;margin:0 auto;padding:16px}
h1{font-size:22px;font-weight:800;margin-bottom:4px}
h2{font-size:15px;font-weight:700;color:var(--muted);margin-bottom:12px}
.subtitle{font-size:13px;color:var(--muted);margin-bottom:16px}
.card{background:var(--card);border:1px solid var(--border);border-radius:var(--radius);padding:16px;margin-bottom:12px}
.card h3{font-size:14px;font-weight:700;color:var(--accent);margin-bottom:12px;text-transform:uppercase;letter-spacing:.05em}
.stats{display:grid;grid-template-columns:1fr 1fr;gap:8px}
.stat{background:var(--card2);border-radius:10px;padding:10px 12px}
.stat .k{font-size:11px;color:var(--muted);text-transform:uppercase;letter-spacing:.06em;font-weight:700}
.stat .v{font-size:18px;font-weight:800;margin-top:2px}
.stat .v.on{color:var(--green)}.stat .v.off{color:var(--muted)}
.row{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-bottom:10px}
.row3{display:grid;grid-template-columns:1fr 1fr 1fr;gap:10px;margin-bottom:10px}
label{display:block;font-size:11px;color:var(--muted);font-weight:700;text-transform:uppercase;letter-spacing:.05em;margin-bottom:5px}
select,input[type=number],textarea{width:100%;background:var(--card2);border:1px solid var(--border);color:var(--text);border-radius:10px;padding:10px 12px;font:inherit;font-size:15px;-webkit-appearance:none;appearance:none}
select{background-image:url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='12' height='8'%3E%3Cpath d='M1 1l5 5 5-5' stroke='%237a8fa0' stroke-width='2' fill='none'/%3E%3C/svg%3E");background-repeat:no-repeat;background-position:right 12px center;padding-right:32px}
textarea{min-height:100px;resize:vertical;font-family:ui-monospace,'SF Mono',Menlo,monospace;font-size:13px}
.btns{display:flex;gap:8px;flex-wrap:wrap;margin-top:12px}
button{border:0;border-radius:10px;padding:12px 18px;font:inherit;font-size:14px;font-weight:700;cursor:pointer;transition:opacity .15s}
button:active{opacity:.7}
.btn-primary{background:var(--accent);color:#000}
.btn-secondary{background:var(--card2);color:var(--text);border:1px solid var(--border)}
.btn-danger{background:var(--red);color:#fff}
.btn-send{background:var(--green);color:#000}
pre{background:var(--card2);color:var(--accent2);border-radius:var(--radius);padding:14px;font-size:12px;line-height:1.5;white-space:pre-wrap;word-break:break-word;min-height:80px;max-height:300px;overflow-y:auto}
.tag{display:inline-block;padding:3px 8px;border-radius:6px;font-size:11px;font-weight:700}
.tag-on{background:#1b5e20;color:#a5d6a7}.tag-off{background:#b71c1c33;color:#ef9a9a}
.tag-ok{background:#0d47a133;color:var(--accent2)}.tag-err{background:#b71c1c33;color:#ef9a9a}
nav.tabs{position:fixed;bottom:0;left:0;right:0;background:var(--card);border-top:1px solid var(--border);display:flex;z-index:99;padding-bottom:env(safe-area-inset-bottom)}
nav.tabs a{flex:1;display:flex;flex-direction:column;align-items:center;gap:3px;padding:10px 0 8px;color:var(--muted);text-decoration:none;font-size:11px;font-weight:700;transition:color .15s}
nav.tabs a.active{color:var(--accent)}
nav.tabs svg{width:22px;height:22px}
.kvrow{display:flex;justify-content:space-between;padding:8px 0;border-bottom:1px solid var(--border)}
.kvrow:last-child{border-bottom:0}
.kvrow .kk{color:var(--muted);font-size:13px}.kvrow .vv{font-weight:700;font-size:13px}
@media(min-width:768px){main{padding:24px 16px}body{padding-bottom:0}nav.tabs{position:static;border-top:0;border-bottom:1px solid var(--border);margin-bottom:20px;max-width:640px;margin-left:auto;margin-right:auto;border-radius:var(--radius);background:var(--card)}nav.tabs a{flex-direction:row;gap:6px;padding:14px 0;font-size:13px}}
)CSS";

const char* kTabsHtml = R"HTML(
<nav class="tabs">
<a href="/" id="t1"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M3 9l9-7 9 7v11a2 2 0 01-2 2H5a2 2 0 01-2-2z"/><polyline points="9 22 9 12 15 12 15 22"/></svg>控制</a>
<a href="/debug" id="t2"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polyline points="16 18 22 12 16 6"/><polyline points="8 6 2 12 8 18"/></svg>调试</a>
<a href="/admin" id="t3"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.65 1.65 0 00.33 1.82l.06.06a2 2 0 010 2.83 2 2 0 01-2.83 0l-.06-.06a1.65 1.65 0 00-1.82-.33 1.65 1.65 0 00-1 1.51V21a2 2 0 01-4 0v-.09A1.65 1.65 0 009 19.4a1.65 1.65 0 00-1.82.33l-.06.06a2 2 0 01-2.83-2.83l.06-.06A1.65 1.65 0 004.68 15a1.65 1.65 0 00-1.51-1H3a2 2 0 010-4h.09A1.65 1.65 0 004.6 9a1.65 1.65 0 00-.33-1.82l-.06-.06a2 2 0 012.83-2.83l.06.06A1.65 1.65 0 009 4.68a1.65 1.65 0 001-1.51V3a2 2 0 014 0v.09a1.65 1.65 0 001 1.51 1.65 1.65 0 001.82-.33l.06-.06a2 2 0 012.83 2.83l-.06.06A1.65 1.65 0 0019.4 9a1.65 1.65 0 001.51 1H21a2 2 0 010 4h-.09a1.65 1.65 0 00-1.51 1z"/></svg>管理</a>
</nav>
)HTML";

const char* kRootScript = R"JS(
const $=id=>document.getElementById(id);
document.getElementById('t1').classList.add('active');
function fill(s){
  const m=s.cn105.mock_state;
  $('power').value=m.power;
  $('mode').value=m.mode;
  $('temp').value=m.target_temperature_f;
  $('fan').value=m.fan;
  $('vane').value=m.vane;
  $('wide').value=m.wide_vane;
  $('s-power').textContent=m.power;
  $('s-power').className='v '+(m.power==='ON'?'on':'off');
  $('s-mode').textContent=m.mode;
  $('s-temp').textContent=m.target_temperature_f+'°F';
  $('s-room').textContent=m.room_temperature_f+'°F';
  $('s-fan').textContent=m.fan;
  $('s-wifi').textContent=s.wifi.connected?s.wifi.ip+' ('+s.wifi.rssi+'dBm)':'断开';
  const t=s.cn105.transport_status;
  $('s-transport').textContent=s.cn105.transport==='real'?(t.connected?'已连接':'连接中...'):'Mock';
  $('s-hk').textContent=s.homekit.started?(s.homekit.paired_controllers+'个控制器'):'未启动';
}
async function refresh(){
  try{const r=await fetch('/api/status');const j=await r.json();fill(j);$('out').textContent='就绪';}
  catch(e){$('out').textContent='获取状态失败: '+e;}
}
function params(){
  const q=new URLSearchParams();
  q.set('power',$('power').value);q.set('mode',$('mode').value);
  q.set('temperature_f',$('temp').value);q.set('fan',$('fan').value);
  q.set('vane',$('vane').value);q.set('wide_vane',$('wide').value);
  return q.toString();
}
async function send(){
  try{
    const r=await fetch('/api/cn105/mock/build-set?'+params(),{method:'POST'});
    const j=await r.json();
    $('out').textContent=j.ok?'已发送':'错误: '+(j.error||'未知');
    if(j.ok)setTimeout(refresh,300);
  }catch(e){$('out').textContent='发送失败: '+e;}
}
refresh();setInterval(refresh,5000);
)JS";

const char* kDebugScript = R"JS(
const $=id=>document.getElementById(id);
document.getElementById('t2').classList.add('active');
const out=$('out');
function show(t,d){out.textContent=t+'\n'+(typeof d==='string'?d:JSON.stringify(d,null,2));}
async function api(p){try{const r=await fetch(p);show(p,await r.json());}catch(e){show('错误',''+e);}}
async function decode(){
  const h=$('hex').value.trim();
  if(!h){show('错误','请输入 hex 数据');return;}
  api('/api/cn105/decode?hex='+encodeURIComponent(h));
}
async function sample(){api('/api/cn105/mock/build-set?power=ON&mode=COOL&temperature_f=77&fan=AUTO&vane=AUTO&wide_vane=%7C');}
async function loadTransport(){
  try{
    const r=await fetch('/api/status');const j=await r.json();const t=j.cn105.transport_status;
    $('tp').textContent=
      'Phase: '+t.phase+'\nConnected: '+t.connected+
      '\nConnect Attempts: '+t.connect_attempts+'\nPoll Cycles: '+t.poll_cycles+
      '\nRX Packets: '+t.rx_packets+' / Errors: '+t.rx_errors+
      '\nTX Packets: '+t.tx_packets+'\nSets Pending: '+t.sets_pending+
      (t.last_error?'\nLast Error: '+t.last_error:'');
  }catch(e){$('tp').textContent='错误: '+e;}
}
loadTransport();
)JS";

const char* kAdminScript = R"JS(
const $=id=>document.getElementById(id);
document.getElementById('t3').classList.add('active');
async function loadInfo(){
  try{
    const r=await fetch('/api/status');const j=await r.json();
    $('i-device').textContent=j.device;
    $('i-phase').textContent=j.phase;
    $('i-uptime').textContent=Math.floor(j.uptime_ms/1000)+'s';
    $('i-wifi').textContent=j.wifi.ip+' ('+j.wifi.rssi+'dBm)';
    $('i-mac').textContent=j.wifi.mac;
    $('i-fs').textContent=j.filesystem.used_bytes+' / '+j.filesystem.total_bytes+' bytes';
    $('i-hk-status').textContent=j.homekit.started?'已启动':'未启动';
    $('i-hk-paired').textContent=j.homekit.paired_controllers;
    $('i-hk-code').textContent=j.homekit.setup_code;
    $('i-hk-payload').textContent=j.homekit.setup_payload||'-';
    $('i-transport').textContent=j.cn105.transport;
  }catch(e){$('msg').textContent='加载失败: '+e;}
}
async function reboot(){
  if(!confirm('确定要重启设备吗？'))return;
  try{await fetch('/api/reboot',{method:'POST'});$('msg').textContent='重启中...';}
  catch(e){$('msg').textContent='重启请求已发送';}
}
loadInfo();
)JS";

}  // namespace

namespace web_pages {

bool renderRoot(char* out, size_t out_len) {
    if (out == nullptr || out_len == 0) return false;
    const int written = std::snprintf(out, out_len,
        R"HTML(<!doctype html><html lang="zh-Hans"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="apple-mobile-web-app-capable" content="yes">
<title>%s</title><style>%s</style></head><body>
%s
<main>
<h1>三菱空调</h1>
<div class="subtitle">远程控制面板</div>
<div class="stats" style="margin-bottom:14px">
<div class="stat"><div class="k">电源</div><div class="v" id="s-power">--</div></div>
<div class="stat"><div class="k">模式</div><div class="v" id="s-mode">--</div></div>
<div class="stat"><div class="k">设定温度</div><div class="v" id="s-temp">--</div></div>
<div class="stat"><div class="k">室温</div><div class="v" id="s-room">--</div></div>
<div class="stat"><div class="k">风速</div><div class="v" id="s-fan">--</div></div>
<div class="stat"><div class="k">WiFi</div><div class="v" id="s-wifi">--</div></div>
<div class="stat"><div class="k">Transport</div><div class="v" id="s-transport">--</div></div>
<div class="stat"><div class="k">HomeKit</div><div class="v" id="s-hk">--</div></div>
</div>
<div class="card">
<h3>遥控器</h3>
<div class="row">
<div><label>电源</label><select id="power"><option>OFF</option><option>ON</option></select></div>
<div><label>模式</label><select id="mode"><option>COOL</option><option>HEAT</option><option>DRY</option><option>FAN</option><option>AUTO</option></select></div>
</div>
<div class="row3">
<div><label>温度 °F</label><input id="temp" type="number" min="50" max="88" step="1" value="77"></div>
<div><label>风速</label><select id="fan"><option>AUTO</option><option>QUIET</option><option>1</option><option>2</option><option>3</option><option>4</option></select></div>
<div><label>风向</label><select id="vane"><option>AUTO</option><option>1</option><option>2</option><option>3</option><option>4</option><option>5</option><option>SWING</option></select></div>
</div>
<div><label>左右风向</label><select id="wide"><option value="|">中间</option><option value="&lt;&lt;">最左</option><option value="&lt;">左</option><option value="&gt;">右</option><option value="&gt;&gt;">最右</option><option value="&lt;&gt;">分散</option><option>SWING</option></select></div>
<div class="btns"><button class="btn-send" onclick="send()">发送</button><button class="btn-secondary" onclick="refresh()">刷新</button></div>
</div>
<pre id="out">加载中...</pre>
</main><script>%s</script></body></html>)HTML",
        app_config::kDeviceName, kCss, kTabsHtml, kRootScript);
    return written > 0 && static_cast<size_t>(written) < out_len;
}

bool renderDebug(char* out, size_t out_len) {
    if (out == nullptr || out_len == 0) return false;
    const int written = std::snprintf(out, out_len,
        R"HTML(<!doctype html><html lang="zh-Hans"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>%s - 调试</title><style>%s</style></head><body>
%s
<main>
<h1>调试工具</h1>
<div class="subtitle">协议分析 &amp; 传输状态</div>
<div class="card">
<h3>传输层状态</h3>
<pre id="tp">加载中...</pre>
<div class="btns"><button class="btn-secondary" onclick="loadTransport()">刷新</button></div>
</div>
<div class="card">
<h3>Raw Packet Decode</h3>
<textarea id="hex">FC 41 01 30 10 01 1F 01 01 03 00 00 00 00 00 00 00 00 03 B2 00 A4</textarea>
<div class="btns"><button class="btn-primary" onclick="decode()">Decode</button><button class="btn-secondary" onclick="sample()">77°F 示例</button></div>
</div>
<div class="card">
<h3>API</h3>
<div class="btns">
<button class="btn-secondary" onclick="api('/api/health')">Health</button>
<button class="btn-secondary" onclick="api('/api/status')">Status</button>
<button class="btn-secondary" onclick="api('/api/cn105/mock/status')">Mock</button>
</div>
</div>
<pre id="out">选择一个操作...</pre>
</main><script>%s</script></body></html>)HTML",
        app_config::kDeviceName, kCss, kTabsHtml, kDebugScript);
    return written > 0 && static_cast<size_t>(written) < out_len;
}

bool renderAdmin(char* out, size_t out_len) {
    if (out == nullptr || out_len == 0) return false;
    const int written = std::snprintf(out, out_len,
        R"HTML(<!doctype html><html lang="zh-Hans"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>%s - 管理</title><style>%s</style></head><body>
%s
<main>
<h1>系统管理</h1>
<div class="subtitle">设备信息与维护</div>
<div class="card">
<h3>设备信息</h3>
<div class="kvrow"><span class="kk">名称</span><span class="vv" id="i-device">--</span></div>
<div class="kvrow"><span class="kk">阶段</span><span class="vv" id="i-phase">--</span></div>
<div class="kvrow"><span class="kk">运行时间</span><span class="vv" id="i-uptime">--</span></div>
<div class="kvrow"><span class="kk">WiFi</span><span class="vv" id="i-wifi">--</span></div>
<div class="kvrow"><span class="kk">MAC</span><span class="vv" id="i-mac">--</span></div>
<div class="kvrow"><span class="kk">存储</span><span class="vv" id="i-fs">--</span></div>
<div class="kvrow"><span class="kk">传输模式</span><span class="vv" id="i-transport">--</span></div>
</div>
<div class="card">
<h3>HomeKit</h3>
<div class="kvrow"><span class="kk">状态</span><span class="vv" id="i-hk-status">--</span></div>
<div class="kvrow"><span class="kk">已配对</span><span class="vv" id="i-hk-paired">--</span></div>
<div class="kvrow"><span class="kk">配对码</span><span class="vv" id="i-hk-code">--</span></div>
<div class="kvrow"><span class="kk">Setup Payload</span><span class="vv" id="i-hk-payload">--</span></div>
</div>
<div class="card">
<h3>维护</h3>
<div class="btns"><button class="btn-danger" onclick="reboot()">重启设备</button></div>
<div id="msg" style="margin-top:10px;font-size:13px;color:var(--orange)"></div>
</div>
</main><script>%s</script></body></html>)HTML",
        app_config::kDeviceName, kCss, kTabsHtml, kAdminScript);
    return written > 0 && static_cast<size_t>(written) < out_len;
}

}  // namespace web_pages
