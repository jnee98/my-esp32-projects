# ESP32-C6 Water Level Monitor

**Two versions included:**
- **`main/`** - ESP-IDF project for **real ESP32-C6 hardware** (native, no Arduino)
- **`wokwi/`** - Arduino project for **Wokwi simulation** (ESP32 classic)

---

## ⚡ Quick Start: Wokwi Simulation (No Hardware Needed)

> **Wokwi only supports Arduino framework on ESP32 (classic), ESP32-S3, ESP32-C3.**
> The ESP-IDF project in `main/` **cannot run on Wokwi**. Use the `wokwi/` folder instead.

### Run on Wokwi (Browser)

1. **Open** https://wokwi.com/projects/new/esp32
2. **Load Diagram**: Top-left menu ☰ → "Load Diagram" → Copy-paste contents of `wokwi/diagram.json`
3. **Load Code**: Copy `wokwi/sketch.ino` into the code editor (replace default sketch)
4. **Click** ▶️ "Start Simulation"
5. **Open Serial Monitor** (bottom panel) → Type `h` for help

### Wokwi Wiring (auto-loaded from diagram.json)

| Component | Pin | ESP32 GPIO |
|-----------|-----|------------|
| HC-SR04 TRIG | → | GPIO 4 |
| HC-SR04 ECHO | → | GPIO 5 |
| SSD1306 SDA | → | GPIO 21 |
| SSD1306 SCL | → | GPIO 22 |
| SSD1306 VCC | → | 3.3V |
| SSD1306 GND | → | GND |
| HC-SR04 VCC | → | 5V |
| HC-SR04 GND | → | GND |

---

## 🔧 Real Hardware: ESP32-C6 (ESP-IDF)

### Hardware Required
- **ESP32-C6 DevKitC-1**
- **HC-SR04** ultrasonic sensor (5V - use voltage divider on ECHO pin!)
- **SSD1306** 128x64 I2C OLED display

### Pin Connections (ESP32-C6)

| Component | Pin | ESP32-C6 GPIO |
|-----------|-----|---------------|
| HC-SR04 TRIG | → | GPIO 4 |
| HC-SR04 ECHO | → | GPIO 5 (via voltage divider) |
| SSD1306 SDA | → | GPIO 8 |
| SSD1306 SCL | → | GPIO 9 |
| SSD1306 VCC | → | 3.3V |
| SSD1306 GND | → | GND |
| HC-SR04 VCC | → | 5V |
| HC-SR04 GND | → | GND |

⚠️ **Important**: HC-SR04 ECHO outputs 5V. Use a voltage divider (1kΩ + 2kΩ) to drop to 3.3V for GPIO 5.

### Build & Flash (Linux/macOS)

```bash
# Setup ESP-IDF environment (once)
source ~/esp/esp-idf/export.sh

# Build
idf.py build

# Flash (adjust port)
idf.py -p /dev/ttyUSB0 flash

# Monitor
idf.py -p /dev/ttyUSB0 monitor

# All in one
idf.py -p /dev/ttyUSB0 flash monitor
```

### Build & Flash (Windows)

**Option 1: ESP-IDF Command Prompt (Recommended)**

1. Install ESP-IDF using the [official installer](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/get-started/windows-setup.html) (includes Python, Git, toolchain)
2. Open **"ESP-IDF 5.x Command Prompt"** from Start Menu
3. Navigate to project:
   ```cmd
   cd path\to\my-esp32-projects\main
   ```
4. Build and flash:
   ```cmd
   idf.py build
   idf.py -p COM3 flash
   idf.py -p COM3 monitor
   ```

**Option 2: PowerShell / Command Prompt**

If you installed ESP-IDF manually:
```powershell
# Setup environment (run once per session)
. $env:IDF_PATH\export.ps1
# or for cmd.exe:
%IDF_PATH%\export.bat

# Then same commands:
idf.py build
idf.py -p COM3 flash monitor
```

**Option 3: VS Code + ESP-IDF Extension**

