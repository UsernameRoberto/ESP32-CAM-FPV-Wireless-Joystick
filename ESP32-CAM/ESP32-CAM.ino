#include "Arduino.h"
#include "driver/ledc.h"
#include "esp_camera.h"
#include <WiFi.h>
#include <WiFiManager.h>
#undef HTTP_ANY
#include <esp_http_server.h>
#include <Preferences.h>
Preferences prefs;
WiFiManager wm;

void saveIfChanged_Int(const char* key, int newVal, int &currentVal) {
  if (newVal != currentVal) {
    currentVal = newVal;
    prefs.putInt(key, newVal);
    Serial.printf("Saved %s = %d\n", key, newVal);
  }
}

void saveIfChanged_Float(const char* key, float newVal, float &currentVal) {
  if (abs(newVal - currentVal) > 0.01) {
    currentVal = newVal;
    prefs.putFloat(key, newVal);
    Serial.printf("Saved %s = %.2f\n", key, newVal);
  }
}

void saveIfChanged_String(const char* key, String newVal, String &currentVal) {
  if (newVal != currentVal) {
    currentVal = newVal;
    prefs.putString(key, newVal);
    Serial.printf("Saved %s = %s\n", key, newVal.c_str());
  }
}

// ===== CAMERA PINS (AI THINKER) =====
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22


// ===== STREAM + TOGGLES =====
bool overlayTemp = true;

// ===== LED CONFIG =====
#define LED_PIN 4
#define LED_CHANNEL LEDC_CHANNEL_0
#define LED_FREQ 5000
#define LED_RESOLUTION 8  // 0–255 PWM

int ledBrightness = 0; // 0–100
bool ledState = false;
String videoQuality = "VGA";      // default resolution
int jpegQuality = 50;             // default
float zoomRatio = 1.0;            // saved zoom
int serverPort = 8181; // default



// ===== STREAM HANDLER =====
#define _STREAM_CONTENT_TYPE "multipart/x-mixed-replace;boundary=frame"
#define _STREAM_BOUNDARY "\r\n--frame\r\n"
#define _STREAM_PART "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n"


static esp_err_t stream_handler(httpd_req_t *req){
  camera_fb_t * fb = NULL;
  char part[64];

  // Set response type (unchanged)
  httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);

  // 🔍 Get socket (NEW - early validation)
  int sockfd = httpd_req_to_sockfd(req);
  if (sockfd < 0) {
    Serial.println("❌ Invalid socket - client not connected");
    return ESP_FAIL;
  }

  //Serial.println("🎥 Stream started");

  while(true){

    // 🔍 Optional safety check (NEW)
    if (sockfd < 0) {
      Serial.println("⚠️ Socket lost");
      break;
    }

    // Get frame (unchanged)
    fb = esp_camera_fb_get();
    if(!fb){
      Serial.println("Camera capture failed");
      break;
    }

    // Send header (unchanged logic, improved debug)
    size_t hlen = snprintf(part, sizeof(part), _STREAM_PART, fb->len);
    if(httpd_resp_send_chunk(req, part, hlen) != ESP_OK){
      //Serial.println("⚠️ Client disconnected (header)");
      esp_camera_fb_return(fb);
      break;
    }

    // Send image (unchanged logic, improved debug)
    if(httpd_resp_send_chunk(req, (const char*)fb->buf, fb->len) != ESP_OK){
      //Serial.println("⚠️ Client disconnected (frame)");
      esp_camera_fb_return(fb);
      break;
    }

    // Send boundary (unchanged logic, improved debug)
    if(httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY)) != ESP_OK){
      //Serial.println("⚠️ Client disconnected (boundary)");
      esp_camera_fb_return(fb);
      break;
    }

    // Return frame buffer (unchanged - critical!)
    esp_camera_fb_return(fb);

    // 🔥 Small delay = stability + lower CPU load (unchanged)
    vTaskDelay(2);   // much more responsive
    taskYIELD();     // 🔥 give CPU to other tasks
  }

  //Serial.println("🛑 Stream ended");

  return ESP_OK;
}
// ===== TOGGLE TEMP on off HANDLER =====
static esp_err_t toggle_temp_handler(httpd_req_t *req){
  overlayTemp = !overlayTemp;
  String s = overlayTemp ? "ON" : "OFF";
  Serial.println("Overlay Toggle");
  httpd_resp_set_type(req, "text/plain");
  return httpd_resp_send(req, s.c_str(), s.length());
}
// ===== TOGGLE TEMP WIFI =====
static esp_err_t cpu_temp_handler(httpd_req_t *req){  
    int rssi = WiFi.RSSI();

    float signalPercent;
    if (rssi <= -100) signalPercent = 0;
    else if (rssi >= -50) signalPercent = 100;
    else signalPercent = 2 * (rssi + 100);

    float temp = temperatureRead();

    char buf[64];

    // IMPORTANT: no space before \n
    snprintf(buf, sizeof(buf), "%.1f°C\n📶%.0f%%", temp, signalPercent);

    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_send(req, buf, strlen(buf));
}


