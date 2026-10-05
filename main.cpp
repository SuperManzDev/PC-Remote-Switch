#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

// --- WIFI CREDENTIALS ---
const char* ssid = "ATTRKbdFEa ext 2.4";
const char* password = "YOUR_PASSWORD_HERE";

// --- GPIO PIN ASSIGNMENTS ---
const int PIN_POWER_SW  = 22; // Motherboard Power (+)
const int PIN_RESET_SW  = 23; // Motherboard Reset (+)
const int PIN_POWER_LED = 34; // Motherboard Power LED (+) [Input Only]
const int PIN_BLUE_LED  = 2;  // Onboard Blue LED
const int PIN_RED_LED   = 18; // External Red LED (Moved to GPIO 18 to prevent boot strapping freeze!)

WebServer server(80);

// --- SYSTEM STATES & SHARED VARIABLES ---
enum PcState { STATE_OFF, STATE_ON, STATE_SLEEP };
volatile PcState currentPcState = STATE_OFF;
volatile bool safetyLock = false;

// Async Action & LED Flags for Core 0
volatile int pendingActionPin = -1;
volatile int pendingActionDuration = 0;
volatile bool triggerApiFlash = false;
String lastActionName = "";

// Rolling Event Log
const int MAX_LOGS = 8;
String eventLogs[MAX_LOGS];
int logIndex = 0;

void addLog(String message) {
  unsigned long sec = millis() / 1000;
  String entry = "[" + String(sec) + "s] " + message;
  eventLogs[logIndex] = entry;
  logIndex = (logIndex + 1) % MAX_LOGS;
  Serial.println(entry);
}

// Helper to print debug info over Serial
void printSerialDebug() {
  Serial.println("\n==============================================");
  Serial.println("  🖥️  ESP32 DUAL-CORE PC CONTROLLER ONLINE");
  Serial.println("==============================================");
  Serial.print("  🌐 IP Address : ");
  Serial.println("http://" + WiFi.localIP().toString());
  Serial.print("  📡 Wi-Fi RSSI : ");
  Serial.print(WiFi.RSSI());
  Serial.println(" dBm");
  Serial.print("  ⚡ PC State   : ");
  Serial.println((currentPcState == STATE_ON) ? "ON" : (currentPcState == STATE_SLEEP) ? "SLEEPING" : "OFF");
  Serial.print("  🔒 Safety Lock: ");
  Serial.println(safetyLock ? "LOCKED" : "UNLOCKED");
  Serial.println("==============================================\n");
}

