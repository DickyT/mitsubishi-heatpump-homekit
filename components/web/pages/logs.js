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
    setText('log-active',j.active?'写入中':'未启用');
    setText('log-current',j.current||'-');
    setText('log-size',(j.current_bytes||0)+' bytes');
    setText('log-level',j.level||'-');
    logFiles=j.logs||[];
    const list=$('log-list');list.innerHTML='';
    if(!j.logs||!j.logs.length){list.textContent='没有日志文件。';return;}
    j.logs.forEach(log=>{
      const row=document.createElement('div');row.className='listrow';
      const meta=document.createElement('div');meta.className='listmeta';
      meta.textContent=(log.current?'[当前] ':'')+log.name;
      const small=document.createElement('small');small.textContent=log.size+' bytes';meta.appendChild(small);
      const actions=document.createElement('div');actions.className='actions';
      const view=document.createElement('button');view.className='btn-secondary';view.textContent='查看';view.onclick=()=>loadLog(log.name);
      const down=document.createElement('a');down.className='linkbtn';down.href='/api/log/file?file='+encodeURIComponent(log.name);down.textContent='下载';
      const del=document.createElement('button');
      del.className='btn-danger';
      del.textContent=log.current?'清空':'删除';
      del.onclick=()=>log.current?clearCurrentLog():deleteLog(log.name);
      actions.appendChild(view);actions.appendChild(down);actions.appendChild(del);row.appendChild(meta);row.appendChild(actions);list.appendChild(row);
    });
    const current=j.logs.find(x=>x.current)||j.logs[0];if(current)loadLog(current.name);
  }catch(e){$('log-list').textContent='读取日志失败: '+e;}
}
async function loadLog(name){
  selectedLog=name;
  $('log-body').textContent='读取 '+name+' ...';
  try{const r=await fetch('/api/log/file?file='+encodeURIComponent(name));$('log-body').textContent=await r.text();}
  catch(e){$('log-body').textContent='读取失败: '+e;}
}
async function deleteLog(name){
  if(!confirm('删除日志 '+name+' ?'))return;
  try{
    const r=await fetch('/api/files/delete?path='+encodeURIComponent(name),{method:'POST'});
    const j=await r.json();
    if(!j.ok){
      alert('删除失败: '+(j.message||j.error||'unknown'));
      return;
    }
    if(selectedLog===name){
      selectedLog='';
      $('log-body').textContent='日志已删除。';
    }
    loadLogs();
  }catch(e){
    alert('删除失败: '+e);
  }
}
async function clearCurrentLog(){
  if(!confirm('清空当前日志吗？日志会在同一个文件里继续写入。'))return;
  try{
    const r=await fetch('/api/maintenance/clear-logs',{method:'POST'});
    const j=await r.json();
    if(!j.ok){
      alert('清空失败: '+(j.message||j.error||'unknown'));
      return;
    }
    $('log-body').textContent='当前日志已清空。';
    liveOffset=0;
    loadLogs();
  }catch(e){
    alert('清空失败: '+e);
  }
}
async function deleteAllLogs(){
  if(!logFiles.length){
    alert('没有日志文件。');
    return;
  }
  if(!confirm('删除所有历史日志并清空当前日志吗？这个操作不可恢复。'))return;
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
  $('log-body').textContent=failed?'部分日志清理失败，请刷新后重试。':'所有日志已删除，当前日志已清空。';
  loadLogs();
}
async function pollLive(){
  try{
    const r=await fetch('/api/log/live?offset='+liveOffset);const j=await r.json();
    if(!j.ok){$('live-body').textContent='Live 失败: '+(j.error||'unknown');return;}
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