// ===== RESOLUTION HANDLER =====
static esp_err_t set_quality_handler(httpd_req_t *req){
  char buf[16];

  if(httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK){
    char res[8];

    if(httpd_query_key_value(buf, "res", res, sizeof(res)) == ESP_OK){

      // Determine requested resolution
      String newRes = (strcmp(res, "QVGA") == 0) ? "QVGA" : "VGA";

      // Apply to camera
      sensor_t * s = esp_camera_sensor_get();
      if(newRes == "QVGA"){
        s->set_framesize(s, FRAMESIZE_QVGA);
      } else {
        s->set_framesize(s, FRAMESIZE_VGA);
      }

      // Save ONLY if changed (also updates videoQuality internally)
      saveIfChanged_String("res", newRes, videoQuality);
    }
  }

  httpd_resp_set_type(req, "text/plain");
  return httpd_resp_send(req, videoQuality.c_str(), videoQuality.length());
}

// ===== JPEG QUALITY HANDLER =====
static esp_err_t set_jpeg_handler(httpd_req_t *req){
  char buf[10];
  if(httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK){
    char val[4];
    if(httpd_query_key_value(buf, "quality", val, sizeof(val)) == ESP_OK){
      int q = atoi(val);
      if(q >= 10 && q <= 63){
        sensor_t * s = esp_camera_sensor_get();
        s->set_quality(s, q);
        jpegQuality = q;
        saveIfChanged_Int("jpeg", q, jpegQuality);
      }
    }
  }
  httpd_resp_set_type(req, "text/plain");
  return httpd_resp_send(req, String(jpegQuality).c_str(), String(jpegQuality).length());
}

// ===== ZOOM HANDLER =====
static esp_err_t set_zoom_handler(httpd_req_t *req){
  char buf[10];
  if(httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK){
    char val[6];
    if(httpd_query_key_value(buf, "zoom", val, sizeof(val)) == ESP_OK){
      float z = atof(val);
      if(z >= 0.5 && z <= 2.0){ // limit zoom
        zoomRatio = z;
        saveIfChanged_Float("zoom", z, zoomRatio);
      }
    }
  }
  httpd_resp_set_type(req, "text/plain");
  return httpd_resp_send(req, String(zoomRatio).c_str(), String(zoomRatio).length());
}

// ===== PWM LED HANDLER =====
static esp_err_t set_led_handler(httpd_req_t *req){
  char buf[32];
  int value = -1;

  // Get query string
  if(httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK){
    char param[8];

    // Extract "val"
    if(httpd_query_key_value(buf, "val", param, sizeof(param)) == ESP_OK){
      value = atoi(param);
    }
  }

  // Validate input
  if(value < 0 || value > 100){
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_send(req, "Invalid value (0-100)", -1);
  }

  // Save value
  saveIfChanged_Int("led", value, ledBrightness);

  // Convert 0–100 → 0–255
  int pwm = (value * 255) / 100;

  // Apply PWM (ESP-IDF style)
  ledc_set_duty(LEDC_HIGH_SPEED_MODE, LED_CHANNEL, pwm);
  ledc_update_duty(LEDC_HIGH_SPEED_MODE, LED_CHANNEL);

  // Debug output
  Serial.print("LED brightness set to: ");
  Serial.print(value);
  Serial.print("% (PWM=");
  Serial.print(pwm);
  Serial.println(")");

  // Response
  char resp[16];
  snprintf(resp, sizeof(resp), "%d", value);

  httpd_resp_set_type(req, "text/plain");
  return httpd_resp_send(req, resp, strlen(resp));
}


// ===== INDEX PAGE HANDLER =====
static esp_err_t index_handler(httpd_req_t *req){
  const char* html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
  <meta name="theme-color" content="#000000">
  <meta name="msapplication-navbutton-color" content="#000000">
  <meta name="apple-mobile-web-app-capable" content="yes">
  <meta name="apple-mobile-web-app-status-bar-style" content="#000000">
  <meta name="viewport" content="width=device-width, initial-scale=1">
<link rel="icon" type="image/svg+xml" href='data:image/svg+xml,<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100"><text x="50" y="50" font-size="100" text-anchor="middle" dominant-baseline="middle" fill="Red">🚗</text></svg>' />
  <title>ESP32-CAM FPV</title>
  <style>
    body {
      background: #000;
      color: #fff;
      font-family: 'Segoe UI', sans-serif;
      text-align: center;
      margin: 0;
      padding: 0;
      overflow-Y:auto;
    }
    h2 {
      margin: 10px 0;
      color: #00ffff;
    }
    canvas {
      top: 50%;
      left:50%;
      position:absolute;
      transform:translate(-50%,-50%);
      background: #000;
      border: 2px solid #00ffff;
      border-radius: 10px;
      z-index:9999;
    }
    
    
    button {
      background: linear-gradient(to bottom, #1e1e1e, #444);
      color: #fff;
      border: 2px solid #00ffff;
      border-radius: 8px;
      padding: 10px 20px;
      margin: 5px;
      font-size: 16px;
      cursor: pointer;
      transition: all 0.2s;
    }
    button:hover {
      background: linear-gradient(to bottom, #00ffff, #1e1e1e);
      color: #000;
    }
    #fullscreen-button {
        position: fixed;
        top: 5px;
        right: 5px;
        background: rgba(0, 0, 0, 0.7);
        border: 0;
        width: 40px;
        height: 40px;
        box-sizing: border-box;
        transition: transform 0.3s;
        cursor: pointer;
        display: flex;
        align-items: center;
        justify-content: center;
        padding: 0; /* Remove default padding */
        fill: #fff;
        z-index: 9999;
}
body[fullscreen] canvas {
  transition: all 0.3s ease;
}
#fullscreen-button:hover {
        background: rgba(0, 0, 0, 0.5);
}
#fullscreen-button svg:nth-child(2) {
        display: none;
}
[fullscreen] #fullscreen-button svg:nth-child(1) {
        display: none;
}
[fullscreen] #fullscreen-button svg:nth-child(2) {
        display: inline-block;
}
#fullscreen-button svg {
        width: 24px;
        height: 24px;
        flex-shrink: 0;
}
#fullscreen-button svg {
        pointer-events: none;
}


