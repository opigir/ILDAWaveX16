#include <Arduino.h> 
#include <vector>
#include <Adafruit_NeoPixel.h>
#include <SDCard.h>
#include <Renderer.h>
#include <IDNServer.h>
#include <IWPServer.h>
#include <WiFi.h>
#include "esp_wifi.h"
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "driver/timer.h"
#include <Preferences.h>
#include "esp_task_wdt.h"
#include "wifi_credentials.h"
#include <ESPmDNS.h>
#include <ElegantOTA.h>

using namespace std;

// Serial buffer for web interface - circular buffer to prevent memory fragmentation
char serialBuffer[3000];
int serialHead = 0;
int serialSize = 0;
const int MAX_SERIAL_BUFFER = 3000;

Preferences preferences;
Adafruit_NeoPixel pixels(1, PIN_LED, NEO_GRB + NEO_KHZ800);
SDCard sd;
Renderer renderer;
AsyncWebServer server(80);
File current_file;

IDNServer idn;
IWPServer iwp;

String currentlyPlayingFile = "";

// Function to add text to serial buffer for web display
void addToSerialBuffer(String text) {
  for (int i = 0; i < text.length(); i++) {
    serialBuffer[serialHead] = text[i];
    serialHead = (serialHead + 1) % MAX_SERIAL_BUFFER;
    if (serialSize < MAX_SERIAL_BUFFER) {
      serialSize++;
    }
  }
}

// Function to get serial buffer as String for web interface
String getSerialBuffer() {
  String result = "";
  result.reserve(serialSize + 1);

  if (serialSize < MAX_SERIAL_BUFFER) {
    // Buffer not full yet, read from beginning
    for (int i = 0; i < serialSize; i++) {
      result += serialBuffer[i];
    }
  } else {
    // Buffer is full, read from head to end, then from beginning to head
    for (int i = serialHead; i < MAX_SERIAL_BUFFER; i++) {
      result += serialBuffer[i];
    }
    for (int i = 0; i < serialHead; i++) {
      result += serialBuffer[i];
    }
  }
  return result;
}

// Custom print functions that also store in buffer
void webSerial(String text) {
  Serial.print(text);
  addToSerialBuffer(text);
}

void webSerialln(String text) {
  Serial.println(text);
  addToSerialBuffer(text + "\n");
}

