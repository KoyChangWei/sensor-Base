#include <EEPROM.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h> 

// OLED Display Libraries
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Firebase ESP32 Client
#include <Firebase_ESP_Client.h>

// Addons for Firebase (TokenHelper is usually sufficient for basic RTDB)
#include "addons/TokenHelper.h"

// --- Configuration: EEPROM ---
#define EEPROM_SIZE 128 // Increased size for SSID, Pass, DeviceID
#define SSID_ADDR 0
#define PASS_ADDR 32
#define DEVID_ADDR 64
#define CRED_MAX_LEN 30 // Max length for each credential including null terminator

// --- Configuration: OLED ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32 // Or 64 if you have a 128x64 OLED
#define OLED_RESET -1    // Reset pin # (or -1 if sharing Arduino reset pin)
#define I2C_SDA 21        // Default SDA pin for ESP32
#define I2C_SCL 22       // Default SCL pin for ESP32
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- Configuration: Firebase ---
#define API_KEY "AIzaSyA8YXAwTfFyOYscuWYmDGd5t4jwktkOCH4"
#define DATABASE_URL "https://asg2-a34e6-default-rtdb.firebaseio.com/"
#define USER_EMAIL "koy@gmail.com"
#define USER_PASSWORD "123456"

// Firebase paths to read
#define FB_PATH_OLED_STATUS "/101/OLED/status"
#define FB_PATH_OLED_MESSAGE "/101/OLED/message"
#define FB_PATH_OLED_DEVICE_ID "/101/OLED/device_id" 
#define FB_PATH_CREDENTIAL_STATUS "/101/Credential/status" // New path for reconfiguration

// --- Configuration: System ---
#define AP_MODE_BUTTON_PIN 0 // Button to force AP mode (connect to GND)
#define STATUS_LED_PIN 2     // LED to indicate AP mode or other statuses

// Global variables
WebServer server(80);
String esid, epass, edevid; // Stored credentials from EEPROM
String content;             // For web server HTML
bool apmode = false;        // Flag to indicate if device is in Access Point mode
bool scanComplete = false;  // Flag to prevent repeat scans in AP mode
String scannedNetworks = "<p>Scanning networks...</p>"; // HTML for scanned WiFi

// Firebase global objects
FirebaseData fbdoStream; // For reading data
FirebaseData fbdoSet;    // For setting data (if needed, e.g., error reporting)
FirebaseAuth auth;
FirebaseConfig config_fb;

// Variables to store data fetched from Firebase
String oled_status_fb = "N/A";
String oled_message_fb = "N/A";
String oled_device_id_fb = "N/A";
String credential_status_fb = "1"; // Default to "1" (normal operation)

unsigned long lastFirebaseRead = 0;
const long firebaseReadInterval = 5000; // Read from Firebase every 5 seconds

// --- Function Prototypes ---
void readData();
void writeData(String ssid_w, String pass_w, String devid_w);
void clearData();
void ap_mode();
void launchWeb(int webtype);
void createWebServer(int webtype);
boolean testWifi();
void initOLED();
void updateOLEDDisplay(String line1, String line2, String line3, String line4);
void fetchFirebaseData();
String readStringFromEEPROM(int addrOffset, int len);
void writeStringToEEPROM(int addrOffset, const String& strToWrite, int maxLen);


