const $=id=>document.getElementById(id);
document.getElementById('t5').classList.add('active');
let homekitStatus=null;
let qrLibraryPromise=null;
let uptimeAnchorMs=0;
let uptimeAnchorClientMs=0;
let uptimeTimer=null;
let settingsLoaded=false;
let otaUploading=false;
let otaUploadResult=null;
let otaApplying=false;
let cn105ModalSnapshot=null;
let settingsDirty=false;

const cn105AdvancedFieldIds=[
  'cfg-cn105-rx-pin',
  'cfg-cn105-tx-pin',
  'cfg-cn105-baud',
  'cfg-cn105-data-bits',
  'cfg-cn105-parity',
  'cfg-cn105-stop-bits',
  'cfg-cn105-rx-pullup',
  'cfg-cn105-tx-open-drain'
];
const settingsFieldIds=[
  'cfg-device-name',
  'cfg-wifi-ssid',
  'cfg-wifi-password',
  'cfg-homekit-code',
  'cfg-homekit-setup-id',
  'cfg-homekit-manufacturer',
  'cfg-homekit-model',
  'cfg-homekit-serial',
  'cfg-led-pin',
  'cfg-cn105-mode',
  'cfg-log-level',
  'cfg-poll-active',
  'cfg-poll-off',
  ...cn105AdvancedFieldIds
];

function cn105FormatSummary(){
  const dataBits=$('cfg-cn105-data-bits').value||'8';
  const parity=$('cfg-cn105-parity').value||'E';
  const stopBits=$('cfg-cn105-stop-bits').value||'1';
  const baud=$('cfg-cn105-baud').value||'2400';
  const rx=$('cfg-cn105-rx-pin').value||'26';
  const tx=$('cfg-cn105-tx-pin').value||'32';
  return `${dataBits}${parity}${stopBits} ${baud} RX G${rx} TX G${tx}`;
}

function updateCn105AdvancedSummary(){
  $('cn105-advanced-btn').textContent=(settingsDirty?'* ':'')+cn105FormatSummary();
}

function setSettingsDirty(dirty){
  settingsDirty=dirty;
  $('cfg-save-btn').disabled=!dirty;
  $('cfg-save-btn').textContent=dirty?'* 保存并重启':'保存并重启';
  $('cn105-advanced-btn').classList.toggle('dirty',dirty);
  updateCn105AdvancedSummary();
}

function markSettingsDirty(){
  setSettingsDirty(true);
}

function captureCn105AdvancedValues(){
  const values={};
  cn105AdvancedFieldIds.forEach(id=>{values[id]=$(id).value;});
  return values;
}

function restoreCn105AdvancedValues(values){
  if(!values)return;
  cn105AdvancedFieldIds.forEach(id=>{
    if(Object.prototype.hasOwnProperty.call(values,id))$(id).value=values[id];
  });
  updateCn105AdvancedSummary();
}

function openCn105Modal(){
  cn105ModalSnapshot=captureCn105AdvancedValues();
  updateCn105AdvancedSummary();
  $('cn105-modal').classList.add('open');
  $('cn105-modal').setAttribute('aria-hidden','false');
}

function closeCn105Modal(keepChanges,e){
  if(e){
    e.preventDefault();
    e.stopPropagation();
  }
  if(!keepChanges)restoreCn105AdvancedValues(cn105ModalSnapshot);
  cn105ModalSnapshot=null;
  updateCn105AdvancedSummary();
  if(keepChanges)markSettingsDirty();
  $('cn105-modal').classList.remove('open');
  $('cn105-modal').setAttribute('aria-hidden','true');
}

function renderTransportStatus(transportStatus){
  if(!transportStatus){
    $('tp').textContent='暂无传输层状态';
    return;
  }
  $('tp').textContent=
    'Phase: '+transportStatus.phase+'\nConnected: '+transportStatus.connected+
    '\nConnect Attempts: '+transportStatus.connect_attempts+'\nPoll Cycles: '+transportStatus.poll_cycles+
    '\nRX Packets: '+transportStatus.rx_packets+' / Errors: '+transportStatus.rx_errors+
    '\nTX Packets: '+transportStatus.tx_packets+'\nSets Pending: '+transportStatus.sets_pending+
    (transportStatus.last_error?'\nLast Error: '+transportStatus.last_error:'');
}

async function loadTransport(){
  try{
    const r=await fetch('/api/status');
    const j=await r.json();
    renderTransportStatus(j.cn105&&j.cn105.transport_status);
  }catch(e){
    $('tp').textContent='错误: '+e;
  }
}