/* Style the slider */
#ledSlider {
    /*position: absolute;  */        /* absolute so we can place it relative to window */
    /*left: 50%;   */                /* center horizontally */
    /*bottom: 18px;  */               /* 20px from bottom */
    /*transform: translateX(-50%);*/ /* shift by half width to truly center */
    -webkit-appearance: none;
  width: 150px;
  max-width: 150px;
  height: 12px;
  border-radius: 6px;
  background: #222;
  box-shadow: inset 0 0 5px rgba(0,255,128,0.3);
  outline: none;
  margin: 10px auto;
  display: block;
  transition: background 0.3s;
}

/* Track hover glow */
#ledSlider:hover {
  background: #333;
}

/* Webkit thumb */
#ledSlider::-webkit-slider-thumb {
  -webkit-appearance: none;
  appearance: none;
  width: 24px;
  height: 24px;
  border-radius: 50%;
  background: #00ffff;
  border: 2px solid #00ffff;
  cursor: pointer;
  box-shadow: 0 0 10px rgba(0,255,128,0.7);
  transition: transform 0.2s, box-shadow 0.2s;
}

/* Pulse effect when >50% */
#ledSlider.pulse::-webkit-slider-thumb {
  animation: pulseThumb 1s infinite;
}

@keyframes pulseThumb {
  0%   { box-shadow: 0 0 10px rgba(0,255,128,0.7); transform: scale(1); }
  50%  { box-shadow: 0 0 20px rgba(0,255,128,1); transform: scale(1.2); }
  100% { box-shadow: 0 0 10px rgba(0,255,128,0.7); transform: scale(1); }
}

/* Firefox */
#ledSlider::-moz-range-thumb {
  width: 24px;
  height: 24px;
  border-radius: 50%;
  background: #00ffff;
  border: 2px solid #00ffff;
  cursor: pointer;
  box-shadow: 0 0 10px rgba(0,255,128,0.7);
}

/* IE */
#ledSlider::-ms-thumb {
  width: 24px;
  height: 24px;
  border-radius: 50%;
  background: #00ffff;
  border: 2px solid #00ffff;
  cursor: pointer;
  box-shadow: 0 0 10px rgba(0,255,128,0.7);
}

/* Fixed gear button top-left */
#menuToggleBtn {
    position: fixed;
    top: 5px;
    left: 5px;
    z-index: 1000;
    font-size: 20px;
    background: transparent;
    border: none;
    border-radius: 8px;
    padding: 6px 10px;
    cursor: pointer;
    transition: left 0.3s;
}

/* Controls container fixed, initially hidden on left */
.controls {
  position: fixed;
  top: 50%;
  transform:translateY(-50%);
  left: -220px; /* hidden off-screen */
  width: 200px;
  display: flex;
  flex-direction: column;
  /*gap: 8px;*/
  background: rgba(0,0,0,0.8);
  /*padding: 10px;*/
  border-radius: 8px;
  color: white;
  transition: left 0.3s;
  z-index: 99999;
  /*height:80vh;*/
  /*overflow-y:auto;*/
}

/* Optional: button styles inside controls */
.controls button {
  padding: 6px 10px;
  font-size: 16px;
  border: none;
  border-radius: 4px;
  cursor: pointer;
}
@media only screen and (max-width: 600px) {
  body {
    /* Style for Mobile */
}
     .controls {
  position: fixed;
  top: 50%;
  transform:translateY(-50%);
  left: -220px; /* hidden off-screen */
  width: 200px;
  display: flex;
  flex-direction: column;
  /*gap: 8px;*/
  background: rgba(0,0,0,0.8);
  /*padding: 10px;*/
  border-radius: 8px;
  color: white;
  transition: left 0.3s;
  z-index: 99999;
  height:80vh;
  overflow-y:auto;
}

  
}
  </style>  
  