// ==========================================================================
// SETUP FUNCTION
// ==========================================================================
void setup() {
  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, LOW); // LED OFF initially

  Serial.begin(115200);
  Serial.println("\nStarting IoT Credential & Data Display System...");

  initOLED();
  updateOLEDDisplay("Booting...", "", "", "");

  EEPROM.begin(EEPROM_SIZE);
  readData(); // Read SSID, Password, Device ID from EEPROM

  pinMode(AP_MODE_BUTTON_PIN, INPUT_PULLUP);
  delay(100); // Small delay for pin to stabilize

  if (digitalRead(AP_MODE_BUTTON_PIN) == LOW || esid.length() == 0) {
    Serial.println("Button pressed or no SSID. Forcing AP Mode.");

    // If button pressed and we have WiFi credentials and Firebase is set up, update Firebase status
    if (digitalRead(AP_MODE_BUTTON_PIN) == LOW && esid.length() > 0) {
      // Try to connect to WiFi to update Firebase before entering AP mode
      if (testWifi() && Firebase.ready()) {
        Serial.println("Setting Firebase Credential/status to 0...");
        if (Firebase.RTDB.setString(&fbdoSet, FB_PATH_CREDENTIAL_STATUS, "0")) {
          Serial.println("Firebase Credential/status successfully set to 0");
          updateOLEDDisplay("Button Pressed", "Firebase Updated", "Entering AP Mode", "");
          delay(2000); // Give time for user to see the message
        } else {
          Serial.println("Failed to set Firebase Credential/status: " + fbdoSet.errorReason());
        }
      }
    }

    updateOLEDDisplay("AP Mode...", "Connect to:", "ESP32_Config", "192.168.4.1");
    ap_mode();
  } else {
    updateOLEDDisplay("Connecting WiFi...", esid, "", "Device: " + edevid);
    if (testWifi()) {
      Serial.println("WiFi Connected!");
      digitalWrite(STATUS_LED_PIN, LOW); // LED OFF in normal STA mode
      updateOLEDDisplay("WiFi Connected!", esid, "Device: " + edevid, "FB Init...");

      // Firebase Configuration
      config_fb.api_key = API_KEY;
      config_fb.database_url = DATABASE_URL;
      auth.user.email = USER_EMAIL;
      auth.user.password = USER_PASSWORD;

      // Assign the callback function for token status processing
      config_fb.token_status_callback = tokenStatusCallback; // Required for ESP32

      Firebase.begin(&config_fb, &auth);
      Firebase.reconnectWiFi(true);

      Serial.println("Initializing Firebase...");
      // Wait for Firebase to be ready (initial authentication)
      // This can take a few seconds
      unsigned long firebase_init_start = millis();
      while(Firebase.getToken() == "" && millis() - firebase_init_start < 30000){ // Timeout after 30s
        Serial.print(".");
        delay(500);
        if(!Firebase.ready() && Firebase.isTokenExpired()){
             Serial.println("Token expired during init, trying to refresh.");
             Firebase.refreshToken(&config_fb);
        }
      }

      if(Firebase.ready()){
        Serial.println("\nFirebase connection established.");
        updateOLEDDisplay("Firebase Ready!", "Fetching data...", "", "Device: " + edevid);
        
        // Set credential status to "1" (normal operation) during startup
        if (Firebase.RTDB.setString(&fbdoSet, FB_PATH_CREDENTIAL_STATUS, "1")) {
          Serial.println("Credential status set to '1' (normal operation)");
        } else {
          Serial.println("Failed to set credential status: " + fbdoSet.errorReason());
        }
        
        fetchFirebaseData(); // Initial fetch
      } else {
        Serial.println("\nFirebase connection failed. Check credentials or network.");
        Serial.print("Error: "); Serial.println(fbdoStream.errorReason());
        updateOLEDDisplay("Firebase Failed!", fbdoStream.errorReason().substring(0,15), "", "Check Serial");
      }

    } else {
      Serial.println("Failed to connect to WiFi. Entering AP Mode.");
      updateOLEDDisplay("WiFi Failed.", "Starting AP Mode", "ESP32_Config", "192.168.4.1");
      ap_mode();
    }
  }
}

