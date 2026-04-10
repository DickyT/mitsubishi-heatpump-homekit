#pragma once

#include <WebServer.h>
#include <WiFi.h>

#include "AppConfig.h"
#include "DebugLog.h"
#include "FileSystemManager.h"
#include "MitsubishiProtocol.h"

class WebUI {
    using MaintenanceCallback = bool (*)(String& message);
    using RebootCallback = void (*)();

    WebServer server_;
    int port_;
    uint32_t bootTime_;
    MitsubishiProtocol* proto_;
    const char* pairingCode_;
    const char* homekitName_;
    MaintenanceCallback clearHomeKitCallback_;
    MaintenanceCallback clearAllCallback_;
    RebootCallback rebootCallback_;
    File uploadFile_;
    String uploadPath_;
    bool uploadOk_ = false;
    String uploadMessage_;

public:
    WebUI(int port,
          MitsubishiProtocol* proto,
          const char* pairingCode,
          const char* homekitName,
          MaintenanceCallback clearHomeKitCallback = nullptr,
          MaintenanceCallback clearAllCallback = nullptr,
          RebootCallback rebootCallback = nullptr)
        : server_(port),
          port_(port),
          proto_(proto),
          pairingCode_(pairingCode),
          homekitName_(homekitName),
          clearHomeKitCallback_(clearHomeKitCallback),
          clearAllCallback_(clearAllCallback),
          rebootCallback_(rebootCallback) {
        bootTime_ = millis();
    }

    void begin() {
        server_.on("/", HTTP_GET, [this]() { handleRemoteRoot(); });
        server_.on("/debug", HTTP_GET, [this]() { handleDebugRoot(); });
        server_.on("/logs", HTTP_GET, [this]() { handleLogsRoot(); });
        server_.on("/files", HTTP_GET, [this]() { handleFilesRoot(); });
        server_.on("/api/status", HTTP_GET, [this]() { handleStatus(); });
        server_.on("/api/ping", HTTP_GET, [this]() { handlePing(); });
        server_.on("/api/serial", HTTP_POST, [this]() { handleSerialPost(); });
        server_.on("/api/logs", HTTP_GET, [this]() { handleLogsList(); });
        server_.on("/api/log/file", HTTP_GET, [this]() { handleLogFile(); });
        server_.on("/api/log/live", HTTP_GET, [this]() { handleLiveLog(); });
        server_.on("/api/files", HTTP_GET, [this]() { handleFilesList(); });
        server_.on("/api/files/download", HTTP_GET, [this]() { handleFileDownload(); });
        server_.on("/api/files/delete", HTTP_POST, [this]() { handleFileDelete(); });
        server_.on("/api/files/create-file", HTTP_POST, [this]() { handleCreateFile(); });
        server_.on("/api/files/create-dir", HTTP_POST, [this]() { handleCreateDir(); });
        server_.on("/api/files/upload", HTTP_POST, [this]() { handleUploadComplete(); }, [this]() { handleFileUpload(); });
        server_.on("/api/remote/build", HTTP_POST, [this]() { handleRemoteBuild(); });
        server_.on("/api/mock/decode", HTTP_POST, [this]() { handleMockDecode(); });
        server_.on("/api/raw/decode", HTTP_POST, [this]() { handleRawDecode(); });
        server_.on("/api/homekit/clear", HTTP_POST, [this]() { handleClearHomeKit(); });
        server_.on("/api/nvs/clear", HTTP_POST, [this]() { handleClearAll(); });
        server_.on("/api/reboot", HTTP_POST, [this]() { handleReboot(); });
        server_.onNotFound([this]() { handleNotFound(); });
        server_.begin();
        DebugLog::printf("[WebUI] Started on port %d\n", port_);
    }

