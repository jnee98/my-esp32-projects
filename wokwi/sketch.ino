/**
 * Water Level Monitor - Wokwi Simulation (Arduino)
 * 
 * Hardware (Wokwi):
 * - ESP32 DevKit v4
 * - HC-SR04 Ultrasonic Sensor
 * - SSD1306 128x64 I2C OLED
 * 
 * Connections:
 * - HC-SR04: TRIG=GPIO4, ECHO=GPIO5, VCC=5V, GND=GND
 * - SSD1306: SDA=GPIO21, SCL=GPIO22, VCC=3.3V, GND=GND
 * 
 * Serial Commands:
 *   h/help  - Show help
 *   c       - Toggle calibration mode
 *   d <cm>  - Set datum distance (e.g., d 150.5)
 *   r       - Reset calibration
 *   s       - Single reading
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Pin definitions (ESP32 classic defaults)
#define TRIG_PIN 4
#define ECHO_PIN 5
#define OLED_SDA 21
#define OLED_SCL 22

// OLED settings
#define OLED_ADDR 0x3C
#define OLED_W 128
#define OLED_H 64

// Sensor settings
#define ECHO_TIMEOUT_US 30000
#define NUM_SAMPLES 5
#define MIN_VALID_CM 2.0
#define MAX_VALID_CM 400.0
#define SAMPLE_DELAY_MS 80

Adafruit_SSD1306 display(OLED_W, OLED_H, &Wire, -1);

float datum = 0;
bool calMode = false;

uint32_t lastRead = 0, lastDisp = 0, lastPrint = 0;
float lastGap = -1, lastDepth = -1;
bool lastCal = false;

void setup() {
  Serial.begin(115200);
  delay(500);
  
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);

  Wire.begin(OLED_SDA, OLED_SCL);
  
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("ERROR: SSD1306 init failed!");
    while (1) delay(100);
  }

  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(2);
  display.setCursor(10, 20);
  display.println("WATER");
  display.setCursor(10, 40);
  display.println("LEVEL");
  display.display();
  delay(2000);

  printHelp();
}

void loop() {
  handleSerial();
  
  uint32_t now = millis();

  if (now - lastRead >= 100) { lastRead = now; readSensor(); }
  if (now - lastDisp >= 1000) { lastDisp = now; updateDisplay(); }
  if (now - lastPrint >= 1000) { lastPrint = now; printStatus(); }
}

float readDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  
  uint32_t dur = pulseIn(ECHO_PIN, HIGH, ECHO_TIMEOUT_US);
  if (dur == 0) return -1;
  return dur / 58.0;
}

float readMedian() {
  float samples[NUM_SAMPLES];
  int valid = 0;
  
  for (int i = 0; i < NUM_SAMPLES; i++) {
    float d = readDistance();
    if (d >= MIN_VALID_CM && d <= MAX_VALID_CM) {
      samples[valid++] = d;
    }
    delay(SAMPLE_DELAY_MS);
  }
  
  if (valid < 3) return -1;
  
  for (int i = 1; i < valid; i++) {
    float key = samples[i];
    int j = i - 1;
    while (j >= 0 && samples[j] > key) {
      samples[j + 1] = samples[j];
      j--;
    }
    samples[j + 1] = key;
  }
  
  return samples[valid / 2];
}

void readSensor() {
  float gap = readMedian();
  if (gap < 0) return;
  
  lastGap = gap;
  lastCal = (datum > 0);
  
  if (lastCal) {
    float d = datum - gap;
    lastDepth = (d >= 0) ? d : -1;
  }
  
  if (calMode) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("CALIBRATION MODE");
    display.println("----------------");
    display.print("Air gap: ");
    display.print(gap, 1);
    display.println(" cm");
    display.println();
    display.println("Measure sensor to");
    display.println("tank bottom");
    display.display();
  }
}

void updateDisplay() {
  if (calMode) return;
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("WATER LEVEL MONITOR");
  
  display.setCursor(0, 20);
  display.print("AIR GAP: ");
  if (lastGap >= 0) { display.print(lastGap, 1); display.print(" cm"); }
  else display.print("NO ECHO");
  
  display.setCursor(0, 42);
  display.print("DEPTH: ");
  if (lastCal) {
    if (lastDepth >= 0) { display.print(lastDepth, 1); display.print(" cm"); }
    else display.print("OVERFLOW");
  } else {
    display.print("UNCALIBRATED");
  }
  
  display.setCursor(0, 56);
  display.print(lastCal ? "CALIBRATED" : "UNCALIBRATED - Press 'c'");
  display.display();
}

void printStatus() {
  if (calMode) return;
  
  Serial.print("Air Gap: ");
  if (lastGap >= 0) Serial.print(lastGap, 1);
  else Serial.print("NO ECHO");
  Serial.print(" cm");
  
  if (lastCal) {
    Serial.print(" | Depth: ");
    if (lastDepth >= 0) Serial.print(lastDepth, 1);
    else Serial.print("OVERFLOW");
    Serial.print(" cm");
  } else {
    Serial.print(" | UNCALIBRATED");
  }
  Serial.println();
}

void handleSerial() {
  if (!Serial.available()) return;
  
  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  cmd.toLowerCase();
  
  if (cmd == "h" || cmd == "help") printHelp();
  else if (cmd == "c" || cmd == "calibrate") { calMode = !calMode; Serial.println(calMode ? "\n>>> CAL ON <<<" : "\n>>> CAL OFF <<<"); }
  else if (cmd.startsWith("d ")) { float v = cmd.substring(2).toFloat(); if (v > 0) { datum = v; Serial.print("Datum: "); Serial.print(datum, 1); Serial.println(" cm"); } else Serial.println("Usage: d <cm>"); }
  else if (cmd == "r" || cmd == "reset") { datum = 0; Serial.println("Cal cleared"); }
  else if (cmd == "s" || cmd == "single") { float g = readMedian(); Serial.print("Gap: "); if (g>=0) Serial.print(g,1); else Serial.print("ERR"); Serial.println(" cm"); }
  else if (cmd.length() > 0) { Serial.print("Unknown: "); Serial.print(cmd); Serial.println(" (h for help)"); }
}

void printHelp() {
  Serial.println("\n=== WATER LEVEL MONITOR ===");
  Serial.println("h/help  - Help");
  Serial.println("c       - Toggle calibration");
  Serial.println("d <cm>  - Set datum (e.g., d 150.5)");
  Serial.println("r       - Reset calibration");
  Serial.println("s       - Single reading");
  Serial.println("===========================\n");
}