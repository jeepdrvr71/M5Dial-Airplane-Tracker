#include <M5Dial.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// --- WI-FI CONFIGURATION ---
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// --- OPENSKY API CREDENTIALS (OAUTH2) ---
// Find these in your OpenSky Account page under "API Client". You can get these credentials when you create an account.
const char* clientID = "YOUR_CLIENT_ID";    
const char* clientSecret = "YOUR_CLIENT_SECRET";

// --- FALLBACK RADAR CENTER ---
// Used ONLY if the auto-location API fails
float centerLat = 42.524;   //Example: Wixom, MI. Change these coordinates to default to your location if autolocate fails
float centerLon = -83.536;

// --- RADAR ZOOM SETTINGS (IN MILES) ---
float zoomRadius = 100.0 / 69.0; // Start at a 100-mile radius
float minZoom = 30.0 / 69.0;     // NEW Minimum 30 miles
float maxZoom = 300.0 / 69.0;    // NEW Maximum 300 miles
float zoomStep = 10.0 / 69.0;    // Change by 10 miles per dial click
long oldEncoderPos = 0;

// Zoom UI variables
unsigned long zoomFlashTimer = 0;
bool showZoomFlash = false;

// Timers & Status
unsigned long lastUpdate = 0;
const unsigned long updateInterval = 10000; 
unsigned long lastFrame = 0;
String apiStatus = "Waiting for data...";

// --- OAUTH2 TOKEN VARIABLES ---
String accessToken = "";
unsigned long lastTokenFetch = 0;
unsigned long tokenLifetime = 0;

// --- EXPANDED PLANE DATA STRUCTURE ---
struct Plane {
  String callsign;
  float lat;
  float lon;
  float altitude; 
  String country; 
  int screenX;
  int screenY;
};

const int MAX_PLANES = 50; 
Plane planes[MAX_PLANES];
int planeCount = 0;
int selectedPlaneIndex = -1; 

void setup() {
  auto cfg = M5.config();
  M5Dial.begin(cfg, true, true);
  M5Dial.Display.setBrightness(128);
  
  M5Dial.Display.fillScreen(TFT_BLACK);
  M5Dial.Display.setTextDatum(middle_center);
  M5Dial.Display.drawString("Connecting Wi-Fi...", 120, 120);

  // --- WI-FI CONNECTION LOGIC ---
  WiFi.mode(WIFI_STA); 
  WiFi.disconnect(); 
  delay(100);

  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() != WL_CONNECTED) {
    M5Dial.Display.fillScreen(TFT_RED);
    M5Dial.Display.drawString("Wi-Fi Failed!", 120, 120);
    while (true) { delay(100); } 
  }
  
  M5Dial.Display.fillScreen(TFT_BLACK);
  M5Dial.Display.drawString("Wi-Fi Connected!", 120, 120);
  delay(1000);
  
  // --- NEW: AUTO-LOCATE USING IP ADDRESS ---
  getGeolocation();
  
  oldEncoderPos = M5Dial.Encoder.read();
  
  // Fetch our first VIP Token on startup
  refreshAccessToken();
  fetchFlightData();
}

void loop() {
  M5Dial.update();

  // 1. Handle Zoom (Rotary Encoder)
  long newEncoderPos = M5Dial.Encoder.read();
  if (newEncoderPos != oldEncoderPos) {
    if (newEncoderPos > oldEncoderPos) {
      zoomRadius -= zoomStep; 
    } else {
      zoomRadius += zoomStep; 
    }
    if (zoomRadius < minZoom) zoomRadius = minZoom;
    if (zoomRadius > maxZoom) zoomRadius = maxZoom;
    
    oldEncoderPos = newEncoderPos;
    selectedPlaneIndex = -1; 
    
    showZoomFlash = true;
    zoomFlashTimer = millis();
  }

  // 2. Handle Touch (Select Plane)
  auto t = M5Dial.Touch.getDetail();
  if (t.wasPressed()) {
    int touchX = t.x;
    int touchY = t.y;
    selectedPlaneIndex = -1; 
    
    for (int i = 0; i < planeCount; i++) {
      int dx = touchX - planes[i].screenX;
      int dy = touchY - planes[i].screenY;
      if (dx*dx + dy*dy < 225) { 
        selectedPlaneIndex = i;
        break;
      }
    }
  }

  // 3. Handle Periodic API Updates
  if (millis() - lastUpdate > updateInterval) {
    fetchFlightData();
    lastUpdate = millis();
  }

  // 4. Handle Animation (Redraw screen)
  if (millis() - lastFrame > 33) {
    drawRadar();
    lastFrame = millis();
  }
}