    void loop() {
        server_.handleClient();
    }

private:
    void handleRemoteRoot() {
        DebugLog::debugf("[WebUI] GET / from %s\n", server_.client().remoteIP().toString().c_str());

        const heatpumpSettings& s = proto_->getCurrentSettings();
        const heatpumpStatus& st = proto_->getCurrentStatus();

        String html = pageStart("CN105 Virtual Remote");
        html += "<div class='card'>";
        html += "<div class='header-row'><div><h1>CN105 Virtual Remote</h1>";
        html += "<p>先在本地组装遥控器 payload，只有点击 <code>Send To Server</code> 才会提交给服务端生成 CN105 配置预览。</p></div>";
        html += navHtml("/") + "</div>";
        html += "<div class='top-meta'>" + metaLine() + "</div>";
        html += "<div class='page-actions'><button id='homekitModalBtn' class='ghost-link ghost-button action-button' type='button'>HomeKit 配对</button><button id='adminModalBtn' class='ghost-link ghost-button action-button' type='button'>Admin</button></div>";

        html += "<div class='section'>";
        html += "<div class='section-title'>状态总览</div>";
        html += "<div class='state-grid'>";
        html += "<div id='homekitStatePanel' class='state-panel'><div class='section-title'>HomeKit 状态预览</div>";
        html += statusRow("通讯模式", AppConfig::cn105TransportModeLabel(AppConfig::CN105_TRANSPORT_MODE));
        html += statusRow("连接状态", proto_->isConnected() ? "已连接" : "未连接");
        html += statusRow("电源", s.power ? s.power : "--");
        html += statusRow("模式", homeKitModePreview(s.mode));
        html += statusRow("目标温度", s.temperature > 0 ? String(proto_->getTargetTemperatureF(), 0) + " F" : "--");
        html += statusRow("室温", !isnan(st.roomTemperature) ? String(proto_->getRoomTemperatureF(), 0) + " F" : "--");
        html += statusRow("风速", homeKitFanPreview(s.fan));
        html += statusRow("摆风", isSwingEnabled(s.vane) ? "开启" : "关闭");
        html += "</div>";
        html += "<div id='cn105StatePanel' class='state-panel'><div class='section-title'>CN105 完整状态</div>";
        html += statusRow("通讯模式", AppConfig::cn105TransportModeLabel(AppConfig::CN105_TRANSPORT_MODE));
        html += statusRow("连接状态", proto_->isConnected() ? "已连接" : "未连接");
        html += statusRow("电源", s.power ? s.power : "--");
        html += statusRow("模式", s.mode ? s.mode : "--");
        html += statusRow("目标温度", s.temperature > 0 ? String(proto_->getTargetTemperatureF(), 0) + " F / " + String(s.temperature, 1) + " C" : "--");
        html += statusRow("室温", !isnan(st.roomTemperature) ? String(proto_->getRoomTemperatureF(), 0) + " F / " + String(st.roomTemperature, 1) + " C" : "--");
        html += statusRow("室外温度", !isnan(st.outsideAirTemperature) ? String(proto_->getOutsideTemperatureF(), 0) + " F / " + String(st.outsideAirTemperature, 1) + " C" : "--");
        html += statusRow("风速", s.fan ? s.fan : "--");
        html += statusRow("垂直风向", s.vane ? s.vane : "--");
        html += statusRow("水平风向", s.wideVane ? s.wideVane : "--");
        html += statusRow("i-see", s.iSee ? "开启" : "关闭");
        html += statusRow("运行中", st.operating ? "是" : "否");
        html += statusRow("压缩机频率", !isnan(st.compressorFrequency) ? String(st.compressorFrequency, 1) + " Hz" : "--");
        html += statusRow("输入功率", !isnan(st.inputPower) ? String(st.inputPower, 1) + " W" : "--");
        html += statusRow("累计电量", !isnan(st.kWh) ? String(st.kWh, 1) + " kWh" : "--");
        html += statusRow("运行时长", !isnan(st.runtimeHours) ? String(st.runtimeHours, 1) + " h" : "--");
        html += statusRow("运行阶段", st.stage ? st.stage : "--");
        html += statusRow("子模式", st.subMode ? st.subMode : "--");
        html += "</div></div>";
        html += "</div>";

        html += "<div class='section'>";
        html += "<div class='section-title'>服务端回显</div>";
        html += "<pre id='output'>Waiting for action...</pre>";
        html += "</div>";

        html += "<div class='section'>";
        html += "<div class='section-title'>遥控器草稿</div>";
        html += "<div class='grid'>";
        html += selectField("Power", "power", "OFF,ON", "ON");
        html += selectField("Mode", "mode", "AUTO,HEAT,COOL,DRY,FAN", "AUTO");
        html += numberField("Temperature (F)", "temperatureF",
                            String(AppConfig::MIN_TARGET_TEMPERATURE_F),
                            String(AppConfig::MAX_TARGET_TEMPERATURE_F),
                            String(AppConfig::TARGET_TEMPERATURE_STEP_F),
                            String(AppConfig::DEFAULT_TARGET_TEMPERATURE_F, 0));
        html += selectField("Fan", "fan", "AUTO,QUIET,1,2,3,4", "AUTO");
        html += selectField("Vertical Vane", "vane", "AUTO,1,2,3,4,5,SWING", "AUTO");
        html += selectField("Horizontal Vane", "wideVane", "<<,<,|,>,>>,<>,SWING,AIRFLOW CONTROL", "AIRFLOW CONTROL");
        html += "</div>";
        html += "<div class='controls' style='margin-top:14px;'><button id='sendBtn'>Send To Server</button><button id='resetBtn' class='secondary'>Reset Draft</button><button id='mock02Btn' class='secondary'>模拟读取当前设置</button><button id='mock03Btn' class='secondary'>模拟读取温度信息</button><button id='mock06Btn' class='secondary'>模拟读取运行状态</button><button id='mock09Btn' class='secondary'>模拟读取阶段信息</button><button id='mockAllBtn' class='secondary'>模拟读取全部状态</button></div>";
        html += "</div>";

        html += "<div class='section'>";
        html += "<div class='section-title'>本地 Payload 预览</div>";
        html += "<pre id='draft'></pre>";
        html += "</div>";

        html += "<div class='section'>";
        html += "<div class='section-title'>原始回复解析</div>";
        html += "<p>把一整串 <code>FC 62 ...</code> 十六进制包贴进来，服务端会直接按 CN105 回复包解析。支持带空格和连续 hex 两种写法。</p>";
        html += "<textarea id='rawPacket' rows='4' placeholder='例如: FC 62 01 30 10 06 00 00 21 01 01 A4 00 7C 00 00 00 00 00 00 00 14'></textarea>";
        html += "<div class='controls' style='margin-top:12px;'><button id='decodeRawBtn' class='secondary'>解析原始回复</button></div>";
        html += "</div>";
        html += "</div>";
        html += homeKitModalHtml();
        html += adminModalHtml();
        html += liveLogModalHtml();

        html += "<script>"
                "const fields={"
                "power:document.getElementById('power'),"
                "mode:document.getElementById('mode'),"
                "temperatureF:document.getElementById('temperatureF'),"
                "fan:document.getElementById('fan'),"
                "vane:document.getElementById('vane'),"
                "wideVane:document.getElementById('wideVane')};"
                "const output=document.getElementById('output');"
                "const draft=document.getElementById('draft');"
                "const rawPacket=document.getElementById('rawPacket');"
                "const homekitModal=document.getElementById('homekitModal');"
                "const adminModal=document.getElementById('adminModal');"
                "const liveLogModal=document.getElementById('liveLogModal');"
                "const liveLogBody=document.getElementById('liveLogBody');"
                "const homekitStatePanel=document.getElementById('homekitStatePanel');"
                "const cn105StatePanel=document.getElementById('cn105StatePanel');"
                "const maintenanceOutput=document.getElementById('maintenanceOutput');"
                "let liveLogOffset=0;"
                "let liveLogTimer=null;"
                "const defaults={power:'ON',mode:'AUTO',temperatureF:'" + String(AppConfig::DEFAULT_TARGET_TEMPERATURE_F, 0) + "',fan:'AUTO',vane:'AUTO',wideVane:'AIRFLOW CONTROL'};"
                "function currentDraft(){return {"
                "power:fields.power.value,"
                "mode:fields.mode.value,"
                "temperatureF:fields.temperatureF.value,"
                "fan:fields.fan.value,"
                "vane:fields.vane.value,"
                "wideVane:fields.wideVane.value"
                "};}"
                "function renderDraft(){"
                "const d=currentDraft();"
                "draft.textContent="
                "'power=' + d.power + '\\n' +"
                "'mode=' + d.mode + '\\n' +"
                "'temperature_f=' + d.temperatureF + '\\n' +"
                "'fan=' + d.fan + '\\n' +"
                "'vane=' + d.vane + '\\n' +"
                "'wideVane=' + d.wideVane;"
                "}"
                "async function sendDraft(){"
                "const d=currentDraft();"
                "output.textContent='Sending draft to server...';"
                "const body=new URLSearchParams(d);"
                "try{"
                "const resp=await fetch('/api/remote/build',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});"
                "const data=await resp.json();"
                "output.textContent=(data.configFile ? data.configFile + '\\n\\n' : '') + JSON.stringify(data,null,2);"
                "if(data.status){applyStatus(data.status);}"
                "}catch(err){"
                "output.textContent='Send failed: '+err;"
                "}"
                "}"
                "function resetDraft(){"
                "Object.keys(fields).forEach(key=>fields[key].value=defaults[key]);"
                "renderDraft();"
                "output.textContent='草稿已在本地重置，还没有发送到服务端。';"
                "}"
                "async function loadMockResponse(code,label){"
                "output.textContent='正在从服务端读取' + label + '...';"
                "try{"
                "const body=new URLSearchParams({code});"
                "const resp=await fetch('/api/mock/decode',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});"
                "const data=await resp.json();"
                "if(data.status){applyStatus(data.status);}"
                "output.textContent=JSON.stringify(data,null,2);"
                "}catch(err){"
                "output.textContent='读取状态失败: '+err;"
                "}"
                "}"
                "async function decodeRawPacket(){"
                "const raw=rawPacket.value.trim();"
                "if(!raw){"
                "output.textContent='请先输入原始十六进制包。';"
                "return;"
                "}"
                "output.textContent='正在解析原始回复包...';"
                "try{"
                "const body=new URLSearchParams({packet:raw});"
                "const resp=await fetch('/api/raw/decode',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});"
                "const data=await resp.json();"
                "if(data.status){applyStatus(data.status);}"
                "output.textContent=JSON.stringify(data,null,2);"
                "}catch(err){"
                "output.textContent='原始包解析失败: '+err;"
                "}"
                "}"
                "function applyStatus(status){"
                "if(status.power) fields.power.value=status.power;"
                "if(status.mode) fields.mode.value=status.mode;"
                "if(status.targetTemperatureF !== undefined) fields.temperatureF.value=Math.round(Number(status.targetTemperatureF));"
                "if(status.fan) fields.fan.value=status.fan;"
                "if(status.vane) fields.vane.value=status.vane;"
                "if(status.wideVane) fields.wideVane.value=status.wideVane;"
                "renderStatePanels(status);"
                "renderDraft();"
                "}"
                "function stateRow(key,value){return `<div class='status-row'><div class='status-key'>${key}</div><div class='status-val'>${value ?? '--'}</div></div>`;}"
                "function displayTemp(f,c){return f == null ? '--' : `${Math.round(Number(f))} F${c == null ? '' : ' / '+Number(c).toFixed(1)+' C'}`;}"
                "function displayNumber(value,unit){return value == null ? '--' : `${Number(value).toFixed(1)} ${unit}`;}"
                "function hkMode(mode){return mode === 'HEAT' || mode === 'COOL' || mode === 'AUTO' ? mode : `AUTO (CN105: ${mode || '--'})`;}"
                "function hkFan(fan){"
                "if(fan === 'AUTO') return '0% / AUTO';"
                "if(fan === 'QUIET') return '14% / QUIET';"
                "if(fan === '1') return '28% / 1';"
                "if(fan === '2') return '42% / 2';"
                "if(fan === '3') return '71% / 3';"
                "if(fan === '4') return '100% / 4';"
                "return fan || '--';"
                "}"
                "function renderStatePanels(status){"
                "const swing=status.vane === 'SWING' ? '开启' : '关闭';"
                "homekitStatePanel.innerHTML='<div class=\"section-title\">HomeKit 状态预览</div>' +"
                "stateRow('通讯模式',status.transportMode || '--') +"
                "stateRow('连接状态',status.connected ? '已连接' : '未连接') +"
                "stateRow('电源',status.power || '--') +"
                "stateRow('模式',hkMode(status.mode)) +"
                "stateRow('目标温度',status.targetTemperatureF == null ? '--' : `${Math.round(Number(status.targetTemperatureF))} F`) +"
                "stateRow('室温',status.roomTemperatureF == null ? '--' : `${Math.round(Number(status.roomTemperatureF))} F`) +"
                "stateRow('风速',hkFan(status.fan)) +"
                "stateRow('摆风',swing);"
                "cn105StatePanel.innerHTML='<div class=\"section-title\">CN105 完整状态</div>' +"
                "stateRow('通讯模式',status.transportMode || '--') +"
                "stateRow('连接状态',status.connected ? '已连接' : '未连接') +"
                "stateRow('电源',status.power || '--') +"
                "stateRow('模式',status.mode || '--') +"
                "stateRow('目标温度',displayTemp(status.targetTemperatureF,status.targetTemperatureRawC)) +"
                "stateRow('室温',displayTemp(status.roomTemperatureF,status.roomTemperatureRawC)) +"
                "stateRow('室外温度',displayTemp(status.outsideAirTemperatureF,status.outsideAirTemperatureRawC)) +"
                "stateRow('风速',status.fan || '--') +"
                "stateRow('垂直风向',status.vane || '--') +"
                "stateRow('水平风向',status.wideVane || '--') +"
                "stateRow('i-see',status.iSee ? '开启' : '关闭') +"
                "stateRow('运行中',status.operating ? '是' : '否') +"
                "stateRow('压缩机频率',displayNumber(status.compressorFrequency,'Hz')) +"
                "stateRow('输入功率',displayNumber(status.inputPower,'W')) +"
                "stateRow('累计电量',displayNumber(status.kWh,'kWh')) +"
                "stateRow('运行时长',displayNumber(status.runtimeHours,'h')) +"
                "stateRow('运行阶段',status.stage || '--') +"
                "stateRow('子模式',status.subMode || '--');"
                "}"
                "function renderHomeKitQr(){"
                "const target=document.getElementById('homekitQr');"
                "const payload=document.getElementById('setupPayload').textContent;"
                "if(!window.QRCode){"
                "target.textContent='QRCode library failed to load from cdnjs.';"
                "return;"
                "}"
                "target.textContent='';"
                "new QRCode(target,{text:payload,width:220,height:220,colorDark:'#0b1b2b',colorLight:'#ffffff',correctLevel:QRCode.CorrectLevel.M});"
                "}"
                "function openHomeKitModal(){homekitModal.classList.add('open');renderHomeKitQr();}"
                "function closeHomeKitModal(){homekitModal.classList.remove('open');}"
                "function openAdminModal(){adminModal.classList.add('open');}"
                "function closeAdminModal(){adminModal.classList.remove('open');}"
                "async function pollLiveLog(){"
                "try{"
                "const resp=await fetch('/api/log/live?offset='+liveLogOffset);"
                "const data=await resp.json();"
                "if(data.reset){liveLogBody.textContent='';}"
                "if(data.text){liveLogBody.textContent+=data.text;}"
                "liveLogOffset=data.nextOffset || 0;"
                "if(liveLogBody.textContent.length>24000){liveLogBody.textContent=liveLogBody.textContent.slice(-16000);}"
                "liveLogBody.scrollTop=liveLogBody.scrollHeight;"
                "}catch(err){liveLogBody.textContent+='\\n[live log error] '+err;}"
                "}"
                "function openLiveLog(){"
                "liveLogModal.classList.add('open');"
                "liveLogBody.textContent='';"
                "liveLogOffset=0;"
                "pollLiveLog();"
                "liveLogTimer=setInterval(pollLiveLog,1500);"
                "}"
                "function closeLiveLog(){"
                "liveLogModal.classList.remove('open');"
                "if(liveLogTimer){clearInterval(liveLogTimer);liveLogTimer=null;}"
                "}"
                "async function maintenancePost(url,label,confirmText){"
                "if(!confirm(confirmText)){return;}"
                "maintenanceOutput.textContent='正在执行：'+label+'...';"
                "try{"
                "const resp=await fetch(url,{method:'POST'});"
                "const data=await resp.json();"
                "maintenanceOutput.textContent=JSON.stringify(data,null,2);"
                "}catch(err){"
                "maintenanceOutput.textContent=label+' 失败: '+err;"
                "}"
                "}"
                "async function loadStatus(){"
                "output.textContent='正在读取当前状态...';"
                "try{"
                "const resp=await fetch('/api/status');"
                "const data=await resp.json();"
                "if(data.status){applyStatus(data.status);}"
                "output.textContent=JSON.stringify(data,null,2);"
                "}catch(err){"
                "output.textContent='状态读取失败: '+err;"
                "renderDraft();"
                "}"
                "}"
                "Object.values(fields).forEach(el=>el.addEventListener('input',renderDraft));"
                "document.getElementById('sendBtn').addEventListener('click',sendDraft);"
                "document.getElementById('resetBtn').addEventListener('click',resetDraft);"
                "document.getElementById('mock02Btn').addEventListener('click',()=>loadMockResponse('0x02','当前设置'));"
                "document.getElementById('mock03Btn').addEventListener('click',()=>loadMockResponse('0x03','温度信息'));"
                "document.getElementById('mock06Btn').addEventListener('click',()=>loadMockResponse('0x06','运行状态'));"
                "document.getElementById('mock09Btn').addEventListener('click',()=>loadMockResponse('0x09','阶段信息'));"
                "document.getElementById('mockAllBtn').addEventListener('click',()=>loadMockResponse('all','全部状态'));"
                "document.getElementById('decodeRawBtn').addEventListener('click',decodeRawPacket);"
                "document.getElementById('homekitModalBtn').addEventListener('click',openHomeKitModal);"
                "document.getElementById('adminModalBtn').addEventListener('click',openAdminModal);"
                "document.getElementById('homekitCloseBtn').addEventListener('click',closeHomeKitModal);"
                "document.getElementById('homekitModalBackdrop').addEventListener('click',closeHomeKitModal);"
                "document.getElementById('adminCloseBtn').addEventListener('click',closeAdminModal);"
                "document.getElementById('adminModalBackdrop').addEventListener('click',closeAdminModal);"
                "document.getElementById('adminLiveLogBtn').addEventListener('click',openLiveLog);"
                "document.getElementById('liveLogCloseBtn').addEventListener('click',closeLiveLog);"
                "document.getElementById('liveLogModalBackdrop').addEventListener('click',closeLiveLog);"
                "document.getElementById('clearHomeKitBtn').addEventListener('click',()=>maintenancePost('/api/homekit/clear','清除 HomeKit 数据','确定清除 HomeKit 配对数据吗？这不会自动重启。'));"
                "document.getElementById('clearAllBtn').addEventListener('click',()=>maintenancePost('/api/nvs/clear','清除全部 HomeSpan 数据','确定清除全部 HomeSpan NVS 数据吗？这不会自动重启。'));"
                "document.getElementById('rebootBtn').addEventListener('click',()=>maintenancePost('/api/reboot','重启设备','确定现在重启设备吗？'));"
                "document.addEventListener('keydown',e=>{if(e.key==='Escape'){closeHomeKitModal();closeAdminModal();closeLiveLog();}});"
                "loadStatus();"
                "</script>";
        html += pageEnd();

        server_.send(200, "text/html; charset=utf-8", html);
    }

