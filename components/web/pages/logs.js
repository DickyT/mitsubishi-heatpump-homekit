const $=id=>document.getElementById(id);
document.getElementById('t3').classList.add('active');
let liveOffset=0;
let liveTimer=null;
function setText(id,v){$(id).textContent=v;}
async function loadLogs(){
  try{
    const r=await fetch('/api/logs');const j=await r.json();
    setText('log-active',j.active?'写入中':'未启用');
    setText('log-current',j.current||'-');
    setText('log-size',(j.current_bytes||0)+' bytes');
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
      actions.appendChild(view);actions.appendChild(down);row.appendChild(meta);row.appendChild(actions);list.appendChild(row);
    });
    const current=j.logs.find(x=>x.current)||j.logs[0];if(current)loadLog(current.name);
  }catch(e){$('log-list').textContent='读取日志失败: '+e;}
}
async function loadLog(name){
  $('log-body').textContent='读取 '+name+' ...';
  try{const r=await fetch('/api/log/file?file='+encodeURIComponent(name));$('log-body').textContent=await r.text();}
  catch(e){$('log-body').textContent='读取失败: '+e;}
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