void init_wifi() {
  String ssid;
  String password;
  String mdns_name;

  preferences.begin("app", false);
  ssid = preferences.getString("ssid", "");
  password = preferences.getString("password", "");
  mdns_name = preferences.getString("mdns_name", "ildawavex16");
  preferences.end();

  if (ssid == "" || password == ""){
    ssid = WIFI_SSID;
    password = WIFI_PASSWORD;
  }

  WiFi.mode(WIFI_AP_STA);

  WiFi.softAP("ILDAWaveX16", "ildawave");
  webSerial("AP IP address: ");
  webSerialln(WiFi.softAPIP().toString());

  WiFi.begin(ssid, password);
  webSerial("Connecting to WiFi");

  uint8_t attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      webSerial(".");
      attempts++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    webSerial("\nLocal IP: ");
    webSerialln(WiFi.localIP().toString());

    // Start mDNS service
    if (MDNS.begin(mdns_name.c_str())) {
      webSerial("mDNS responder started: ");
      webSerial(mdns_name);
      webSerialln(".local");
      MDNS.addService("http", "tcp", 80);
    } else {
      webSerialln("Error starting mDNS");
    }
  }
  else webSerialln("\nWiFi Connection Failed!");
}

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>SD Player</title>
<style>
*{box-sizing:border-box}:root{--bg:#1a1a1f;--surface:#252530;--primary:#32323f;--accent:#4a9eff;--text:#e8e8ec;--text-dim:#8888a0;--border:#3a3a48;--radius:0.5em}html,body{margin:0;padding:0;min-height:100%}body{font-family:system-ui,-apple-system,sans-serif;background:var(--bg);color:var(--text);line-height:1.5}main{max-width:60em;margin:0 auto;padding:1em}h2{text-align:center;margin:.5em 0 0;font-size:1.5em;background:linear-gradient(90deg,red,#ff8000,#ff0,#0f0,#0ff,#0080ff,#8000ff,#ff0080,red);background-size:200% 100%;-webkit-background-clip:text;background-clip:text;color:transparent;animation:r 3s linear infinite}@keyframes r{to{background-position:200% 50%}}.card{background:var(--surface);border-radius:var(--radius);padding:1em;margin-bottom:1em}.card-title{font-weight:600;margin-bottom:.75em;padding-bottom:.5em;border-bottom:1px solid var(--border);display:flex;align-items:center;gap:.5em}#tableContainer{max-height:12em;overflow-y:auto;border-radius:var(--radius);background:var(--primary)}table{width:100%;border-collapse:collapse}th,td{padding:.6em .8em;text-align:left}th{background:var(--bg);position:sticky;top:0;font-size:.85em;text-transform:uppercase;color:var(--text-dim)}tr:not(:last-child) td{border-bottom:1px solid var(--border)}tr:hover td{background:rgba(74,158,255,.1)}tr.selected td{background:rgba(74,158,255,.25)}.btn-row{display:flex;gap:.5em;margin-top:1em;flex-wrap:wrap}button{flex:1;min-width:6em;padding:.75em 1em;border:none;border-radius:var(--radius);background:var(--primary);color:var(--text);font-size:1em;cursor:pointer;transition:background .2s,transform .1s}button:hover{background:var(--accent)}button:active{transform:scale(.97)}.control-group{margin-bottom:1em}.control-group:last-child{margin-bottom:0}.control-label{display:flex;justify-content:space-between;margin-bottom:.3em;font-size:.9em}.control-value{color:var(--accent);font-weight:600}input[type=range]{width:100%;height:.4em;border-radius:.2em;background:var(--primary);outline:none;-webkit-appearance:none}input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:1.2em;height:1.2em;border-radius:50%;background:var(--accent);cursor:pointer}input[type=range]::-moz-range-thumb{width:1.2em;height:1.2em;border-radius:50%;background:var(--accent);cursor:pointer;border:none}.input-group{display:flex;flex-direction:column;gap:.5em}input[type=text]{width:100%;padding:.7em;border:1px solid var(--border);border-radius:var(--radius);background:var(--primary);color:var(--text);font-size:1em;transition:border-color .2s}input[type=text]:focus{outline:2px solid var(--accent);outline-offset:-2px}input[type=text]::placeholder{color:var(--text-dim)}input[type=text].invalid{border-color:#ff4444}input[type=text].valid{border-color:#44ff44}button:disabled{opacity:0.6;cursor:not-allowed}footer{text-align:center;padding:1em;color:var(--text-dim);font-size:.8em}@media(max-width:30em){main{padding:.5em}.card{padding:.8em}th,td{padding:.5em;font-size:.9em}}
</style>
</head>
<body>
<main>
<h2>&#9889; ILDAWaveX16 &#9889;</h2>
<div id="deviceInfo" style="text-align:center;margin-bottom:1em;padding:0.5em;font-size:0.85em;color:var(--text-dim);">
<div>IP: <span id="deviceIP">Loading...</span> | mDNS: <span id="deviceMDNS">Loading...</span>.local</div>
</div>
<div id="nowPlaying" style="display:none;text-align:center;margin-bottom:1em;padding:0.75em;background:linear-gradient(90deg,var(--accent),var(--primary));border-radius:var(--radius);font-weight:600;">
<div>&#9654; Now Playing: <span id="currentFile"></span></div>
</div>
<div class="card">
<div class="card-title">&#128194; SD Card</div>
<div id="tableContainer">
<table id="fileTable"><tr><th>Filename</th><th>Size</th></tr><!-- FILE_ROWS --></table>
</div>
<div class="btn-row">
<button onclick="playFile()">&#9654; Play</button>
<button onclick="stopFile()">&#9209; Stop</button>
</div>
</div>
<div class="card">
<div class="card-title">&#9881; Projection Settings</div>
<div class="control-group">
<div class="control-label"><span>Scan Period</span><span class="control-value"><span id="rateValue">10</span> &micro;s</span></div>
<input type="range" id="scanRate" min="10" max="10000" value="10" oninput="updateSettings()">
</div>
<div class="control-group">
<div class="control-label"><span>Brightness</span><span class="control-value"><span id="brightnessValue">100</span>%</span></div>
<input type="range" id="brightness" min="0" max="100" value="100" oninput="updateSettings()">
</div>
</div>
<div class="card">
<div class="card-title" style="cursor:pointer;" onclick="toggleSettings()">&#9881; Settings <span id="settingsToggle">&#9660;</span></div>
<div id="settingsContainer">
<div style="padding:1em;">
<div style="font-weight:600;margin-bottom:0.75em;padding-bottom:0.5em;border-bottom:1px solid var(--border);display:flex;align-items:center;gap:0.5em;">&#128246; Wi-Fi Settings</div>
<div class="input-group">
<input type="text" id="ssid" placeholder="SSID">
<input type="text" id="pass" placeholder="Password">
<button onclick="setWiFi()">Save &amp; Connect</button>
</div>
</div>
<div style="padding:1em;">
<div style="font-weight:600;margin-bottom:0.75em;padding-bottom:0.5em;border-bottom:1px solid var(--border);display:flex;align-items:center;gap:0.5em;">&#127760; Network Name - DNS</div>
<div class="input-group">
<div style="display:flex;align-items:center;gap:0.5em;margin-bottom:0.5em;">
<input type="text" id="mdns_name" placeholder="e.g. laser1" style="flex:1;">
<span style="color:var(--text-dim);font-size:0.9em;">.local</span>
</div>
<button onclick="setMDNS()">Save & Restart</button>
</div>
</div>
<div style="padding:1em;">
<div style="cursor:pointer;font-weight:600;margin-bottom:0.75em;padding-bottom:0.5em;border-bottom:1px solid var(--border);display:flex;align-items:center;gap:0.5em;" onclick="toggleSerial()">&#128196; Serial Monitor <span id="serialToggle">&#9660;</span></div>
<div id="serialContainer">
<div id="serialOutput" style="height:12em;overflow-y:auto;background:var(--primary);border-radius:var(--radius);padding:0.75em;font-family:monospace;font-size:0.85em;line-height:1.2em;white-space:pre-wrap;border:1px solid var(--border);"></div>
<div class="btn-row">
<button onclick="clearSerial()">Clear</button>
<button onclick="toggleAutoUpdate()" id="autoUpdateBtn">Auto-update: ON</button>
</div>
</div>
</div>
<div style="padding:1em;">
<div class="btn-row">
<button onclick="window.open('/update', '_blank')">&#8593; OTA Update</button>
</div>
</div>
</div>
</div>
</main>
<footer><a href="https://stanleyprojects.com/" style="color:inherit;text-decoration:none" target="_blank" rel="noopener noreferrer">StanleyProjects</a> | VER 0.1</footer>
<script>
let s=null,autoUpdate=true,updateInterval;

document.addEventListener("DOMContentLoaded",()=>{
  document.querySelectorAll("#fileTable tr").forEach((r,i)=>{
    if(!i)return;
    r.onclick=()=>{
      document.querySelectorAll("#fileTable tr").forEach(x=>x.classList.remove("selected"));
      r.classList.add("selected");
      s=r.dataset.filename
    };
    r.ondblclick=()=>{s=r.dataset.filename;playFile()}
  });
  startSerialUpdate();
  loadMDNSName();
  loadDeviceInfo();
  setInterval(loadDeviceInfo, 5000)
});

function loadMDNSName(){
  fetch("/get_mdns").then(r=>r.text()).then(name=>{
    document.getElementById("mdns_name").value=name
  }).catch(console.error)
}

function loadDeviceInfo(){
  fetch("/get_device_info").then(r=>r.json()).then(data=>{
    document.getElementById("deviceIP").textContent=data.ip;
    document.getElementById("deviceMDNS").textContent=data.mdns;

    const nowPlaying=document.getElementById("nowPlaying");
    const currentFile=document.getElementById("currentFile");

    if(data.playing){
      currentFile.textContent=data.playing;
      nowPlaying.style.display="block"
    }else{
      nowPlaying.style.display="none"
    }
  }).catch(console.error)
}

function playFile(){
  if(!s){alert("Select a file.");return}
  fetch(`/play?file=${encodeURIComponent(s)}&rate=${document.getElementById("scanRate").value}`).then(()=>loadDeviceInfo())
}

function stopFile(){
  fetch("/stop").then(()=>loadDeviceInfo())
}

function updateSettings(){
  const r=document.getElementById("scanRate"),b=document.getElementById("brightness");
  document.getElementById("rateValue").textContent=r.value;
  document.getElementById("brightnessValue").textContent=b.value;
  fetch(`/control?rate=${r.value}&brightness=${b.value}`).catch(console.error)
}

function validateInput(input,isValid){
  input.classList.remove("valid","invalid");
  input.classList.add(isValid?"valid":"invalid");
  return isValid
}

function setWiFi(){
  const ssidInput=document.getElementById("ssid");
  const passInput=document.getElementById("pass");

  const ssidValid=validateInput(ssidInput,ssidInput.value.length>0);
  const passValid=validateInput(passInput,passInput.value.length>=8||passInput.value.length===0);

  if(!ssidValid){
    alert("SSID cannot be empty");
    return
  }
  if(!passValid){
    alert("Password must be at least 8 characters or empty for open network");
    return
  }

  const btn=event.target;
  const originalText=btn.textContent;
  btn.textContent="Saving...";
  btn.disabled=true;

  fetch(`/set_wifi?ssid=${encodeURIComponent(ssidInput.value)}&pass=${encodeURIComponent(passInput.value)}`)
    .then(()=>{
      btn.textContent="WiFi Updated!";
      setTimeout(()=>{btn.textContent=originalText;btn.disabled=false},3000)
    })
    .catch((err)=>{
      // Connection reset is expected during restart - treat as success
      if(err.message.includes("fetch")||err.message.includes("Failed")){
        btn.textContent="WiFi Updated!";
        setTimeout(()=>{btn.textContent=originalText;btn.disabled=false},3000)
      }else{
        btn.textContent="Error!";
        setTimeout(()=>{btn.textContent=originalText;btn.disabled=false},2000)
      }
    })
}

function setMDNS(){
  const mdnsInput=document.getElementById("mdns_name");
  const mdnsRegex=/^[a-z0-9]([a-z0-9-]*[a-z0-9])?$/i;

  const mdnsValid=validateInput(mdnsInput,mdnsInput.value.length>0&&mdnsInput.value.length<=63&&mdnsRegex.test(mdnsInput.value));

  if(!mdnsValid){
    alert("mDNS name must be 1-63 characters, alphanumeric with hyphens (not starting/ending with hyphen)");
    return
  }

  const btn=event.target;
  const originalText=btn.textContent;
  btn.textContent="Saving...";
  btn.disabled=true;

  const newName=mdnsInput.value;

  fetch(`/set_mdns?name=${encodeURIComponent(newName)}`)
    .then(()=>{
      btn.textContent="mDNS Updated!";
      setTimeout(()=>{
        btn.textContent="Redirecting...";
        window.location.href=`http://${newName}.local/`
      },1000)
    })
    .catch((err)=>{
      // Connection reset is expected during restart - treat as success
      if(err.message.includes("fetch")||err.message.includes("Failed")){
        btn.textContent="mDNS Updated!";
        setTimeout(()=>{
          btn.textContent="Redirecting...";
          window.location.href=`http://${newName}.local/`
        },2000)
      }else{
        btn.textContent="Error!";
        setTimeout(()=>{btn.textContent=originalText;btn.disabled=false},2000)
      }
    })
}

function updateSerial(){
  fetch("/serial").then(r=>r.text()).then(data=>{
    const out=document.getElementById("serialOutput");
    const wasAtBottom=out.scrollTop>=out.scrollHeight-out.clientHeight-5;
    out.textContent=data;
    if(wasAtBottom)out.scrollTop=out.scrollHeight
  }).catch(console.error)
}

function clearSerial(){
  fetch("/clear_serial");
  document.getElementById("serialOutput").textContent=""
}

function toggleAutoUpdate(){
  autoUpdate=!autoUpdate;
  document.getElementById("autoUpdateBtn").textContent=`Auto-update: ${autoUpdate?"ON":"OFF"}`;
  if(autoUpdate)startSerialUpdate();else clearInterval(updateInterval)
}

function toggleSettings(){
  const container=document.getElementById("settingsContainer");
  const toggle=document.getElementById("settingsToggle");

  if(container.style.display==="none"){
    container.style.display="block";
    toggle.textContent="▼"
  }else{
    container.style.display="none";
    toggle.textContent="▶"
  }
}

function toggleSerial(){
  const container=document.getElementById("serialContainer");
  const toggle=document.getElementById("serialToggle");

  if(container.style.display==="none"){
    container.style.display="block";
    toggle.textContent="▼"
  }else{
    container.style.display="none";
    toggle.textContent="▶"
  }
}

function startSerialUpdate(){
  updateSerial();
  updateInterval=setInterval(updateSerial,1000)
}
</script>
</body>
</html>
)rawliteral";

