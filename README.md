M5Dial Live Flight Radar ✈️📡
A live, interactive ADS-B airplane radar tracker built for the M5Stack M5Dial (ESP32-S3). This project uses the OpenSky Network API to fetch live flight data and visually plots aircraft on the M5Dial's round touchscreen display.

✨ Features

Rotary Encoder Zoom: Spin the physical dial to zoom the radar radius in and out (from a tight 30-mile radius up to a massive 240-mile regional view). Flashes a temporary UI element to show the current radius in miles.

Touchscreen Interaction: Tap on any yellow aircraft on the radar to select it. A popup will display the flight's Callsign, Altitude (converted to feet), and Origin Country.

Live Sweeping Animation: Renders a classic, continuous green radar sweep animation at ~30 frames per second.

Secure OAuth2 Authentication: Uses OpenSky's modern Client ID / Client Secret authentication flow to secure a high-tier polling rate (updates every 10 seconds) without getting IP banned.

Memory Optimized: Downloads massive API payloads directly into a 48KB RAM buffer on the ESP32-S3, bypassing standard Wi-Fi stream timeouts.

🛠 Hardware & Software Requirements
Hardware:

M5Stack M5Dial (Powered via USB-C)

Software & Libraries (Arduino IDE):

M5Dial (and its dependency M5Unified) - For display, touch, and encoder controls.

ArduinoJson (by Benoit Blanchon) - Required for parsing the API responses.

Standard ESP32 Core Libraries: WiFi.h, WiFiClientSecure.h, HTTPClient.h

🚀 Setup & Installation
1. Get OpenSky API Credentials
OpenSky requires an account to fetch live data reliably without getting rate-limited.

Create a free account at OpenSky Network.

Navigate to your Account profile and find the API Client / OAuth2 section.

Generate a new set of credentials to get your Client ID and Client Secret.

2. Configure the Code
Open the .ino file in the Arduino IDE and locate the --- CONFIGURATION --- section at the top of the file. Update the following variables:

C++
// --- WI-FI CONFIGURATION ---
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// --- OPENSKY API CREDENTIALS (OAUTH2) ---
const char* clientID = "YOUR_CLIENT_ID";
const char* clientSecret = "YOUR_CLIENT_SECRET";


3. Flash to the M5Dial
Select M5Dial (or ESP32-S3 Dev Module if M5Dial isn't showing in your boards manager) in the Arduino IDE, compile, and upload.

🕹️ How to Use
Booting Up: The device will connect to Wi-Fi and securely log in to OpenSky.

Location: is manually set in LAT and LONG coordinates. Tried an autolocation method but it seemed to slow things down with the ESP32

Zooming: Turn the outer dial. Counter-clockwise zooms out (up to 240 miles), clockwise zooms in (down to 30 miles).

Selecting Planes: Planes appear as yellow dots. Tap a dot on the screen. It will turn red, and a popup will display the flight information. Tap anywhere else in the black space to deselect the plane and hide the popup.

⚠️ Known Limitations
No Flight Paths: The OpenSky /states/all endpoint provides physical vector data (latitude, longitude, altitude) but not origin/destination airports.

2.4 GHz Wi-Fi Only: The ESP32-S3 chip cannot see or connect to 5 GHz Wi-Fi networks. Ensure you are connecting to a 2.4 GHz network or a mobile hotspot with "Maximize Compatibility" enabled.

Data Delays: OpenSky relies on crowd-sourced ADS-B receivers. If a plane is flying very low in a rural area without receivers nearby, it may temporarily disappear from the radar.
