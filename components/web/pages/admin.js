const $=id=>document.getElementById(id);
document.getElementById('t5').classList.add('active');
let homekitStatus=null;
let qrLibraryPromise=null;
let uptimeAnchorMs=0;
let uptimeAnchorClientMs=0;
let uptimeTimer=null;

function formatHomeKitCode(code){
  const digits=String(code||'').replace(/\D/g,'');
  if(digits.length!==8)return code||'--';
  return `${digits.slice(0,4)}-${digits.slice(4)}`;
}

function normalizeHomeKitCodeInput(code){
  return formatHomeKitCode(code).replace(/\s+/g,'');
}

function formatUptime(ms){
  const totalSeconds=Math.max(0,Math.floor(ms/1000));
  const days=Math.floor(totalSeconds/86400);
  const hours=Math.floor((totalSeconds%86400)/3600);
  const minutes=Math.floor((totalSeconds%3600)/60);
  const seconds=totalSeconds%60;
  const hh=String(hours).padStart(2,'0');
  const mm=String(minutes).padStart(2,'0');
  const ss=String(seconds).padStart(2,'0');
  return days>0?`${days}d ${hh}:${mm}:${ss}`:`${hh}:${mm}:${ss}`;
}

function formatBootTime(unixMs,valid){
  if(!unixMs)return '--';
  const d=new Date(unixMs);
  if(Number.isNaN(d.getTime()))return '--';
  return d.toLocaleString();
}

function signalIcon(rssi){
  if(typeof rssi!=='number'||Number.isNaN(rssi))return '▱▱▱';
  if(rssi>=-55)return '▰▰▰';
  if(rssi>=-67)return '▰▰▱';
  if(rssi>=-75)return '▰▱▱';
  return '▱▱▱';
}

function refreshLocalUptime(){
  if(!uptimeAnchorMs||!uptimeAnchorClientMs)return;
  const elapsed=Math.max(0,Date.now()-uptimeAnchorClientMs);
  $('i-uptime').textContent=formatUptime(uptimeAnchorMs+elapsed);
}

function syncUptime(status){
  uptimeAnchorMs=status.uptime_ms||0;
  uptimeAnchorClientMs=Date.now();
  $('i-boot-time').textContent=formatBootTime(Date.now()-uptimeAnchorMs,true);
  refreshLocalUptime();
  if(!uptimeTimer){
    uptimeTimer=setInterval(refreshLocalUptime,1000);
  }
}

async function saveConfig(){
  const params=new URLSearchParams();
  params.set('device_name',$('cfg-device-name').value.trim()||'Mitsubishi AC');
  params.set('homekit_code',normalizeHomeKitCodeInput($('cfg-homekit-code').value.trim()));
  params.set('led_enabled',$('cfg-led-enabled').value);
  params.set('cn105_mode',$('cfg-cn105-mode').value);
  params.set('cn105_baud',$('cfg-cn105-baud').value);
  params.set('log_level',$('cfg-log-level').value);
  params.set('poll_active_ms',$('cfg-poll-active').value);
  params.set('poll_off_ms',$('cfg-poll-off').value);
  $('msg').textContent='设置保存中...';
  try{
    const r=await fetch('/api/config/save',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:params.toString()});
    const j=await r.json();
    $('msg').textContent=(j.ok?'完成: ':'失败: ')+(j.message||'设置已保存')+(j.reboot_required?'\n部分改动需要重启后完全生效。':'');
    await loadInfo();
  }catch(e){
    $('msg').textContent='保存失败: '+e;
  }
}

function setOtaMessage(text,isError=false){
  const el=$('ota-msg');
  el.style.color=isError?'var(--red)':'var(--orange)';
  el.textContent=text;
}