// ==========================================================================
// MAIN LOOP
// ==========================================================================
void loop() {
  if (apmode) {
    server.handleClient();
    if (!scanComplete) {
      int n = WiFi.scanComplete();
      if (n == -2) { // Scan not triggered
        WiFi.scanNetworks(true, false, false, 300, 0); // Async scan
      } else if (n >= 0) {
        scannedNetworks = "<table border='1' style='width:100%; border-collapse:collapse;'>";
        scannedNetworks += "<tr><th>SSID</th><th>Signal Strength</th><th>Channel</th></tr>";
        for (int i = 0; i < n; ++i) {
          scannedNetworks += "<tr><td>" + WiFi.SSID(i) + "</td><td>" + String(WiFi.RSSI(i)) + " dBm</td><td>" + String(WiFi.channel(i)) + "</td></tr>";
        }
        scannedNetworks += "</table>";
        scanComplete = true;
        WiFi.scanDelete(); // Free memory
        Serial.println("Network scan complete for AP mode.");
      }
    }
  } else { // Normal Operation Mode (Connected to WiFi)
    if (WiFi.status() == WL_CONNECTED && Firebase.ready()) {
      if (millis() - lastFirebaseRead > firebaseReadInterval) {
        fetchFirebaseData();
        
        // Check if credential status is "0" (reconfiguration requested)
        if (credential_status_fb == "0") {
          Serial.println("Remote reconfiguration requested via Firebase!");
          updateOLEDDisplay("Remote Reconfig", "Requested", "Entering AP Mode", "");
          delay(2000); // Give user time to see the message
          ap_mode();    // Enter AP mode
          return;       // Exit the loop iteration
        }
        
        lastFirebaseRead = millis();
      }
    } else if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi disconnected. Attempting to reconnect...");
      updateOLEDDisplay("WiFi Lost!", "Reconnecting...", "", "Device: " + edevid);
      if (!testWifi()) { // Try to reconnect
         Serial.println("Reconnect failed. Entering AP Mode.");
         updateOLEDDisplay("Reconnect Fail", "Starting AP Mode", "ESP32_Config", "192.168.4.1");
         ap_mode(); // Fallback to AP mode if reconnect fails
      } else {
         Serial.println("WiFi Reconnected!");
         updateOLEDDisplay("WiFi Online!", esid, "", "Device: " + edevid);
         // Re-check Firebase connection as WiFi was lost
         if (!Firebase.ready()) {
            Firebase.begin(&config_fb, &auth); // Re-initialize Firebase
         }
      }
    } else if (!Firebase.ready()){ // WiFi connected but Firebase not ready
        Serial.println("Firebase not ready. Checking token...");
        updateOLEDDisplay("Firebase Issue", "Token Refresh?", fbdoStream.errorReason().substring(0,15), "Device: " + edevid);
        if(Firebase.isTokenExpired()){
            Serial.println("Firebase token expired. Refreshing...");
            Firebase.refreshToken(&config_fb);
        } else if (Firebase.getToken() == "") {
            Serial.println("Firebase token is empty. Re-initializing...");
            Firebase.begin(&config_fb, &auth);
        }
        delay(2000); // Wait a bit before next check
    }

    // Update OLED with current data regardless of Firebase fetch interval if not in AP mode
    if (!apmode) {
         String wifiStat = (WiFi.status() == WL_CONNECTED) ? "WiFi: OK" : "WiFi: Fail";
         String fbStat = Firebase.ready() ? "FB: OK" : ("FB: " + fbdoStream.errorReason().substring(0,6));
         updateOLEDDisplay(oled_device_id_fb.substring(0,20), oled_status_fb.substring(0,20), oled_message_fb.substring(0,20), wifiStat + " " + fbStat );
    }
  }
}

// ==========================================================================
// EEPROM FUNCTIONS
// ==========================================================================
String readStringFromEEPROM(int addrOffset, int len) {
  char data[len + 1];
  for (int i = 0; i < len; i++) {
    data[i] = EEPROM.read(addrOffset + i);
  }
  data[len] = '\0'; // Null terminate
  return String(data).c_str(); // Use c_str() to trim trailing non-printable chars
}

void writeStringToEEPROM(int addrOffset, const String& strToWrite, int maxLen) {
  for (int i = 0; i < maxLen; i++) {
    if (i < strToWrite.length()) {
      EEPROM.write(addrOffset + i, strToWrite[i]);
    } else {
      EEPROM.write(addrOffset + i, 0); // Fill remaining space with null terminators
    }
  }
}

void readData() {
  Serial.println("Reading credentials from EEPROM...");
  esid = readStringFromEEPROM(SSID_ADDR, CRED_MAX_LEN -1);
  epass = readStringFromEEPROM(PASS_ADDR, CRED_MAX_LEN -1);
  edevid = readStringFromEEPROM(DEVID_ADDR, CRED_MAX_LEN -1);

  Serial.println("EEPROM - SSID: " + esid);
  Serial.println("EEPROM - Password: " + epass);
  Serial.println("EEPROM - Device ID: " + edevid);
}

void writeData(String ssid_w, String pass_w, String devid_w) {
  Serial.println("Writing to EEPROM...");
  clearData(); // Clear relevant parts before writing

  writeStringToEEPROM(SSID_ADDR, ssid_w, CRED_MAX_LEN -1);
  writeStringToEEPROM(PASS_ADDR, pass_w, CRED_MAX_LEN -1);
  writeStringToEEPROM(DEVID_ADDR, devid_w, CRED_MAX_LEN -1);

  if (EEPROM.commit()) {
    Serial.println("EEPROM successfully committed");
    esid = ssid_w; // Update global vars
    epass = pass_w;
    edevid = devid_w;
  } else {
    Serial.println("ERROR! EEPROM commit failed");
  }
}

