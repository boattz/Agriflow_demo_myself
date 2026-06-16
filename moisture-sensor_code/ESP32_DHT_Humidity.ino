/*
 * =====================================================
 *  Agriflow — ESP32 DHT Humidity Sensor
 *  Sends Humidity + Temperature → Node.js Dashboard
 * =====================================================
 *
 *  WIRING (DHT22 recommended, DHT11 also works):
 *  ┌──────────────┬────────────────┐
 *  │  DHT Pin     │  ESP32 Pin     │
 *  ├──────────────┼────────────────┤
 *  │  VCC         │  3.3V          │
 *  │  GND         │  GND           │
 *  │  DATA        │  GPIO 4        │
 *  └──────────────┴────────────────┘
 *  (Add 10kΩ pull-up resistor between DATA and VCC)
 *
 *  LIBRARY REQUIRED:
 *  Arduino IDE → Tools → Manage Libraries
 *  → Search "DHT sensor library" by Adafruit → Install
 *  → Also install "Adafruit Unified Sensor" if prompted
 *
 *  FIRST BOOT:
 *  ESP32 opens "Agriflow-Setup" WiFi AP. Connect from your
 *  phone/computer, fill in home WiFi + dashboard IP, then Save.
 *  Settings stored in NVS — subsequent boots reconnect automatically.
 *  Hold BOOT button ~3s to reconfigure.
 * =====================================================
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <DHT.h>
#include "config_portal.h"   // WiFi + server IP via captive portal (NVS-persisted)

// ── Config (loaded from NVS at boot) ──────────────────
DeviceConfig cfg;
String serverUrl;

const char* DEVICE_ID   = "ESP32_DHT";

// ── DHT Sensor ────────────────────────────────────────
#define DHT_PIN  4           // GPIO pin connected to DHT DATA
#define DHT_TYPE DHT22       // Use DHT11 if you have DHT11

DHT dht(DHT_PIN, DHT_TYPE);

// ── Interval ──────────────────────────────────────────
const unsigned long INTERVAL = 30000; // Send every 30 seconds
unsigned long lastSend = 0;

// ── Reset Button ───────────────────────────────────────
unsigned long resetPressedSince = 0;

// ─────────────────────────────────────────────────────
void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.println("\n🔌 Connecting to WiFi: " + cfg.wifiSsid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPass.c_str());

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (++attempts > 40) {
      Serial.println("\n❌ WiFi failed — opening config portal.");
      clearConfig();
      startConfigPortal();
      ESP.restart();
    }
  }

  Serial.println();
  Serial.println("✅ WiFi Connected!");
  Serial.print("   IP Address : ");
  Serial.println(WiFi.localIP());
  Serial.print("   Signal     : ");
  Serial.print(WiFi.RSSI());
  Serial.println(" dBm");
  Serial.print("   Dashboard  : ");
  Serial.println(serverUrl);
  Serial.println("─────────────────────────────────");
}

// ─────────────────────────────────────────────────────
bool sendData(float humidity, float temperature, float heatIndex) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WARN] WiFi not connected");
    return false;
  }

  HTTPClient http;
  http.begin(serverUrl);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(10000);

  // Skip SSL certificate verification for HTTPS (Railway, cloud servers)
  if (serverUrl.startsWith("https")) {
    http.setInsecure();
  }

  // Build JSON
  String json = "{";
  json += "\"device\":\"" + String(DEVICE_ID) + "\",";
  json += "\"humidity\":"    + String(humidity,    1) + ",";
  json += "\"temperature\":" + String(temperature, 1) + ",";
  json += "\"heatIndex\":"   + String(heatIndex,   1);
  json += "}";

  Serial.print("[SEND] "); Serial.println(json);

  int code = http.POST(json);
  http.end();

  if (code == 200) {
    Serial.println("[OK]   Server received data ✓");
    return true;
  } else {
    Serial.print("[ERR]  HTTP "); Serial.println(code);
    return false;
  }
}

// ─────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("=====================================");
  Serial.println("  Agriflow — Humidity Monitor  ");
  Serial.println("=====================================");

  dht.begin();
  Serial.println("[OK] DHT sensor initialized on GPIO " + String(DHT_PIN));

  pinMode(CP_RESET_PIN, INPUT_PULLUP);

  if (!loadConfig(cfg)) {
    startConfigPortal();
    if (!loadConfig(cfg)) ESP.restart();
  }
  serverUrl = buildServerUrl(cfg);

  connectWiFi();
}

// ─────────────────────────────────────────────────────
void loop() {
  // Reconfigure by holding BOOT ~3s.
  if (checkResetButton(resetPressedSince)) {
    Serial.println("[RESET] Button held — clearing config and rebooting.");
    clearConfig();
    delay(200);
    ESP.restart();
  }

  // Auto-reconnect WiFi
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }

  if (millis() - lastSend >= INTERVAL) {
    lastSend = millis();

    // Read sensor (DHT22 needs ~2s between reads)
    float humidity    = dht.readHumidity();
    float temperature = dht.readTemperature();      // Celsius

    // Validate
    if (isnan(humidity) || isnan(temperature)) {
      Serial.println("[ERR] Failed to read DHT sensor! Check wiring.");
      return;
    }

    // Compute heat index
    float heatIndex = dht.computeHeatIndex(temperature, humidity, false);

    Serial.println("--------------------------------------");
    Serial.print("[DATA] Humidity    : "); Serial.print(humidity,    1); Serial.println(" %");
    Serial.print("[DATA] Temperature : "); Serial.print(temperature, 1); Serial.println(" °C");
    Serial.print("[DATA] Heat Index  : "); Serial.print(heatIndex,   1); Serial.println(" °C");

    sendData(humidity, temperature, heatIndex);
  }
}