</head>
<body>
<script>
// --- Fullscreen ---
if (document.fullscreenEnabled) {
  const fullscreen_button = document.createElement("button");
  fullscreen_button.setAttribute('id', 'fullscreen-button');
  fullscreen_button.setAttribute('title', 'Fullscreen');
  fullscreen_button.addEventListener("click", toggle_fullscreen);
  fullscreen_button.innerHTML = `
        <svg viewBox="0 0 24 24">
            <path d="M7 14H5v5h5v-2H7v-3zm-2-4h2V7h3V5H5v5zm12 
            7h-3v2h5v-5h-2v3zM14 5v2h3v3h2V5h-5z"/>
        </svg>
        <svg viewBox="0 0 24 24">
            <path d="M5 16h3v3h2v-5H5v2zm3-8H5v2h5V5H8v3zm6 
            11h2v-3h3v-2h-5v5zm2-11V5h-2v5h5V8h-3z"/>
        </svg>
    `;
  document.body.appendChild(fullscreen_button);

  document.addEventListener("keydown", function(e) {
    if (e.key === "Escape" && document.fullscreenElement) {
      toggle_fullscreen();
    }
  });

  document.addEventListener("fullscreenchange", updateFullscreenIcon);
  updateFullscreenIcon();
}

function toggle_fullscreen() {
  if (!document.fullscreenElement) {
    document.body.requestFullscreen();
  } else {
    document.exitFullscreen();
  }
}

function updateFullscreenIcon() {
  const isFullscreen = document.fullscreenElement;
  if (isFullscreen) {
    document.body.setAttribute("fullscreen", "");
  } else {
    document.body.removeAttribute("fullscreen");
  }
}
  </script>
<!-- Gear toggle button -->
<button title="Settings" id="menuToggleBtn">⚙️</button>
  <!-- Sliding Control Panel -->
<div id="slidingControls" class="controls">
    <button onclick="toggleTemp()">🎥 CPU Temp</button>
    <button onclick="toggleQuality()">⚡ Video Quality</button>
    <button onclick="toggleJPEG()">🎞️ JPEG Quality</button>
    <button onclick="zoomIn()">🔍 Zoom +</button>
    <button onclick="zoomOut()">🔍 Zoom -</button>
    <button onclick="takeSnapshot()">📸 Snapshot</button>
    <button id="recordBtn" onclick="toggleRecording()">⏺ Record</button>
    <button id="rotateBtn">🔄 Rotate Video</button>
    <button>
  <div id="ledValue">0%</div>  
<!-- LED Slider -->
<input 
    type="range" 
    min="0" 
    max="100" 
    value="0" 
    id="ledSlider" 
    oninput="setLED()"></button>
</div>
  <canvas id="cam" width="640" height="480"></canvas>
<script>
/* ==========================
   Global Variables
   ========================== */
let overlay = false;
let lastTemp = "--";
let ledState = false;
let videoQuality = 'VGA';
let jpegQuality = 50;
const jpegOptions = [50, 60, 70, 80, 90, 100];
let zoomRatio = 1;
const zoomStep = 0.25;
const zoomMin = 0.5;
const zoomMax = 1.5;
let ledBrightness = 0;   // global, 0–100 in percent

let mediaRecorder;
let recordedChunks = [];
let isRecording = false;

let fps = 0;
let frameCount = 0;
let lastFpsTime = performance.now();

const canvas = document.getElementById('cam');
const ctx = canvas.getContext('2d');
const img = new Image();

/* ==========================
   Load Settings from localStorage
   ========================== */
if (localStorage.getItem('ledBrightness')) {
    ledBrightness = parseInt(localStorage.getItem('ledBrightness'));
}
if (localStorage.getItem('jpegQuality')) {
    jpegQuality = parseInt(localStorage.getItem('jpegQuality'));
}
if (localStorage.getItem('videoQuality')) {
    videoQuality = localStorage.getItem('videoQuality');
}
if (localStorage.getItem('zoomRatio')) {
    zoomRatio = parseFloat(localStorage.getItem('zoomRatio'));
}
if (localStorage.getItem('overlay')) {
    overlay = (localStorage.getItem('overlay') === 'ON');
}

// Update LED slider UI
if(document.getElementById("ledSlider")) {
    document.getElementById("ledSlider").value = ledBrightness;
    document.getElementById("ledValue").innerText = "💡 Brightness: " + ledBrightness + "%";
}

/* ==========================
   Save Settings to localStorage
   ========================== */
function saveSettings() {
    localStorage.setItem('ledBrightness', ledBrightness);
    localStorage.setItem('jpegQuality', jpegQuality);
    localStorage.setItem('videoQuality', videoQuality);
    localStorage.setItem('zoomRatio', zoomRatio);
    localStorage.setItem('overlay', overlay ? 'ON' : 'OFF');
}

/* ==========================
   Snapshot Function
   ========================== */
function takeSnapshot(){
  let now = new Date();
  let timestamp = now.getFullYear() + '-' +
    String(now.getMonth()+1).padStart(2,'0') + '-' +
    String(now.getDate()).padStart(2,'0') + '_' +
    String(now.getHours()).padStart(2,'0') + '-' +
    String(now.getMinutes()).padStart(2,'0') + '-' +
    String(now.getSeconds()).padStart(2,'0');

  let dataURL = canvas.toDataURL('image/jpeg', 0.9);

  let a = document.createElement('a');
  a.href = dataURL;
  a.download = 'ESP32CAM_' + timestamp + '.jpg';
  document.body.appendChild(a);
  a.click();
  document.body.removeChild(a);
}