// ============================================================================
// CORE 0: HARDWARE MONITORING, SWITCH ACTUATION & LED ENGINE
// ============================================================================
void TaskHardware(void * pvParameters) {
  unsigned long lastLedSampleTime = 0;
  unsigned long lastWifiLedTime = 0;
  int ledHighCount = 0;
  int ledLowCount = 0;
  unsigned long sampleWindowStart = millis();
  bool blueLedState = false;

  for (;;) { 
    unsigned long currentMillis = millis();

    // 1. HANDLE API ACTIVITY LED FLASH (Red & Blue Blink)
    if (triggerApiFlash) {
      triggerApiFlash = false;
      for (int i = 0; i < 4; i++) {
        digitalWrite(PIN_BLUE_LED, HIGH);
        digitalWrite(PIN_RED_LED, LOW);
        vTaskDelay(pdMS_TO_TICKS(60));
        digitalWrite(PIN_BLUE_LED, LOW);
        digitalWrite(PIN_RED_LED, HIGH);
        vTaskDelay(pdMS_TO_TICKS(60));
      }
      digitalWrite(PIN_RED_LED, LOW); 
    }

    // 2. HANDLE PENDING SWITCH ACTIONS (Open-Drain Emulation)
    if (pendingActionPin != -1) {
      int pin = pendingActionPin;
      int duration = pendingActionDuration;
      pendingActionPin = -1; 

      pinMode(pin, OUTPUT);
      digitalWrite(pin, LOW); // Pull to GND (Press)
      vTaskDelay(pdMS_TO_TICKS(duration));
      pinMode(pin, INPUT);    // Float High-Z (Release)
      
      addLog("Executed: " + lastActionName);
    }

    // 3. WI-FI STATUS LED ENGINE (Blue LED)
    if (WiFi.status() == WL_CONNECTED) {
      digitalWrite(PIN_BLUE_LED, HIGH);
    } 
    else if (WiFi.status() == WL_DISCONNECTED || WiFi.status() == WL_IDLE_STATUS) {
      if (currentMillis - lastWifiLedTime >= 500) {
        lastWifiLedTime = currentMillis;
        blueLedState = !blueLedState;
        digitalWrite(PIN_BLUE_LED, blueLedState ? HIGH : LOW);
      }
    } 
    else {
      if (currentMillis - lastWifiLedTime >= 80) {
        lastWifiLedTime = currentMillis;
        blueLedState = !blueLedState;
        digitalWrite(PIN_BLUE_LED, blueLedState ? HIGH : LOW);
      }
    }

    // 4. SAMPLE POWER LED (Detects ON, OFF, and SLEEP pulsing)
    if (currentMillis - lastLedSampleTime >= 20) { 
      lastLedSampleTime = currentMillis;
      if (digitalRead(PIN_POWER_LED) == HIGH) {
        ledHighCount++;
      } else {
        ledLowCount++;
      }
    }

    if (currentMillis - sampleWindowStart >= 1500) {
      PcState newState;
      if (ledHighCount > 0 && ledLowCount > 0) {
        newState = STATE_SLEEP; 
      } else if (ledHighCount > ledLowCount) {
        newState = STATE_ON;    
      } else {
        newState = STATE_OFF;   
      }

      if (newState != currentPcState) {
        currentPcState = newState;
        String stateStr = (newState == STATE_ON) ? "ON" : (newState == STATE_SLEEP) ? "SLEEPING" : "OFF";
        addLog("PC State Changed -> " + stateStr);
      }

      ledHighCount = 0;
      ledLowCount = 0;
      sampleWindowStart = currentMillis;
    }

    vTaskDelay(pdMS_TO_TICKS(10)); 
  }
}

bool triggerSwitchAction(int pin, int durationMs, String actionName) {
  triggerApiFlash = true; 
  if (safetyLock) {
    addLog("Blocked: " + actionName + " (Safety Lock ON)");
    return false;
  }
  lastActionName = actionName;
  pendingActionDuration = durationMs;
  pendingActionPin = pin; 
  return true;
}

// ============================================================================
// CORE 1: WEB SERVER & REST API HANDLERS
// ============================================================================

void handleApiStatus() {
  triggerApiFlash = true; 
  String stateStr = (currentPcState == STATE_ON) ? "ON" : (currentPcState == STATE_SLEEP) ? "SLEEP" : "OFF";
  String json = "{";
  json += "\"state\":\"" + stateStr + "\",";
  json += "\"lock\":" + String(safetyLock ? "true" : "false") + ",";
  json += "\"uptime\":" + String(millis() / 1000) + ",";
  json += "\"logs\":[";
  
  bool first = true;
  for (int i = 0; i < MAX_LOGS; i++) {
    int idx = (logIndex + i) % MAX_LOGS;
    if (eventLogs[idx].length() > 0) {
      if (!first) json += ",";
      json += "\"" + eventLogs[idx] + "\"";
      first = false;
    }
  }
  json += "]}";
  server.send(200, "application/json", json);
}

void handleApiPower() {
  bool success = triggerSwitchAction(PIN_POWER_SW, 500, "Power Toggle");
  server.send(success ? 200 : 403, "application/json", success ? "{\"status\":\"success\"}" : "{\"status\":\"locked\"}");
}

void handleApiReset() {
  bool success = triggerSwitchAction(PIN_RESET_SW, 500, "System Reset");
  server.send(success ? 200 : 403, "application/json", success ? "{\"status\":\"success\"}" : "{\"status\":\"locked\"}");
}

void handleApiForceOff() {
  bool success = triggerSwitchAction(PIN_POWER_SW, 5000, "Force Shutdown");
  server.send(success ? 200 : 403, "application/json", success ? "{\"status\":\"success\"}" : "{\"status\":\"locked\"}");
}

void handleApiToggleLock() {
  triggerApiFlash = true;
  safetyLock = !safetyLock;
  addLog(String("Safety Lock -> ") + (safetyLock ? "ENABLED" : "DISABLED"));
  server.send(200, "application/json", "{\"lock\":" + String(safetyLock ? "true" : "false") + "}");
}