    void handleDebugRoot() {
        DebugLog::debugf("[WebUI] GET /debug from %s\n", server_.client().remoteIP().toString().c_str());

        String html = pageStart("Web Debug");
        html += "<div class='card'>";
        html += "<div class='header-row'><div><h1>Web Debug</h1>";
        html += "<p>这里保留最早的 ping 和串口回显调试能力，方便继续验证 HTTP 通路和串口输出。</p></div>";
        html += navHtml("/debug") + "</div>";
        html += "<div class='top-meta'>" + metaLine() + "</div>";
        html += "<div class='section'>";
        html += "<div class='section-title'>Action Buttons</div>";
        html += "<input id='serialInput' type='text' maxlength='200' placeholder='Type something to send to Serial'>";
        html += "<div class='controls' style='margin-top:12px;'><button id='pingBtn'>Ping Server</button><button id='sendBtn'>Send To Serial</button></div>";
        html += "<pre id='output'>Waiting for action...</pre>";
        html += "</div>";
        html += "</div>";

        html += "<script>"
                "const btn=document.getElementById('pingBtn');"
                "const sendBtn=document.getElementById('sendBtn');"
                "const input=document.getElementById('serialInput');"
                "const out=document.getElementById('output');"
                "async function ping(){"
                "out.textContent='Pinging server...';"
                "try{"
                "const resp=await fetch('/api/ping');"
                "const data=await resp.json();"
                "out.textContent=JSON.stringify(data,null,2);"
                "}catch(err){"
                "out.textContent='Ping failed: '+err;"
                "}"
                "}"
                "async function sendSerial(){"
                "const text=input.value;"
                "if(!text){"
                "out.textContent='Please enter some text first.';"
                "return;"
                "}"
                "out.textContent='Sending to serial...';"
                "try{"
                "const resp=await fetch('/api/serial',{method:'POST',headers:{'Content-Type':'text/plain'},body:text});"
                "const data=await resp.json();"
                "out.textContent=JSON.stringify(data,null,2);"
                "}catch(err){"
                "out.textContent='Send failed: '+err;"
                "}"
                "}"
                "btn.addEventListener('click',ping);"
                "sendBtn.addEventListener('click',sendSerial);"
                "input.addEventListener('keydown',e=>{if(e.key==='Enter'){sendSerial();}});"
                "</script>";
        html += pageEnd();

        server_.send(200, "text/html; charset=utf-8", html);
    }

