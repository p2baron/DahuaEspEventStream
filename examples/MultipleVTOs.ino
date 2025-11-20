/*
  MultipleVTOs.ino
  - Example: manage TWO Dahua VTO streams simultaneously
  - ESP32-C3 DevKitM-1 (onboard LED on GPIO 8)
*/

#include <WiFi.h>
#include "DahuaVTOClient.h"

static const int LED_PIN = 8;

// ---- Fill in your Wi-Fi once ----
const char* WIFI_SSID = "SSID";
const char* WIFI_PASS = "SECRET PASSWORD";

// Create two clients
DahuaVTOClient vtoA;
DahuaVTOClient vtoB;

// Doorbell event
void onVTOEventA(DahuaEventKind kind, const String& raw, void* ctx) {
  if (kind == DahuaEventKind::DoorbellPress) {
    Serial.println("[APP] VTO A: Doorbell!");
    digitalWrite(LED_PIN, HIGH); delay(120); digitalWrite(LED_PIN, LOW);
  } else {
    // Serial.println("[APP] VTO A line: " + raw);
  }
}

void onVTOEventB(DahuaEventKind kind, const String& raw, void* ctx) {
  if (kind == DahuaEventKind::DoorbellPress) {
    Serial.println("[APP] VTO B: Doorbell!");
    digitalWrite(LED_PIN, HIGH); delay(120); digitalWrite(LED_PIN, LOW);
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // Connect Wi-Fi (you can use your own logic too)
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("Wi-Fi: connecting to %s", WIFI_SSID);
  while (WiFi.status() != WL_CONNECTED) { delay(300); Serial.print("."); }
  Serial.printf("\nWi-Fi connected. IP: %s\n", WiFi.localIP().toString().c_str());

  // Configure VTO A
  DahuaVTOConfig cfgA;
  cfgA.host = "192.168.10.2";
  cfgA.port = 80;
  cfgA.https = false;
  cfgA.user = "admin";
  cfgA.pass = "admin_password";
  cfgA.heartbeat_s = 5;
  cfgA.useEncodedAll = true;

  // Configure VTO B (example different IP/creds)
  DahuaVTOConfig cfgB = cfgA;
  cfgB.host = "192.168.10.3";

  vtoA.setConfig(cfgA);
  vtoB.setConfig(cfgB);

  vtoA.onEvent(onVTOEventA);
  vtoB.onEvent(onVTOEventB);

  // Optional one-shot sanity checks (Digest + reachability)
  int st;
  if (vtoA.testGetSystemInfo(nullptr, &st)) Serial.printf("[APP] VTO A system info OK (%d)\n", st);
  if (vtoB.testGetSystemInfo(nullptr, &st)) Serial.printf("[APP] VTO B system info OK (%d)\n", st);

  // Start both streams (each spawns its own FreeRTOS task)
  vtoA.start();
  vtoB.start();
}

void loop() {
  // your app logic; streams run in background tasks
  delay(1000);
}