void clearData() {
  Serial.println("Clearing EEPROM (SSID, Pass, DevID sections)...");
  for (int i = 0; i < DEVID_ADDR + CRED_MAX_LEN; i++) { // Clear up to end of devid
    EEPROM.write(i, 0);
  }
  EEPROM.commit();
}

// ==========================================================================
// WIFI & WEB SERVER FUNCTIONS (AP MODE)
// ==========================================================================
void ap_mode() {
  apmode = true;
  scanComplete = false; // Reset scan status for AP mode
  digitalWrite(STATUS_LED_PIN, HIGH); // Turn on LED to indicate AP mode
  Serial.println("Setting device to AP Mode.");
  Serial.println("Connect to WiFi: ESP32_Config");
  Serial.println("Then go to http://192.168.4.1 in your browser.");

  Serial.println("Disconnecting existing WiFi connections...");
  WiFi.softAPdisconnect(true); // Disconnect previous AP
  WiFi.disconnect(true);       // Disconnect STA
  delay(200); // Increased delay

  Serial.println("Setting WiFi mode to AP_STA...");
  WiFi.mode(WIFI_AP_STA); // Enable AP + STA for scanning
  delay(200); // Increased delay

  Serial.println("Attempting to start Soft AP: ESP32_Config...");
  bool apStarted = WiFi.softAP("ESP32_Config", "password"); // AP with a simple password
  delay(500); // Increased delay for AP to stabilize

  if (apStarted) {
    Serial.println("Soft AP ESP32_Config started successfully!");
    Serial.print("AP IP address: ");
    Serial.println(WiFi.softAPIP());

    // Start scanning for networks to display on the config page
    WiFi.scanNetworks(true, false, false, 300, 0); // (async, show hidden, passive, max_ms_per_chan, channel)
    launchWeb(0); // Start web server for configuration
  } else {
    Serial.println("!!! Failed to start Soft AP ESP32_Config !!!");
    // Consider updating OLED here to show AP failure
    updateOLEDDisplay("AP Start Failed!", "Check Serial Log", "Device Error", "");

  }
}

void launchWeb(int webtype) {
  Serial.println("Starting Web Server...");
  createWebServer(webtype);
  server.begin();
  Serial.println("Web Server started.");
}

void handleRoot() {
  content = R"rawliteral(
  <!DOCTYPE html>
  <html>
  <head>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>ESP32 WiFi Manager</title>
    <style>
      body {
        font-family: Arial, sans-serif;
        background-color: #f5f5f5;
        color: #333;
        line-height: 1.6;
        padding: 20px;
      }
      .container {
        max-width: 800px;
        margin: 0 auto;
        padding: 20px;
        background: #fff;
        border-radius: 8px;
        box-shadow: 0 2px 5px rgba(0,0,0,0.1);
      }
      h1 {
        color: #2196F3;
        text-align: center;
        margin-bottom: 20px;
      }
      h2 {
        color: #1976D2;
        margin-bottom: 10px;
      }
      .current-settings {
        background: #e3f2fd;
        padding: 15px;
        border-radius: 8px;
        margin-bottom: 20px;
      }
      form {
        display: grid;
        gap: 15px;
      }
      label {
        font-weight: bold;
      }
      input[type="text"],
      input[type="password"] {
        padding: 8px;
        border: 1px solid #ddd;
        border-radius: 4px;
        width: 100%;
      }
      button {
        background-color: #2196F3;
        color: white;
        padding: 10px;
        border: none;
        border-radius: 4px;
        cursor: pointer;
        font-weight: bold;
      }
      .button-danger {
        background-color: #f44336;
      }
      table {
        width: 100%;
        border-collapse: collapse;
        margin-top: 10px;
      }
      th, td {
        padding: 8px;
        text-align: left;
        border-bottom: 1px solid #ddd;
      }
      th {
        background-color: #2196F3;
        color: white;
      }
    </style>
  </head>
  <body>
    <div class="container">
      <h1>ESP32 WiFi Manager</h1>
      
      <div class="current-settings">
        <h2>Current Settings</h2>
        <p><strong>WiFi SSID:</strong> )rawliteral" + esid + R"rawliteral(</p>
        <p><strong>WiFi Password:</strong> Present (hidden)</p>
        <p><strong>Device ID:</strong> )rawliteral" + edevid + R"rawliteral(</p>
      </div>

      <form method='get' action='/setting'>
        <div>
          <label for='ssid'>WiFi SSID:</label>
          <input type='text' id='ssid' name='ssid' value=')rawliteral" + esid + R"rawliteral(' required>
        </div>
        
        <div>
          <label for='password'>WiFi Password:</label>
          <input type='password' id='password' name='password'>
          <small>Leave blank to keep current password.</small>
        </div>
        
        <div>
          <label for='devid'>Device ID:</label>
          <input type='text' id='devid' name='devid' value=')rawliteral" + edevid + R"rawliteral(' required>
        </div>
        
        <button type='submit'>Save Settings & Restart</button>
      </form>

      <form method='get' action='/clear' style="margin-top: 15px;">
        <button type='submit' class='button-danger'>Clear All Settings & Restart</button>
      </form>

      <div style="margin-top: 20px;">
        <h3>Available WiFi Networks</h3>
        )rawliteral" + scannedNetworks + R"rawliteral(
      </div>
    </div>
  </body>
  </html>
  )rawliteral";
  server.send(200, "text/html", content);
}