// --- NEW FUNCTION: IP Geolocation ---
void getGeolocation() {
  M5Dial.Display.fillScreen(TFT_BLACK);
  M5Dial.Display.drawString("Finding Location...", 120, 120);

  HTTPClient http;
  // ip-api is a free service that returns location based on your IP address
  http.begin("http://ip-api.com/json/"); 
  int httpCode = http.GET();

  if (httpCode == 200) {
    String payload = http.getString();
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, payload);

    if (!error && !doc["lat"].isNull() && !doc["lon"].isNull()) {
      centerLat = doc["lat"].as<float>();
      centerLon = doc["lon"].as<float>();
      
      M5Dial.Display.fillScreen(TFT_DARKGREEN);
      M5Dial.Display.setTextColor(TFT_WHITE);
      M5Dial.Display.drawString("Location Found!", 120, 100);
      M5Dial.Display.drawString(doc["city"].as<String>(), 120, 130);
      delay(1500);
    } else {
      M5Dial.Display.fillScreen(TFT_MAROON);
      M5Dial.Display.drawString("Loc. Parse Failed", 120, 120);
      delay(1500);
    }
  } else {
    M5Dial.Display.fillScreen(TFT_MAROON);
    M5Dial.Display.drawString("Loc. API Failed", 120, 120);
    delay(1500);
  }
  
  M5Dial.Display.setTextColor(TFT_LIGHTGREY); // Reset text color for radar
  http.end();
}

// --- Securely fetch an OAuth2 Token ---
void refreshAccessToken() {
  apiStatus = "Fetching Token...";
  drawRadar();
  
  WiFiClientSecure client;
  client.setInsecure(); 
  
  HTTPClient http;
  http.begin(client, "https://auth.opensky-network.org/auth/realms/opensky-network/protocol/openid-connect/token");
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  
  String payload = "grant_type=client_credentials&client_id=" + String(clientID) + "&client_secret=" + String(clientSecret);
  
  int httpCode = http.POST(payload);
  
  if (httpCode == 200) {
    DynamicJsonDocument doc(4096);
    DeserializationError error = deserializeJson(doc, http.getStream());
    
    if (!error) {
      accessToken = doc["access_token"].as<String>();
      long expiresIn = doc["expires_in"].as<long>(); 
      
      tokenLifetime = (expiresIn > 60) ? ((expiresIn - 30) * 1000) : (expiresIn * 1000); 
      lastTokenFetch = millis();
      
      apiStatus = "Token Authenticated!";
    } else {
      apiStatus = "Token JSON Failed";
      accessToken = "";
    }
  } else {
    apiStatus = "Token Err: " + String(httpCode);
    accessToken = "";
  }
  http.end();
}

void fetchFlightData() {
  if (WiFi.status() != WL_CONNECTED) {
    apiStatus = "No Wi-Fi!";
    return;
  }

  if (accessToken == "" || (millis() - lastTokenFetch > tokenLifetime)) {
    refreshAccessToken();
  }
  
  if (accessToken == "") return;

  float lamin = centerLat - zoomRadius;
  float lamax = centerLat + zoomRadius;
  float lomin = centerLon - zoomRadius;
  float lomax = centerLon + zoomRadius;

  String url = "https://opensky-network.org/api/states/all?lamin=" + String(lamin, 4) + 
               "&lomin=" + String(lomin, 4) + "&lamax=" + String(lamax, 4) + "&lomax=" + String(lomax, 4);

  WiFiClientSecure client;
  client.setInsecure(); 
  client.setTimeout(15); 

  HTTPClient http;
  http.begin(client, url); 
  
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setUserAgent("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/114.0.0.0 Safari/537.36"); 
  http.setTimeout(15000);
  http.addHeader("Authorization", "Bearer " + accessToken);
  
  int httpCode = http.GET();

  if (httpCode == 200) {
    String payload = http.getString();
    DynamicJsonDocument doc(49152); 
    DeserializationError error = deserializeJson(doc, payload);

    if (!error) {
      planeCount = 0;
      
      if (doc["states"].isNull()) {
         apiStatus = "Planes: 0";
      } else {
        JsonArray states = doc["states"].as<JsonArray>();
        
        for (JsonVariant state : states) {
          if (planeCount >= MAX_PLANES) break; 
          
          if (!state[5].isNull() && !state[6].isNull()) {
            planes[planeCount].callsign = state[1].as<String>();
            planes[planeCount].callsign.trim();
            if (planes[planeCount].callsign == "") planes[planeCount].callsign = "N/A";
            
            planes[planeCount].lon = state[5].as<float>();
            planes[planeCount].lat = state[6].as<float>();
            
            planes[planeCount].altitude = state[7].isNull() ? 0 : state[7].as<float>();
            
            planes[planeCount].country = state[2].as<String>();
            planes[planeCount].country.trim();
            if (planes[planeCount].country == "") planes[planeCount].country = "Unknown";
            
            planeCount++;
          }
        }
        apiStatus = "Planes: " + String(planeCount);
      }
    } else {
      apiStatus = "JSON: " + String(error.c_str());
    }
  } else {
    apiStatus = "API Error: " + String(httpCode);
  }
  http.end();
}

