#include <Arduino.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>

#if __has_include("wifi_config.h")
#include "wifi_config.h"
#else
#define WIFI_CONFIG_HAS_CREDENTIALS 0
static const char WIFI_CONFIG_SSID[] = "YOUR_WIFI_SSID";
static const char WIFI_CONFIG_PASSWORD[] = "YOUR_WIFI_PASSWORD";
#endif

const char *DEVICE_NAME = "goodesp";
const int VIBRATION_PIN = 18;
const int VIBRATION_PWM_CHANNEL = 0;
const int VIBRATION_PWM_FREQ = 5000;
const int VIBRATION_PWM_RESOLUTION = 8;
const unsigned long WIFI_TIMEOUT_MS = 15000;

WebServer server(80);
Preferences prefs;
unsigned long vibrationUntilMs = 0;
int vibrationSpeed = 180;
bool vibrationOn = false;

String readSerialLine(const char *prompt, bool echoInput = true) {
  Serial.print(prompt);
  String value;

  while (true) {
    while (Serial.available() > 0) {
      char c = Serial.read();
      if (c == '\r') {
        continue;
      }
      if (c == '\n') {
        value.trim();
        Serial.println();
        return value;
      }
      value += c;
      if (echoInput) {
        Serial.print(c);
      }
    }
    delay(10);
  }
}

bool connectToWifi(const char *ssid, const char *password) {
  if (ssid == nullptr || ssid[0] == '\0') {
    return false;
  }

  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  unsigned long startedAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < WIFI_TIMEOUT_MS) {
    Serial.print(".");
    delay(500);
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Connected. IP: ");
    Serial.println(WiFi.localIP());
    return true;
  }

  Serial.println("WiFi connection failed.");
  WiFi.disconnect(true);
  delay(300);
  return false;
}

bool connectFromSavedCredentials() {
  prefs.begin("wifi", true);
  String ssid = prefs.getString("ssid", "");
  String password = prefs.getString("password", "");
  prefs.end();

  if (ssid.length() == 0) {
    return false;
  }

  Serial.println("Trying saved ESP WiFi credentials...");
  return connectToWifi(ssid.c_str(), password.c_str());
}

bool connectFromGeneratedConfig() {
#if WIFI_CONFIG_HAS_CREDENTIALS
  Serial.println("Trying WiFi credentials from include/wifi_config.h...");
  if (connectToWifi(WIFI_CONFIG_SSID, WIFI_CONFIG_PASSWORD)) {
    prefs.begin("wifi", false);
    prefs.putString("ssid", WIFI_CONFIG_SSID);
    prefs.putString("password", WIFI_CONFIG_PASSWORD);
    prefs.end();
    Serial.println("Saved WiFi credentials to ESP flash.");
    return true;
  }
#endif
  return false;
}

void promptForWifiOverSerial() {
  Serial.println();
  Serial.println("WiFi setup needed.");
  Serial.println("Open serial monitor at 115200 and enter your WiFi when prompted.");

  while (WiFi.status() != WL_CONNECTED) {
    String ssid = readSerialLine("SSID: ");
    String password = readSerialLine("Password = "YOUR_PASSWORD_HERE"wifi", false);
      prefs.putString("ssid", ssid);
      prefs.putString("password", password);
      prefs.end();
      Serial.println("Saved WiFi credentials to ESP flash.");
      return;
    }

    Serial.println("Try again.");
  }
}