/* ==========================
   Zoom Functions
   ========================== */
function zoomIn(){
  zoomRatio = Math.min(zoomRatio + zoomStep, zoomMax);
  canvas.width = 640 * zoomRatio;
  canvas.height = 480 * zoomRatio;
  fetch('/set_zoom?zoom=' + zoomRatio);
  saveSettings();
}
function zoomOut(){
  zoomRatio = Math.max(zoomRatio - zoomStep, zoomMin);
  canvas.width = 640 * zoomRatio;
  canvas.height = 480 * zoomRatio;
  fetch('/set_zoom?zoom=' + zoomRatio);
  saveSettings();
}

/* ==========================
   JPEG Quality
   ========================== */
function toggleJPEG(){
  let index = jpegOptions.indexOf(jpegQuality);
  index = (index + 1) % jpegOptions.length;
  jpegQuality = jpegOptions[index];
  fetch('/set_jpeg?quality=' + jpegQuality)
    .then(r => r.text())
    .then(s => console.log("JPEG Quality set to " + s));
  saveSettings();
}

/* ==========================
   Video Quality
   ========================== */
function toggleQuality(){
  videoQuality = (videoQuality === 'VGA') ? 'QVGA' : 'VGA';
  fetch('/set_quality?res=' + videoQuality)
    .then(r => r.text())
    .then(s => console.log("Video Quality set to " + s));
  saveSettings();
}

/* ==========================
   CPU Overlay
   ========================== */
function toggleTemp(){
  fetch('/toggle_temp').then(r => r.text()).then(s => {
      overlay = (s === 'ON');
      saveSettings();
  });
}

/* ==========================
   Fetch CPU Temp Every 1s
   ========================== */
setInterval(() => {
  if(overlay){
    fetch('/cpu_temp').then(r => r.text()).then(t => lastTemp = t);
  }
}, 1000);

/* ==========================
   Update Video Frame
   ========================== */
function updateFrame(){ img.src = '/stream?' + Date.now(); }

img.onload = function(){

  ctx.clearRect(0, 0, canvas.width, canvas.height);
  ctx.drawImage(img, 0, 0, canvas.width, canvas.height);

  ctx.font = (20 * zoomRatio) + 'px sans-serif';

  /* ==========================
     UNIVERSAL DRAW FUNCTION
     ========================== */
  function drawOSD(textLines, x, y, align = "left", colors = []) {

    let padding = 10;
    let lineHeight = 30 * zoomRatio;

    // Measure widest line
    let maxWidth = 0;
    textLines.forEach(t => {
      let w = ctx.measureText(t).width;
      if (w > maxWidth) maxWidth = w;
    });

    let boxWidth = maxWidth + padding;
    let boxHeight = lineHeight * textLines.length;

    // Adjust X for alignment
    if (align === "right") x -= boxWidth;
    if (align === "center") x -= boxWidth / 2;

    // Background
    ctx.fillStyle = 'rgba(0,0,0,0)';
    ctx.fillRect(x, y, boxWidth + 10, boxHeight);

    // Draw lines
    textLines.forEach((text, i) => {

      let color = colors[i] || 'white';

      ctx.shadowColor = color;
      ctx.shadowBlur = 8;

      ctx.fillStyle = color;
      ctx.fillText(text, x + 5, y + lineHeight * (i + 1));

      ctx.shadowBlur = 0;
    });
  }

  /* ==========================
     CPU + WIFI (TOP LEFT)
     ========================== */
  if (overlay && lastTemp) {

    let parts = lastTemp.split('\n');
    let tempStr = parts[0] || '';
    let wifiStr = parts[1] || '';

    let tempVal = parseFloat(tempStr);
    let wifiPercent = parseFloat(wifiStr.replace(/[^\d.]/g, ''));

    // CPU color
    let cpuColor = 'lime';
    if (!isNaN(tempVal)) {
      if (tempVal < 40) cpuColor = 'lime';
      else if (tempVal < 60) cpuColor = 'yellow';
      else cpuColor = 'red';
    }

    // WiFi color
    let wifiColor = 'white';
    if (!isNaN(wifiPercent)) {
      if (wifiPercent >= 75) wifiColor = 'lime';
      else if (wifiPercent >= 50) wifiColor = 'yellow';
      else wifiColor = 'red';
    }

    drawOSD(
      ['🎥 ' + tempStr, wifiStr],
      5,
      5,
      "left",
      [cpuColor, wifiColor]
    );
  }

 /* ==========================
   LED (TOP RIGHT) cold-white effect
   ========================== */
let isOn = ledBrightness > 0;

// Map ledBrightness (0–100) to alpha (soft glow)
let alpha = 0.2 + (ledBrightness / 100) * 0.8; // 0.1–0.9

// Cold white LED color
let ledColor = `rgba(255,255,255,${alpha})`;

let ledText = `💡 ${isOn ? 'ON' : 'OFF'} ${ledBrightness}%`;

drawOSD(
  [ledText],
  canvas.width - 5,
  5,
  "right",
  [ledColor]
);
  /* ==========================
     ZOOM (TOP CENTER)
     ========================== */
  drawOSD(
    [`🔍 ${zoomRatio.toFixed(2)}x`],
    canvas.width / 2,
    5,
    "center",
    ['lightblue']
  );

  /* ==========================
     VIDEO QUALITY (BOTTOM LEFT)
     ========================== */
  drawOSD(
    [`⚡ ${videoQuality}`],
    5,
    canvas.height - 70 * zoomRatio,
    "left",
    ['cyan']
  );

  /* ==========================
     FPS (BOTTOM LEFT UNDER QUALITY)
     ========================== */
  let fpsColor = 'lime';
  if (fps < 15) fpsColor = 'red';
  else if (fps < 25) fpsColor = 'yellow';

  drawOSD(
    [`🎯 ${fps} FPS`],
    5,
    canvas.height - 40 * zoomRatio,
    "left",
    [fpsColor]
  );

  /* ==========================
     JPEG QUALITY (BOTTOM CENTER)
     ========================== */
  drawOSD(
    [`🎞️ ${jpegQuality}%`],
    canvas.width / 2,
    canvas.height - 40 * zoomRatio,
    "center",
    ['magenta']
  );

  /* ==========================
     TIME (BOTTOM RIGHT)
     ========================== */
  let now = new Date();
  let dtText = '⏱️ ' + now.toLocaleTimeString();

  drawOSD(
    [dtText],
    canvas.width - 5,
    canvas.height - 40 * zoomRatio,
    "right",
    ['orange']
  );

  /* ==========================
   RECORDING INDICATOR PULSING
   ========================== */
if(isRecording){
    const pulse = 0.5 + 0.5 * Math.sin(performance.now() / 250); // 0–1, 500ms period
    const red = Math.floor(255 * pulse); // pulse intensity
    const alpha = 0.3 + 0.7 * pulse; // glow alpha

    // Glow
    ctx.fillStyle = `rgba(${red},0,0,${alpha})`;
    ctx.beginPath();
    ctx.arc(15, 15, 10, 0, 2 * Math.PI); // bigger for glow
    ctx.fill();

    // Core dot
    ctx.fillStyle = `rgba(255,0,0,1)`;
    ctx.beginPath();
    ctx.arc(15, 15, 6, 0, 2 * Math.PI);
    ctx.fill();
}
  /* ==========================
     FPS COUNTER UPDATE
     ========================== */
  frameCount++;
  let nowPerf = performance.now();

  if (nowPerf - lastFpsTime >= 1000) {
    fps = frameCount;
    frameCount = 0;
    lastFpsTime = nowPerf;
  }

requestAnimationFrame(updateFrame); 
};
updateFrame();

