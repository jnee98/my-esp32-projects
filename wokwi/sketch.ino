/*
 * Water Level Monitor - ESP32 + HC-SR04
 * Wokwi Simulation Project
 * 
 * Hardware:
 * - ESP32 DevKit V1
 * - HC-SR04 Ultrasonic Sensor (simulation only - NOT for real water tanks!)
 * - SSD1306 OLED Display (128x64, I2C)
 * 
 * Connections:
 * HC-SR04:
 *   VCC -> 5V (or 3.3V)
 *   GND -> GND
 *   TRIG -> GPIO 5
 *   ECHO -> GPIO 18 (MUST use voltage divider: 1kΩ + 2kΩ for 5V→3.3V)
 * 
 * SSD1306:
 *   VCC -> 3.3V
 *   GND -> GND
 *   SDA -> GPIO 21
 *   SCL -> GPIO 22
 * 
 * ⚠️ REAL HARDWARE NOTES:
 * - HC-SR04 is NOT waterproof. For real water tanks, use JSN-SR04T (waterproof, ~2-3cm dead zone)
 * - HC-SR04 max range ~400cm (13ft). 55ft tank exceeds range - use scaled simulation data
 * - WiFi AP mode works on real hardware; Wokwi only supports station mode (no browser AP access)
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>

// ==================== CONFIGURATION ====================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

#define TRIG_PIN 5
#define ECHO_PIN 18

#define MEDIAN_FILTER_SIZE 5
#define MIN_VALID_SAMPLES 3
#define VALID_RANGE_MIN 2.0
#define VALID_RANGE_MAX 400.0

#define SIMULATOR_MODE_RANDOM 0
#define SIMULATOR_MODE_RANDOM_WALK 1
#define SIMULATOR_MODE_SINE 2
#define SIMULATOR_MODE_STEP 3
#define SIMULATOR_MODE_RAMP 4
#define SIMULATOR_MODE_REAL_DATA 5  // New: feed real data via WebSocket/API

// Tank Configuration
#define TANK_HEIGHT_FT 55.0
#define TANK_HEIGHT_CM (TANK_HEIGHT_FT * 30.48)  // 1676.4 cm

// WiFi AP Configuration (for standalone operation)
const char* AP_SSID = "WaterLevelMonitor";
const char* AP_PASSWORD = "waterlevel123";

// Web Server
WebServer server(80);
WebSocketsServer webSocket(81);

// OLED Display
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ==================== GLOBAL STATE ====================
struct Reading {
  float timestamp;
  float raw_distance;
  float median_distance;
  float air_gap;
  float depth;
  bool calibrated;
  bool valid;
};

struct Calibration {
  float datum_cm = 150.0;
  bool is_calibrated = false;
};

struct HistoryEntry {
  Reading reading;
  unsigned long micros_timestamp;
};

// Result structs (must be before functions that use them)
struct RegressionResult {
  String model = "linear";
  String equation = "N/A";
  float intercept = 0;
  float slope_per_sec = 0;
  float slope_per_hour = 0;
  float r_squared = 0;
  int sample_count = 0;
  float time_start = 0;
  float time_end = 0;
  String interpretation = "Insufficient data";
};

struct StatsResult {
  int total_samples = 0;
  int valid_samples = 0;
  int calibrated_samples = 0;
  float time_start = 0;
  float time_end = 0;
  float duration_sec = 0;
  struct { int count; float mean; float stddev; float min; float max; float range; } air_gap_stats;
  struct { int count; float mean; float stddev; float min; float max; float range; } depth_stats;
  struct { float datum_cm; bool is_calibrated; } calibration;
};

// Simulator state
int simulator_mode = SIMULATOR_MODE_RANDOM_WALK;
float sim_min_distance = 20.0;      // Min air gap (tank nearly full)
float sim_max_distance = 400.0;     // Max air gap (tank empty, sensor limit)
float sim_step_size = 5.0;
float sim_position = 200.0;
float sim_direction = 1.0;
unsigned long sim_last_time = 0;

// Real data simulation
#define MAX_REAL_DATA 1000
float real_data_buffer[MAX_REAL_DATA];
int real_data_count = 0;
int real_data_index = 0;
bool real_data_playback = false;
unsigned long real_data_last_time = 0;
float real_data_interval_ms = 1000;  // 1 second between data points

// Median filter
float distance_buffer[MEDIAN_FILTER_SIZE];
int buffer_index = 0;
int buffer_count = 0;

// Calibration
Calibration calibration;

// History
#define MAX_HISTORY 500
HistoryEntry history[MAX_HISTORY];
int history_count = 0;
int history_head = 0;
bool history_enabled = true;

// Statistics
unsigned long total_samples = 0;
unsigned long valid_samples = 0;
unsigned long calibrated_samples = 0;
unsigned long start_time = 0;

// WebSocket clients
#define MAX_WS_CLIENTS 5
bool ws_clients[MAX_WS_CLIENTS] = {false};

// Timing
unsigned long last_sensor_read = 0;
unsigned long last_display_update = 0;
unsigned long last_ws_broadcast = 0;
unsigned long last_stats_update = 0;

const unsigned long SENSOR_INTERVAL = 500;      // 500ms
const unsigned long DISPLAY_INTERVAL = 1000;    // 1s
const unsigned long WS_BROADCAST_INTERVAL = 1000; // 1s
const unsigned long STATS_INTERVAL = 5000;      // 5s
// ==================== MEDIAN FILTER ====================
float getMedian(float* buffer, int count) {
  if (count == 0) return 0;
  float sorted[MEDIAN_FILTER_SIZE];
  for (int i = 0; i < count; i++) sorted[i] = buffer[i];
  for (int i = 0; i < count - 1; i++)
    for (int j = 0; j < count - i - 1; j++)
      if (sorted[j] > sorted[j + 1]) { float t = sorted[j]; sorted[j] = sorted[j + 1]; sorted[j + 1] = t; }
  return sorted[count / 2];
}

void addToBuffer(float value) {
  distance_buffer[buffer_index] = value;
  buffer_index = (buffer_index + 1) % MEDIAN_FILTER_SIZE;
  if (buffer_count < MEDIAN_FILTER_SIZE) buffer_count++;
}

// ==================== SIMULATOR ====================
float simulateDistance() {
  unsigned long now = millis();
  float dt = (now - sim_last_time) / 1000.0;
  sim_last_time = now;
  switch (simulator_mode) {
    case SIMULATOR_MODE_RANDOM:
      sim_position = sim_min_distance + random(0, 10000) / 10000.0 * (sim_max_distance - sim_min_distance);
      break;
    case SIMULATOR_MODE_RANDOM_WALK: {
      float change = (random(-1000, 1001) / 1000.0) * sim_step_size;
      sim_position += change;
      if (sim_position < sim_min_distance) sim_position = sim_min_distance + (sim_min_distance - sim_position);
      else if (sim_position > sim_max_distance) sim_position = sim_max_distance - (sim_position - sim_max_distance);
      break;
    }
    case SIMULATOR_MODE_SINE: {
      float period = 30.0, amplitude = (sim_max_distance - sim_min_distance) / 2.0, center = (sim_max_distance + sim_min_distance) / 2.0;
      sim_position = center + amplitude * sin(2 * PI * (now / 1000.0) / period);
      break;
    }
    case SIMULATOR_MODE_STEP: {
      static unsigned long step_change_time = 0;
      static float target = sim_min_distance;
      if (now - step_change_time > 10000) { step_change_time = now; target = (target == sim_min_distance) ? sim_max_distance : sim_min_distance; }
      if (sim_position < target) sim_position += sim_step_size * dt;
      else if (sim_position > target) sim_position -= sim_step_size * dt;
      break;
    }
    case SIMULATOR_MODE_RAMP: {
      sim_position += sim_step_size * sim_direction * dt;
      if (sim_position >= sim_max_distance) { sim_position = sim_max_distance; sim_direction = -1; }
      else if (sim_position <= sim_min_distance) { sim_position = sim_min_distance; sim_direction = 1; }
      break;
    }
    case SIMULATOR_MODE_REAL_DATA: {
      if (real_data_playback && real_data_count > 0) {
        unsigned long now = millis();
        if (now - real_data_last_time >= real_data_interval_ms) {
          real_data_last_time = now;
          sim_position = real_data_buffer[real_data_index];
          real_data_index = (real_data_index + 1) % real_data_count;
        }
      }
      break;
    }
  }
  float noise = (random(-500, 501) / 1000.0);
  return sim_position + noise;
}

Reading takeReading() {
  Reading r; r.timestamp = millis() / 1000.0;
  float raw = simulateDistance(); r.raw_distance = raw;
  addToBuffer(raw); float median = getMedian(distance_buffer, buffer_count); r.median_distance = median;
  r.valid = (median >= VALID_RANGE_MIN && median <= VALID_RANGE_MAX && buffer_count >= MIN_VALID_SAMPLES);
  r.air_gap = r.valid ? median : -1; r.calibrated = calibration.is_calibrated;
  r.depth = (r.valid && calibration.is_calibrated) ? (calibration.datum_cm - median) : -1;
  total_samples++; if (r.valid) valid_samples++; if (r.valid && r.calibrated) calibrated_samples++;
  return r;
}

// ==================== HISTORY ====================
void addToHistory(const Reading& r) {
  if (!history_enabled) return;
  HistoryEntry entry; entry.reading = r; entry.micros_timestamp = micros();
  history[history_head] = entry; history_head = (history_head + 1) % MAX_HISTORY;
  if (history_count < MAX_HISTORY) history_count++;
}
// ==================== REGRESSION ====================
RegressionResult computeRegression() {
  RegressionResult result;
  struct DataPoint { float t; float d; };
  DataPoint points[MAX_HISTORY];
  int count = 0;
  
  for (int i = 0; i < history_count; i++) {
    int idx = (history_head - 1 - i + MAX_HISTORY) % MAX_HISTORY;
    if (history[idx].reading.valid && history[idx].reading.calibrated && history[idx].reading.depth >= 0) {
      points[count].t = history[idx].reading.timestamp;
      points[count].d = history[idx].reading.depth;
      count++;
      if (count >= MAX_HISTORY) break;
    }
  }
  
  if (count < 2) return result;
  
  result.sample_count = count;
  result.time_start = points[count - 1].t;
  result.time_end = points[0].t;
  
  float sum_t = 0, sum_d = 0, sum_tt = 0, sum_td = 0;
  for (int i = 0; i < count; i++) {
    sum_t += points[i].t;
    sum_d += points[i].d;
    sum_tt += points[i].t * points[i].t;
    sum_td += points[i].t * points[i].d;
  }
  
  float n = count;
  float denom = n * sum_tt - sum_t * sum_t;
  if (fabs(denom) < 0.0001) return result;
  
  result.slope_per_sec = (n * sum_td - sum_t * sum_d) / denom;
  result.intercept = (sum_d - result.slope_per_sec * sum_t) / n;
  result.slope_per_hour = result.slope_per_sec * 3600.0;
  
  float mean_d = sum_d / n;
  float ss_tot = 0, ss_res = 0;
  for (int i = 0; i < count; i++) {
    float pred = result.intercept + result.slope_per_sec * points[i].t;
    ss_tot += (points[i].d - mean_d) * (points[i].d - mean_d);
    ss_res += (points[i].d - pred) * (points[i].d - pred);
  }
  result.r_squared = (ss_tot > 0) ? (1.0 - ss_res / ss_tot) : 0;
  
  char eqn[64];
  sprintf(eqn, "depth = %.4f + %.6f * time", result.intercept, result.slope_per_sec);
  result.equation = eqn;
  
  if (result.slope_per_hour > 1.0) {
    result.interpretation = "Water level rising at " + String(abs(result.slope_per_hour), 1) + " cm/hour";
  } else if (result.slope_per_hour < -1.0) {
    result.interpretation = "Water level falling at " + String(abs(result.slope_per_hour), 1) + " cm/hour";
  } else {
    result.interpretation = "Water level stable";
  }
  
  return result;
}
// ==================== STATISTICS ====================
StatsResult computeStats() {
  StatsResult stats;
  stats.total_samples = total_samples;
  stats.valid_samples = valid_samples;
  stats.calibrated_samples = calibrated_samples;
  stats.calibration.datum_cm = calibration.datum_cm;
  stats.calibration.is_calibrated = calibration.is_calibrated;
  
  if (history_count == 0) return stats;
  
  stats.time_start = history[(history_head - history_count + MAX_HISTORY) % MAX_HISTORY].reading.timestamp;
  stats.time_end = history[(history_head - 1 + MAX_HISTORY) % MAX_HISTORY].reading.timestamp;
  stats.duration_sec = stats.time_end - stats.time_start;
  
  auto compute = [](float* data, int n, float& mean, float& stddev, float& min_v, float& max_v) {
    if (n == 0) { mean = stddev = min_v = max_v = 0; return; }
    min_v = max_v = data[0];
    float sum = 0;
    for (int i = 0; i < n; i++) { sum += data[i]; if (data[i] < min_v) min_v = data[i]; if (data[i] > max_v) max_v = data[i]; }
    mean = sum / n;
    float var = 0;
    for (int i = 0; i < n; i++) var += (data[i] - mean) * (data[i] - mean);
    stddev = sqrt(var / n);
  };
  
  float ag_data[MAX_HISTORY];
  float d_data[MAX_HISTORY];
  int ag_count = 0, d_count = 0;
  
  for (int i = 0; i < history_count; i++) {
    int idx = (history_head - 1 - i + MAX_HISTORY) % MAX_HISTORY;
    if (history[idx].reading.valid) {
      ag_data[ag_count++] = history[idx].reading.air_gap;
      if (history[idx].reading.calibrated && history[idx].reading.depth >= 0) {
        d_data[d_count++] = history[idx].reading.depth;
      }
    }
  }
  
  float mean, stddev, min_v, max_v;
  compute(ag_data, ag_count, mean, stddev, min_v, max_v);
  stats.air_gap_stats = {ag_count, mean, stddev, min_v, max_v, max_v - min_v};
  
  compute(d_data, d_count, mean, stddev, min_v, max_v);
  stats.depth_stats = {d_count, mean, stddev, min_v, max_v, max_v - min_v};
  
  return stats;
}
// ==================== DISPLAY ====================
void updateDisplay(const Reading& r) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.print(F("-WATER LEVEL MONITOR-"));
  display.drawFastHLine(0, 10, SCREEN_WIDTH, WHITE);
  display.setCursor(0, 12);
  display.print(F("AIR ["));
  int air_bar = constrain(map(r.air_gap, 0, 200, 0, 10), 0, 10);
  for (int i = 0; i < 10; i++) display.print(i < air_bar ? '#' : ' ');
  display.print(F("] "));
  display.print(r.air_gap >= 0 ? String(r.air_gap, 1) : "---");
  display.setCursor(0, 22);
  display.print(F("DEP ["));
  int dep_bar = constrain(map(r.depth, 0, 50, 0, 10), 0, 10);
  for (int i = 0; i < 10; i++) display.print(i < dep_bar ? '#' : ' ');
  display.print(F("] "));
  display.print(r.depth >= 0 ? String(r.depth, 1) : "---");
  if (r.calibrated) display.print(F("*"));
  display.setCursor(0, 32);
  display.print(F("* CAL "));
  display.print(calibration.datum_cm, 1);
  display.print(F("cm"));
  display.drawFastHLine(0, 42, SCREEN_WIDTH, WHITE);
  display.setCursor(0, 44);
  display.print(F("[OK]  Loop: "));
  display.print(total_samples);
  display.print(F(" | Mode:"));
  const char* modes[] = {"RANDOM", "RND_WALK", "SINE", "STEP", "RAMP", "REAL_DATA"};
  if (simulator_mode >= 0 && simulator_mode <= 5) display.print(modes[simulator_mode]);
  display.setCursor(0, 54);
  display.print(F("S:"));
  display.print(buffer_count);
  display.print(F("/"));
  display.print(MEDIAN_FILTER_SIZE);
  display.print(F(" M:"));
  display.print(r.median_distance, 1);
  display.display();
}

// ==================== WEBSOCKET ====================
void webSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      ws_clients[num] = true;
      Serial.printf("[WS] Client %u connected\n", num);
      break;
    case WStype_DISCONNECTED:
      ws_clients[num] = false;
      Serial.printf("[WS] Client %u disconnected\n", num);
      break;
    case WStype_TEXT: {
      DynamicJsonDocument doc(512);
      deserializeJson(doc, payload);
      String cmd = doc["cmd"];
      if (cmd == "ping") {
        webSocket.sendTXT(num, "{\"type\":\"pong\"}");
      } else if (cmd == "calibrate") {
        float datum = doc["datum"];
        calibration.datum_cm = datum;
        calibration.is_calibrated = true;
        String resp = "{\"type\":\"calibrated\",\"datum\":" + String(datum, 1) + "}";
        webSocket.sendTXT(num, resp);
      } else if (cmd == "set_mode") {
        simulator_mode = doc["mode"];
        sim_min_distance = doc["min"] | 20;
        sim_max_distance = doc["max"] | 400;
        sim_step_size = doc["step"] | 5.0;
      } else if (cmd == "load_real_data") {
        // Load real data array: {"cmd":"load_real_data","data":[100,150,200,250,300],"interval_ms":1000}
        JsonArray data = doc["data"];
        real_data_count = 0;
        for (JsonVariant v : data) {
          if (real_data_count < MAX_REAL_DATA) {
            real_data_buffer[real_data_count++] = v.as<float>();
          }
        }
        real_data_index = 0;
        real_data_interval_ms = doc["interval_ms"] | 1000;
        real_data_playback = true;
        simulator_mode = SIMULATOR_MODE_REAL_DATA;
        String resp = "{\"type\":\"real_data_loaded\",\"count\":" + String(real_data_count) + "}";
        webSocket.sendTXT(num, resp);
        Serial.printf("[WS] Loaded %d real data points\n", real_data_count);
      } else if (cmd == "stop_real_data") {
        real_data_playback = false;
        webSocket.sendTXT(num, "{\"type\":\"real_data_stopped\"}");
      }
      break;
    }
  }
}

void broadcastSensorUpdate(const Reading& r) {
  DynamicJsonDocument doc(512);
  doc["type"] = "sensor_update";
  doc["data"]["timestamp"] = r.timestamp;
  doc["data"]["raw_distance_cm"] = r.raw_distance;
  doc["data"]["median_distance_cm"] = r.median_distance;
  doc["data"]["air_gap_cm"] = r.air_gap;
  doc["data"]["depth_cm"] = r.depth;
  doc["data"]["calibrated"] = r.calibrated;
  doc["data"]["valid"] = r.valid;
  String json;
  serializeJson(doc, json);
  webSocket.broadcastTXT(json);
}
// ==================== HTTP HANDLERS ====================
void handleRoot() {
  server.send(200, "text/html", R"HTML(
<!DOCTYPE html><html><head><title>Water Level Monitor</title><meta name=viewport content=width=device-width,initial-scale=1><style>body{font-family:sans-serif;max-width:600px;margin:20px auto;padding:0 10px}.card{background:#f5f5f5;border-radius:8px;padding:15px;margin:10px 0}.value{font-size:2em;font-weight:bold;color:#2563eb}.label{color:#666;font-size:.9em}.badge{display:inline-block;padding:4px 8px;border-radius:4px;font-size:.8em}.valid{background:#dcfce7;color:#166534}.invalid{background:#fee2e2;color:#991b1b}.calibrated{background:#dbeafe;color:#1e40af}.uncalibrated{background:#fef3c7;color:#92400e}button{padding:10px 20px;margin:5px;border:none;border-radius:4px;cursor:pointer}.btn-primary{background:#2563eb;color:white}input,select{padding:8px;margin:5px;border:1px solid #ddd;border-radius:4px}pre{background:#1e293b;color:#e2e8f0;padding:10px;border-radius:4px;overflow-x:auto}</style></head><body><h1>Water Level Monitor</h1><div class=card><div class=label>Connection</div><div id=wsStatus class=badge invalid>Disconnected</div></div><div class=card><div class=label>Air Gap</div><div class=value id=airGap>-- cm</div></div><div class=card><div class=label>Water Depth</div><div class=value id=depth>-- cm</div></div><div class=card><div class=label>Raw Distance</div><div class=value id=rawDist>-- cm</div></div><div class=card><div class=label>Median Distance</div><div class=value id=medianDist>-- cm</div></div><div class=card><div class=label>Status</div><div id=validity><span class=badge invalid>Invalid</span></div></div><div class=card><div class=label>Calibration</div><div id=calBadge class=badge uncalibrated>Not Calibrated</div></div><hr><h3>Calibration</h3><input type=number id=datumInput placeholder="Datum (cm)" step=0.1 value=150><button class=btn-primary onclick=calibrate()>Calibrate</button><h3>Simulator Mode</h3><select id=modeSelect><option value=0>Random</option><option value=1 selected>Random Walk</option><option value=2>Sine Wave</option><option value=3>Step</option><option value=4>Ramp</option></select><input type=number id=minInput placeholder="Min (cm)" value=140 step=1><input type=number id=maxInput placeholder="Max (cm)" value=155 step=1><input type=number id=stepInput placeholder=Step value=1 step=0.1><button class=btn-primary onclick=setMode()>Apply</button><hr><h3>Debug</h3><button onclick=fetchStats()>Get Stats</button><button onclick=fetchRegression()>Get Regression</button><button onclick=fetchHistory()>Get History</button><pre id=debugOutput></pre><script>const ws=new WebSocket('ws://'+location.hostname+':81/');ws.onopen=()=>{document.getElementById('wsStatus').textContent='Connected';document.getElementById('wsStatus').className='badge valid'};ws.onclose=()=>{document.getElementById('wsStatus').textContent='Disconnected';document.getElementById('wsStatus').className='badge invalid'};ws.onmessage=(e)=>{const m=JSON.parse(e.data);if(m.type=='sensor_update'){const d=m.data;document.getElementById('airGap').textContent=(d.air_gap_cm>=0?d.air_gap_cm.toFixed(1):'--')+' cm';document.getElementById('depth').textContent=(d.depth_cm>=0?d.depth_cm.toFixed(1):'--')+' cm';document.getElementById('rawDist').textContent=d.raw_distance_cm.toFixed(1)+' cm';document.getElementById('medianDist').textContent=d.median_distance_cm.toFixed(1)+' cm';document.getElementById('validity').innerHTML='<span class=badge '+(d.valid?'valid':'invalid')+'>'+(d.valid?'Valid':'Invalid')+'</span>';document.getElementById('calBadge').textContent=d.calibrated?'Calibrated':'Not Calibrated';document.getElementById('calBadge').className='badge '+(d.calibrated?'calibrated':'uncalibrated')}};function s(c,d={}){ws.send(JSON.stringify({cmd:c,...d}))}function calibrate(){s('calibrate',{datum:parseFloat(document.getElementById('datumInput').value)})}function setMode(){s('set_mode',{mode:parseInt(document.getElementById('modeSelect').value),min:parseFloat(document.getElementById('minInput').value),max:parseFloat(document.getElementById('maxInput').value),step:parseFloat(document.getElementById('stepInput').value)})}async function api(p){const r=await fetch(p);return r.json()}function show(d){document.getElementById('debugOutput').textContent=JSON.stringify(d,null,2)}async function fetchStats(){show(await api('/stats'))}async function fetchRegression(){show(await api('/regression'))}async function fetchHistory(){show(await api('/history'))}</script></body></html>
)HTML");
}

void handleSensor() {
  Reading r = takeReading(); addToHistory(r);
  DynamicJsonDocument doc(512);
  doc["timestamp"]=r.timestamp; doc["raw_distance_cm"]=r.raw_distance; doc["median_distance_cm"]=r.median_distance;
  doc["air_gap_cm"]=r.air_gap; doc["depth_cm"]=r.depth; doc["calibrated"]=r.calibrated; doc["valid"]=r.valid;
  String js; serializeJson(doc,js); server.send(200,"application/json",js);
}
void handleHistory() {
  int l = server.arg("limit").toInt(); if (l<=0||l>history_count) l=min(50,history_count);
  int o = server.arg("offset").toInt(); if (o<0) o=0;
  DynamicJsonDocument doc(8192); doc["total"]=history_count; doc["limit"]=l; doc["offset"]=o;
  JsonArray a=doc.createNestedArray("data");
  for (int i=0;i<l;i++){int idx=(history_head-1-o-i+MAX_HISTORY)%MAX_HISTORY;if(idx<0||idx>=MAX_HISTORY)break;const Reading& r=history[idx].reading;JsonObject oo=a.createNestedObject();oo["timestamp"]=r.timestamp;oo["raw_distance"]=r.raw_distance;oo["median_distance"]=r.median_distance;oo["air_gap_cm"]=r.air_gap;oo["depth_cm"]=r.depth;oo["calibrated"]=r.calibrated;oo["valid"]=r.valid;}
  String js;serializeJson(doc,js);server.send(200,"application/json",js);
}

void handleHistoryCSV() {
  String cs="timestamp,raw_distance,median_distance,air_gap_cm,depth_cm,calibrated,valid\n";
  for(int i=0;i<history_count;i++){int idx=(history_head-1-i+MAX_HISTORY)%MAX_HISTORY;const Reading& r=history[idx].reading;cs+=String(r.timestamp,3)+","+String(r.raw_distance,2)+","+String(r.median_distance,2)+","+String(r.air_gap,2)+","+String(r.depth,2)+","+(r.calibrated?"true":"false")+","+(r.valid?"true":"false")+"\n";}
  server.send(200,"text/csv",cs);
}

void handleRegression() {
  RegressionResult rg=computeRegression();
  DynamicJsonDocument doc(1024);
  doc["model"]=rg.model; doc["equation"]=rg.equation; doc["intercept_cm"]=rg.intercept; doc["slope_cm_per_sec"]=rg.slope_per_sec;
  doc["slope_cm_per_hour"]=rg.slope_per_hour; doc["r_squared"]=rg.r_squared; doc["sample_count"]=rg.sample_count;
  doc["time_range"]["start"]=rg.time_start; doc["time_range"]["end"]=rg.time_end; doc["time_range"]["duration_sec"]=rg.time_end-rg.time_start;
  doc["interpretation"]=rg.interpretation;
  String js;serializeJson(doc,js);server.send(200,"application/json",js);
}

void handleStats() {
  StatsResult st=computeStats();
  DynamicJsonDocument doc(1024);
  doc["total_samples"]=st.total_samples; doc["valid_samples"]=st.valid_samples; doc["calibrated_samples"]=st.calibrated_samples;
  doc["time_range"]["start"]=st.time_start; doc["time_range"]["end"]=st.time_end; doc["time_range"]["duration_sec"]=st.duration_sec;
  doc["air_gap_stats"]["count"]=st.air_gap_stats.count; doc["air_gap_stats"]["mean"]=st.air_gap_stats.mean; doc["air_gap_stats"]["stddev"]=st.air_gap_stats.stddev;
  doc["air_gap_stats"]["min"]=st.air_gap_stats.min; doc["air_gap_stats"]["max"]=st.air_gap_stats.max; doc["air_gap_stats"]["range"]=st.air_gap_stats.range;
  doc["depth_stats"]["count"]=st.depth_stats.count; doc["depth_stats"]["mean"]=st.depth_stats.mean; doc["depth_stats"]["stddev"]=st.depth_stats.stddev;
  doc["depth_stats"]["min"]=st.depth_stats.min; doc["depth_stats"]["max"]=st.depth_stats.max; doc["depth_stats"]["range"]=st.depth_stats.range;
  doc["calibration"]["datum_cm"]=st.calibration.datum_cm; doc["calibration"]["is_calibrated"]=st.calibration.is_calibrated;
  String js;serializeJson(doc,js);server.send(200,"application/json",js);
}

void handleCalibrate() {
  if (server.method()!=HTTP_POST){server.send(405,"text/plain","Method not allowed");return;}
  DynamicJsonDocument doc(256);deserializeJson(doc,server.arg("plain"));float d=doc["datum"]|server.arg("datum").toFloat();
  calibration.datum_cm=d;calibration.is_calibrated=true;
  DynamicJsonDocument r(256);r["success"]=true;r["datum_cm"]=d;r["message"]="Calibrated";
  String js;serializeJson(r,js);server.send(200,"application/json",js);
}

void handleConfig() {
  DynamicJsonDocument doc(512);doc["simulator_mode"]=simulator_mode;doc["min_distance"]=sim_min_distance;doc["max_distance"]=sim_max_distance;doc["step_size"]=sim_step_size;
  doc["auto_calibrate"]=calibration.is_calibrated;doc["enable_history"]=history_enabled;doc["history_max"]=MAX_HISTORY;
  doc["tank_height_ft"]=TANK_HEIGHT_FT;doc["tank_height_cm"]=TANK_HEIGHT_CM;
  String js;serializeJson(doc,js);server.send(200,"application/json",js);
}

void handleRealData() {
  if (server.method() == HTTP_POST) {
    DynamicJsonDocument doc(2048);
    deserializeJson(doc, server.arg("plain"));
    JsonArray data = doc["data"];
    real_data_count = 0;
    for (JsonVariant v : data) {
      if (real_data_count < MAX_REAL_DATA) {
        real_data_buffer[real_data_count++] = v.as<float>();
      }
    }
    real_data_index = 0;
    real_data_interval_ms = doc["interval_ms"] | 1000;
    real_data_playback = true;
    simulator_mode = SIMULATOR_MODE_REAL_DATA;
    DynamicJsonDocument resp(256);
    resp["success"] = true;
    resp["count"] = real_data_count;
    resp["message"] = "Real data loaded";
    String js;serializeJson(resp,js);server.send(200,"application/json",js);
    Serial.printf("[HTTP] Loaded %d real data points\n", real_data_count);
  } else if (server.method() == HTTP_GET) {
    DynamicJsonDocument doc(2048);
    doc["playback"] = real_data_playback;
    doc["count"] = real_data_count;
    doc["index"] = real_data_index;
    doc["interval_ms"] = real_data_interval_ms;
    doc["mode"] = simulator_mode;
    JsonArray arr = doc.createNestedArray("data");
    for (int i = 0; i < real_data_count; i++) arr.add(real_data_buffer[i]);
    String js;serializeJson(doc,js);server.send(200,"application/json",js);
  } else {
    server.send(405,"text/plain","Method not allowed");
  }
}

void handleStopRealData() {
  real_data_playback = false;
  DynamicJsonDocument doc(256);
  doc["success"] = true;
  doc["message"] = "Real data playback stopped";
  String js;serializeJson(doc,js);server.send(200,"application/json",js);
}

void handleStatus() {
  DynamicJsonDocument doc(512);doc["running"]=true;doc["uptime_sec"]=millis()/1000;doc["free_heap"]=ESP.getFreeHeap();
  doc["wifi_clients"]=WiFi.softAPgetStationNum();doc["ws_clients"]=0;
  for(int i=0;i<MAX_WS_CLIENTS;i++)if(ws_clients[i])doc["ws_clients"] = doc["ws_clients"].as<int>() + 1;
  String js;serializeJson(doc,js);server.send(200,"application/json",js);
}

void handleEndpoints() {
  server.send(200,"application/json",R"JSON({"endpoints":[{"path":"/","method":"GET","description":"Web UI"},{"path":"/sensor","method":"GET","description":"Current sensor reading"},{"path":"/history","method":"GET","description":"Sensor history (paginated)"},{"path":"/history.csv","method":"GET","description":"Download history as CSV"},{"path":"/regression","method":"GET","description":"Linear regression on depth vs time"},{"path":"/stats","method":"GET","description":"Statistical summary of history"},{"path":"/calibrate","method":"POST","description":"Set calibration datum"},{"path":"/config","method":"GET","description":"Get server configuration"},{"path":"/realtime","method":"POST/GET","description":"Load real data (POST) or get status (GET)"},{"path":"/realtime/stop","method":"POST","description":"Stop real data playback"},{"path":"/status","method":"GET","description":"Get server status"},{"path":"/endpoints","method":"GET","description":"List all available endpoints"}])JSON");
}
// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println(F("\n\n[Boot] Water Level Monitor Starting..."));
  Wire.begin(21, 22);
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("[OLED] SSD1306 allocation failed"));
  } else {
    Serial.println(F("[OLED] Display initialized"));
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(0, 0);
    display.println(F("Water Level Monitor"));
    display.println(F("Starting..."));
    display.display();
  }
  sim_last_time = millis();
  sim_position = (sim_min_distance + sim_max_distance) / 2.0;
  start_time = millis();
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  IPAddress IP = WiFi.softAPIP();
  Serial.print(F("[WiFi] AP Started: "));
  Serial.println(AP_SSID);
  Serial.print(F("[WiFi] IP Address: "));
  Serial.println(IP);
  if (MDNS.begin("waterlevel")) {
    Serial.println(F("[mDNS] Responder started: waterlevel.local"));
  }
  server.on("/", handleRoot);
  server.on("/sensor", handleSensor);
  server.on("/history", handleHistory);
  server.on("/history.csv", handleHistoryCSV);
  server.on("/regression", handleRegression);
  server.on("/stats", handleStats);
  server.on("/calibrate", handleCalibrate);
  server.on("/config", handleConfig);
  server.on("/realtime", handleRealData);       // POST/GET real data
  server.on("/realtime/stop", handleStopRealData); // POST to stop
  server.on("/status", handleStatus);
  server.on("/endpoints", handleEndpoints);
  server.begin();
  Serial.println(F("[HTTP] Server started on port 80"));
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  Serial.println(F("[WS] WebSocket server started on port 81"));
  Serial.println(F("\n[Endpoints]"));
  Serial.println(F("  Web UI:       http://waterlevel.local/  (or http://<IP>/)"));
  Serial.println(F("  Sensor:       http://waterlevel.local/sensor"));
  Serial.println(F("  History:      http://waterlevel.local/history"));
  Serial.println(F("  History CSV:  http://waterlevel.local/history.csv"));
  Serial.println(F("  Regression:   http://waterlevel.local/regression"));
  Serial.println(F("  Stats:        http://waterlevel.local/stats"));
  Serial.println(F("  Calibrate:    POST http://waterlevel.local/calibrate"));
  Serial.println(F("  Real Data:    POST/GET http://waterlevel.local/realtime"));
  Serial.println(F("  Stop Real:    POST http://waterlevel.local/realtime/stop"));
  Serial.println(F("  Config:       http://waterlevel.local/config"));
  Serial.println(F("  Status:       http://waterlevel.local/status"));
  Serial.println(F("  Endpoints:    http://waterlevel.local/endpoints"));
  Serial.println(F("  WebSocket:    ws://waterlevel.local:81/"));
  Serial.println(F("\n[Simulator] Mode: RANDOM_WALK"));
  Serial.println(F("[Simulator] Range: 140-155 cm"));
  Serial.println(F("[Ready] System operational!\n"));
}

// ==================== LOOP ====================
void loop() {
  unsigned long now = millis();
  webSocket.loop();
  server.handleClient();
  if (now - last_sensor_read >= SENSOR_INTERVAL) {
    last_sensor_read = now;
    Reading r = takeReading();
    addToHistory(r);
  }
  if (now - last_display_update >= DISPLAY_INTERVAL) {
    last_display_update = now;
    Reading r = takeReading();
    updateDisplay(r);
  }
  if (now - last_ws_broadcast >= WS_BROADCAST_INTERVAL) {
    last_ws_broadcast = now;
    Reading r = takeReading();
    broadcastSensorUpdate(r);
  }
  if (now - last_stats_update >= STATS_INTERVAL) {
    last_stats_update = now;
    StatsResult stats = computeStats();
    Serial.printf("[Stats] Total: %d, Valid: %d, Cal: %d, Heap: %d\n", stats.total_samples, stats.valid_samples, stats.calibrated_samples, ESP.getFreeHeap());
  }
}