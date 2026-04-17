const $=id=>document.getElementById(id);
document.getElementById('t2').classList.add('active');
const out=$('out');
function show(t,d){out.textContent=t+'\n'+(typeof d==='string'?d:JSON.stringify(d,null,2));}
async function api(p){try{const r=await fetch(p);show(p,await r.json());}catch(e){show('\u9519\u8bef',''+e);}}
async function decode(){
  const h=$('hex').value.trim();
  if(!h){show('\u9519\u8bef','\u8bf7\u8f93\u5165 hex \u6570\u636e');return;}
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
  }catch(e){$('tp').textContent='\u9519\u8bef: '+e;}
}
loadTransport();
