const $=id=>document.getElementById(id);
document.getElementById('t5').classList.add('active');
async function loadInfo(){
  try{
    const r=await fetch('/api/status');const j=await r.json();
    $('i-device').textContent=j.device;
    $('i-phase').textContent=j.phase;
    $('i-uptime').textContent=Math.floor(j.uptime_ms/1000)+'s';
    $('i-wifi').textContent=j.wifi.ip+' ('+j.wifi.rssi+'dBm)';
    $('i-mac').textContent=j.wifi.mac;
    $('i-fs').textContent=j.filesystem.used_bytes+' / '+j.filesystem.total_bytes+' bytes';
    $('i-hk-status').textContent=j.homekit.started?'\u5df2\u542f\u52a8':'\u672a\u542f\u52a8';
    $('i-hk-paired').textContent=j.homekit.paired_controllers;
    $('i-hk-code').textContent=j.homekit.setup_code;
    $('i-hk-payload').textContent=j.homekit.setup_payload||'-';
    $('i-transport').textContent=j.cn105.transport;
  }catch(e){$('msg').textContent='\u52a0\u8f7d\u5931\u8d25: '+e;}
}
async function reboot(){
  if(!confirm('\u786e\u5b9a\u8981\u91cd\u542f\u8bbe\u5907\u5417\uff1f'))return;
  try{await fetch('/api/reboot',{method:'POST'});$('msg').textContent='\u91cd\u542f\u4e2d...';}
  catch(e){$('msg').textContent='\u91cd\u542f\u8bf7\u6c42\u5df2\u53d1\u9001';}
}
async function maintenance(url,label,prompt){
  if(!confirm(prompt))return;
  $('msg').textContent=label+'\u6267\u884c\u4e2d...';
  try{
    const r=await fetch(url,{method:'POST'});const j=await r.json();
    $('msg').textContent=(j.ok?'\u5b8c\u6210: ':'\u5931\u8d25: ')+(j.message||label)+(j.rebooting?'\n\u8bbe\u5907\u5c06\u91cd\u542f...':'');
    setTimeout(loadInfo,800);
  }catch(e){$('msg').textContent=label+'\u8bf7\u6c42\u5931\u8d25: '+e;}
}
loadInfo();
