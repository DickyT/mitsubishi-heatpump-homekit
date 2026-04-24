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
  $('s-wifi').textContent=s.wifi.connected?s.wifi.ip+' ('+s.wifi.rssi+'dBm)':'Disconnected';
  const t=s.cn105.transport_status;
  $('s-transport').textContent=s.cn105.transport==='real'?(t.connected?'Connected':'Connecting...'):'Mock';
  $('s-hk').textContent=s.homekit.started?(s.homekit.paired_controllers+' controller(s)'):'Not started';
}
async function refresh(){
  try{const r=await fetch('/api/status');const j=await r.json();fill(j);$('out').textContent=draftLocked?'Local draft not sent':'Ready';}
  catch(e){$('out').textContent='Status fetch failed: '+e;}
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
    $('out').textContent=j.ok?'Sent':'Error: '+(j.error||'unknown');
    if(j.ok){draftLocked=false;setTimeout(refresh,300);}
  }catch(e){$('out').textContent='Send failed: '+e;}
}
formIds.forEach(id=>$(id).addEventListener('input',()=>{draftLocked=true;}));
formIds.forEach(id=>$(id).addEventListener('change',()=>{draftLocked=true;}));
refresh();setInterval(refresh,5000);