/* ==========================
   LED Slider
   ========================== */
let lastSendTime = 0;
let pendingValue = null;
let timer = null;

const SEND_INTERVAL = 120; // 80–150ms is safe

function setLED() {
    let val = document.getElementById("ledSlider").value;
    ledBrightness = parseInt(val);

    document.getElementById("ledValue").innerText =
        "💡 Brightness: " + ledBrightness + "%";

    let now = Date.now();

    // Send immediately if enough time passed
    if (now - lastSendTime >= SEND_INTERVAL) {
        fetch('/set_led?val=' + val);
        lastSendTime = now;
        pendingValue = null;
    } else {
        // Store latest value
        pendingValue = val;

        // Schedule one delayed send
        if (!timer) {
            timer = setTimeout(() => {
                if (pendingValue !== null) {
                    fetch('/set_led?val=' + pendingValue);
                    lastSendTime = Date.now();
                }
                timer = null;
                pendingValue = null;
            }, SEND_INTERVAL);
        }
    }

    saveSettings();
}


/* ==========================
   Rotate Video Canvas
   ========================== */
let rotation = parseInt(localStorage.getItem('camRotation')) || 0;
const originalWidth = 640;
const originalHeight = 480;

// Button handler
document.getElementById('rotateBtn').addEventListener('click', () => {
    rotation = (rotation + 90) % 360;

    // Swap canvas size for 90°/270° rotations
    if(rotation % 180 !== 0){
        canvas.width = originalHeight;
        canvas.height = originalWidth;
    } else {
        canvas.width = originalWidth;
        canvas.height = originalHeight;
    }

    localStorage.setItem('camRotation', rotation);
});

const originalDrawImage = ctx.drawImage.bind(ctx);
ctx.drawImage = function(image, ...args){

    ctx.save();
    ctx.translate(canvas.width / 2, canvas.height / 2);
    ctx.rotate(rotation * Math.PI / 180);

    let sx, sy, sw, sh;

    // If cropping args exist → use them (zoom mode)
    if (args.length === 8) {
        [sx, sy, sw, sh] = args;
    } else {
        // fallback (no zoom)
        sw = image.width;
        sh = image.height;
        sx = 0;
        sy = 0;
    }

    const drawWidth = canvas.width;
    const drawHeight = canvas.height;

    originalDrawImage(
        image,
        sx, sy, sw, sh,
        -drawWidth / 2,
        -drawHeight / 2,
        drawWidth,
        drawHeight
    );

    ctx.restore();
};