function uploadOta(){
  const fileInput=$('ota-file');
  const file=fileInput&&fileInput.files&&fileInput.files[0];
  if(!file){
    setOtaMessage('请选择一个 .bin 固件文件。',true);
    return;
  }
  if(!file.name.endsWith('.bin')){
    if(!confirm('这个文件名不是 .bin，仍然继续上传吗？'))return;
  }
  if(!confirm('确定上传 OTA 固件吗？上传成功后需要重启才会切换到新固件。'))return;

  const progress=$('ota-progress');
  progress.style.display='block';
  progress.value=0;
  setOtaMessage('OTA 上传中...');

  const xhr=new XMLHttpRequest();
  xhr.open('POST','/api/ota/upload');
  xhr.setRequestHeader('Content-Type','application/octet-stream');
  xhr.upload.onprogress=e=>{
    if(e.lengthComputable){
      progress.value=Math.round((e.loaded/e.total)*100);
    }
  };
  xhr.onload=()=>{
    let result=null;
    try{result=JSON.parse(xhr.responseText||'{}');}catch(e){}
    if(xhr.status>=200&&xhr.status<300&&result&&result.ok){
      progress.value=100;
      const warning=result.rollback?`\n提醒: ${result.warning}`:'';
      setOtaMessage(`上传完成: ${result.uploaded_version||'unknown'} -> ${result.partition||'OTA'}。点击“重启应用更新”后生效。${warning}`,false);
    }else{
      const err=(result&&result.error)||xhr.responseText||`HTTP ${xhr.status}`;
      setOtaMessage('OTA 上传失败: '+err,true);
    }
  };
  xhr.onerror=()=>setOtaMessage('OTA 上传失败: 网络错误',true);
  xhr.send(file);
}


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
  $('hk-modal-code').textContent=formatHomeKitCode(homekitStatus.setup_code);
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
    $('i-version').textContent=j.version||'--';
    $('ota-current-version').textContent=j.version||'--';
    $('i-runtime').textContent=j.cn105.transport==='real'?'真实 CN105':'Mock CN105';
    syncUptime(j);
    $('i-wifi-info').textContent=`${j.wifi.ssid||'--'} | ${j.wifi.ip||'0.0.0.0'} | ${signalIcon(j.wifi.rssi)} ${j.wifi.rssi} dBm | BSSID ${j.wifi.bssid||'--'}`;
    $('i-mac').textContent=j.wifi.mac;
    $('i-fs').textContent=j.filesystem.used_bytes+' / '+j.filesystem.total_bytes+' bytes';
    $('i-hk-status').textContent=j.homekit.started?'\u5df2\u542f\u52a8':'\u672a\u542f\u52a8';
    $('i-hk-paired').textContent=j.homekit.paired_controllers;
    $('i-hk-code').textContent=formatHomeKitCode(j.homekit.setup_code);
    $('i-hk-model').textContent=j.homekit.model||'--';
    $('i-hk-fw').textContent=j.homekit.firmware_revision||'--';
    $('i-log-current').textContent=j.log.current||'-';
    $('i-log-level').textContent=j.log.level||'-';
    $('cfg-device-name').value=(j.config&&j.config.device_name)||j.device||'';
    $('cfg-homekit-code').value=formatHomeKitCode(j.config&&j.config.homekit_code);
    $('cfg-led-enabled').value=(j.config&&j.config.led_enabled)===false?'0':'1';
    $('cfg-cn105-mode').value=(j.config&&j.config.cn105_mode)||'real';
    $('cfg-cn105-baud').value=String((j.config&&j.config.cn105_baud)||2400);
    $('cfg-log-level').value=(j.config&&j.config.log_level)||'info';
    $('cfg-poll-active').value=(j.config&&j.config.poll_active_ms)||15000;
    $('cfg-poll-off').value=(j.config&&j.config.poll_off_ms)||60000;
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
$('cfg-save-btn').addEventListener('click',saveConfig);
$('ota-upload-btn').addEventListener('click',uploadOta);
$('cfg-homekit-code').addEventListener('blur',()=>{$('cfg-homekit-code').value=normalizeHomeKitCodeInput($('cfg-homekit-code').value);});
document.querySelectorAll('[data-close-modal="hk-modal"]').forEach(el=>el.addEventListener('click',closeHomeKitModal));
document.addEventListener('keydown',e=>{
  if(e.key==='Escape'&&$('hk-modal').classList.contains('open'))closeHomeKitModal();
});

loadInfo();