    void handleLogsRoot() {
        DebugLog::debugf("[WebUI] GET /logs from %s\n", server_.client().remoteIP().toString().c_str());

        String html = pageStart("Logs");
        html += "<div class='card'>";
        html += "<div class='header-row'><div><h1>Logs</h1>";
        html += "<p>每次开机都会创建一个新的 SPIFFS 日志文件。默认 Info level 不记录 heartbeat；需要更细的串口 poll 细节时可以在 AppConfig 里把 LOG_LEVEL 改成 Debug。</p></div>";
        html += navHtml("/logs") + "</div>";
        html += "<div class='top-meta'>" + metaLine() + "</div>";
        html += "<div class='page-actions'><button id='liveLogBtn' class='ghost-link ghost-button action-button' type='button'>Live 当前日志</button></div>";
        html += "<div class='section'>";
        html += "<div class='section-title'>日志文件</div>";
        html += "<pre id='logList'>Loading logs...</pre>";
        html += "</div>";
        html += "<div class='section'>";
        html += "<div class='section-title'>日志内容</div>";
        html += "<pre id='logBody'>选择一个日志文件查看。</pre>";
        html += "</div>";
        html += "</div>";
        html += liveLogModalHtml();
        html += "<script>"
                "const logList=document.getElementById('logList');"
                "const logBody=document.getElementById('logBody');"
                "const liveLogModal=document.getElementById('liveLogModal');"
                "const liveLogBody=document.getElementById('liveLogBody');"
                "let liveLogOffset=0;"
                "let liveLogTimer=null;"
                "async function loadLogs(){"
                "try{"
                "const resp=await fetch('/api/logs');"
                "const data=await resp.json();"
                "if(!data.logs || data.logs.length===0){logList.textContent='没有日志文件。';return;}"
                "logList.innerHTML='';"
                "data.logs.forEach(log=>{"
                "const row=document.createElement('div');"
                "const btn=document.createElement('button');"
                "btn.className='secondary action-button';"
                "btn.textContent='查看';"
                "btn.addEventListener('click',()=>loadLog(log.name));"
                "const link=document.createElement('a');"
                "link.className='ghost-link action-button';"
                "link.href='/api/log/file?file='+encodeURIComponent(log.name);"
                "link.textContent='下载';"
                "row.style.marginBottom='10px';"
                "row.append(`${log.current ? '[current] ' : ''}${log.name} (${log.size} bytes) `);"
                "row.appendChild(btn);"
                "row.append(' ');"
                "row.appendChild(link);"
                "logList.appendChild(row);"
                "});"
                "const current=data.logs.find(log=>log.current) || data.logs[0];"
                "if(current){loadLog(current.name);}"
                "}catch(err){logList.textContent='读取日志列表失败: '+err;}"
                "}"
                "async function loadLog(name){"
                "logBody.textContent='Loading '+name+'...';"
                "try{"
                "const resp=await fetch('/api/log/file?file='+encodeURIComponent(name));"
                "logBody.textContent=await resp.text();"
                "}catch(err){logBody.textContent='读取日志失败: '+err;}"
                "}"
                "async function pollLiveLog(){"
                "try{"
                "const resp=await fetch('/api/log/live?offset='+liveLogOffset);"
                "const data=await resp.json();"
                "if(data.reset){liveLogBody.textContent='';}"
                "if(data.text){liveLogBody.textContent+=data.text;}"
                "liveLogOffset=data.nextOffset || 0;"
                "if(liveLogBody.textContent.length>24000){liveLogBody.textContent=liveLogBody.textContent.slice(-16000);}"
                "liveLogBody.scrollTop=liveLogBody.scrollHeight;"
                "}catch(err){liveLogBody.textContent+='\\n[live log error] '+err;}"
                "}"
                "function openLiveLog(){"
                "liveLogModal.classList.add('open');"
                "liveLogBody.textContent='';"
                "liveLogOffset=0;"
                "pollLiveLog();"
                "liveLogTimer=setInterval(pollLiveLog,1500);"
                "}"
                "function closeLiveLog(){"
                "liveLogModal.classList.remove('open');"
                "if(liveLogTimer){clearInterval(liveLogTimer);liveLogTimer=null;}"
                "}"
                "document.getElementById('liveLogBtn').addEventListener('click',openLiveLog);"
                "document.getElementById('liveLogCloseBtn').addEventListener('click',closeLiveLog);"
                "document.getElementById('liveLogModalBackdrop').addEventListener('click',closeLiveLog);"
                "document.addEventListener('keydown',e=>{if(e.key==='Escape'){closeLiveLog();}});"
                "loadLogs();"
                "</script>";
        html += pageEnd();

        server_.send(200, "text/html; charset=utf-8", html);
    }

    void handleFilesRoot() {
        DebugLog::debugf("[WebUI] GET /files from %s\n", server_.client().remoteIP().toString().c_str());

        String html = pageStart("Files");
        html += "<div class='card'>";
        html += "<div class='header-row'><div><h1>SPIFFS File Manager</h1>";
        html += "<p>这里可以查看、下载、上传和删除 SPIFFS 文件。SPIFFS 没有真正目录，文件夹用 <code>.keep</code> 占位模拟。</p></div>";
        html += navHtml("/files") + "</div>";
        html += "<div class='top-meta'>" + metaLine() + "</div>";
        html += "<div class='section'><div class='section-title'>当前目录</div><input id='dirInput' value='/'><div class='controls' style='margin-top:12px;'><button id='loadBtn'>刷新</button><button id='upBtn' class='secondary'>上一级</button></div><pre id='fsInfo'>Loading...</pre></div>";
        html += "<div class='section'><div class='section-title'>文件</div><pre id='fileList'>Loading files...</pre></div>";
        html += "<div class='section'><div class='section-title'>新建</div><div class='grid'><input id='newFilePath' placeholder='/notes.txt'><input id='newDirPath' placeholder='/my-folder'></div><div class='controls' style='margin-top:12px;'><button id='createFileBtn' class='secondary'>新建空文件</button><button id='createDirBtn' class='secondary'>新建文件夹</button></div></div>";
        html += "<div class='section'><div class='section-title'>上传</div><form id='uploadForm'><input id='uploadDir' name='dir' value='/'><input id='uploadFile' name='file' type='file'><div class='controls' style='margin-top:12px;'><button type='submit'>上传文件</button></div></form><pre id='fileOutput'>等待操作...</pre></div>";
        html += "</div>";
        html += "<script>"
                "const dirInput=document.getElementById('dirInput');"
                "const uploadDir=document.getElementById('uploadDir');"
                "const fileList=document.getElementById('fileList');"
                "const fsInfo=document.getElementById('fsInfo');"
                "const out=document.getElementById('fileOutput');"
                "function normDir(v){v=(v||'/').trim();if(!v.startsWith('/'))v='/'+v;if(v.length>1&&v.endsWith('/'))v=v.slice(0,-1);return v||'/';}"
                "function fullPath(v){v=(v||'').trim();if(!v)return '';if(v.startsWith('/'))return v;const d=normDir(dirInput.value);return d==='/'?'/'+v:d+'/'+v;}"
                "async function loadFiles(){"
                "const dir=normDir(dirInput.value);dirInput.value=dir;uploadDir.value=dir;"
                "try{"
                "const resp=await fetch('/api/files?dir='+encodeURIComponent(dir));"
                "const data=await resp.json();"
                "fsInfo.textContent=JSON.stringify(data.info||{},null,2);"
                "if(!data.ok){fileList.textContent=data.error||'读取失败';return;}"
                "fileList.innerHTML='';"
                "if(!data.items.length){fileList.textContent='这个目录是空的。';return;}"
                "data.items.forEach(item=>{"
                "const row=document.createElement('div');row.style.marginBottom='10px';"
                "const open=document.createElement('button');open.className='secondary';open.textContent=item.type==='dir'?'打开':'下载';"
                "open.addEventListener('click',()=>{if(item.type==='dir'){dirInput.value=item.name;loadFiles();}else{location.href='/api/files/download?path='+encodeURIComponent(item.name);}});"
                "const del=document.createElement('button');del.className='danger';del.textContent='删除';"
                "del.addEventListener('click',()=>deletePath(item.name));"
                "row.append(`${item.type==='dir'?'[dir] ':'[file] '}${item.name} ${item.type==='file'?'('+item.size+' bytes) ':''}`);"
                "row.appendChild(open);row.append(' ');row.appendChild(del);"
                "fileList.appendChild(row);"
                "});"
                "}catch(err){fileList.textContent='读取失败: '+err;}"
                "}"
                "async function postForm(url,body){"
                "const resp=await fetch(url,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});"
                "const data=await resp.json();out.textContent=JSON.stringify(data,null,2);loadFiles();"
                "}"
                "function deletePath(path){if(confirm('删除 '+path+' ?'))postForm('/api/files/delete',new URLSearchParams({path}));}"
                "document.getElementById('loadBtn').addEventListener('click',loadFiles);"
                "document.getElementById('upBtn').addEventListener('click',()=>{let d=normDir(dirInput.value);if(d!=='/'){dirInput.value=d.substring(0,d.lastIndexOf('/'))||'/';loadFiles();}});"
                "document.getElementById('createFileBtn').addEventListener('click',()=>postForm('/api/files/create-file',new URLSearchParams({path:fullPath(document.getElementById('newFilePath').value),content:''})));"
                "document.getElementById('createDirBtn').addEventListener('click',()=>postForm('/api/files/create-dir',new URLSearchParams({path:fullPath(document.getElementById('newDirPath').value)})));"
                "document.getElementById('uploadForm').addEventListener('submit',async e=>{"
                "e.preventDefault();const form=new FormData(e.target);out.textContent='Uploading...';"
                "try{const resp=await fetch('/api/files/upload?dir='+encodeURIComponent(uploadDir.value),{method:'POST',body:form});const data=await resp.json();out.textContent=JSON.stringify(data,null,2);loadFiles();}"
                "catch(err){out.textContent='上传失败: '+err;}"
                "});"
                "loadFiles();"
                "</script>";
        html += pageEnd();

        server_.send(200, "text/html; charset=utf-8", html);
    }