/* ==========================
   TOGGLE VIDEO RECORDING
   ========================== */
function toggleRecording(){
    const btn = document.getElementById("recordBtn");

    if(!isRecording){
        startRecording();
        btn.innerText = "⏹ Stop";
        btn.style.background = "red";
    } else {
        stopRecording();
        btn.innerText = "⏺ Record";
        btn.style.background = "";
    }
}

function startRecording(){
    const stream = canvas.captureStream(25); // FPS

    mediaRecorder = new MediaRecorder(stream, {
        mimeType: 'video/webm; codecs=vp9'
    });

    recordedChunks = [];

    mediaRecorder.ondataavailable = e => {
        if(e.data.size > 0){
            recordedChunks.push(e.data);
        }
    };

    mediaRecorder.onstop = () => {
        const blob = new Blob(recordedChunks, { type: 'video/webm' });
        const url = URL.createObjectURL(blob);

        let now = new Date();
        let timestamp = now.getFullYear() + '-' +
          String(now.getMonth()+1).padStart(2,'0') + '-' +
          String(now.getDate()).padStart(2,'0') + '_' +
          String(now.getHours()).padStart(2,'0') + '-' +
          String(now.getMinutes()).padStart(2,'0') + '-' +
          String(now.getSeconds()).padStart(2,'0');

        const a = document.createElement('a');
        a.href = url;
        a.download = 'ESP32CAM_VIDEO_' + timestamp + '.webm';
        document.body.appendChild(a);
        a.click();
        document.body.removeChild(a);
    };

    mediaRecorder.start();
    isRecording = true;

    console.log("🔴 Recording started");
}

function stopRecording() {
    if (mediaRecorder && isRecording) {
        mediaRecorder.stop();
        isRecording = false;
        console.log("🛑 Recording stopped");
    }
}
/* ==========================
   TOGGLE MENU
   ========================== */
let menuVisible = false;

document.getElementById('menuToggleBtn').addEventListener('click', () => {
    const controlsDiv = document.querySelector('.controls');

    if (!menuVisible) {
        controlsDiv.style.left = '10px';  // slide in
        menuVisible = true;
    } else {
        controlsDiv.style.left = '-220px'; // slide out
        menuVisible = false;
    }
});

