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

  preferences.begin("app", false); 
  ssid = preferences.getString("ssid", ""); 
  password = preferences.getString("password", "");
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
    if (MDNS.begin("ildawavex16")) {
      webSerialln("mDNS responder started: ildawavex16.local");
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
*{box-sizing:border-box}:root{--bg:#1a1a1f;--surface:#252530;--primary:#32323f;--accent:#4a9eff;--text:#e8e8ec;--text-dim:#8888a0;--border:#3a3a48;--radius:0.5em}html,body{margin:0;padding:0;min-height:100%}body{font-family:system-ui,-apple-system,sans-serif;background:var(--bg);color:var(--text);line-height:1.5}main{max-width:60em;margin:0 auto;padding:1em}h2{text-align:center;margin:.5em 0 1em;font-size:1.5em;background:linear-gradient(90deg,red,#ff8000,#ff0,#0f0,#0ff,#0080ff,#8000ff,#ff0080,red);background-size:200% 100%;-webkit-background-clip:text;background-clip:text;color:transparent;animation:r 3s linear infinite}@keyframes r{to{background-position:200% 50%}}.card{background:var(--surface);border-radius:var(--radius);padding:1em;margin-bottom:1em}.card-title{font-weight:600;margin-bottom:.75em;padding-bottom:.5em;border-bottom:1px solid var(--border);display:flex;align-items:center;gap:.5em}#tableContainer{max-height:12em;overflow-y:auto;border-radius:var(--radius);background:var(--primary)}table{width:100%;border-collapse:collapse}th,td{padding:.6em .8em;text-align:left}th{background:var(--bg);position:sticky;top:0;font-size:.85em;text-transform:uppercase;color:var(--text-dim)}tr:not(:last-child) td{border-bottom:1px solid var(--border)}tr:hover td{background:rgba(74,158,255,.1)}tr.selected td{background:rgba(74,158,255,.25)}.btn-row{display:flex;gap:.5em;margin-top:1em;flex-wrap:wrap}button{flex:1;min-width:6em;padding:.75em 1em;border:none;border-radius:var(--radius);background:var(--primary);color:var(--text);font-size:1em;cursor:pointer;transition:background .2s,transform .1s}button:hover{background:var(--accent)}button:active{transform:scale(.97)}.control-group{margin-bottom:1em}.control-group:last-child{margin-bottom:0}.control-label{display:flex;justify-content:space-between;margin-bottom:.3em;font-size:.9em}.control-value{color:var(--accent);font-weight:600}input[type=range]{width:100%;height:.4em;border-radius:.2em;background:var(--primary);outline:none;-webkit-appearance:none}input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:1.2em;height:1.2em;border-radius:50%;background:var(--accent);cursor:pointer}input[type=range]::-moz-range-thumb{width:1.2em;height:1.2em;border-radius:50%;background:var(--accent);cursor:pointer;border:none}.input-group{display:flex;flex-direction:column;gap:.5em}input[type=text]{width:100%;padding:.7em;border:1px solid var(--border);border-radius:var(--radius);background:var(--primary);color:var(--text);font-size:1em}input[type=text]:focus{outline:2px solid var(--accent);outline-offset:-2px}input[type=text]::placeholder{color:var(--text-dim)}footer{text-align:center;padding:1em;color:var(--text-dim);font-size:.8em}@media(max-width:30em){main{padding:.5em}.card{padding:.8em}th,td{padding:.5em;font-size:.9em}}
</style>
</head>
<body>
<main>
<h2>&#9889; ILDAWaveX16 &#9889;</h2>
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
<div class="card-title">&#128246; Wi-Fi Settings</div>
<div class="input-group">
<input type="text" id="ssid" placeholder="SSID">
<input type="text" id="pass" placeholder="Password">
<button onclick="setWiFi()">Save &amp; Connect</button>
</div>
</div>
<div class="card">
<div class="card-title">&#128196; Serial Monitor</div>
<div id="serialOutput" style="height:12em;overflow-y:auto;background:var(--primary);border-radius:var(--radius);padding:0.75em;font-family:monospace;font-size:0.85em;line-height:1.2em;white-space:pre-wrap;border:1px solid var(--border);"></div>
<div class="btn-row">
<button onclick="clearSerial()">Clear</button>
<button onclick="toggleAutoUpdate()" id="autoUpdateBtn">Auto-update: ON</button>
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
  startSerialUpdate()
});

function playFile(){
  if(!s){alert("Select a file.");return}
  fetch(`/play?file=${encodeURIComponent(s)}&rate=${document.getElementById("scanRate").value}`)
}

function stopFile(){
  fetch("/stop")
}

function updateSettings(){
  const r=document.getElementById("scanRate"),b=document.getElementById("brightness");
  document.getElementById("rateValue").textContent=r.value;
  document.getElementById("brightnessValue").textContent=b.value;
  fetch(`/control?rate=${r.value}&brightness=${b.value}`).catch(console.error)
}

function setWiFi(){
  fetch(`/set_wifi?ssid=${encodeURIComponent(document.getElementById("ssid").value)}&pass=${encodeURIComponent(document.getElementById("pass").value)}`)
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

    pixels.setPixelColor(0, pixels.Color(0, 255, 0));
    pixels.show();

    request->send(200, "text/plain", "Playing " + file + " at " + String(rate) + " ms");
  });

  server.on("/stop", HTTP_GET, [](AsyncWebServerRequest *request) {
    renderer.sd_stop();
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