    void handleStatus() {
        DebugLog::debugf("[WebUI] GET /api/status from %s\n", server_.client().remoteIP().toString().c_str());

        String json = "{";
        json += "\"ok\":true,";
        json += "\"status\":" + buildStatusJson() + ",";
        json += "\"packet\":" + buildSetPacketJson() + ",";
        json += "\"responsePacket\":" + buildResponsePacketJson();
        json += "}";

        server_.send(200, "application/json", json);
    }

    void handleLogsList() {
        DebugLog::debugf("[WebUI] GET /api/logs from %s\n", server_.client().remoteIP().toString().c_str());

        String json = "{";
        json += "\"ok\":true,";
        json += "\"current\":\"" + jsonEscape(DebugLog::currentLogPath()) + "\",";
        json += "\"active\":" + String(DebugLog::persistentLogActive() ? "true" : "false") + ",";
        json += "\"logs\":" + DebugLog::logsJson();
        json += "}";

        server_.send(200, "application/json", json);
    }

    void handleLogFile() {
        DebugLog::debugf("[WebUI] GET /api/log/file from %s\n", server_.client().remoteIP().toString().c_str());

        String fileName = argOrDefault("file", DebugLog::currentLogPath());
        File file = DebugLog::openLogFile(fileName);
        if (!file) {
            server_.send(404, "text/plain; charset=utf-8", "Log file not found");
            return;
        }

        server_.sendHeader("Content-Disposition", "attachment; filename=\"" + downloadFileName(fileName) + "\"");
        server_.streamFile(file, "text/plain; charset=utf-8");
        file.close();
    }

    void handleLiveLog() {
        DebugLog::printf(AppConfig::LogLevel::Verbose,
                      "[WebUI] GET /api/log/live from %s\n",
                      server_.client().remoteIP().toString().c_str());

        String path = DebugLog::currentLogPath();
        File file = DebugLog::openLogFile(path);
        if (!file) {
            server_.send(404, "application/json", "{\"ok\":false,\"error\":\"active log file not found\"}");
            return;
        }

        size_t size = file.size();
        size_t offset = static_cast<size_t>(argOrDefault("offset", "0").toInt());
        bool reset = false;
        if (offset == 0 && size > 4096) {
            offset = size - 4096;
            reset = true;
        } else if (offset > size) {
            offset = 0;
            reset = true;
        }

        file.seek(offset, SeekSet);
        String text;
        while (file.available() && text.length() < 4096) {
            text += static_cast<char>(file.read());
        }
        size_t nextOffset = file.position();
        file.close();

        String json = "{";
        json += "\"ok\":true,";
        json += "\"path\":\"" + jsonEscape(path) + "\",";
        json += "\"size\":" + String(size) + ",";
        json += "\"nextOffset\":" + String(nextOffset) + ",";
        json += "\"reset\":" + String(reset ? "true" : "false") + ",";
        json += "\"text\":\"" + jsonEscape(text) + "\"";
        json += "}";
        server_.send(200, "application/json", json);
    }

    void handleFilesList() {
        DebugLog::debugf("[WebUI] GET /api/files from %s\n", server_.client().remoteIP().toString().c_str());

        String dir = argOrDefault("dir", "/");
        String json = FileSystemManager::listJson(dir);
        if (json.endsWith("}")) {
            json.remove(json.length() - 1);
            json += ",\"info\":" + FileSystemManager::infoJson() + "}";
        }
        server_.send(200, "application/json", json);
    }

    void handleFileDownload() {
        DebugLog::debugf("[WebUI] GET /api/files/download from %s\n", server_.client().remoteIP().toString().c_str());

        String path = argOrDefault("path", "");
        File file = FileSystemManager::openRead(path);
        if (!file) {
            server_.send(404, "text/plain; charset=utf-8", "File not found");
            return;
        }

        server_.sendHeader("Content-Disposition", "attachment; filename=\"" + downloadFileName(path) + "\"");
        server_.streamFile(file, "application/octet-stream");
        file.close();
    }

    void handleFileDelete() {
        DebugLog::debugf("[WebUI] POST /api/files/delete from %s\n", server_.client().remoteIP().toString().c_str());

        String message;
        bool ok = FileSystemManager::remove(argOrDefault("path", ""), message);
        sendActionJson(ok, "delete", message);
    }

    void handleCreateFile() {
        DebugLog::debugf("[WebUI] POST /api/files/create-file from %s\n", server_.client().remoteIP().toString().c_str());

        String message;
        bool ok = FileSystemManager::createFile(argOrDefault("path", ""), argOrDefault("content", ""), message);
        sendActionJson(ok, "create-file", message);
    }

    void handleCreateDir() {
        DebugLog::debugf("[WebUI] POST /api/files/create-dir from %s\n", server_.client().remoteIP().toString().c_str());

        String message;
        bool ok = FileSystemManager::createDirectory(argOrDefault("path", ""), message);
        sendActionJson(ok, "create-dir", message);
    }

    void handleUploadComplete() {
        DebugLog::debugf("[WebUI] POST /api/files/upload complete from %s\n", server_.client().remoteIP().toString().c_str());

        if (!uploadOk_ && uploadMessage_.length() == 0) {
            uploadMessage_ = "no upload received";
        }

        String json = "{";
        json += "\"ok\":" + String(uploadOk_ ? "true" : "false") + ",";
        json += "\"path\":\"" + jsonEscape(uploadPath_) + "\",";
        json += "\"message\":\"" + jsonEscape(uploadMessage_) + "\"";
        json += "}";
        server_.send(uploadOk_ ? 200 : 400, "application/json", json);
        uploadPath_ = "";
        uploadMessage_ = "";
        uploadOk_ = false;
    }

    void handleFileUpload() {
        HTTPUpload& upload = server_.upload();

        if (upload.status == UPLOAD_FILE_START) {
            if (uploadFile_) {
                uploadFile_.close();
            }
            uploadOk_ = false;
            uploadMessage_ = "";
            uploadPath_ = FileSystemManager::joinPath(server_.arg("dir"), upload.filename);
            if (!FileSystemManager::isSafePath(uploadPath_) || uploadPath_ == "/") {
                uploadMessage_ = "invalid upload path";
                return;
            }
            uploadFile_ = FileSystemManager::openWrite(uploadPath_);
            if (!uploadFile_) {
                uploadMessage_ = "failed to open upload file";
                return;
            }
        } else if (upload.status == UPLOAD_FILE_WRITE) {
            if (uploadFile_) {
                uploadFile_.write(upload.buf, upload.currentSize);
            }
        } else if (upload.status == UPLOAD_FILE_END) {
            if (uploadFile_) {
                uploadFile_.close();
                uploadOk_ = true;
                uploadMessage_ = "upload complete";
            } else if (uploadMessage_.length() == 0) {
                uploadMessage_ = "upload failed";
            }
        } else if (upload.status == UPLOAD_FILE_ABORTED) {
            if (uploadFile_) {
                uploadFile_.close();
            }
            uploadMessage_ = "upload aborted";
        }
    }

    void handlePing() {
        DebugLog::debugf("[WebUI] GET /api/ping from %s\n", server_.client().remoteIP().toString().c_str());

        String json = "{";
        json += "\"ok\":true,";
        json += "\"message\":\"" + String(AppConfig::PING_MESSAGE) + "\",";
        json += "\"uptimeMs\":" + String(millis()) + ",";
        json += "\"wifiMode\":\"" + wifiModeLabel() + "\",";
        json += "\"ssid\":\"" + wifiSsid() + "\",";
        json += "\"ipAddress\":\"" + wifiIp() + "\"";
        json += "}";

        server_.send(200, "application/json", json);
    }

