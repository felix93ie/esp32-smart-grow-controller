#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <time.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#define OLED_SDA_PIN 25
#define OLED_SCL_PIN 26
#define PWM_DIM_PIN  27

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
WebServer server(80);

const int PWM_FREQ = 1000;
const int PWM_RES  = 8;

#if __has_include("secrets.h")
  #include "secrets.h"
#else
  const char* ssid     = "YOUR_WIFI_SSID";
  const char* password = "YOUR_WIFI_PASSWORD";
#endif
const char* ntpServer     = "pool.ntp.org";
const long  gmtOffset_sec = -28800;
const int   daylightOffset_sec = 3600;

enum ControlMode { MODE_AUTO, MODE_MANUAL };
ControlMode currentMode = MODE_AUTO;

int manualIntensity = 100;
int lightOnHour     = 8;  // 8:00 AM
int lightOffHour    = 0;  // 12:00 AM Midnight (16-Hour Schedule)
int activeIntensity = 0;

void setLightIntensity(int percentage) {
  percentage = constrain(percentage, 0, 100);
  activeIntensity = percentage;
  int invertedDuty = map(percentage, 0, 100, 255, 0);
  
  #if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    ledcWrite(PWM_DIM_PIN, invertedDuty);
  #else
    ledcWrite(0, invertedDuty);
  #endif
}

String getHTMLPage() {
  String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>ESP32 Grow Light Controller</title>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; background-color: #121212; color: #e0e0e0; text-align: center; margin: 0; padding: 20px; }";
  html += ".card { background: #1e1e1e; max-width: 420px; margin: 15px auto; padding: 20px; border-radius: 12px; box-shadow: 0 4px 12px rgba(0,0,0,0.6); }";
  html += "h1 { color: #4CAF50; font-size: 24px; margin-bottom: 5px; }";
  html += "h2 { color: #81C784; font-size: 18px; border-bottom: 1px solid #333; padding-bottom: 8px; margin-top:0; }";
  html += ".btn { background: #4CAF50; color: white; border: none; padding: 12px 18px; margin: 6px; font-size: 15px; border-radius: 6px; cursor: pointer; text-decoration: none; display: inline-block; font-weight:bold; }";
  html += ".btn-off { background: #e53935; }";
  html += ".btn-auto { background: #1e88e5; }";
  html += "input[type=range] { width: 85%; margin: 15px 0; }";
  html += "input[type=number] { width: 60px; padding: 6px; font-size: 16px; text-align: center; background: #2c2c2c; color: #fff; border: 1px solid #555; border-radius: 4px; }";
  html += ".stat { font-size: 22px; font-weight: bold; color: #FFD54F; }";
  html += "</style></head><body>";

  html += "<h1>🌱 ESP32 Grow Light</h1>";

  html += "<div class='card'><h2>Status</h2>";
  html += "<p>Current Mode: <b>" + String(currentMode == MODE_AUTO ? "AUTO (Schedule)" : "MANUAL OVERRIDE") + "</b></p>";
  html += "<p>Output Intensity: <span class='stat'>" + String(activeIntensity) + "%</span></p>";
  html += "<p>Schedule: <b>" + String(lightOnHour) + ":00 to " + String(lightOffHour) + ":00</b></p>";
  html += "</div>";

  html += "<div class='card'><h2>Manual Controls</h2>";
  html += "<a href='/set?mode=auto' class='btn btn-auto'>AUTO Schedule</a>";
  html += "<a href='/set?mode=manual&val=100' class='btn'>Full ON (100%)</a>";
  html += "<a href='/set?mode=manual&val=0' class='btn btn-off'>Turn OFF (0%)</a>";
  
  html += "<form action='/set' method='GET' style='margin-top:20px;'>";
  html += "<input type='hidden' name='mode' value='manual'>";
  html += "<label>Dimmer Slider (0-100%): </label><br>";
  html += "<input type='range' name='val' min='0' max='100' value='" + String(manualIntensity) + "' onchange='this.form.submit()'><br>";
  html += "<span>Set Level: " + String(manualIntensity) + "%</span>";
  html += "</form></div>";

  html += "<div class='card'><h2>Adjust Schedule</h2>";
  html += "<form action='/set_schedule' method='GET'>";
  html += "<label>ON Hour (0-23): </label>";
  html += "<input type='number' name='on' min='0' max='23' value='" + String(lightOnHour) + "'> <br><br>";
  html += "<label>OFF Hour (0-23): </label>";
  html += "<input type='number' name='off' min='0' max='23' value='" + String(lightOffHour) + "'><br><br>";
  html += "<input type='submit' class='btn' value='Save Schedule'>";
  html += "</form></div>";

  html += "</body></html>";
  return html;
}

