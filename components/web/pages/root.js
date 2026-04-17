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
  $('s-temp').textContent=m.target_temperature_f+'\u00b0F';
  $('s-room').textContent=m.room_temperature_f+'\u00b0F';
  $('s-fan').textContent=m.fan;
  $('s-wifi').textContent=s.wifi.connected?s.wifi.ip+' ('+s.wifi.rssi+'dBm)':'\u65ad\u5f00';
  const t=s.cn105.transport_status;
  $('s-transport').textContent=s.cn105.transport==='real'?(t.connected?'\u5df2\u8fde\u63a5':'\u8fde\u63a5\u4e2d...'):'Mock';
  $('s-hk').textContent=s.homekit.started?(s.homekit.paired_controllers+'\u4e2a\u63a7\u5236\u5668'):'\u672a\u542f\u52a8';
}
async function refresh(){
  try{const r=await fetch('/api/status');const j=await r.json();fill(j);$('out').textContent='\u5c31\u7eea';}
  catch(e){$('out').textContent='\u83b7\u53d6\u72b6\u6001\u5931\u8d25: '+e;}
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
    $('out').textContent=j.ok?'\u5df2\u53d1\u9001':'\u9519\u8bef: '+(j.error||'\u672a\u77e5');
    if(j.ok)setTimeout(refresh,300);
  }catch(e){$('out').textContent='\u53d1\u9001\u5931\u8d25: '+e;}
}
refresh();setInterval(refresh,5000);
