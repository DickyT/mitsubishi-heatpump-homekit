#pragma once

#include <WebServer.h>
#include <WiFi.h>

#include "AppConfig.h"
#include "MitsubishiProtocol.h"

class WebUI {
    WebServer server_;
    int port_;
    uint32_t bootTime_;
    MitsubishiProtocol* proto_;

public:
    WebUI(int port, MitsubishiProtocol* proto)
        : server_(port), port_(port), proto_(proto) {
        bootTime_ = millis();
    }

    void begin() {
        server_.on("/", HTTP_GET, [this]() { handleRemoteRoot(); });
        server_.on("/debug", HTTP_GET, [this]() { handleDebugRoot(); });
        server_.on("/api/status", HTTP_GET, [this]() { handleStatus(); });
        server_.on("/api/ping", HTTP_GET, [this]() { handlePing(); });
        server_.on("/api/serial", HTTP_POST, [this]() { handleSerialPost(); });
        server_.on("/api/remote/build", HTTP_POST, [this]() { handleRemoteBuild(); });
        server_.onNotFound([this]() { handleNotFound(); });
        server_.begin();
        Serial.printf("[WebUI] Started on port %d\n", port_);
    }

    void loop() {
        server_.handleClient();
    }

private:
    void handleRemoteRoot() {
        Serial.printf("[WebUI] GET / from %s\n", server_.client().remoteIP().toString().c_str());

        String html = pageStart("CN105 Virtual Remote");
        html += "<div class='card'>";
        html += "<div class='header-row'><div><h1>CN105 Virtual Remote</h1>";
        html += "<p>先在本地组装遥控器 payload，只有点击 <code>Send To Server</code> 才会提交给服务端生成 CN105 配置预览。</p></div>";
        html += "<a class='ghost-link' href='/debug'>Open Debug</a></div>";
        html += metaLine();

        html += "<div class='section'>";
        html += "<div class='section-title'>Server Echo</div>";
        html += "<pre id='output'>Waiting for action...</pre>";
        html += "</div>";

        html += "<div class='section'>";
        html += "<div class='section-title'>Remote Draft</div>";
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
        html += "<div class='controls' style='margin-top:14px;'><button id='sendBtn'>Send To Server</button><button id='resetBtn' class='secondary'>Reset Draft</button></div>";
        html += "</div>";

        html += "<div class='section'>";
        html += "<div class='section-title'>Local Payload Preview</div>";
        html += "<pre id='draft'></pre>";
        html += "</div>";
        html += "</div>";

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
                "output.textContent='Draft reset locally. Nothing has been sent yet.';"
                "}"
                "function applyStatus(status){"
                "if(status.power) fields.power.value=status.power;"
                "if(status.mode) fields.mode.value=status.mode;"
                "if(status.targetTemperatureF !== undefined) fields.temperatureF.value=Math.round(Number(status.targetTemperatureF));"
                "if(status.fan) fields.fan.value=status.fan;"
                "if(status.vane) fields.vane.value=status.vane;"
                "if(status.wideVane) fields.wideVane.value=status.wideVane;"
                "renderDraft();"
                "}"
                "async function loadStatus(){"
                "output.textContent='Loading current status from server...';"
                "try{"
                "const resp=await fetch('/api/status');"
                "const data=await resp.json();"
                "if(data.status){applyStatus(data.status);}"
                "output.textContent=JSON.stringify(data,null,2);"
                "}catch(err){"
                "output.textContent='Status load failed: '+err;"
                "renderDraft();"
                "}"
                "}"
                "Object.values(fields).forEach(el=>el.addEventListener('input',renderDraft));"
                "document.getElementById('sendBtn').addEventListener('click',sendDraft);"
                "document.getElementById('resetBtn').addEventListener('click',resetDraft);"
                "loadStatus();"
                "</script>";
        html += pageEnd();

        server_.send(200, "text/html; charset=utf-8", html);
    }

