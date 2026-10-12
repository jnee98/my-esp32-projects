# ESP32-C6 Water Level Monitor

ESP-IDF project for monitoring water level using HC-SR04 ultrasonic sensor and SSD1306 OLED display.

## Hardware
- **ESP32-C6 DevKitC-1**
- **HC-SR04** ultrasonic sensor (5V - use voltage divider on ECHO pin!)
- **SSD1306** 128x64 I2C OLED display

## Pin Connections

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

## Build & Flash

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

## Serial Commands

| Command | Description |
|---------|-------------|
| `h` / `help` | Show help |
| `c` / `calibrate` | Toggle calibration mode |
| `d <cm>` | Set datum distance (e.g., `d 150.5`) |
| `r` / `reset` | Clear calibration |
| `s` / `single` | Take single reading |

## Calibration

1. Power on and open serial monitor
2. Type `c` to enter calibration mode
3. Measure distance from sensor face to tank bottom
4. Type `d <your_measurement_in_cm>` (e.g., `d 150.5`)
5. Type `c` to exit calibration mode

Display will now show **Water Depth** instead of **UNCALIBRATED**.

## Features

- **Median filtering** (5 samples) for noise rejection
- **RMT-based** precise pulse width measurement
- **I2C SSD1306** driver with custom 5x7 font
- **Serial CLI** for calibration and monitoring
- **Auto error detection** with display alerts

## Project Structure

```
water-level-monitor-esp32c6/
├── CMakeLists.txt
├── sdkconfig.defaults
├── main/
│   ├── CMakeLists.txt
│   ├── main.c        # App entry point + CLI
│   ├── sensor.h/.c   # HC-SR04 driver (RMT)
│   ├── display.h/.c  # SSD1306 driver (I2C)
│   └── config.h      # Pin definitions & constants
```

## Requirements

- ESP-IDF v5.3+ with ESP32-C6 support
- Python 3.8+

## License

MIT