document.addEventListener('pointerdown', (event) => {
    const controlsDiv = document.querySelector('.controls');
    const menuBtn = document.getElementById('menuToggleBtn');

    const clickedInsideMenu = controlsDiv.contains(event.target);
    const clickedButton = menuBtn.contains(event.target);

    if (!clickedInsideMenu && !clickedButton && menuVisible) {
        controlsDiv.style.left = '-220px'; // same as your close logic
        menuVisible = false;
    }
});
</script>
</body>
</html>
)rawliteral";

  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, html, strlen(html));
}
// ===== SETUP =====
void setup() {
  Serial.begin(115200);
  delay(100); // small delay to allow Serial monitor startup

  // --- Initialize Preferences ---
  prefs.begin("fpv", false);

  // Load saved values (with defaults)
  ledBrightness = prefs.getInt("led", 0);
  jpegQuality   = prefs.getInt("jpeg", 50);
  videoQuality  = prefs.getString("res", "VGA");
  zoomRatio     = prefs.getFloat("zoom", 1.0);
  serverPort    = prefs.getInt("port", 8181);
  Serial.printf("Loaded server port: %d\n", serverPort);
  Serial.println("Loaded settings:");
  Serial.println(ledBrightness);
  Serial.println(jpegQuality);
  Serial.println(videoQuality);
  Serial.println(zoomRatio);

  // --- Clear old WiFi credentials ---
  WiFi.disconnect(true, true); // forget all networks
  delay(1000);

  // --- WiFiManager setup ---
  WiFiManager wm;
  wm.setDebugOutput(true);
  wm.setConfigPortalTimeout(180);  // optional timeout

  String deviceName = "ESP32-CAM-" + String((uint32_t)ESP.getEfuseMac(), HEX);

  // --- Add custom parameter for server port ---
  char portStr[6];
  sprintf(portStr, "%d", serverPort);
  WiFiManagerParameter customPort("port", "Server Port", portStr, 5);
  wm.addParameter(&customPort);

// --- Uncomment either - Always restart AP selection  
//   bool res = wm.startConfigPortal("ESP32-CAM-Setup");

// --- Or Uncomment - AutoConnect will start AP if no saved WiFi, else connect
 bool res = wm.autoConnect(deviceName.c_str());

  if(!res){
    Serial.println("❌ Failed to connect");
    ESP.restart();
  }

  // Read the port value from WiFiManager if changed
  serverPort = atoi(customPort.getValue());
  prefs.putInt("port", serverPort);
  Serial.printf("Server running on port: %d\n", serverPort);

  Serial.println("✅ WiFi connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  // Optional: prevent auto reconnect to old WiFi
  WiFi.setAutoReconnect(false);
  WiFi.persistent(true); // saves new WiFi for next boot

  // --- LED PWM setup (ESP-IDF style) ---
  ledc_timer_config_t timer_conf = {};
  timer_conf.speed_mode = LEDC_HIGH_SPEED_MODE;
  timer_conf.duty_resolution = LEDC_TIMER_8_BIT;
  timer_conf.timer_num = LEDC_TIMER_1;
  timer_conf.freq_hz = LED_FREQ;
  timer_conf.clk_cfg = LEDC_AUTO_CLK;
  ledc_timer_config(&timer_conf);

  ledc_channel_config_t ch_conf = {};
  ch_conf.gpio_num = LED_PIN;
  ch_conf.speed_mode = LEDC_HIGH_SPEED_MODE;
  ch_conf.channel = LED_CHANNEL;
  ch_conf.intr_type = LEDC_INTR_DISABLE;
  ch_conf.timer_sel = LEDC_TIMER_1;
  ch_conf.duty = 0;     // start OFF
  ch_conf.hpoint = 0;
  ledc_channel_config(&ch_conf);

  // --- Camera config ---
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0       = Y2_GPIO_NUM;
  config.pin_d1       = Y3_GPIO_NUM;
  config.pin_d2       = Y4_GPIO_NUM;
  config.pin_d3       = Y5_GPIO_NUM;
  config.pin_d4       = Y6_GPIO_NUM;
  config.pin_d5       = Y7_GPIO_NUM;
  config.pin_d6       = Y8_GPIO_NUM;
  config.pin_d7       = Y9_GPIO_NUM;
  config.pin_xclk     = XCLK_GPIO_NUM;
  config.pin_pclk     = PCLK_GPIO_NUM;
  config.pin_vsync    = VSYNC_GPIO_NUM;
  config.pin_href     = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn     = PWDN_GPIO_NUM;
  config.pin_reset    = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size   = FRAMESIZE_VGA;
  config.jpeg_quality = 20;
  config.fb_count     = 2;
  config.fb_location  = CAMERA_FB_IN_PSRAM;

  if(esp_camera_init(&config) != ESP_OK){
    Serial.println("Camera init failed");
    return;
  }

  // --- HTTP Server ---
  httpd_config_t http_config = HTTPD_DEFAULT_CONFIG();
  http_config.server_port = serverPort;  // <-- Set server port dynamically
  httpd_handle_t server = NULL;

  if(httpd_start(&server, &http_config) == ESP_OK){
    Serial.println("Server Started!");
      }

    httpd_uri_t index_uri = {
      .uri = "/",
      .method = HTTP_GET,
      .handler = index_handler,
      .user_ctx = NULL,
      .is_websocket = false,
      .handle_ws_control_frames = NULL,
      .supported_subprotocol = NULL
    };
    httpd_register_uri_handler(server, &index_uri);

    httpd_uri_t stream_uri = {
      .uri = "/stream",
      .method = HTTP_GET,
      .handler = stream_handler,
      .user_ctx = NULL,
      .is_websocket = false,
      .handle_ws_control_frames = NULL,
      .supported_subprotocol = NULL
    };
    httpd_register_uri_handler(server, &stream_uri);    

    httpd_uri_t toggle_uri = {
      .uri = "/toggle_temp",
      .method = HTTP_GET,
      .handler = toggle_temp_handler,
      .user_ctx = NULL,
      .is_websocket = false,
      .handle_ws_control_frames = NULL,
      .supported_subprotocol = NULL
    };
    httpd_register_uri_handler(server, &toggle_uri);
    
    httpd_uri_t cpu_uri = {
      .uri = "/cpu_temp",
      .method = HTTP_GET,
      .handler = cpu_temp_handler,
      .user_ctx = NULL,
      .is_websocket = false,
      .handle_ws_control_frames = NULL,
      .supported_subprotocol = NULL
    };
    httpd_register_uri_handler(server, &cpu_uri);

    httpd_uri_t quality_uri = {
      .uri = "/set_quality",
      .method = HTTP_GET,
      .handler = set_quality_handler,
      .user_ctx = NULL,
      .is_websocket = false,
      .handle_ws_control_frames = NULL,
      .supported_subprotocol = NULL
    };
    httpd_register_uri_handler(server, &quality_uri);

    httpd_uri_t jpeg_uri = {
      .uri = "/set_jpeg",
      .method = HTTP_GET,
      .handler = set_jpeg_handler,
      .user_ctx = NULL,
      .is_websocket = false,
      .handle_ws_control_frames = NULL,
      .supported_subprotocol = NULL
    };
    httpd_register_uri_handler(server, &jpeg_uri);

    httpd_uri_t zoom_uri = {
      .uri = "/set_zoom",
      .method = HTTP_GET,
      .handler = set_zoom_handler,
      .user_ctx = NULL,
      .is_websocket = false,
      .handle_ws_control_frames = NULL,
      .supported_subprotocol = NULL
    };
    httpd_register_uri_handler(server, &zoom_uri);

    httpd_uri_t led_pwm_uri = {
      .uri = "/set_led",
      .method = HTTP_GET,
      .handler = set_led_handler,
      .user_ctx = NULL,
      .is_websocket = false,
      .handle_ws_control_frames = NULL,
      .supported_subprotocol = NULL
    };
    httpd_register_uri_handler(server, &led_pwm_uri);    
   
}

void loop(){}
