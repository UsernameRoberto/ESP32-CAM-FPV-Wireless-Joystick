# 🚗 ESP32-CAM FPV + Wireless Arduino Joystick

This repository contains all the code and instructions for building a **wireless FPV camera system** using an **ESP32-CAM**, and controlling motors remotely via a **wireless Arduino joystick with HC-12** modules.  

The project is designed for clean, low-noise operation, perfect for **Arduino Mega + L293 motor control**, and allows future upgrades with improved motor drivers.

---

## 📌 Features

### ESP32-CAM FPV

- Real-time MJPEG streaming via HTTP
- Overlay telemetry:
  - CPU temperature
  - WiFi signal strength
  - FPS counter
  - Video resolution / JPEG quality
  - LED brightness
  - Zoom ratio
  - Time and recording status
- LED PWM control via browser slider
- Zoom, rotate, snapshot, and recording directly from web UI
- Fullscreen mode support
- Settings persistence (localStorage + ESP32 Preferences)

### Wireless Arduino Joystick + HC-12

- Joystick controls sent wirelessly to Arduino Mega
- Button and axis support for motor control
- No interference from nearby power cables
- Compatible with L293 motor driver (future upgrade to modern drivers)

---

## 🛠️ Hardware Setup

### 1️⃣ ESP32-CAM FPV

      +------------------+
      |     ESP32-CAM    |
      |------------------|
      | GPIO 4  -> LED   |
      | Camera Pins      |
      | WiFi -> Browser  |
      +--------+---------+
               |
           HTTP MJPEG
               |
       +-------v--------+
       | Web Browser UI |
       | (Video + OSD)  |
       +----------------+
	   

- LED connected to GPIO 4 (PWM)
- Camera pins follow **AI Thinker ESP32-CAM module**
- Access the live stream at `http://<esp32-ip>:8181/stream`
- Control endpoints:
  - `/toggle_temp` → toggle CPU/WiFi overlay
  - `/cpu_temp` → get current CPU temperature & WiFi %
  - `/set_led?val=<0-100>` → adjust LED brightness
  - `/set_quality?res=VGA|QVGA` → video resolution
  - `/set_jpeg?quality=<10-63>` → JPEG compression quality
  - `/set_zoom?zoom=<0.5-2.0>` → zoom ratio

---

### 2️⃣ Wireless Arduino Joystick + HC-12

      +-------------+           +-------------+
      |  Arduino    |           |  Arduino    |
      |   Uno/ Mega |           |   Mega      |
      |-------------|           |-------------|
      | Joystick    |           | Motor Ctrl  |
      | X/Y/A/B     |           | L293 / drv  |
      +------|------+           +------|------+
             |                          |
         HC-12 Tx/Rx <--------------> HC-12 Tx/Rx
         (wireless, no cable noise)
		 
		 
		 

- Joystick sends commands wirelessly to Mega
- Mega drives motors via L293
- No RX/TX cable interference even near power lines
- Future plan: upgrade to modern motor drivers for smoother PWM control

---

### 3️⃣ Motor Control Notes

- Current setup: **Arduino Mega + L293**
- Future upgrade: advanced drivers (e.g., TB6612FNG or DRV8833)
- Clean PWM and H-bridge control possible with new drivers
- Joystick axes → mapped to motor speed / direction

---

## 💻 Software Usage

1. Connect ESP32-CAM to WiFi (via WiFiManager or hardcoded SSID)
2. Open browser at `http://<esp32-ip>:8181`
3. Use the **control panel**:
   - **🎥 CPU Temp** → toggle overlay
   - **⚡ Video Quality** → toggle VGA/QVGA
   - **🎞️ JPEG Quality** → cycle through quality presets
   - **🔍 Zoom + / -** → zoom in/out
   - **📸 Snapshot** → save JPEG
   - **⏺ Record** → start/stop video recording
   - **💡 LED Slider** → adjust LED brightness
   - **🔄 Rotate Video** → rotate canvas
4. Wireless joystick:
   - Move joystick → motors respond
   - Press buttons → trigger additional motor commands

---

## ⚡ Browser Controls (ESP32-CAM)

| Control | Endpoint | Notes |
|---------|----------|------|
| Toggle overlay | `/toggle_temp` | ON/OFF CPU & WiFi info |
| Get CPU/WiFi | `/cpu_temp` | Returns `temp°C\nwifi%` |
| Set LED | `/set_led?val=0-100` | PWM brightness |
| Set Video Quality | `/set_quality?res=VGA|QVGA` | Change resolution |
| Set JPEG Quality | `/set_jpeg?quality=10-63` | Adjust compression |
| Set Zoom | `/set_zoom?zoom=0.5-2.0` | Zoom in/out |

---

## 🔧 Future Improvements

- Replace L293 with modern motor drivers for smoother PWM
- Expand joystick with additional axes and buttons
- Integrate accelerometer or gyroscope for advanced control
- Add logging or telemetry to browser UI
- Support for multiple ESP32-CAM units on the same network

---

## 📦 Repository Structure

/ESP32-CAM-FPV
|-- /Arduino-Joystick
|-- /ESP32-CAM
|-- README.md
|-- LICENSE

---

## ⚠️ Notes

- Keep HC-12 modules properly powered (3.3–5V)
- Avoid long wires for analog joystick near motors to reduce noise
- Use **separate power supply for motors** if necessary
- All settings for FPV camera are saved in **Preferences + browser localStorage**

---

## 🔗 References

- [ESP32-CAM Camera Web Server](https://randomnerdtutorials.com/esp32-cam-video-streaming-web-server-camera-home-assistant/)  
- [HC-12 Wireless Serial Module](https://www.elecrow.com/wiki/index.php?title=HC-12)  
- [Arduino L293 Motor Driver](https://www.arduino.cc/en/Tutorial/LibraryExamples/L293D)  

---

+----------------------------------------+
| 🎥 CPU Temp      💡 LED %      🔍 Zoom |
|                                        |
|             [Camera Stream]            |
|                                        |
| ⚡ Video Q   🎯 FPS   🎞️ JPEG Q  ⏱️ Time |
+----------------------------------------+

------------------------- ⚠️ IMPORTANT!!! -------------------------

That way you simply plug the **HU-M16 joystick shield** onto the Arduino and hook the **HC-12** directly onto the joystick.  
The **SET pin on HC-12 is not used**.  
Plug the HC-12 into the **“Bluetooth” marked place** on the shield (upper right corner of the joystick shield).  
**Arduino + Joystick + HC-12 (Uno / Joystick Shield) - NO WIRES NEEDED!** 😎


Pin Configuration

// ================== PINS ==================
#define RX_PIN 13   // HC-12 RX 
#define TX_PIN 12   // HC-13 TX

Software Settings

// ================== SETTINGS ==================
#define BAUD 9600          // Serial baud rate
#define DEADZONE 100       // Joystick deadzone for smoother control
#define SEND_INTERVAL 50   // 50 ms = 20Hz update rate

------------------------- ⚙️ SETTINGS ⚙️ -------------------------

These settings are **crucial** for smooth joystick control:

```cpp
#define DEADZONE 100       // Joystick deadzone for smoother control
#define SEND_INTERVAL 50   // 50 ms = 20Hz update rate



------------------------- ⚙️ SETTINGS ⚙️ -------------------------


Do not change these unless you know what you’re doing! 😎

## ⚡ Author

**Roberto** – IoT, FPV, and robotics projects  

		 