void setupServer() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    String page = index_html;
    page.replace(F("<!-- FILE_ROWS -->"), sd.generateFileRows());
    request->send(200, "text/html", page);
    });

  server.on("/play", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (!request->hasParam("file")) {
      request->send(400, "text/plain", "Missing file");
      return;
    }

    if (request->hasParam("rate")) {
      int rate = request->getParam("rate")->value().toInt();
      renderer.change_freq(rate);
    }

    if (request->hasParam("brightness")) {
      int brightness = request->getParam("brightness")->value().toInt();
      renderer.change_brightness(brightness);
    }

    String file = request->getParam("file")->value();
    int rate = request->getParam("rate")->value().toInt();

    renderer.sd_stop();

    current_file = sd.getFile(file.c_str());

    if (renderer.rendererRunning == 0) renderer.start();
    renderer.sd_start(current_file);

    currentlyPlayingFile = file;

    pixels.setPixelColor(0, pixels.Color(0, 255, 0));
    pixels.show();

    request->send(200, "text/plain", "Playing " + file + " at " + String(rate) + " ms");
  });

  server.on("/stop", HTTP_GET, [](AsyncWebServerRequest *request) {
    renderer.sd_stop();
    currentlyPlayingFile = "";
    pixels.setPixelColor(0, pixels.Color(0, 0, 255));
    pixels.show();
    request->send(200, "text/plain", "Stopped");
  });

  server.on("/control", HTTP_GET, [](AsyncWebServerRequest *request) {
    bool handled = false;

    if (request->hasParam("rate")) {
      int rate = request->getParam("rate")->value().toInt();
      renderer.change_freq(rate);
      handled = true;
    }

    if (request->hasParam("brightness")) {
      int brightness = request->getParam("brightness")->value().toInt();
      renderer.change_brightness(brightness);
      handled = true;
    }

    if (handled) {
      String response = "Updated settings:";
      if (request->hasParam("rate")) response += " rate=" + request->getParam("rate")->value();
      if (request->hasParam("brightness")) response += " brightness=" + request->getParam("brightness")->value();
      request->send(200, "text/plain", response);
    } else request->send(400, "text/plain", "No valid parameters provided");
  });

  server.on("/set_wifi", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("ssid") || !request->hasParam("pass")) {
      request->send(400, "text/plain", "Missing ssid or pass parameter");
      return;
    }

    String ssid = request->getParam("ssid")->value();
    String pass = request->getParam("pass")->value();

    if (ssid == "") return;

    preferences.begin("app", false);
    preferences.putString("ssid", ssid);
    preferences.putString("password", pass);
    preferences.end();

    request->send(200, "text/plain", "Wi-Fi settings saved. Restarting.");
    delay(100); // Give time for response to be sent
    ESP.restart();
  });

  server.on("/serial", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", getSerialBuffer());
  });

  server.on("/clear_serial", HTTP_GET, [](AsyncWebServerRequest *request) {
    serialHead = 0;
    serialSize = 0;
    request->send(200, "text/plain", "Serial buffer cleared");
  });

  server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "Not found");
  });

  server.on("/get_mdns", HTTP_GET, [](AsyncWebServerRequest *request) {
    preferences.begin("app", false);
    String mdns_name = preferences.getString("mdns_name", "ildawavex16");
    preferences.end();
    request->send(200, "text/plain", mdns_name);
  });

  server.on("/get_device_info", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = "{";
    json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
    preferences.begin("app", false);
    String mdns_name = preferences.getString("mdns_name", "ildawavex16");
    preferences.end();
    json += "\"mdns\":\"" + mdns_name + "\",";
    json += "\"playing\":\"" + currentlyPlayingFile + "\"";
    json += "}";
    request->send(200, "application/json", json);
  });

  server.on("/set_mdns", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("name")) {
      request->send(400, "text/plain", "Missing name parameter");
      return;
    }

    String mdns_name = request->getParam("name")->value();

    if (mdns_name == "") return;

    preferences.begin("app", false);
    preferences.putString("mdns_name", mdns_name);
    preferences.end();

    request->send(200, "text/plain", "mDNS name saved. Restarting.");
    delay(100); // Give time for response to be sent
    ESP.restart();
  });

  ElegantOTA.begin(&server);

  server.begin();
}

void udp_loop(void* pvParameters) {
  while(1) {
    idn.loop();
    iwp.loop();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

void setup() {
  Serial.begin(115200);
  // while(!Serial.availableForWrite()){}

  pixels.begin();
  pixels.clear();
  pixels.show();

  pixels.setPixelColor(0, pixels.Color(0, 0, 255));
  pixels.show();

  init_wifi();
  esp_wifi_set_ps(WIFI_PS_NONE);
  sd.begin();
  sd.mount();
  idn.begin();
  idn.setRendererHandle(&renderer);
  iwp.begin();
  iwp.setRendererHandle(&renderer);

  renderer.begin();
  renderer.start();

  xTaskCreatePinnedToCore (udp_loop, "udp_loop", 8192, NULL, 2, NULL, 0);

  setupServer();

  // sd.list();

  // current_file = sd.getFile("/animation.ild");
  // renderer.sd_start(current_file);
}

void loop(){ vTaskDelete(NULL); }