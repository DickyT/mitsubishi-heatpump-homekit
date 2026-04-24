/****************************************************************************
 * Kiri Bridge
 * CN105 HomeKit controller for Mitsubishi heat pumps
 * https://kiri.dkt.moe
 * https://github.com/DickyT/kiri-homekit
 *
 * Copyright (c) 2026
 * All Rights Reserved.
 * Licensed under terms of the GPL-3.0 License.
 ****************************************************************************/

const $=id=>document.getElementById(id);
document.getElementById('t1').classList.add('active');
const formIds=['power','mode','temp','fan','vane','wide'];
let draftLocked=false;
let sending=false;

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

function setFormState(state){
  $('power').value=state.power;
  $('mode').value=state.mode;
  $('temp').value=state.temp;
  $('fan').value=state.fan;
  $('vane').value=state.vane;
  $('wide').value=state.wide;
}

function setBusy(busy){
  sending=busy;
  formIds.forEach(id=>$(id).disabled=busy);
  document.querySelectorAll('.card button').forEach(button=>button.disabled=busy);
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
  const name=(s.config&&s.config.device_name)||s.device||'Kiri Bridge';
  $('page-title').textContent=name;
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
async function refresh(force=false){
  if(sending){
    return;
  }
  try{
    $('out').textContent=force?'Querying CN105 info 0x02/0x03/0x06...':'Loading...';
    const r=await fetch(force?'/api/cn105/refresh':'/api/status',{method:force?'POST':'GET'});
    const j=await r.json();
    if(!j.ok&&j.error){
      $('out').textContent='Refresh failed: '+j.error;
      return;
    }
    fill(j);
    $('out').textContent=draftLocked?'Local draft not sent':'Ready';
  }
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
  if(sending){
    return;
  }
  const previous=currentFormState();
  try{
    setBusy(true);
    $('out').textContent='Sending command and confirming CN105 state...';
    const r=await fetch('/api/cn105/mock/build-set?'+params(),{method:'POST'});
    const j=await r.json();
    if(j.ok){
      draftLocked=false;
      if(j.mock_state){
        setFormState(remoteFormState(j.mock_state));
      }
      $('out').textContent='Sent and confirmed';
      setTimeout(refresh,300);
      return;
    }
    setFormState(previous);
    draftLocked=false;
    $('out').textContent='Send failed: '+(j.error||'unknown');
  }catch(e){
    setFormState(previous);
    draftLocked=false;
    $('out').textContent='Send failed: '+e;
  }finally{
    setBusy(false);
  }
}
formIds.forEach(id=>$(id).addEventListener('input',()=>{draftLocked=true;}));
formIds.forEach(id=>$(id).addEventListener('change',()=>{draftLocked=true;}));
refresh();setInterval(refresh,5000);