void handleSetting() {
  String new_ssid = server.arg("ssid");
  String new_pass = server.arg("password");
  String new_devid = server.arg("devid");

  String final_pass = epass; // Default to old password
  if (new_pass.length() > 0) { // If new password is provided
    final_pass = new_pass;
  }

  writeData(new_ssid, final_pass, new_devid);

  String response_html = R"rawliteral(
  <!DOCTYPE html><html><head><title>Settings Saved</title><meta http-equiv="refresh" content="5;url=/" />
  <style>body{font-family: Arial, sans-serif; text-align:center; padding-top:50px;}</style></head>
  <body><h2>Settings Saved!</h2><p>SSID: )rawliteral" + new_ssid + R"rawliteral(<br>Device ID: )rawliteral" + new_devid + R"rawliteral(</p>
  <p>Device will restart in 5 seconds to apply settings.</p></body></html>)rawliteral";
  server.send(200, "text/html", response_html);

  delay(1000); // Give client time to receive response
  Serial.println("Restarting ESP to apply settings...");
  ESP.restart();
}

void handleClear() {
  clearData();
  esid = ""; epass = ""; edevid = ""; // Clear global vars
  String response_html = R"rawliteral(
  <!DOCTYPE html><html><head><title>Settings Cleared</title><meta http-equiv="refresh" content="5;url=/" />
  <style>body{font-family: Arial, sans-serif; text-align:center; padding-top:50px;}</style></head>
  <body><h2>All Settings Cleared!</h2><p>Device will restart in 5 seconds. Please reconnect to AP mode to reconfigure.</p></body></html>)rawliteral";
  server.send(200, "text/html", response_html);
  delay(1000);
  Serial.println("Restarting ESP after clearing settings...");
  ESP.restart();
}

void createWebServer(int webtype) {
  if (webtype == 0) { // Main configuration page
    server.on("/", HTTP_GET, handleRoot);
    server.on("/setting", HTTP_GET, handleSetting);
    server.on("/clear", HTTP_GET, handleClear);
    server.onNotFound([]() {
      server.send(404, "text/plain", "Not found");
    });
  }
}

boolean testWifi() {
  if (esid.length() == 0) {
    Serial.println("No SSID configured.");
    return false;
  }
  Serial.println("Connecting to WiFi: " + esid);
  WiFi.softAPdisconnect(true);
  WiFi.disconnect(true);
  delay(100);
  WiFi.mode(WIFI_STA);
  delay(100);
  WiFi.begin(esid.c_str(), epass.c_str());

  int c = 0;
  while (WiFi.status() != WL_CONNECTED && c < 30) { // Try for ~15 seconds
    Serial.print(".");
    delay(500);
    c++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected. IP address: " + WiFi.localIP().toString());
    WiFi.setAutoReconnect(true);
    WiFi.persistent(true);
    return true;
  } else {
    Serial.println("\nFailed to connect to WiFi.");
    return false;
  }
}

