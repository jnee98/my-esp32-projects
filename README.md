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

---

## 📋 Serial Commands (Both Versions)

| Command | Description |
|---------|-------------|
| `h` / `help` | Show help |
| `c` / `calibrate` | Toggle calibration mode |
| `d <cm>` | Set datum distance (e.g., `d 150.5`) |
| `r` / `reset` | Clear calibration |
| `s` / `single` | Take single reading |

---

## 🎯 Calibration (Both Versions)

1. Power on and open serial monitor
2. Type `c` to enter calibration mode
3. Measure distance from sensor face to tank bottom
4. Type `d <your_measurement_in_cm>` (e.g., `d 150.5`)
5. Type `c` to exit calibration mode

Display will now show **Water Depth** instead of **UNCALIBRATED**.

---

## ✨ Features

- **Median filtering** (5 samples) for noise rejection
- **Precise pulse measurement**: RMT hardware (ESP-IDF) / `pulseIn()` (Arduino)
- **SSD1306 OLED** driver with custom 5x7 font
- **Serial CLI** for calibration and monitoring
- **Auto error detection** with display alerts

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
│   ├── main.c        # App entry point + CLI
│   ├── sensor.h/.c   # HC-SR04 driver (RMT)
│   ├── display.h/.c  # SSD1306 driver (I2C)
│   └── config.h      # Pin definitions & constants
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