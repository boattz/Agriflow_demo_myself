/*
 * ============================================
 *  Smart Sprinkler — ESP32 Moisture Sensor
 *  Sends moisture data to the Node.js dashboard.
 *
 *  First boot: joins its own "SmartSprinkler-Setup" WiFi AP,
 *  opens a setup page to configure your home WiFi + dashboard IP.
 *  After that it reconnects automatically (stored in NVS).
 *
 *  Hold the BOOT button ~3s to reconfigure.
 * ============================================
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include "config_portal.h"

// ── Device ───────────────────────────────────
const char* DEVICE_ID = "ESP32_01";

// ── Config (loaded from NVS at boot) ─────────
DeviceConfig cfg;
String serverUrl;

// ── Sensor Pin ───────────────────────────────
const int MOISTURE_PIN = 34;

// ── Send Interval ────────────────────────────
const unsigned long SEND_INTERVAL = 5000; // ms
unsigned long lastSendTime = 0;

// ── Reset Button ─────────────────────────────
unsigned long resetPressedSince = 0;

// ─────────────────────────────────────────────
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
}

// ─────────────────────────────────────────────
bool sendMoistureData(int rawValue) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("⚠️  WiFi not connected, skipping...");
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

  // Build JSON payload
  String json = "{\"device\":\"" + String(DEVICE_ID) + "\","
                "\"moisture\":" + String(rawValue) + "}";

  Serial.print("📤 Sending → ");
  Serial.println(json);

  int httpCode = http.POST(json);

  if (httpCode > 0) {
    Serial.print("✅ HTTP Response: ");
    Serial.println(httpCode);
    http.end();
    return true;
  } else {
    Serial.print("❌ HTTP Error: ");
    Serial.println(http.errorToString(httpCode));
    http.end();
    return false;
  }
}

// ─────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("=====================================");
  Serial.println("  Smart Sprinkler — Moisture Monitor  ");
  Serial.println("=====================================");

  pinMode(CP_RESET_PIN, INPUT_PULLUP);

  if (!loadConfig(cfg)) {
    startConfigPortal();
    if (!loadConfig(cfg)) ESP.restart();
  }
  serverUrl = buildServerUrl(cfg);
  Serial.print("Dashboard API: ");
  Serial.println(serverUrl);

  connectWiFi();
}

// ─────────────────────────────────────────────
void loop() {
  // Reconfigure by holding BOOT ~3s.
  if (checkResetButton(resetPressedSince)) {
    Serial.println("[RESET] Button held — clearing config and rebooting.");
    clearConfig();
    delay(200);
    ESP.restart();
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("⚠️  WiFi lost! Reconnecting...");
    connectWiFi();
  }

  unsigned long now = millis();
  if (now - lastSendTime >= SEND_INTERVAL) {
    lastSendTime = now;

    // Read sensor (average of 5 samples for stability)
    long sum = 0;
    for (int i = 0; i < 5; i++) {
      sum += analogRead(MOISTURE_PIN);
      delay(10);
    }
    int rawValue = sum / 5;

    // Calculate percentage for local Serial output
    int moistPct = map(rawValue, 4095, 0, 0, 100);
    moistPct = constrain(moistPct, 0, 100);

    Serial.println("─────────────────────────────");
    Serial.print  ("💧 Moisture Raw : "); Serial.println(rawValue);
    Serial.print  ("   Moisture %   : "); Serial.print(moistPct); Serial.println("%");
    Serial.print  ("   WiFi RSSI    : "); Serial.print(WiFi.RSSI()); Serial.println(" dBm");

    sendMoistureData(rawValue);
  }
}