// --- WEB UI HTML/CSS/JS ---
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP32 Dual-Core PC Controller</title>
  <style>
    :root { --bg: #0f172a; --card: #1e293b; --text: #f8fafc; --accent: #3b82f6; --danger: #ef4444; --success: #22c55e; --warn: #eab308; }
    body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; background: var(--bg); color: var(--text); display: flex; justify-content: center; align-items: center; min-height: 100vh; margin: 0; padding: 20px; }
    .dashboard { background: var(--card); padding: 2rem; border-radius: 20px; box-shadow: 0 20px 25px -5px rgba(0,0,0,0.5); width: 100%; max-width: 440px; border: 1px solid #334155; }
    h1 { margin: 0 0 0.5rem 0; font-size: 1.4rem; font-weight: 700; text-align: center; }
    .subtitle { font-size: 0.8rem; color: #94a3b8; text-align: center; margin-bottom: 1.5rem; text-transform: uppercase; letter-spacing: 1px; }
    .status-panel { display: flex; justify-content: space-between; align-items: center; background: #0f172a; padding: 12px 16px; border-radius: 12px; margin-bottom: 1.5rem; border: 1px solid #334155; }
    .badge { display: flex; align-items: center; gap: 8px; font-weight: 700; font-size: 0.9rem; }
    .dot { width: 12px; height: 12px; border-radius: 50%; background: #64748b; transition: all 0.3s; }
    .dot.ON { background: var(--success); box-shadow: 0 0 12px var(--success); }
    .dot.OFF { background: var(--danger); }
    .dot.SLEEP { background: var(--warn); box-shadow: 0 0 12px var(--warn); animation: pulse 1.5s infinite; }
    @keyframes pulse { 0%, 100% { opacity: 1; } 50% { opacity: 0.3; } }
    .lock-btn { background: transparent; border: 1px solid #475569; color: #cbd5e1; padding: 6px 12px; border-radius: 8px; font-size: 0.8rem; cursor: pointer; transition: all 0.2s; }
    .lock-btn.locked { background: rgba(239, 68, 68, 0.2); border-color: var(--danger); color: #fca5a5; }
    .btn-group { display: flex; flex-direction: column; gap: 10px; margin-bottom: 1.5rem; }
    button.action-btn { background: #334155; color: var(--text); border: none; padding: 14px; border-radius: 12px; font-size: 1rem; font-weight: 600; cursor: pointer; transition: all 0.2s; }
    button.action-btn:hover:not(:disabled) { background: #475569; transform: translateY(-1px); }
    button.action-btn:disabled { opacity: 0.4; cursor: not-allowed; }
    .btn-primary { background: var(--accent) !important; }
    .btn-primary:hover:not(:disabled) { background: #2563eb !important; }
    .btn-danger { background: rgba(239, 68, 68, 0.15) !important; color: #f87171 !important; border: 1px solid rgba(239, 68, 68, 0.3) !important; }
    .btn-danger:hover:not(:disabled) { background: rgba(239, 68, 68, 0.25) !important; }
    .log-panel { background: #0f172a; border-radius: 12px; padding: 12px; font-family: monospace; font-size: 0.75rem; color: #94a3b8; max-height: 120px; overflow-y: auto; border: 1px solid #334155; }
    .log-title { font-size: 0.7rem; text-transform: uppercase; color: #64748b; margin-bottom: 6px; font-weight: bold; }
    .log-entry { margin-bottom: 4px; border-bottom: 1px solid #1e293b; padding-bottom: 2px; }
  </style>
</head>
<body>
  <div class="dashboard">
    <h1>Desktop Controller</h1>
    <div class="subtitle">Dual-Core FreeRTOS Engine</div>
    
    <div class="status-panel">
      <div class="badge">
        <div id="statusDot" class="dot"></div>
        <span id="statusText">Connecting...</span>
      </div>
      <button id="lockBtn" class="lock-btn" onclick="toggleLock()">🔓 Unlocked</button>
    </div>

    <div class="btn-group">
      <button id="btnPower" class="action-btn btn-primary" onclick="sendAction('/api/power')">Power Toggle</button>
      <button id="btnReset" class="action-btn" onclick="sendAction('/api/reset')">Restart System</button>
      <button id="btnForce" class="action-btn btn-danger" onclick="sendAction('/api/force-off')">Force Shutdown (Hold 5s)</button>
    </div>

    <div class="log-panel">
      <div class="log-title">System Activity Log</div>
      <div id="logContainer">Loading logs...</div>
    </div>
  </div>

  <script>
    let isLocked = false;

    async function updateStatus() {
      try {
        const res = await fetch('/api/status');
        const data = await res.json();
        
        const dot = document.getElementById('statusDot');
        const text = document.getElementById('statusText');
        dot.className = 'dot ' + data.state;
        text.innerText = 'System ' + data.state;

        isLocked = data.lock;
        const lockBtn = document.getElementById('lockBtn');
        lockBtn.className = isLocked ? 'lock-btn locked' : 'lock-btn';
        lockBtn.innerText = isLocked ? '🔒 Locked' : '🔓 Unlocked';

        document.querySelectorAll('.action-btn').forEach(btn => btn.disabled = isLocked);

        const logBox = document.getElementById('logContainer');
        if (data.logs && data.logs.length > 0) {
          logBox.innerHTML = data.logs.slice().reverse().map(l => `<div class="log-entry">${l}</div>`).join('');
        } else {
          logBox.innerHTML = '<div class="log-entry">No events recorded yet.</div>';
        }
      } catch (e) {
        document.getElementById('statusText').innerText = 'Disconnected';
        document.getElementById('statusDot').className = 'dot';
      }
    }

    async function sendAction(endpoint) {
      if (isLocked) return;
      try {
        const res = await fetch(endpoint, { method: 'POST' });
        if (res.status === 403) alert('Action rejected: Safety Lock is ENABLED.');
        setTimeout(updateStatus, 500);
      } catch (e) { alert('Command failed.'); }
    }

    async function toggleLock() {
      await fetch('/api/lock', { method: 'POST' });
      updateStatus();
    }

    setInterval(updateStatus, 2000);
    updateStatus();
  </script>
</body>
</html>
)rawliteral";

// ============================================================================
// SETUP & MAIN LOOP (CORE 1)
// ============================================================================
void setup() {
  Serial.begin(115200);
  
  // Setup LEDs as OUTPUT
  pinMode(PIN_BLUE_LED, OUTPUT);
  pinMode(PIN_RED_LED, OUTPUT);
  digitalWrite(PIN_BLUE_LED, LOW);
  digitalWrite(PIN_RED_LED, LOW);

  // High-impedance open-circuit setup for motherboard lines
  pinMode(PIN_POWER_SW, INPUT);
  pinMode(PIN_RESET_SW, INPUT);
  pinMode(PIN_POWER_LED, INPUT);

  addLog("System Booting...");

  // --- LAUNCH CORE 0 TASK ---
  xTaskCreatePinnedToCore(
    TaskHardware,    
    "HardwareTask",  
    4096,            
    NULL,            
    1,               
    NULL,            
    0                
  );

  // --- CONNECT WIFI ON CORE 1 ---
  Serial.print("\nConnecting to Wi-Fi: ");
  Serial.println(ssid);
  
  // BROWNOUT PREVENTION HACK:
  // Throttles radio TX power to prevent high-current voltage sags on USB adapters!
  WiFi.setMode(WIFI_STA);
  WiFi.setTxPower(WIFI_POWER_8_5dBm); 
  
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  // Print high-visibility IP banner on boot!
  printSerialDebug();
  addLog("WiFi OK -> " + WiFi.localIP().toString());

  // --- SETUP HTTP ROUTES ---
  server.on("/", HTTP_GET, []() { server.send(200, "text/html", INDEX_HTML); });
  server.on("/api/status", HTTP_GET, handleApiStatus);
  server.on("/api/power", HTTP_POST, handleApiPower);
  server.on("/api/reset", HTTP_POST, handleApiReset);
  server.on("/api/force-off", HTTP_POST, handleApiForceOff);
  server.on("/api/lock", HTTP_POST, handleApiToggleLock);

  server.begin();
}

void loop() {
  server.handleClient();

  // --- THE "ANYTIME" IP FETCHER ---
  // If you open Serial Monitor late and see a blank screen, just hit Enter!
  if (Serial.available() > 0) {
    Serial.readString(); // Clear out whatever letter/Enter key you typed
    printSerialDebug();  // Re-print the IP Address and system status box!
  }

  delay(2); 
}