function formatHomeKitCode(code){
  const digits=String(code||'').replace(/\D/g,'');
  if(digits.length!==8)return code||'--';
  return `${digits.slice(0,4)}-${digits.slice(4)}`;
}

function normalizeHomeKitCodeInput(code){
  return String(code||'').replace(/\D/g,'');
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

function formatDuration(ms){
  const totalSeconds=Math.max(0,Math.ceil((ms||0)/1000));
  const minutes=Math.floor(totalSeconds/60);
  const seconds=totalSeconds%60;
  return `${minutes}m ${String(seconds).padStart(2,'0')}s`;
}

function formatProvisioningStage(prov){
  if(!prov)return '未启用';
  const stage=prov.stage||'idle';
  if(stage==='starting')return '正在打开 BLE 配网';
  if(stage==='waiting')return '等待手机配网';
  if(stage==='connecting')return '正在连接新 WiFi';
  if(stage==='connected')return '已联网，即将重启';
  if(stage==='failed')return '新 WiFi 连接失败';
  if(stage==='timed-out')return '配网窗口已超时关闭';
  if(stage==='save-failed')return '新 WiFi 保存失败';
  if(stage==='start-failed')return 'BLE 配网启动失败';
  if(stage==='init-failed')return 'BLE 配网初始化失败';
  return prov.active?'BLE 配网进行中':'未激活';
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

function openNoticeModal(title,message){
  $('notice-title').textContent=title||'操作失败';
  $('notice-body').textContent=message||'请稍后重试。';
  $('notice-close').disabled=false;
  $('notice-close').style.display='';
  $('notice-modal').classList.add('open');
  $('notice-modal').setAttribute('aria-hidden','false');
}

function openRestartModal(title,message){
  $('notice-title').textContent=title||'正在重启';
  $('notice-body').textContent=message||'设备正在重启，页面将在 5 秒后自动刷新。';
  $('notice-close').disabled=true;
  $('notice-close').style.display='none';
  $('notice-modal').classList.add('open');
  $('notice-modal').setAttribute('aria-hidden','false');
  setTimeout(()=>window.location.reload(),5000);
}

function closeNoticeModal(e){
  if(e){
    e.preventDefault();
    e.stopPropagation();
  }
  if($('notice-close').disabled)return;
  $('notice-modal').classList.remove('open');
  $('notice-modal').setAttribute('aria-hidden','true');
}

async function openDeviceCfgModal(){
  const modal=$('device-cfg-modal');
  const editor=$('device-cfg-editor');
  $('device-cfg-msg').textContent='读取 device_cfg 中...';
  editor.value='加载中...';
  $('device-cfg-save').disabled=false;
  modal.classList.add('open');
  modal.setAttribute('aria-hidden','false');
  try{
    const r=await fetch('/api/config/device-cfg-json');
    const text=await r.text();
    if(!r.ok){
      editor.value='';
      $('device-cfg-msg').textContent='读取失败: '+text;
      return;
    }
    editor.value=text;
    $('device-cfg-msg').textContent='请谨慎编辑。点击取消不会写入任何内容。';
    editor.focus();
  }catch(e){
    editor.value='';
    $('device-cfg-msg').textContent='读取失败: '+e;
  }
}

function closeDeviceCfgModal(e){
  if(e){
    e.preventDefault();
    e.stopPropagation();
  }
  $('device-cfg-modal').classList.remove('open');
  $('device-cfg-modal').setAttribute('aria-hidden','true');
}

async function saveDeviceCfgJson(){
  let parsed=null;
  try{
    parsed=JSON.parse($('device-cfg-editor').value);
  }catch(e){
    $('device-cfg-msg').textContent='JSON 格式错误: '+e.message;
    return;
  }
  $('device-cfg-save').disabled=true;
  $('device-cfg-msg').textContent='正在写入 NVS...';
  try{
    const r=await fetch('/api/config/device-cfg-json',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(parsed)});
    const j=await r.json();
    if(!j.ok){
      $('device-cfg-save').disabled=false;
      $('device-cfg-msg').textContent='写入失败: '+(j.error||j.message||'unknown');
      return;
    }
    closeDeviceCfgModal();
    openRestartModal('NVS 已写入','device_cfg 已保存。设备正在重启，页面将在 5 秒后自动刷新。');
    await fetch('/api/reboot',{method:'POST'}).catch(()=>{});
  }catch(e){
    $('device-cfg-save').disabled=false;
    $('device-cfg-msg').textContent='写入失败: '+e;
  }
}

async function saveConfig(){
  const params=new URLSearchParams();
  const homekitCode=normalizeHomeKitCodeInput($('cfg-homekit-code').value.trim());
  if(homekitCode&&homekitCode.length!==8){
    openNoticeModal('保存失败','HomeKit 配对码需要 8 位数字，例如 1111-2222。');
    return;
  }
  params.set('device_name',$('cfg-device-name').value.trim()||'Mitsubishi AC');
  params.set('wifi_ssid',$('cfg-wifi-ssid').value.trim());
  const wifiPassword=$('cfg-wifi-password').value;
  if(wifiPassword)params.set('wifi_password',wifiPassword);
  if(homekitCode)params.set('homekit_code',homekitCode);
  params.set('homekit_manufacturer',$('cfg-homekit-manufacturer').value.trim());
  params.set('homekit_model',$('cfg-homekit-model').value.trim());
  params.set('homekit_serial',$('cfg-homekit-serial').value.trim());
  params.set('homekit_setup_id',$('cfg-homekit-setup-id').value.trim().toUpperCase());
  params.set('led_pin',$('cfg-led-pin').value);
  params.set('cn105_mode',$('cfg-cn105-mode').value);
  params.set('cn105_rx_pin',$('cfg-cn105-rx-pin').value);
  params.set('cn105_tx_pin',$('cfg-cn105-tx-pin').value);
  params.set('cn105_baud',$('cfg-cn105-baud').value);
  params.set('cn105_data_bits',$('cfg-cn105-data-bits').value);
  params.set('cn105_parity',$('cfg-cn105-parity').value);
  params.set('cn105_stop_bits',$('cfg-cn105-stop-bits').value);
  params.set('cn105_rx_pullup',$('cfg-cn105-rx-pullup').value);
  params.set('cn105_tx_open_drain',$('cfg-cn105-tx-open-drain').value);
  params.set('log_level',$('cfg-log-level').value);
  params.set('poll_active_ms',$('cfg-poll-active').value);
  params.set('poll_off_ms',$('cfg-poll-off').value);
    const button=$('cfg-save-btn');
    button.disabled=true;
  $('msg').textContent='设置保存中，成功后会自动重启...';
  try{
    const r=await fetch('/api/config/save',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:params.toString()});
    const j=await r.json();
    if(!j.ok){
      button.disabled=false;
      openNoticeModal('保存失败',j.error||j.message||'设备拒绝了这次设置保存。');
      return;
    }
    openRestartModal('设置已保存','设备正在重启，页面将在 5 秒后自动刷新。');
    await fetch('/api/reboot',{method:'POST'}).catch(()=>{});
  }catch(e){
    button.disabled=false;
    openNoticeModal('保存失败','请求没有成功发出或设备没有响应：'+e);
  }
}