void handleRoot() {
  server.send(200, "text/html", getHTMLPage());
}

void handleSet() {
  if (server.hasArg("mode")) {
    String m = server.arg("mode");
    if (m == "auto") {
      currentMode = MODE_AUTO;
    } else if (m == "manual") {
      currentMode = MODE_MANUAL;
      if (server.hasArg("val")) {
        manualIntensity = constrain(server.arg("val").toInt(), 0, 100);
      }
    }
  }
  server.sendHeader("Location", "/");
  server.send(303, "text/plain", "");
}

void handleSetSchedule() {
  if (server.hasArg("on") && server.hasArg("off")) {
    lightOnHour  = constrain(server.arg("on").toInt(), 0, 23);
    lightOffHour = constrain(server.arg("off").toInt(), 0, 23);
  }
  server.sendHeader("Location", "/");
  server.send(303, "text/plain", "");
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n==========================================");
  Serial.println("   ESP32 GROW LIGHT CONTROLLER STARTUP    ");
  Serial.println("==========================================");

  Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);

  #if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    ledcAttach(PWM_DIM_PIN, PWM_FREQ, PWM_RES);
  #else
    ledcSetup(0, PWM_FREQ, PWM_RES);
    ledcAttachPin(PWM_DIM_PIN, 0);
  #endif

  setLightIntensity(0);
  Serial.println("[OK] Hardware PWM Initialized on GPIO 27");

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("[ERROR] OLED Display not found at 0x3C!");
  } else {
    Serial.println("[OK] OLED Display detected at 0x3C");
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("ESP32 GROW LIGHT");
    display.println("Connecting Wi-Fi...");
    display.display();
  }

  WiFi.begin(ssid, password);
  Serial.print("[NET] Connecting to Wi-Fi");
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[OK] Wi-Fi Connected!");
    Serial.print("[NET] IP Address: ");
    Serial.println(WiFi.localIP());
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

    server.on("/", handleRoot);
    server.on("/set", handleSet);
    server.on("/set_schedule", handleSetSchedule);
    server.begin();
    Serial.println("[OK] Web Server Started on Port 80");
  } else {
    Serial.println("\n[WARN] Wi-Fi Timeout - Running offline mode.");
  }
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    server.handleClient();
  }

  struct tm timeinfo;
  bool timeValid = getLocalTime(&timeinfo);

  if (currentMode == MODE_AUTO) {
    int currentHour = timeValid ? timeinfo.tm_hour : 12;
    bool isLightOn  = false;

    if (lightOnHour < lightOffHour) {
      isLightOn = (currentHour >= lightOnHour && currentHour < lightOffHour);
    } else {
      isLightOn = (currentHour >= lightOnHour || currentHour < lightOffHour);
    }

    setLightIntensity(isLightOn ? 100 : 0);
  } else {
    setLightIntensity(manualIntensity);
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("ESP32 GROW LIGHT");
  display.println("--------------------");

  if (WiFi.status() == WL_CONNECTED) {
    display.print("IP: ");
    display.println(WiFi.localIP());
  } else {
    display.println("Wi-Fi: Disconnected");
  }

  if (timeValid) {
    display.printf("Time: %02d:%02d:%02d\n", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  } else {
    display.println("Time: Syncing...");
  }

  if (currentMode == MODE_AUTO) {
    display.printf("Mode: AUTO [%d%%]\n", activeIntensity);
  } else {
    display.printf("Mode: MANUAL [%d%%]\n", activeIntensity);
  }

  display.display();
  delay(200);
}