    void handleSerialPost() {
        DebugLog::debugf("[WebUI] POST /api/serial from %s\n", server_.client().remoteIP().toString().c_str());

        String body = server_.arg("plain");
        body.trim();

        if (body.length() == 0) {
            server_.send(400, "application/json", "{\"ok\":false,\"error\":\"empty message\"}");
            return;
        }

        DebugLog::print(AppConfig::SERIAL_MESSAGE_PREFIX);
        DebugLog::println(body);

        String json = "{";
        json += "\"ok\":true,";
        json += "\"sent\":\"" + jsonEscape(body) + "\",";
        json += "\"message\":\"written to serial\"";
        json += "}";

        server_.send(200, "application/json", json);
    }

    void handleRemoteBuild() {
        DebugLog::debugf("[WebUI] POST /api/remote/build from %s\n", server_.client().remoteIP().toString().c_str());

        String power = argOrDefault("power", "ON");
        String mode = argOrDefault("mode", "AUTO");
        String temperatureF = argOrDefault("temperatureF", String(AppConfig::DEFAULT_TARGET_TEMPERATURE_F, 0).c_str());
        String fan = argOrDefault("fan", "AUTO");
        String vane = argOrDefault("vane", "AUTO");
        String wideVane = argOrDefault("wideVane", "AIRFLOW CONTROL");
        float parsedTemperatureF = temperatureF.toFloat();
        bool commandAccepted = proto_->applyRemoteSettings(power, mode, parsedTemperatureF, fan, vane, wideVane);
        String config = proto_->buildMockConfigPreview();

        DebugLog::println("[WebUI] CN105 draft preview:");
        DebugLog::println(config);

        String json = "{";
        json += "\"ok\":" + String(commandAccepted ? "true" : "false") + ",";
        json += "\"message\":\"" + String(commandAccepted ? "remote command accepted" : "remote command rejected") + "\",";
        json += "\"configFile\":\"" + jsonEscape(config) + "\",";
        json += "\"packet\":" + buildSetPacketJson() + ",";
        json += "\"responsePacket\":" + buildResponsePacketJson() + ",";
        json += "\"status\":" + buildStatusJson();
        json += "}";

        server_.send(commandAccepted ? 200 : 503, "application/json", json);
    }

    void handleMockDecode() {
        DebugLog::debugf("[WebUI] POST /api/mock/decode from %s\n", server_.client().remoteIP().toString().c_str());

        if (proto_->isRealTransportEnabled()) {
            server_.send(409, "application/json", "{\"ok\":false,\"error\":\"mock response decode is disabled in Real transport mode\"}");
            return;
        }

        String code = argOrDefault("code", "all");
        code.trim();
        code.toLowerCase();

        bool ok = false;
        String appliedLabel;
        if (code == "all") {
            ok = proto_->decodeAllMockResponses();
            appliedLabel = "all";
        } else if (code == "0x02" || code == "02" || code == "2") {
            ok = proto_->decodeMockResponse(0x02);
            appliedLabel = "0x02";
        } else if (code == "0x03" || code == "03" || code == "3") {
            ok = proto_->decodeMockResponse(0x03);
            appliedLabel = "0x03";
        } else if (code == "0x06" || code == "06" || code == "6") {
            ok = proto_->decodeMockResponse(0x06);
            appliedLabel = "0x06";
        } else if (code == "0x09" || code == "09" || code == "9") {
            ok = proto_->decodeMockResponse(0x09);
            appliedLabel = "0x09";
        }

        if (!ok) {
            server_.send(400, "application/json", "{\"ok\":false,\"error\":\"unsupported or failed state read\"}");
            return;
        }

        DebugLog::debugf("[WebUI] Applied mock response %s\n", appliedLabel.c_str());
        DebugLog::println(proto_->getLastResponsePacketHex());

        String json = "{";
        json += "\"ok\":true,";
        json += "\"message\":\"state read completed\",";
        json += "\"applied\":\"" + jsonEscape(appliedLabel) + "\",";
        json += "\"status\":" + buildStatusJson() + ",";
        json += "\"packet\":" + buildSetPacketJson() + ",";
        json += "\"responsePacket\":" + buildResponsePacketJson();
        json += "}";

        server_.send(200, "application/json", json);
    }

    void handleRawDecode() {
        DebugLog::debugf("[WebUI] POST /api/raw/decode from %s\n", server_.client().remoteIP().toString().c_str());

        String packet = argOrDefault("packet", "");
        String error;
        if (!proto_->decodeRawResponseHex(packet, &error)) {
            String json = "{";
            json += "\"ok\":false,";
            json += "\"error\":\"" + jsonEscape(error) + "\"";
            json += "}";
            server_.send(400, "application/json", json);
            return;
        }

        DebugLog::println("[WebUI] Parsed raw CN105 response:");
        DebugLog::println(proto_->getLastResponsePacketHex());

        String json = "{";
        json += "\"ok\":true,";
        json += "\"message\":\"raw response decoded\",";
        json += "\"status\":" + buildStatusJson() + ",";
        json += "\"packet\":" + buildSetPacketJson() + ",";
        json += "\"responsePacket\":" + buildResponsePacketJson();
        json += "}";

        server_.send(200, "application/json", json);
    }

    void handleClearHomeKit() {
        handleMaintenanceAction("clear HomeKit data", clearHomeKitCallback_);
    }

    void handleClearAll() {
        handleMaintenanceAction("clear all HomeSpan data", clearAllCallback_);
    }

    void handleReboot() {
        DebugLog::debugf("[WebUI] POST /api/reboot from %s\n", server_.client().remoteIP().toString().c_str());

        if (!rebootCallback_) {
            server_.send(501, "application/json", "{\"ok\":false,\"error\":\"reboot callback not configured\"}");
            return;
        }

        server_.send(200, "application/json", "{\"ok\":true,\"message\":\"rebooting\"}");
        rebootCallback_();
    }

    void handleMaintenanceAction(const char* label, MaintenanceCallback callback) {
        DebugLog::debugf("[WebUI] POST maintenance action '%s' from %s\n",
                      label,
                      server_.client().remoteIP().toString().c_str());

        if (!callback) {
            server_.send(501, "application/json", "{\"ok\":false,\"error\":\"maintenance callback not configured\"}");
            return;
        }

        String message;
        bool ok = callback(message);
        String json = "{";
        json += "\"ok\":" + String(ok ? "true" : "false") + ",";
        json += "\"action\":\"" + jsonEscape(label) + "\",";
        json += "\"message\":\"" + jsonEscape(message) + "\"";
        json += "}";
        server_.send(ok ? 200 : 500, "application/json", json);
    }

    void handleNotFound() {
        DebugLog::debugf("[WebUI] 404 %s from %s\n",
                      server_.uri().c_str(),
                      server_.client().remoteIP().toString().c_str());
        server_.send(404, "text/plain; charset=utf-8", "Not found");
    }