function setOtaMessage(text,isError=false){
  const el=$('ota-msg');
  el.style.color=isError?'var(--red)':'var(--orange)';
  el.textContent=text;
}

function resetOtaUi(){
  otaUploadResult=null;
  otaApplying=false;
  const fileInput=$('ota-file');
  const progress=$('ota-progress');
  if(fileInput){
    fileInput.disabled=false;
    fileInput.value='';
  }
  if(progress){
    progress.style.display='none';
    progress.value=0;
  }
  setOtaMessage('');
}

function openOtaModal(result){
  otaUploadResult=result;
  otaApplying=false;
  $('ota-modal-current').textContent=result.current_version||'--';
  $('ota-modal-new').textContent=result.uploaded_version||'--';
  $('ota-modal-partition').textContent=result.partition||'--';
  $('ota-modal-status').textContent='固件已经上传完成。确认后设备会重启并切换到新的 OTA 分区。';
  $('ota-modal').querySelectorAll('button').forEach(button=>button.disabled=false);
  const warning=$('ota-modal-warning');
  if(result.same_or_older||result.rollback){
    warning.style.display='block';
    warning.textContent='注意：新版本号小于或等于当前版本。系统不会禁止回退/重刷，但请确认这是你想要的固件。';
  }else{
    warning.style.display='none';
    warning.textContent='';
  }
  $('ota-modal').classList.add('open');
  $('ota-modal').setAttribute('aria-hidden','false');
}

function closeOtaModal(reset=true){
  if(otaApplying)return;
  $('ota-modal').classList.remove('open');
  $('ota-modal').setAttribute('aria-hidden','true');
  if(reset)resetOtaUi();
}

function setOtaApplying(message){
  otaApplying=true;
  $('ota-modal-status').textContent=message;
  $('ota-modal').querySelectorAll('button').forEach(button=>button.disabled=true);
}

function handleOtaCancel(e){
  if(e){
    e.preventDefault();
    e.stopPropagation();
  }
  closeOtaModal(true);
}

