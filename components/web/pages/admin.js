const $=id=>document.getElementById(id);
document.getElementById('t5').classList.add('active');
let homekitStatus=null;
let qrLibraryPromise=null;

function ensureQrLibrary(){
  if(window.QRCode)return Promise.resolve();
  if(qrLibraryPromise)return qrLibraryPromise;
  qrLibraryPromise=new Promise((resolve,reject)=>{
    const script=document.createElement('script');
    script.src='https://cdnjs.cloudflare.com/ajax/libs/qrcodejs/1.0.0/qrcode.min.js';
    script.onload=()=>resolve();
    script.onerror=()=>reject(new Error('QRCode library failed to load from cdnjs'));
    document.head.appendChild(script);
  });
  return qrLibraryPromise;
}

async function renderHomeKitQr(payload){
  const target=$('hk-qr');
  if(!target)return;
  if(!payload||payload==='-'){
    target.textContent='当前没有可用的 Setup Payload。';
    return;
  }
  target.textContent='二维码加载中...';
  try{
    await ensureQrLibrary();
  }catch(e){
    target.textContent='二维码库加载失败，请直接使用配对码。';
    return;
  }
  target.textContent='';
  target.innerHTML='';
  new QRCode(target,{
    text:payload,
    width:220,
    height:220,
    colorDark:'#09101d',
    colorLight:'#f7fbff',
    correctLevel:QRCode.CorrectLevel.M
  });
}

function openHomeKitModal(){
  if(!homekitStatus)return;
  $('hk-modal-code').textContent=homekitStatus.setup_code||'--';
  $('hk-modal-device').textContent=homekitStatus.accessory_name||'--';
  $('hk-modal-paired').textContent=(homekitStatus.paired_controllers||0)+' 个控制器';
  $('hk-modal-payload').textContent=homekitStatus.setup_payload||'--';
  $('hk-modal').classList.add('open');
  $('hk-modal').setAttribute('aria-hidden','false');
  renderHomeKitQr(homekitStatus.setup_payload||'');
}

function closeHomeKitModal(){
  $('hk-modal').classList.remove('open');
  $('hk-modal').setAttribute('aria-hidden','true');
}

async function loadInfo(){
  try{
    const r=await fetch('/api/status');const j=await r.json();
    homekitStatus=j.homekit||null;
    $('i-device').textContent=j.device;
    $('i-runtime').textContent=j.cn105.transport==='real'?'真实 CN105':'Mock CN105';
    $('i-uptime').textContent=Math.floor(j.uptime_ms/1000)+'s';
    $('i-wifi').textContent=j.wifi.ip+' ('+j.wifi.rssi+'dBm)';
    $('i-mac').textContent=j.wifi.mac;
    $('i-fs').textContent=j.filesystem.used_bytes+' / '+j.filesystem.total_bytes+' bytes';
    $('i-hk-status').textContent=j.homekit.started?'\u5df2\u542f\u52a8':'\u672a\u542f\u52a8';
    $('i-hk-paired').textContent=j.homekit.paired_controllers;
    $('i-hk-code').textContent=j.homekit.setup_code;
    $('i-log-current').textContent=j.log.current||'-';
    $('i-log-level').textContent=j.log.level||'-';
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

$('hk-modal-btn').addEventListener('click',openHomeKitModal);
$('hk-modal-close').addEventListener('click',closeHomeKitModal);
document.querySelectorAll('[data-close-modal="hk-modal"]').forEach(el=>el.addEventListener('click',closeHomeKitModal));
document.addEventListener('keydown',e=>{
  if(e.key==='Escape'&&$('hk-modal').classList.contains('open'))closeHomeKitModal();
});

loadInfo();