1. Install [VS Code](https://code.visualstudio.com/)
2. Install [Espressif IDF extension](https://marketplace.visualstudio.com/items?itemName=espressif.esp-idf-extension)
3. Configure extension with your ESP-IDF path
4. Open `main/` folder → Click "Build" → "Flash" → "Monitor" in status bar

---

### Finding Your COM Port (Windows)

```cmd
# List all COM ports
mode

# Or in PowerShell
Get-CimInstance Win32_SerialPort | Select-Object Name, Description

# Or use Device Manager → Ports (COM & LPT)
# Look for: "USB Serial Device (COM3)" or "Silicon Labs CP210x (COM3)"
```

**Typical ports:**
- ESP32-C6 DevKitC-1: `COM3` - `COM10`
- Replace `COM3` in commands above with your actual port

---

## 📋 Serial Commands (ESP-IDF Version)

| Command | Description |
|---------|-------------|
| `h` / `help` | Show help |
| `c` / `calibrate` | Toggle calibration mode |
| `d <cm>` | Set datum distance (e.g., `d 150.5`) |
| `r` / `reset` | Clear calibration |
| `s` / `single` | Take single reading |
| `m <mode>` | Set operation mode (0-5) |
| `load_real_data` | Enable real data playback mode |
| `stop_real_data` | Stop real data, return to live sensor |
| `real_data <idx> <val>` | Set real data point (e.g., `real_data 0 150.5`) |
| `dump_real_data` | Dump all real data buffer points |

### Operation Modes (`m <mode>`)
| Mode | Value | Description |
|------|-------|-------------|
| **MODE_LIVE_SENSOR** | 0 | Live HC-SR04 readings (default) |
| **MODE_SINE_WAVE** | 1 | Sine wave simulation (0-500cm) |
| **MODE_SQUARE_WAVE** | 2 | Square wave simulation |
| **MODE_TRIANGLE_WAVE** | 3 | Triangle wave simulation |
| **MODE_SAWTOOTH_WAVE** | 4 | Sawtooth wave simulation |
| **MODE_REAL_DATA** | 5 | Playback from real data buffer |

---

## 🌐 Network Features (ESP-IDF Version Only)

### WiFi
- **SSID**: `Wokwi-GUEST` (open network for Wokwi simulation)
- **Mode**: Station (STA)
- **Auto-reconnect**: Up to 10 retries on disconnect

### HTTP REST API (Port 80)

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/realtime` | GET | Get current sensor reading (JSON) |
| `/realtime` | POST | Load real data points (JSON array `[v1,v2,...]`) |
| `/realtime/stop` | POST | Stop real data playback, return to live sensor |
| `/tank/height` | GET | Get tank dimensions (ft & cm) |
| `/realtime/buffer` | GET | Dump entire real data buffer |

#### GET `/realtime` Response
```json
{
  "air_gap_cm": 123.4,
  "water_depth_cm": 1553.0,
  "valid": true,
  "calibrated": true,
  "mode": 0,
  "tank_height_cm": 1676.4,
  "timestamp": 1234567890
}
```

#### POST `/realtime` Request
```json
[150.0, 151.2, 149.8, 150.5, 152.1]
```
- Loads up to 1000 data points
- Automatically switches to **MODE_REAL_DATA**

### WebSocket Server

| URI | Protocol | Description |
|-----|----------|-------------|
| `/ws` | WebSocket | Real-time sensor data stream (100ms interval) |

#### WebSocket Messages (Server → Client)
```json
{
  "air_gap_cm": 123.4,
  "water_depth_cm": 1553.0,
  "valid": true,
  "calibrated": true,
  "mode": 5,
  "real_data_index": 42,
  "timestamp": 1234567890
}
```

#### WebSocket Commands (Client → Server)
| Command | Description |
|---------|-------------|
| `load_real_data` | Start real data playback |
| `stop_real_data` | Stop real data, return to live sensor |

---

## 🎯 Calibration (Both Versions)

1. Power on and open serial monitor
2. Type `c` to enter calibration mode
3. Measure distance from sensor face to tank bottom
4. Type `d <your_measurement_in_cm>` (e.g., `d 150.5`)
5. Type `c` to exit calibration mode

Display will now show **Water Depth** instead of **UNCALIBRATED**.

---

## 📦 Real Data Simulation (ESP-IDF Version)

The ESP-IDF version supports a **1000-point real data buffer** for testing without physical water:

### Loading Real Data via HTTP
```bash
# Load array of distances (air gap in cm)
curl -X POST http://<ESP_IP>/realtime   -H "Content-Type: application/json"   -d '[150.0, 149.5, 148.0, 147.2, 146.8]'
```

### Loading Real Data via WebSocket
```javascript
ws.send('load_real_data');
```

### Loading Real Data via Serial
```
real_data 0 150.0
real_data 1 149.5
...
load_real_data
```

### Stop Real Data
```
stop_real_data
# or via WebSocket:
ws.send('stop_real_data');
```

---

## 📊 Tank Configuration

| Parameter | Value |
|-----------|-------|
| **Tank Height** | 55 ft (1676.4 cm) |
| **Sensor Max Range** | ~400 cm (HC-SR04 limit) |
| **Datum Range** | 0 - 1676.4 cm |
| **Real Data Buffer** | 1000 points |

---

## ✨ Features

- **55ft (1676.4cm) tank** support
- **Median filtering** (5 samples) for noise rejection
- **Precise pulse measurement**: RMT hardware (ESP-IDF) / `pulseIn()` (Arduino)
- **SSD1306 OLED** driver with custom 5x7 font
- **Serial CLI** for calibration and monitoring
- **Auto error detection** with display alerts
- **6 Operation Modes**: Live, Sine, Square, Triangle, Sawtooth, Real Data
- **1000-point real data buffer** for simulated water level playback
- **WiFi Station** mode (Wokwi-GUEST)
- **HTTP REST API** with 5 endpoints
- **WebSocket server** for real-time streaming (100ms updates)
- **Waveform simulation** for testing without water

---

## 📁 Project Structure

```
water-level-monitor-esp32c6/
├── CMakeLists.txt
├── sdkconfig.defaults
├── README.md
├── .gitignore
├── main/                          # ESP-IDF (ESP32-C6 real hardware)
│   ├── CMakeLists.txt
│   ├── main.c        # App entry point + CLI + WiFi + WebServer + WebSocket
│   ├── sensor.h/.c   # HC-SR04 driver (RMT) + simulation modes + real data buffer
│   ├── display.h/.c  # SSD1306 driver (I2C)
│   └── config.h      # Pin definitions, tank config, WiFi, operation modes
└── wokwi/                          # Arduino (Wokwi simulation)
    ├── diagram.json  # Wokwi wiring diagram
    └── sketch.ino    # Arduino code
```

---

## 🔑 Key Differences

| Feature | ESP-IDF (`main/`) | Wokwi Arduino (`wokwi/`) |
|---------|-------------------|--------------------------|
| **Target** | ESP32-C6 real hardware | ESP32 classic (simulated) |
| **Framework** | ESP-IDF (native C) | Arduino (C++) |
| **Pins** | GPIO 4,5,8,9 | GPIO 4,5,21,22 |
| **Sensor** | RMT peripheral (1µs res) | `pulseIn()` |
| **Display** | Custom I2C driver | Adafruit_SSD1306 lib |
| **Voltage** | 3.3V logic (needs divider) | 5V logic (simulated) |
| **Build** | `idf.py build` | Browser (Wokwi) |
| **WiFi** | ✅ Station mode | ❌ |
| **HTTP Server** | ✅ Port 80 | ❌ |
| **WebSocket** | ✅ /ws | ❌ |
| **Simulation Modes** | ✅ 6 modes | ❌ |
| **Real Data Buffer** | ✅ 1000 pts | ❌ |

---

## Requirements

### ESP-IDF Version
- ESP-IDF v5.3+ with ESP32-C6 support
- Python 3.8+

### Wokwi Version
- Web browser
- No installation needed

---

## License

MIT