function uploadOta(){
  if(otaUploading)return;
  otaUploadResult=null;
  const fileInput=$('ota-file');
  const file=fileInput&&fileInput.files&&fileInput.files[0];
  if(!file){
    setOtaMessage('请选择一个 .bin 固件文件。',true);
    return;
  }
  const lowerName=(file.name||'').toLowerCase();
  if(!lowerName.endsWith('.bin')){
    setOtaMessage('只允许上传 .bin 固件文件。',true);
    fileInput.value='';
    return;
  }
  if(!lowerName.endsWith('_0x20000.bin')){
    setOtaMessage('请选择地址为 0x20000 的 app 固件。',true);
    fileInput.value='';
    return;
  }

  const progress=$('ota-progress');
  progress.style.display='block';
  progress.value=0;
  otaUploading=true;
  fileInput.disabled=true;
  setOtaMessage(`OTA 上传中: ${file.name}`);

  const xhr=new XMLHttpRequest();
  xhr.open('POST','/api/ota/upload');
  xhr.setRequestHeader('Content-Type','application/octet-stream');
  xhr.upload.onprogress=e=>{
    if(e.lengthComputable){
      progress.value=Math.round((e.loaded/e.total)*100);
    }
  };
  xhr.onload=()=>{
    otaUploading=false;
    fileInput.disabled=false;
    let result=null;
    try{result=JSON.parse(xhr.responseText||'{}');}catch(e){}
    if(xhr.status>=200&&xhr.status<300&&result&&result.ok){
      progress.value=100;
      progress.style.display='none';
      fileInput.value='';
      setOtaMessage('');
      openOtaModal(result);
    }else{
      const err=(result&&result.error)||xhr.responseText||`HTTP ${xhr.status}`;
      setOtaMessage('OTA 上传失败: '+err,true);
    }
  };
  xhr.onerror=()=>{
    otaUploading=false;
    fileInput.disabled=false;
    setOtaMessage('OTA 上传失败: 网络错误',true);
  };
  xhr.send(file);
}

async function confirmOtaReboot(){
  if(!otaUploadResult)return;
  setOtaApplying('正在应用 OTA 更新，设备会重启。页面将在 5 秒后自动刷新。');
  setOtaMessage('');
  try{
    const r=await fetch('/api/ota/apply',{method:'POST'});
    if(!r.ok){
      const j=await r.json().catch(()=>({}));
      otaApplying=false;
      $('ota-modal').querySelectorAll('button').forEach(button=>button.disabled=false);
      $('ota-modal-status').textContent='OTA 应用失败，请检查错误后重试或重新上传。';
      setOtaMessage('OTA 应用失败: '+(j.error||`HTTP ${r.status}`),true);
      return;
    }
  }catch(e){
    // The reboot can close the TCP connection before the browser sees a response.
  }
  setTimeout(()=>window.location.reload(),5000);
}