// ==========================================================================
// OLED FUNCTIONS
// ==========================================================================
void initOLED() {
  Wire.begin(I2C_SDA, I2C_SCL); // Initialize I2C
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3C for 128x32
    Serial.println(F("SSD1306 allocation failed"));
    for (;;); // Don't proceed, loop forever
  }
  Serial.println("OLED Initialized.");
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("OLED Init OK!");
  display.display();
  delay(1000);
}

void updateOLEDDisplay(String line1 = "", String line2 = "", String line3 = "", String line4 = "") {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  
  display.setCursor(0, 0);
  display.println(line1);
  
  display.setCursor(0, 8); // Next line for 128x32 (height / 4 lines)
  display.println(line2);
  
  display.setCursor(0, 16);
  display.println(line3);

  display.setCursor(0, 24);
  display.println(line4);
  
  display.display();
}

// ==========================================================================
// FIREBASE FUNCTIONS
// ==========================================================================
void fetchFirebaseData() {
  if (WiFi.status() == WL_CONNECTED && Firebase.ready()) {
    Serial.println("------------------------------------");
    Serial.println("Fetching data from Firebase...");

    // Fetch OLED Status
    if (Firebase.RTDB.getString(&fbdoStream, FB_PATH_OLED_STATUS)) {
      if (fbdoStream.dataTypeEnum() == fb_esp_rtdb_data_type_string) {
        oled_status_fb = fbdoStream.stringData();
        Serial.println("FB Status: " + oled_status_fb);
      } else {
         Serial.println("FB Status: Not a string or null. Type: " + fbdoStream.dataType());
         oled_status_fb = "FB:Not String";
      }
    } else {
      Serial.println("Failed to get FB Status: " + fbdoStream.errorReason());
      oled_status_fb = "FB:ErrRead";
    }

    // Fetch OLED Message
    if (Firebase.RTDB.getString(&fbdoStream, FB_PATH_OLED_MESSAGE)) {
       if (fbdoStream.dataTypeEnum() == fb_esp_rtdb_data_type_string) {
        oled_message_fb = fbdoStream.stringData();
        Serial.println("FB Message: " + oled_message_fb);
      } else {
         Serial.println("FB Message: Not a string or null. Type: " + fbdoStream.dataType());
         oled_message_fb = "FB:Not String";
      }
    } else {
      Serial.println("Failed to get FB Message: " + fbdoStream.errorReason());
      oled_message_fb = "FB:ErrRead";
    }

    // Fetch OLED Device ID from Firebase
    if (Firebase.RTDB.getString(&fbdoStream, FB_PATH_OLED_DEVICE_ID)) {
       if (fbdoStream.dataTypeEnum() == fb_esp_rtdb_data_type_string) {
        oled_device_id_fb = fbdoStream.stringData();
        Serial.println("FB Device ID: " + oled_device_id_fb);
      } else {
         Serial.println("FB Device ID: Not a string or null. Type: " + fbdoStream.dataType());
         oled_device_id_fb = "FB:Not String";
      }
    } else {
      Serial.println("Failed to get FB Device ID: " + fbdoStream.errorReason());
      oled_device_id_fb = "FB:ErrRead";
    }

    // Fetch credential status
    if (Firebase.RTDB.getString(&fbdoStream, FB_PATH_CREDENTIAL_STATUS)) {
      if (fbdoStream.dataTypeEnum() == fb_esp_rtdb_data_type_string) {
        credential_status_fb = fbdoStream.stringData();
        Serial.println("Credential Status: " + credential_status_fb);
      } else {
        Serial.println("Credential Status: Not a string or null. Type: " + fbdoStream.dataType());
        credential_status_fb = "1"; // Reset to normal if invalid
      }
    } else {
      Serial.println("Failed to get Credential Status: " + fbdoStream.errorReason());
      // Don't change credential_status_fb here to avoid false triggers
    }
    
    Serial.println("------------------------------------");

  } else {
    Serial.println("Cannot fetch Firebase data: WiFi or Firebase not ready.");
    if(WiFi.status() != WL_CONNECTED) Serial.println(" - WiFi Disconnected");
    if(!Firebase.ready()) Serial.println(" - Firebase Not Ready. Reason: " + fbdoStream.errorReason());
    oled_status_fb = "Net/FB Err";
    oled_message_fb = "Check Conn.";
    oled_device_id_fb = "N/A";
  }
  // The display will be updated in the main loop
}