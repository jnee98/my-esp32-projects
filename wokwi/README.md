# Water Level Monitor - Wokwi Simulation

This is a complete ESP32-based water level monitoring system that runs entirely in the browser via [Wokwi](https://wokwi.com).

## Features

- **ESP32 Web Server** - Serves web UI and REST API over WiFi AP
- **HC-SR04 Ultrasonic Simulation** - Multiple modes: Random, Random Walk, Sine Wave, Step, Ramp
- **SSD1306 OLED Display** - Real-time terminal-style display
- **Median Filter** - 5-sample median filter for noise reduction
- **Auto-calibration** - Set datum reference for depth calculation
- **History & Statistics** - Up to 10,000 samples with mean, stddev, min/max
- **Linear Regression** - Real-time trend analysis with R²
- **WebSocket** - Live sensor updates to web dashboard
- **CSV Export** - Download history for external analysis

## Quick Start

### Option 1: Run on Wokwi (Recommended)
1. Go to https://wokwi.com/projects/new/esp32
2. Copy the contents of `sketch.ino` into the editor
3. Copy `diagram.json` into the diagram editor (click the diagram tab)
4. Copy `wokwi.toml` into the project config
5. Click "Start Simulation"

### Option 2: Local Arduino IDE
1. Install ESP32 board support in Arduino IDE
2. Install libraries:
   - Adafruit SSD1306
   - Adafruit GFX
   - ArduinoJson (v6+)
   - WebSockets (by Markus Sattler)
3. Open `sketch.ino` and upload to ESP32

## Wiring

| Component | ESP32 Pin |
|-----------|-----------|
| HC-SR04 TRIG | GPIO 5 |
| HC-SR04 ECHO | GPIO 18 (**requires voltage divider: 1kΩ + 2kΩ for 5V→3.3V**) |
| HC-SR04 VCC | 5V (or 3.3V) |
| HC-SR04 GND | GND |
| SSD1306 SDA | GPIO 21 |
| SSD1306 SCL | GPIO 22 |
| SSD1306 VCC | 3.3V |
| SSD1306 GND | GND |

> ⚠️ **Important Hardware Notes:**
> - **Voltage Divider Required**: The HC-SR04 ECHO pin outputs 5V when powered from 5V. ESP32 GPIOs are 3.3V tolerant. Use a 1kΩ/2kΩ voltage divider on ECHO → GPIO 18.
> - **Real Water Tank Sensor**: The HC-SR04 is NOT waterproof and will fail in humid/condensing environments. For real tanks, use the **JSN-SR04T** (waterproof ultrasonic) which has a ~2-3cm dead zone.
> - **55ft Tank**: HC-SR04 max range is ~400cm (13ft). For 55ft (1676cm), the sensor must be mounted at the top measuring down, but 55ft exceeds its range. The sketch simulates scaled data.

## API Endpoints

| Method | Path | Description |
|--------|------|-------------|
| GET | `/` | Web dashboard |
| GET | `/sensor` | Current sensor reading |
| GET | `/history?limit=N&offset=M` | Paginated history |
| GET | `/history.csv` | Download CSV |
| GET | `/regression` | Linear regression |
| GET | `/stats` | Statistics summary |
| POST | `/calibrate` | Set datum: `{"datum": 150.5}` |
| GET | `/config` | Current configuration |
| GET | `/status` | System status |
| GET | `/endpoints` | List all endpoints |
| **POST** | `/realtime` | Load real data: `{"data":[100,200,300],"interval_ms":1000}` |
| **GET** | `/realtime` | Get playback status |
| **POST** | `/realtime/stop` | Stop real data playback |
| WS | `:81/` | WebSocket for live updates |

## WebSocket Commands

```json
// Calibrate
{"cmd": "calibrate", "datum": 150.5}

// Change simulator mode
{"cmd": "set_mode", "mode": 1, "min": 20, "max": 400, "step": 5.0}

// Load real data (air gap in cm, 20-400 range)
{"cmd": "load_real_data", "data": [100,150,200,250,300,350,300,250,200,150,100], "interval_ms": 1000}

// Stop real data playback
{"cmd": "stop_real_data"}

// Ping
{"cmd": "ping"}
```

### Simulator Modes

| Mode | Value | Description |
|------|-------|-------------|
| RANDOM | 0 | Random value within range |
| RANDOM_WALK | 1 | Random walk with boundaries (default) |
| SINE | 2 | Sine wave oscillation |
| STEP | 3 | Step function between min/max |
| RAMP | 4 | Triangle wave ramp |
| **REAL_DATA** | **5** | **Playback of loaded real data** |

## Access Points (Real Hardware)

- **WiFi AP**: `WaterLevelMonitor` / `waterlevel123`
- **Web UI**: http://192.168.4.1/ (or http://waterlevel.local/ with mDNS)
- **WebSocket**: ws://192.168.4.1:81/

> ⚠️ **Wokwi Limitation**: Wokwi simulates WiFi in **station mode only** (ESP32 connects to a virtual router). **AP mode does not work** for browser access in Wokwi. The web UI, WebSocket, and mDNS will NOT be accessible from your browser in the simulator.
> 
> **In Wokwi**, you'll see all data on:
> - **Serial Monitor** (115200 baud) - live readings, stats, regression
> - **OLED Display** - terminal-style output
> - **Virtual HC-SR04** - distance slider in diagram
> 
> **On real hardware**, the AP mode works normally for phone/laptop connection.

## Files

- `sketch.ino` - Main Arduino sketch (595 lines)
- `wokwi.toml` - Wokwi project configuration
- `diagram.json` - Wiring diagram for Wokwi editor

## Architecture

The sketch implements a complete IoT water level monitor:
1. **Sensor Layer**: Simulated HC-SR04 with median filtering
2. **Data Layer**: Circular buffer history with statistics
3. **Analysis Layer**: Linear regression for trend detection
4. **Network Layer**: WiFi AP + HTTP + WebSocket
5. **Presentation Layer**: Embedded web UI with Chart.js-ready data
6. **Display Layer**: SSD1306 terminal-style output

## Performance

- Sensor reading: 500ms interval
- Display update: 1s interval  
- WebSocket broadcast: 1s interval
- Statistics logging: 5s interval
- Memory: ~10KB history buffer + JSON buffers
- Free heap: ~200KB+ on ESP32

## License

MIT License - Feel free to use and modify!