function handleOtaConfirm(e){
  if(e){
    e.preventDefault();
    e.stopPropagation();
  }
  confirmOtaReboot();
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
    const prov=j.provisioning||{};
    $('i-device').textContent=j.device;
    $('i-version').textContent=j.version||'--';
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
    $('i-prov-state').textContent=formatProvisioningStage(prov);
    $('i-prov-service').textContent=prov.service_name||`GPIO${prov.button_gpio||39}`;
    $('i-prov-remaining').textContent=prov.active?formatDuration(prov.remaining_ms||0):'--';
    $('i-prov-result').textContent=prov.last_result||'--';
    $('i-prov-pending').textContent=prov.pending_ssid||'--';
    renderTransportStatus(j.cn105&&j.cn105.transport_status);
    if(!settingsLoaded){
      $('cfg-device-name').value=(j.config&&j.config.device_name)||j.device||'';
      $('cfg-wifi-ssid').value=(j.config&&j.config.wifi_ssid)||'';
      $('cfg-wifi-password').value='';
      $('cfg-wifi-password').placeholder=(j.config&&j.config.wifi_password_set)?'已设置，留空则不修改':'未设置';
      $('cfg-homekit-code').value=formatHomeKitCode(j.config&&j.config.homekit_code);
      $('cfg-homekit-manufacturer').value=(j.config&&j.config.homekit_manufacturer)||'';
      $('cfg-homekit-model').value=(j.config&&j.config.homekit_model)||'';
      $('cfg-homekit-serial').value=(j.config&&j.config.homekit_serial)||'';
      $('cfg-homekit-setup-id').value=(j.config&&j.config.homekit_setup_id)||'';
      $('cfg-led-pin').value=(j.config&&j.config.led_pin)||27;
      $('cfg-cn105-mode').value=(j.config&&j.config.cn105_mode)||'real';
      $('cfg-cn105-rx-pin').value=(j.config&&j.config.cn105_rx_pin)||26;
      $('cfg-cn105-tx-pin').value=(j.config&&j.config.cn105_tx_pin)||32;
      $('cfg-cn105-baud').value=String((j.config&&j.config.cn105_baud)||2400);
      $('cfg-cn105-data-bits').value=String((j.config&&j.config.cn105_data_bits)||8);
      $('cfg-cn105-parity').value=(j.config&&j.config.cn105_parity)||'E';
      $('cfg-cn105-stop-bits').value=String((j.config&&j.config.cn105_stop_bits)||1);
      $('cfg-cn105-rx-pullup').value=(j.config&&j.config.cn105_rx_pullup)===false?'0':'1';
      $('cfg-cn105-tx-open-drain').value=(j.config&&j.config.cn105_tx_open_drain)?'1':'0';
      $('cfg-log-level').value=(j.config&&j.config.log_level)||'info';
      $('cfg-poll-active').value=(j.config&&j.config.poll_active_ms)||15000;
      $('cfg-poll-off').value=(j.config&&j.config.poll_off_ms)||60000;
      updateCn105AdvancedSummary();
      settingsLoaded=true;
      setSettingsDirty(false);
    }
  }catch(e){$('msg').textContent='\u52a0\u8f7d\u5931\u8d25: '+e;}
}
async function reboot(){
  if(!confirm('\u786e\u5b9a\u8981\u91cd\u542f\u8bbe\u5907\u5417\uff1f'))return;
  openRestartModal('正在重启','设备正在重启，页面将在 5 秒后自动刷新。');
  try{await fetch('/api/reboot',{method:'POST'});}
  catch(e){}
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
$('transport-refresh-btn').addEventListener('click',loadTransport);
$('cn105-advanced-btn').addEventListener('click',openCn105Modal);
$('cn105-modal-confirm').addEventListener('click',e=>closeCn105Modal(true,e));
$('cn105-modal-cancel').addEventListener('click',e=>closeCn105Modal(false,e));
cn105AdvancedFieldIds.forEach(id=>$(id).addEventListener('change',updateCn105AdvancedSummary));
cn105AdvancedFieldIds.forEach(id=>$(id).addEventListener('input',updateCn105AdvancedSummary));
settingsFieldIds.forEach(id=>$(id).addEventListener('change',markSettingsDirty));
settingsFieldIds.forEach(id=>$(id).addEventListener('input',markSettingsDirty));
$('device-cfg-modal-btn').addEventListener('click',openDeviceCfgModal);
$('device-cfg-cancel').addEventListener('click',closeDeviceCfgModal);
$('device-cfg-save').addEventListener('click',saveDeviceCfgJson);
$('ota-file').addEventListener('change',uploadOta);
$('ota-modal-cancel').addEventListener('click',handleOtaCancel);
$('ota-modal-confirm').addEventListener('click',handleOtaConfirm);
$('notice-close').addEventListener('click',closeNoticeModal);
$('cfg-homekit-code').addEventListener('blur',()=>{$('cfg-homekit-code').value=normalizeHomeKitCodeInput($('cfg-homekit-code').value);});
document.querySelectorAll('[data-close-modal="hk-modal"]').forEach(el=>el.addEventListener('click',closeHomeKitModal));
document.querySelectorAll('[data-close-modal="ota-modal"]').forEach(el=>el.addEventListener('click',handleOtaCancel));
document.querySelectorAll('[data-close-modal="notice-modal"]').forEach(el=>el.addEventListener('click',closeNoticeModal));
document.querySelectorAll('[data-close-modal="cn105-modal"]').forEach(el=>el.addEventListener('click',e=>closeCn105Modal(false,e)));
document.querySelectorAll('[data-close-modal="device-cfg-modal"]').forEach(el=>el.addEventListener('click',closeDeviceCfgModal));
document.addEventListener('keydown',e=>{
  if(e.key==='Escape'&&$('hk-modal').classList.contains('open'))closeHomeKitModal();
  if(e.key==='Escape'&&$('ota-modal').classList.contains('open'))handleOtaCancel(e);
  if(e.key==='Escape'&&$('notice-modal').classList.contains('open')&&!$('notice-close').disabled)closeNoticeModal(e);
  if(e.key==='Escape'&&$('cn105-modal').classList.contains('open'))closeCn105Modal(false,e);
  if(e.key==='Escape'&&$('device-cfg-modal').classList.contains('open'))closeDeviceCfgModal(e);
});

loadInfo();
setInterval(loadInfo,3000);