    String pageStart(const char* title) {
        String html = "<!DOCTYPE html><html><head><meta charset='utf-8'>";
        html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
        html += "<title>" + String(title) + "</title>";
        if (String(title) == "HomeKit" || String(title) == "CN105 Virtual Remote") {
            html += "<script src='https://cdnjs.cloudflare.com/ajax/libs/qrcodejs/1.0.0/qrcode.min.js'></script>";
        }
        html += "<style>";
        html += "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;background:#0d1321;color:#dbe4ee;margin:0;padding:24px;}";
        html += ".card{max-width:860px;margin:0 auto;background:#1d2d44;border-radius:16px;padding:24px;box-shadow:0 18px 48px rgba(0,0,0,0.25);}";
        html += ".header-row{display:flex;justify-content:space-between;gap:16px;align-items:flex-start;flex-wrap:wrap;}";
        html += "h1{margin:0 0 8px;color:#fff;font-size:1.8rem;}";
        html += "p{margin:0 0 12px;color:#b9c7d8;line-height:1.5;}";
        html += "code{background:#102a43;color:#d7f9ff;padding:2px 6px;border-radius:6px;}";
        html += ".meta{font-size:0.95rem;color:#9fb3c8;}";
        html += ".top-meta{margin-top:14px;}";
        html += ".top-meta .meta{margin-bottom:0;}";
        html += ".navbar{display:flex;gap:8px;align-items:center;justify-content:flex-end;flex-wrap:wrap;}";
        html += ".ghost-link{color:#d7f9ff;text-decoration:none;border:1px solid #486581;border-radius:10px;padding:10px 14px;display:inline-block;}";
        html += ".ghost-link.active{background:#102a43;border-color:#7aa6c8;color:#fff;}";
        html += ".ghost-button{background:transparent;}";
        html += ".ghost-button:hover{background:#102a43;}";
        html += ".page-actions{display:flex;gap:10px;align-items:center;flex-wrap:wrap;margin-top:14px;}";
        html += ".action-button{box-sizing:border-box;min-height:34px;padding:8px 12px;font-size:0.9rem;line-height:1;display:inline-flex;align-items:center;justify-content:center;vertical-align:middle;}";
        html += ".section{margin-top:18px;padding:18px;border-radius:14px;background:#13283d;border:1px solid #274560;}";
        html += ".section-title{margin:0 0 10px;color:#fff;font-size:1rem;font-weight:600;}";
        html += ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:12px;}";
        html += ".state-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(280px,1fr));gap:14px;}";
        html += ".state-panel{background:#102a43;border:1px solid #274560;border-radius:12px;padding:14px;}";
        html += ".field label{display:block;color:#b9c7d8;font-size:0.92rem;margin-bottom:6px;}";
        html += ".controls{display:flex;gap:12px;align-items:center;flex-wrap:wrap;}";
        html += ".pairing-card{background:#19324a;border:2px dashed #4a90d9;text-align:center;}";
        html += ".pairing-code{font-size:2rem;font-family:ui-monospace,SFMono-Regular,Menlo,monospace;color:#fff;letter-spacing:0.15em;margin:10px 0;}";
        html += ".pairing-hint{color:#b9c7d8;margin-top:8px;line-height:1.5;}";
        html += ".qr-card{text-align:center;}";
        html += ".qr-box{display:inline-flex;align-items:center;justify-content:center;min-width:220px;min-height:220px;background:#fff;border-radius:18px;padding:14px;color:#0b1b2b;}";
        html += ".qr-box img,.qr-box canvas{border-radius:8px;}";
        html += ".status-row{display:flex;justify-content:space-between;gap:12px;padding:8px 0;border-bottom:1px solid #274560;}";
        html += ".status-row:last-child{border-bottom:none;}";
        html += ".status-key{color:#9fb3c8;}";
        html += ".status-val{color:#fff;font-weight:600;}";
        html += "input,select,textarea{display:block;width:100%;box-sizing:border-box;padding:12px 14px;border-radius:10px;border:1px solid #486581;background:#102a43;color:#f0f4f8;font-size:1rem;}";
        html += "textarea{resize:vertical;font-family:ui-monospace,SFMono-Regular,Menlo,monospace;}";
        html += "button{background:#3e5c76;color:#fff;border:none;border-radius:10px;padding:12px 18px;font-size:1rem;cursor:pointer;}";
        html += "button:hover{background:#537999;}";
        html += "button.secondary{background:#284b63;}";
        html += "button.danger{background:#8a3b3b;}";
        html += "button.danger:hover{background:#a84949;}";
        html += ".modal{display:none;position:fixed;inset:0;z-index:10;align-items:center;justify-content:center;padding:20px;}";
        html += ".modal.open{display:flex;}";
        html += ".modal-backdrop{position:absolute;inset:0;background:rgba(4,10,18,0.72);}";
        html += ".modal-panel{position:relative;max-width:760px;max-height:88vh;overflow:auto;background:#1d2d44;border:1px solid #486581;border-radius:18px;padding:22px;box-shadow:0 24px 70px rgba(0,0,0,0.45);}";
        html += ".modal-header{display:flex;justify-content:space-between;gap:12px;align-items:flex-start;}";
        html += ".modal-close{background:#284b63;padding:8px 12px;}";
        html += "pre{margin:12px 0 0;background:#0b1b2b;color:#d7f9ff;border-radius:12px;padding:16px;min-height:120px;white-space:pre-wrap;word-break:break-word;}";
        html += "</style></head><body>";
        return html;
    }

    String navHtml(const char* activePath) {
        String active = activePath ? String(activePath) : String("/");
        String html = "<nav class='navbar'>";
        html += navLink("/", "Remote", active);
        html += navLink("/debug", "Debug", active);
        html += navLink("/logs", "Logs", active);
        html += navLink("/files", "Files", active);
        html += "</nav>";
        return html;
    }

    String navLink(const char* path, const char* label, const String& activePath) {
        String html = "<a class='ghost-link";
        if (activePath == path) {
            html += " active";
        }
        html += "' href='" + String(path) + "'>" + String(label) + "</a>";
        return html;
    }

    String statusRow(const String& key, const String& value) {
        return "<div class='status-row'><div class='status-key'>" + key + "</div><div class='status-val'>" + value + "</div></div>";
    }

    bool isSwingEnabled(const char* vane) {
        return vane && strcmp(vane, "SWING") == 0;
    }

    String formattedPairingCode() {
        String rawCode = pairingCode_ ? String(pairingCode_) : String(AppConfig::HOMEKIT_PAIRING_CODE);
        if (rawCode.length() == 8) {
            return rawCode.substring(0, 3) + "-" + rawCode.substring(3, 5) + "-" + rawCode.substring(5, 8);
        }
        return rawCode;
    }

    String homeKitModePreview(const char* mode) {
        if (!mode) return "--";
        if (strcmp(mode, "HEAT") == 0) return "HEAT";
        if (strcmp(mode, "COOL") == 0) return "COOL";
        if (strcmp(mode, "AUTO") == 0) return "AUTO";
        return String("AUTO (CN105: ") + mode + ")";
    }

    String homeKitFanPreview(const char* fan) {
        if (!fan) return "--";
        if (strcmp(fan, "AUTO") == 0) return "0% / AUTO";
        if (strcmp(fan, "QUIET") == 0) return "14% / QUIET";
        if (strcmp(fan, "1") == 0) return "28% / 1";
        if (strcmp(fan, "2") == 0) return "42% / 2";
        if (strcmp(fan, "3") == 0) return "71% / 3";
        if (strcmp(fan, "4") == 0) return "100% / 4";
        return fan;
    }

    String homeKitModalHtml() {
        String html;
        html += "<div id='homekitModal' class='modal' aria-hidden='true'>";
        html += "<div id='homekitModalBackdrop' class='modal-backdrop'></div>";
        html += "<div class='modal-panel'>";
        html += "<div class='modal-header'><div><h1>HomeKit 配对</h1>";
        html += "<p>打开 iPhone 的“家庭”App 添加配件，扫描二维码或输入下面的配对码。</p></div>";
        html += "<button id='homekitCloseBtn' class='modal-close' type='button'>关闭</button></div>";
        html += "<div class='section pairing-card'>";
        html += "<div class='section-title'>配对信息</div>";
        html += "<div class='pairing-code'>" + formattedPairingCode() + "</div>";
        html += "<div class='pairing-hint'>设备名: " + String(homekitName_ ? homekitName_ : "--") + "</div>";
        html += "<div class='pairing-hint'>Bridge: " + String(AppConfig::HOMEKIT_BRIDGE_NAME) + "</div>";
        html += "<div class='pairing-hint'>QR ID: " + String(AppConfig::HOMEKIT_QR_ID) + "</div>";
        html += "</div>";
        html += "<div class='section qr-card'>";
        html += "<div class='section-title'>HomeKit 二维码</div>";
        html += "<div id='homekitQr' class='qr-box'></div>";
        html += "<div class='pairing-hint'>Setup Payload: <code id='setupPayload'>" + String(AppConfig::HOMEKIT_SETUP_PAYLOAD.data()) + "</code></div>";
        html += "<div class='pairing-hint'>如果二维码没出现，说明当前浏览器没法从 cdnjs 加载二维码库；配对码依然可以手动输入。</div>";
        html += "</div>";
        html += "</div></div>";
        return html;
    }

    String liveLogModalHtml() {
        String html;
        html += "<div id='liveLogModal' class='modal' aria-hidden='true'>";
        html += "<div id='liveLogModalBackdrop' class='modal-backdrop'></div>";
        html += "<div class='modal-panel'>";
        html += "<div class='modal-header'><div><h1>Live Log</h1>";
        html += "<p>打开这个窗口后才会开始轮询当前 active log 文件；关闭窗口就停止。</p></div>";
        html += "<button id='liveLogCloseBtn' class='modal-close' type='button'>关闭</button></div>";
        html += "<div class='section'><div class='section-title'>当前日志</div>";
        html += "<pre id='liveLogBody'>Waiting for live log...</pre></div>";
        html += "</div></div>";
        return html;
    }

    String adminModalHtml() {
        String html;
        html += "<div id='adminModal' class='modal' aria-hidden='true'>";
        html += "<div id='adminModalBackdrop' class='modal-backdrop'></div>";
        html += "<div class='modal-panel'>";
        html += "<div class='modal-header'><div><h1>Admin</h1>";
        html += "<p>这里放维护和危险操作。清除动作只擦 NVS 数据，不会自动重启；重启后 HomeSpan 才会完整重新加载这些状态。</p></div>";
        html += "<button id='adminCloseBtn' class='modal-close' type='button'>关闭</button></div>";
        html += "<div class='section'>";
        html += "<div class='section-title'>维护操作</div>";
        html += "<div class='controls' style='margin-top:12px;'><button id='clearHomeKitBtn' class='danger'>清除 HomeKit 数据</button><button id='clearAllBtn' class='danger'>清除全部 HomeSpan 数据</button><button id='rebootBtn' class='secondary'>重启设备</button><button id='adminLiveLogBtn' class='secondary' type='button'>Live 当前日志</button><a class='ghost-link' href='/logs'>查看日志</a><a class='ghost-link' href='/files'>文件管理器</a></div>";
        html += "<pre id='maintenanceOutput'>等待操作...</pre>";
        html += "</div>";
        html += "</div></div>";
        return html;
    }

    void sendActionJson(bool ok, const char* action, const String& message) {
        String json = "{";
        json += "\"ok\":" + String(ok ? "true" : "false") + ",";
        json += "\"action\":\"" + jsonEscape(action ? String(action) : String("")) + "\",";
        json += "\"message\":\"" + jsonEscape(message) + "\"";
        json += "}";
        server_.send(ok ? 200 : 400, "application/json", json);
    }

    String downloadFileName(const String& rawPath) {
        String path = FileSystemManager::normalizePath(rawPath);
        int slash = path.lastIndexOf('/');
        String name = slash >= 0 ? path.substring(slash + 1) : path;
        if (name.length() == 0) {
            return "download.bin";
        }
        name.replace("\"", "_");
        return name;
    }

    String pageEnd() {
        return "</body></html>";
    }

    String metaLine() {
        uint32_t sec = (millis() - bootTime_) / 1000;
        return "<p class='meta'>WiFi Mode: " + wifiModeLabel() + " | SSID: " + wifiSsid() + " | IP: " + wifiIp() +
               " | Signal: " + wifiRssi() + " | CN105: " +
               AppConfig::cn105TransportModeLabel(AppConfig::CN105_TRANSPORT_MODE) +
               " | Uptime: " + String(sec) + "s</p>";
    }

    String selectField(const char* label, const char* id, const char* csv, const char* defaultValue) {
        String html = "<div class='field'><label for='" + String(id) + "'>" + String(label) + "</label><select id='" + String(id) + "'>";

        int start = 0;
        String values = String(csv);
        while (start <= values.length()) {
            int comma = values.indexOf(',', start);
            String item = comma == -1 ? values.substring(start) : values.substring(start, comma);
            item.trim();
            html += "<option value='" + item + "'";
            if (item == defaultValue) {
                html += " selected";
            }
            html += ">" + item + "</option>";
            if (comma == -1) {
                break;
            }
            start = comma + 1;
        }

        html += "</select></div>";
        return html;
    }

    String numberField(const char* label, const char* id, const String& minVal, const String& maxVal, const String& step, const String& value) {
        String html = "<div class='field'><label for='" + String(id) + "'>" + String(label) + "</label>";
        html += "<input id='" + String(id) + "' type='number' min='" + String(minVal) + "' max='" + String(maxVal) +
                "' step='" + String(step) + "' value='" + String(value) + "'></div>";
        return html;
    }

    String buildStatusJson() {
        auto& s = proto_->getCurrentSettings();
        auto& st = proto_->getCurrentStatus();

        String json = "{";
        json += "\"connected\":" + String(proto_->isConnected() ? "true" : "false") + ",";
        json += "\"transportMode\":\"" + String(AppConfig::cn105TransportModeLabel(AppConfig::CN105_TRANSPORT_MODE)) + "\",";
        json += "\"power\":\"" + jsonEscape(s.power ? s.power : "") + "\",";
        json += "\"mode\":\"" + jsonEscape(s.mode ? s.mode : "") + "\",";
        json += "\"targetTemperatureF\":" + jsonFloat(proto_->getTargetTemperatureF(), 0) + ",";
        json += "\"targetTemperatureRawC\":" + jsonFloat(s.temperature, 1) + ",";
        json += "\"fan\":\"" + jsonEscape(s.fan ? s.fan : "") + "\",";
        json += "\"vane\":\"" + jsonEscape(s.vane ? s.vane : "") + "\",";
        json += "\"wideVane\":\"" + jsonEscape(s.wideVane ? s.wideVane : "") + "\",";
        json += "\"iSee\":" + String(s.iSee ? "true" : "false") + ",";
        json += "\"roomTemperatureF\":" + jsonFloat(proto_->getRoomTemperatureF(), 0) + ",";
        json += "\"roomTemperatureRawC\":" + jsonFloat(st.roomTemperature, 1) + ",";
        json += "\"outsideAirTemperatureF\":" + jsonFloat(proto_->getOutsideTemperatureF(), 0) + ",";
        json += "\"outsideAirTemperatureRawC\":" + jsonFloat(st.outsideAirTemperature, 1) + ",";
        json += "\"operating\":" + String(st.operating ? "true" : "false") + ",";
        json += "\"compressorFrequency\":" + jsonFloat(st.compressorFrequency, 1) + ",";
        json += "\"inputPower\":" + jsonFloat(st.inputPower, 1) + ",";
        json += "\"kWh\":" + jsonFloat(st.kWh, 1) + ",";
        json += "\"runtimeHours\":" + jsonFloat(st.runtimeHours, 1) + ",";
        json += "\"stage\":\"" + jsonEscape(st.stage ? st.stage : "") + "\",";
        json += "\"subMode\":\"" + jsonEscape(st.subMode ? st.subMode : "") + "\"";
        json += "}";
        return json;
    }

    String buildSetPacketJson() {
        const cn105SetPacketBuild& build = proto_->getLastBuild();
        String json = "{";
        json += "\"hex\":\"" + jsonEscape(proto_->getLastPacketHex()) + "\",";
        json += "\"control1\":" + String(build.control1) + ",";
        json += "\"control2\":" + String(build.control2) + ",";
        json += "\"encodedTemperatureC\":" + jsonFloat(build.encodedTemperatureC, 1) + ",";
        json += "\"usedHighPrecisionTemperature\":" + String(build.usedHighPrecisionTemperature ? "true" : "false");
        json += "}";
        return json;
    }

    String buildResponsePacketJson() {
        const cn105InfoResponseBuild& build = proto_->getLastResponseBuild();
        String json = "{";
        json += "\"hex\":\"" + jsonEscape(proto_->getLastResponsePacketHex()) + "\",";
        json += "\"valid\":" + String(build.valid ? "true" : "false") + ",";
        json += "\"infoCode\":" + String(build.infoCode);
        json += "}";
        return json;
    }

    String argOrDefault(const char* name, const String& defaultValue) {
        if (!server_.hasArg(name)) {
            return defaultValue;
        }
        String value = server_.arg(name);
        value.trim();
        if (value.length() == 0) {
            return defaultValue;
        }
        return value;
    }

    String jsonFloat(float value, int decimals) {
        if (isnan(value)) {
            return "null";
        }
        return String(value, decimals);
    }

    String wifiModeLabel() {
        wifi_mode_t mode = WiFi.getMode();
        if (mode == WIFI_AP) return "AP";
        if (mode == WIFI_STA) return WiFi.status() == WL_CONNECTED ? "STA" : "STA (connecting)";
        if (mode == WIFI_AP_STA) return "AP+STA";
        return "OFF";
    }

    String wifiSsid() {
        wifi_mode_t mode = WiFi.getMode();
        if (mode == WIFI_AP || mode == WIFI_AP_STA) {
            return WiFi.softAPSSID();
        }
        if (WiFi.status() == WL_CONNECTED) {
            return WiFi.SSID();
        }
        return "--";
    }

    String wifiIp() {
        wifi_mode_t mode = WiFi.getMode();
        if (mode == WIFI_AP || mode == WIFI_AP_STA) {
            return WiFi.softAPIP().toString();
        }
        if (WiFi.status() == WL_CONNECTED) {
            return WiFi.localIP().toString();
        }
        return "--";
    }

    String wifiRssi() {
        if (WiFi.status() != WL_CONNECTED) {
            return "--";
        }
        return String(WiFi.RSSI()) + " dBm";
    }

    String jsonEscape(const String& input) {
        String out;
        out.reserve(input.length() + 16);

        for (size_t i = 0; i < input.length(); i++) {
            char c = input[i];
            if (c == '\\' || c == '"') {
                out += '\\';
                out += c;
            } else if (c == '\n') {
                out += "\\n";
            } else if (c == '\r') {
                out += "\\r";
            } else if (c == '\t') {
                out += "\\t";
            } else {
                out += c;
            }
        }

        return out;
    }
};
