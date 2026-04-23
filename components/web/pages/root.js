const $=id=>document.getElementById(id);
document.getElementById('t1').classList.add('active');
const formIds=['power','mode','temp','fan','vane','wide'];
let draftLocked=false;

function currentFormState(){
  return {
    power:$('power').value,
    mode:$('mode').value,
    temp:String($('temp').value),
    fan:$('fan').value,
    vane:$('vane').value,
    wide:$('wide').value,
  };
}

function remoteFormState(m){
  return {
    power:m.power,
    mode:m.mode,
    temp:String(m.target_temperature_f),
    fan:m.fan,
    vane:m.vane,
    wide:m.wide_vane,
  };
}

function sameFormState(a,b){
  return a.power===b.power&&a.mode===b.mode&&a.temp===b.temp&&a.fan===b.fan&&a.vane===b.vane&&a.wide===b.wide;
}

function updateDraftLockFromRemote(m){
  if(!draftLocked){
    return;
  }
  if(sameFormState(currentFormState(),remoteFormState(m))){
    draftLocked=false;
  }
}

function fill(s){
  const m=s.cn105.mock_state;
  updateDraftLockFromRemote(m);
  if(!draftLocked){
    $('power').value=m.power;
    $('mode').value=m.mode;
    $('temp').value=m.target_temperature_f;
    $('fan').value=m.fan;
    $('vane').value=m.vane;
    $('wide').value=m.wide_vane;
  }
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
  try{const r=await fetch('/api/status');const j=await r.json();fill(j);$('out').textContent=draftLocked?'\u672c\u5730\u8349\u7a3f\u672a\u53d1\u9001':'\u5c31\u7eea';}
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
    if(j.ok){draftLocked=false;setTimeout(refresh,300);}
  }catch(e){$('out').textContent='\u53d1\u9001\u5931\u8d25: '+e;}
}
formIds.forEach(id=>$(id).addEventListener('input',()=>{draftLocked=true;}));
formIds.forEach(id=>$(id).addEventListener('change',()=>{draftLocked=true;}));
refresh();setInterval(refresh,5000);