String pageHtml() {
  String ip = WiFi.localIP().toString();
  String html;
  html += "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>goodesp vibration</title>";
  html += "<style>body{font-family:Arial,sans-serif;margin:24px;max-width:520px}button,input{font-size:20px;padding:12px;margin:6px 0;width:100%;box-sizing:border-box}button{cursor:pointer}.row{display:grid;grid-template-columns:1fr 1fr;gap:10px}.card{border:1px solid #ccc;border-radius:8px;padding:16px}.value{font-size:22px;font-weight:bold}</style>";
  html += "<script>function setSpeed(v){document.getElementById('speedValue').textContent=v;fetch('/speed?value='+v)}function vibrate(){fetch('/vibrate?ms='+document.getElementById('ms').value+'&speed='+document.getElementById('speed').value)}function on(){fetch('/on?speed='+document.getElementById('speed').value)}function off(){fetch('/off')}</script>";
  html += "</head><body><h1>goodesp</h1><div class='card'>";
  html += "<p>IP: " + ip + "</p>";
  html += "<p>Speed: <span id='speedValue' class='value'>" + String(vibrationSpeed) + "</span> / 255</p>";
  html += "<input id='speed' type='range' min='0' max='255' value='" + String(vibrationSpeed) + "' oninput='setSpeed(this.value)'>";
  html += "<button onclick=\"fetch('/vibrate?ms=1000&speed='+document.getElementById('speed').value)\">Vibrate 1 second</button>";
  html += "<div class='row'><button onclick='on()'>On</button><button onclick='off()'>Off</button></div>";
  html += "<input id='ms' type='number' min='50' max='10000' value='500'>";
  html += "<button onclick='vibrate()'>Vibrate custom ms</button>";
  html += "</div></body></html>";
  return html;
}

void writeVibration(int speed) {
  speed = constrain(speed, 0, 255);
  ledcWrite(VIBRATION_PWM_CHANNEL, speed);
  vibrationOn = speed > 0;
}

void stopVibration() {
  writeVibration(0);
  vibrationUntilMs = 0;
}

void setVibrationSpeed(int speed) {
  vibrationSpeed = constrain(speed, 0, 255);
  if (vibrationOn) {
    writeVibration(vibrationSpeed);
  }
}

void startVibration(unsigned long durationMs) {
  durationMs = constrain(durationMs, 50UL, 10000UL);
  writeVibration(vibrationSpeed);
  vibrationUntilMs = millis() + durationMs;
  Serial.print("Vibration on for ms: ");
  Serial.println(durationMs);
  Serial.print("Vibration speed: ");
  Serial.println(vibrationSpeed);
}

void setupWebServer() {
  server.on("/", []() {
    server.send(200, "text/html", pageHtml());
  });

  server.on("/vibrate", []() {
    unsigned long durationMs = server.hasArg("ms") ? server.arg("ms").toInt() : 1000;
    if (server.hasArg("speed")) {
      setVibrationSpeed(server.arg("speed").toInt());
    }
    startVibration(durationMs);
    server.send(200, "text/plain", "vibrating");
  });

  server.on("/on", []() {
    if (server.hasArg("speed")) {
      setVibrationSpeed(server.arg("speed").toInt());
    }
    writeVibration(vibrationSpeed);
    vibrationUntilMs = 0;
    Serial.print("Vibration on at speed: ");
    Serial.println(vibrationSpeed);
    server.send(200, "text/plain", "on");
  });

  server.on("/off", []() {
    stopVibration();
    Serial.println("Vibration off");
    server.send(200, "text/plain", "off");
  });

  server.on("/speed", []() {
    if (server.hasArg("value")) {
      setVibrationSpeed(server.arg("value").toInt());
    }
    server.send(200, "text/plain", String(vibrationSpeed));
  });

  server.on("/status", []() {
    String json = "{\"name\":\"goodesp\",\"ip\":\"" + WiFi.localIP().toString() + "\",\"vibration\":" + (vibrationOn ? "true" : "false") + ",\"speed\":" + String(vibrationSpeed) + "}";
    server.send(200, "application/json", json);
  });

  server.begin();
  Serial.println("Web server started.");
  Serial.print("Open: http://");
  Serial.println(WiFi.localIP());
  Serial.println("Or try: http://goodesp.local");
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  ledcSetup(VIBRATION_PWM_CHANNEL, VIBRATION_PWM_FREQ, VIBRATION_PWM_RESOLUTION);
  ledcAttachPin(VIBRATION_PIN, VIBRATION_PWM_CHANNEL);
  stopVibration();

  Serial.println();
  Serial.println("working");
  Serial.println("goodesp");

  if (!connectFromSavedCredentials() && !connectFromGeneratedConfig()) {
    promptForWifiOverSerial();
  }

  if (MDNS.begin(DEVICE_NAME)) {
    MDNS.addService("http", "tcp", 80);
    Serial.println("mDNS started as goodesp.local");
  }

  setupWebServer();
}

void loop() {
  server.handleClient();

  if (vibrationUntilMs != 0 && millis() >= vibrationUntilMs) {
    stopVibration();
    Serial.println("Vibration off");
  }
}