    void handleDebugRoot() {
        Serial.printf("[WebUI] GET /debug from %s\n", server_.client().remoteIP().toString().c_str());

        String html = pageStart("Web Debug");
        html += "<div class='card'>";
        html += "<div class='header-row'><div><h1>Web Debug</h1>";
        html += "<p>这里保留最早的 ping 和串口回显调试能力，方便继续验证 HTTP 通路和串口输出。</p></div>";
        html += "<a class='ghost-link' href='/'>Open Remote</a></div>";
        html += metaLine();
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

    void handleStatus() {
        Serial.printf("[WebUI] GET /api/status from %s\n", server_.client().remoteIP().toString().c_str());

        auto& s = proto_->getCurrentSettings();
        auto& st = proto_->getCurrentStatus();

        String json = "{";
        json += "\"ok\":true,";
        json += "\"status\":{";
        json += "\"connected\":" + String(proto_->isConnected() ? "true" : "false") + ",";
        json += "\"power\":\"" + jsonEscape(s.power ? s.power : "") + "\",";
        json += "\"mode\":\"" + jsonEscape(s.mode ? s.mode : "") + "\",";
        json += "\"targetTemperatureF\":" + String(proto_->getTargetTemperatureF(), 0) + ",";
        json += "\"targetTemperatureRawC\":" + String(s.temperature, 1) + ",";
        json += "\"fan\":\"" + jsonEscape(s.fan ? s.fan : "") + "\",";
        json += "\"vane\":\"" + jsonEscape(s.vane ? s.vane : "") + "\",";
        json += "\"wideVane\":\"" + jsonEscape(s.wideVane ? s.wideVane : "") + "\",";
        json += "\"roomTemperatureF\":" + String(proto_->getRoomTemperatureF(), 0) + ",";
        json += "\"roomTemperatureRawC\":" + String(st.roomTemperature, 1) + ",";
        json += "\"outsideAirTemperatureF\":" + String(proto_->getOutsideTemperatureF(), 0) + ",";
        json += "\"outsideAirTemperatureRawC\":" + String(st.outsideAirTemperature, 1) + ",";
        json += "\"operating\":" + String(st.operating ? "true" : "false") + ",";
        json += "\"compressorFrequency\":" + String(st.compressorFrequency, 1) + ",";
        json += "\"inputPower\":" + String(st.inputPower, 1) + ",";
        json += "\"kWh\":" + String(st.kWh, 1) + ",";
        json += "\"runtimeHours\":" + String(st.runtimeHours, 1);
        json += "},";
        json += "\"packet\":{";
        json += "\"hex\":\"" + jsonEscape(proto_->getLastPacketHex()) + "\",";
        json += "\"control1\":" + String(proto_->getLastBuild().control1) + ",";
        json += "\"control2\":" + String(proto_->getLastBuild().control2) + ",";
        json += "\"encodedTemperatureC\":" + String(proto_->getLastBuild().encodedTemperatureC, 1) + ",";
        json += "\"usedHighPrecisionTemperature\":" + String(proto_->getLastBuild().usedHighPrecisionTemperature ? "true" : "false");
        json += "}";
        json += "}";

        server_.send(200, "application/json", json);
    }

    void handlePing() {
        Serial.printf("[WebUI] GET /api/ping from %s\n", server_.client().remoteIP().toString().c_str());

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
        Serial.printf("[WebUI] POST /api/serial from %s\n", server_.client().remoteIP().toString().c_str());

        String body = server_.arg("plain");
        body.trim();

        if (body.length() == 0) {
            server_.send(400, "application/json", "{\"ok\":false,\"error\":\"empty message\"}");
            return;
        }

        Serial.print(AppConfig::SERIAL_MESSAGE_PREFIX);
        Serial.println(body);

        String json = "{";
        json += "\"ok\":true,";
        json += "\"sent\":\"" + jsonEscape(body) + "\",";
        json += "\"message\":\"written to serial\"";
        json += "}";

        server_.send(200, "application/json", json);
    }

    void handleRemoteBuild() {
        Serial.printf("[WebUI] POST /api/remote/build from %s\n", server_.client().remoteIP().toString().c_str());

        String power = argOrDefault("power", "ON");
        String mode = argOrDefault("mode", "AUTO");
        String temperatureF = argOrDefault("temperatureF", String(AppConfig::DEFAULT_TARGET_TEMPERATURE_F, 0).c_str());
        String fan = argOrDefault("fan", "AUTO");
        String vane = argOrDefault("vane", "AUTO");
        String wideVane = argOrDefault("wideVane", "AIRFLOW CONTROL");
        float parsedTemperatureF = temperatureF.toFloat();
        proto_->applyMockRemoteSettings(power, mode, parsedTemperatureF, fan, vane, wideVane);
        String config = proto_->buildMockConfigPreview();

        Serial.println("[WebUI] CN105 draft preview:");
        Serial.println(config);

        auto& s = proto_->getCurrentSettings();
        auto& st = proto_->getCurrentStatus();

        String json = "{";
        json += "\"ok\":true,";
        json += "\"message\":\"draft accepted by dummy endpoint\",";
        json += "\"configFile\":\"" + jsonEscape(config) + "\",";
        json += "\"packet\":{";
        json += "\"hex\":\"" + jsonEscape(proto_->getLastPacketHex()) + "\",";
        json += "\"control1\":" + String(proto_->getLastBuild().control1) + ",";
        json += "\"control2\":" + String(proto_->getLastBuild().control2) + ",";
        json += "\"encodedTemperatureC\":" + String(proto_->getLastBuild().encodedTemperatureC, 1) + ",";
        json += "\"usedHighPrecisionTemperature\":" + String(proto_->getLastBuild().usedHighPrecisionTemperature ? "true" : "false");
        json += "},";
        json += "\"status\":{";
        json += "\"connected\":" + String(proto_->isConnected() ? "true" : "false") + ",";
        json += "\"power\":\"" + jsonEscape(s.power ? s.power : "") + "\",";
        json += "\"mode\":\"" + jsonEscape(s.mode ? s.mode : "") + "\",";
        json += "\"targetTemperatureF\":" + String(proto_->getTargetTemperatureF(), 0) + ",";
        json += "\"targetTemperatureRawC\":" + String(s.temperature, 1) + ",";
        json += "\"fan\":\"" + jsonEscape(s.fan ? s.fan : "") + "\",";
        json += "\"vane\":\"" + jsonEscape(s.vane ? s.vane : "") + "\",";
        json += "\"wideVane\":\"" + jsonEscape(s.wideVane ? s.wideVane : "") + "\",";
        json += "\"roomTemperatureF\":" + String(proto_->getRoomTemperatureF(), 0) + ",";
        json += "\"roomTemperatureRawC\":" + String(st.roomTemperature, 1) + ",";
        json += "\"outsideAirTemperatureF\":" + String(proto_->getOutsideTemperatureF(), 0) + ",";
        json += "\"outsideAirTemperatureRawC\":" + String(st.outsideAirTemperature, 1) + ",";
        json += "\"operating\":" + String(st.operating ? "true" : "false") + ",";
        json += "\"compressorFrequency\":" + String(st.compressorFrequency, 1) + ",";
        json += "\"inputPower\":" + String(st.inputPower, 1) + ",";
        json += "\"kWh\":" + String(st.kWh, 1) + ",";
        json += "\"runtimeHours\":" + String(st.runtimeHours, 1);
        json += "}";
        json += "}";

        server_.send(200, "application/json", json);
    }

    void handleNotFound() {
        Serial.printf("[WebUI] 404 %s from %s\n",
                      server_.uri().c_str(),
                      server_.client().remoteIP().toString().c_str());
        server_.send(404, "text/plain; charset=utf-8", "Not found");
    }

    String pageStart(const char* title) {
        String html = "<!DOCTYPE html><html><head><meta charset='utf-8'>";
        html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
        html += "<title>" + String(title) + "</title>";
        html += "<style>";
        html += "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;background:#0d1321;color:#dbe4ee;margin:0;padding:24px;}";
        html += ".card{max-width:860px;margin:0 auto;background:#1d2d44;border-radius:16px;padding:24px;box-shadow:0 18px 48px rgba(0,0,0,0.25);}";
        html += ".header-row{display:flex;justify-content:space-between;gap:16px;align-items:flex-start;flex-wrap:wrap;}";
        html += "h1{margin:0 0 8px;color:#fff;font-size:1.8rem;}";
        html += "p{margin:0 0 12px;color:#b9c7d8;line-height:1.5;}";
        html += "code{background:#102a43;color:#d7f9ff;padding:2px 6px;border-radius:6px;}";
        html += ".meta{font-size:0.95rem;color:#9fb3c8;}";
        html += ".ghost-link{color:#d7f9ff;text-decoration:none;border:1px solid #486581;border-radius:10px;padding:10px 14px;display:inline-block;}";
        html += ".section{margin-top:18px;padding:18px;border-radius:14px;background:#13283d;border:1px solid #274560;}";
        html += ".section-title{margin:0 0 10px;color:#fff;font-size:1rem;font-weight:600;}";
        html += ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:12px;}";
        html += ".field label{display:block;color:#b9c7d8;font-size:0.92rem;margin-bottom:6px;}";
        html += ".controls{display:flex;gap:12px;align-items:center;flex-wrap:wrap;}";
        html += "input,select{display:block;width:100%;box-sizing:border-box;padding:12px 14px;border-radius:10px;border:1px solid #486581;background:#102a43;color:#f0f4f8;font-size:1rem;}";
        html += "button{background:#3e5c76;color:#fff;border:none;border-radius:10px;padding:12px 18px;font-size:1rem;cursor:pointer;}";
        html += "button:hover{background:#537999;}";
        html += "button.secondary{background:#284b63;}";
        html += "pre{margin:12px 0 0;background:#0b1b2b;color:#d7f9ff;border-radius:12px;padding:16px;min-height:120px;white-space:pre-wrap;word-break:break-word;}";
        html += "</style></head><body>";
        return html;
    }

    String pageEnd() {
        return "</body></html>";
    }

    String metaLine() {
        uint32_t sec = (millis() - bootTime_) / 1000;
        return "<p class='meta'>WiFi Mode: " + wifiModeLabel() + " | SSID: " + wifiSsid() + " | IP: " + wifiIp() +
               " | Signal: " + wifiRssi() + " | Uptime: " + String(sec) + "s</p>";
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
