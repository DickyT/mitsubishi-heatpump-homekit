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
document.getElementById('t3').classList.add('active');
let liveOffset=0;
let liveTimer=null;
let selectedLog='';
let logFiles=[];
function setText(id,v){$(id).textContent=v;}
async function loadLogs(){
  try{
    const r=await fetch('/api/logs');const j=await r.json();
    setText('log-active',j.active?'Writing':'Disabled');
    setText('log-current',j.current||'-');
    setText('log-size',(j.current_bytes||0)+' bytes');
    setText('log-level',j.level||'-');
    logFiles=j.logs||[];
    const list=$('log-list');list.innerHTML='';
    if(!j.logs||!j.logs.length){list.textContent='No log files.';return;}
    j.logs.forEach(log=>{
      const row=document.createElement('div');row.className='listrow';
      const meta=document.createElement('div');meta.className='listmeta';
      meta.textContent=log.name+(log.current?' (current)':'');
      const small=document.createElement('small');small.textContent=log.size+' bytes';meta.appendChild(small);
      const actions=document.createElement('div');actions.className='actions';
      const view=document.createElement('button');view.className='btn-secondary btn-compact';view.textContent='View';view.onclick=()=>loadLog(log.name);
      const down=document.createElement('a');down.className='linkbtn btn-compact';down.href='/api/log/file?file='+encodeURIComponent(log.name);down.textContent='Download';
      const del=document.createElement('button');
      del.className='btn-danger btn-compact';
      del.textContent=log.current?'Clear':'Delete';
      del.onclick=()=>log.current?clearCurrentLog():deleteLog(log.name);
      actions.appendChild(view);actions.appendChild(down);actions.appendChild(del);row.appendChild(meta);row.appendChild(actions);list.appendChild(row);
    });
    const current=j.logs.find(x=>x.current)||j.logs[0];if(current)loadLog(current.name);
  }catch(e){$('log-list').textContent='Failed to read logs: '+e;}
}
async function loadLog(name){
  selectedLog=name;
  $('log-body').textContent='Reading '+name+' ...';
  try{const r=await fetch('/api/log/file?file='+encodeURIComponent(name));$('log-body').textContent=await r.text();}
  catch(e){$('log-body').textContent='Read failed: '+e;}
}
async function deleteLog(name){
  if(!confirm('Delete log '+name+'?'))return;
  try{
    const r=await fetch('/api/files/delete?path='+encodeURIComponent(name),{method:'POST'});
    const j=await r.json();
    if(!j.ok){
      alert('Delete failed: '+(j.message||j.error||'unknown'));
      return;
    }
    if(selectedLog===name){
      selectedLog='';
      $('log-body').textContent='Log deleted.';
    }
    loadLogs();
  }catch(e){
    alert('Delete failed: '+e);
  }
}
async function clearCurrentLog(){
  if(!confirm('Clear the current log? Logging will continue in the same file.'))return;
  try{
    const r=await fetch('/api/maintenance/clear-logs',{method:'POST'});
    const j=await r.json();
    if(!j.ok){
      alert('Clear failed: '+(j.message||j.error||'unknown'));
      return;
    }
    $('log-body').textContent='Current log cleared.';
    liveOffset=0;
    loadLogs();
  }catch(e){
    alert('Clear failed: '+e);
  }
}
async function deleteAllLogs(){
  if(!logFiles.length){
    alert('No log files.');
    return;
  }
  if(!confirm('Delete all historical logs and clear the current log? This cannot be undone.'))return;
  let failed=0;
  for(const log of logFiles){
    if(log.current)continue;
    try{
      const r=await fetch('/api/files/delete?path='+encodeURIComponent(log.name),{method:'POST'});
      const j=await r.json();
      if(!j.ok)failed++;
    }catch(e){
      failed++;
    }
  }
  try{
    const r=await fetch('/api/maintenance/clear-logs',{method:'POST'});
    const j=await r.json();
    if(!j.ok)failed++;
  }catch(e){
    failed++;
  }
  selectedLog='';
  liveOffset=0;
  $('log-body').textContent=failed?'Some logs could not be cleared. Refresh and try again.':'All logs deleted. Current log cleared.';
  loadLogs();
}
async function pollLive(){
  try{
    const r=await fetch('/api/log/live?offset='+liveOffset);const j=await r.json();
    if(!j.ok){$('live-body').textContent='Live failed: '+(j.error||'unknown');return;}
    if(j.reset)$('live-body').textContent='';
    if(j.text)$('live-body').textContent+=j.text;
    liveOffset=j.nextOffset||0;
    if($('live-body').textContent.length>24000)$('live-body').textContent=$('live-body').textContent.slice(-16000);
    $('live-body').scrollTop=$('live-body').scrollHeight;
  }catch(e){$('live-body').textContent+='\n[live error] '+e;}
}
function openLive(){if(liveTimer)clearInterval(liveTimer);$('live-card').style.display='block';$('live-body').textContent='';liveOffset=0;pollLive();liveTimer=setInterval(pollLive,1500);}
function closeLive(){if(liveTimer)clearInterval(liveTimer);liveTimer=null;$('live-card').style.display='none';}
loadLogs();