void drawRadar() {
  M5Dial.Display.fillScreen(TFT_BLACK);
  
  M5Dial.Display.drawCircle(120, 120, 40, TFT_DARKGREY);
  M5Dial.Display.drawCircle(120, 120, 80, TFT_DARKGREY);
  M5Dial.Display.drawCircle(120, 120, 119, TFT_DARKGREY);
  
  M5Dial.Display.drawLine(115, 120, 125, 120, TFT_GREEN);
  M5Dial.Display.drawLine(120, 115, 120, 125, TFT_GREEN);

  float sweepAngle = (millis() % 3000) / 3000.0 * 2.0 * PI;
  int lineX = 120 + cos(sweepAngle) * 119;
  int lineY = 120 + sin(sweepAngle) * 119;
  M5Dial.Display.drawLine(120, 120, lineX, lineY, TFT_DARKGREEN);

  for (int i = 0; i < planeCount; i++) {
    float lonDiff = planes[i].lon - centerLon;
    float latDiff = planes[i].lat - centerLat;

    planes[i].screenX = 120 + (lonDiff / zoomRadius) * 120;
    planes[i].screenY = 120 - (latDiff / zoomRadius) * 120; 

    if (planes[i].screenX > 0 && planes[i].screenX < 240 && planes[i].screenY > 0 && planes[i].screenY < 240) {
      uint16_t pColor = (i == selectedPlaneIndex) ? TFT_RED : TFT_YELLOW;
      M5Dial.Display.fillCircle(planes[i].screenX, planes[i].screenY, 3, pColor);
    }
  }

  M5Dial.Display.setTextColor(TFT_LIGHTGREY);
  M5Dial.Display.setTextDatum(bottom_center);
  M5Dial.Display.drawString(apiStatus, 120, 230);

  // --- EXPANDED INFO POPUP ---
  if (selectedPlaneIndex != -1) {
    Plane p = planes[selectedPlaneIndex];
    
    M5Dial.Display.fillRect(30, 40, 180, 70, TFT_DARKCYAN);
    M5Dial.Display.drawRect(30, 40, 180, 70, TFT_WHITE);
    
    M5Dial.Display.setTextColor(TFT_WHITE, TFT_DARKCYAN);
    M5Dial.Display.setTextDatum(top_center);
    
    M5Dial.Display.drawString("Flt: " + p.callsign, 120, 45);
    
    String altStr = p.altitude > 0 ? String((int)(p.altitude * 3.28084)) + " ft" : "GND/Unk";
    M5Dial.Display.drawString("Alt: " + altStr, 120, 65);
    
    M5Dial.Display.drawString("Orig: " + p.country, 120, 85);
  }

  if (showZoomFlash) {
    if (millis() - zoomFlashTimer < 1000) { 
      float radiusMiles = zoomRadius * 69.0; 
      
      M5Dial.Display.fillRect(50, 100, 140, 40, TFT_PURPLE);
      M5Dial.Display.drawRect(50, 100, 140, 40, TFT_WHITE);
      
      M5Dial.Display.setTextColor(TFT_WHITE, TFT_PURPLE);
      M5Dial.Display.setTextDatum(middle_center);
      
      String flashText = "Radius: " + String((int)radiusMiles) + " mi";
      M5Dial.Display.drawString(flashText, 120, 120);
    } else {
      showZoomFlash = false; 
    }
  }
}