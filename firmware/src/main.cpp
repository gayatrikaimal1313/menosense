#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <WiFiClient.h>
#include <Wire.h>
#include <ArduinoJson.h>

// --- SETTINGS ---
const char* AP_SSID_SETUP = "MenoSense_Setup";
const char* BACKEND_URL = "http://192.168.86.45:8000/api/data"; // Update this to your computer's IP if needed

// Pins
#define SDA_PIN 4   // D2 on ESP8266
#define SCL_PIN 5   // D1 on ESP8266
#define ADC_PIN A0
#define LED_STATUS 15 // D8

// SHT20 Address
#define SHT20_ADDR 0x40

// Global Objects
ESP8266WebServer server(80);
WiFiClient wifiClient;

// --- CALIBRATION TABLE (Voltage -> Conductance uS) ---
struct CalPoint {
    float voltage;
    float conductance;
};

CalPoint calTable[] = {
    {0.050, 20.000},
    {0.360, 4.545},
    {0.380, 10.000},
    {0.530, 2.128},
    {0.590, 1.000},
    {0.650, 0.455},
    {0.653, 0.213},
    {0.662, 0.100}
};
const int calTableSize = sizeof(calTable) / sizeof(calTable[0]);

float voltageToConductance(float voltage) {
    if (voltage <= calTable[0].voltage) return calTable[0].conductance;
    if (voltage >= calTable[calTableSize - 1].voltage) return calTable[calTableSize - 1].conductance;

    for (int i = 0; i < calTableSize - 1; i++) {
        if (voltage >= calTable[i].voltage && voltage <= calTable[i + 1].voltage) {
            float v1 = calTable[i].voltage;
            float c1 = calTable[i].conductance;
            float v2 = calTable[i + 1].voltage;
            float c2 = calTable[i + 1].conductance;
            return c1 + (voltage - v1) * (c2 - c1) / (v2 - v1);
        }
    }
    return 0.0;
}

// --- SHT20 FUNCTIONS (As provided in baseline) ---
float readTemperature() {
    Wire.beginTransmission(SHT20_ADDR);
    Wire.write(0xF3);
    Wire.endTransmission();
    delay(85);
    Wire.requestFrom(SHT20_ADDR, 3);
    if (Wire.available() < 2) return NAN;
    uint16_t raw = Wire.read() << 8;
    raw |= Wire.read();
    raw &= 0xFFFC;
    return -46.85 + 175.72 * ((float)raw / 65536.0);
}

float readHumidity() {
    Wire.beginTransmission(SHT20_ADDR);
    Wire.write(0xF5);
    Wire.endTransmission();
    delay(30);
    Wire.requestFrom(SHT20_ADDR, 3);
    if (Wire.available() < 2) return NAN;
    uint16_t raw = Wire.read() << 8;
    raw |= Wire.read();
    raw &= 0xFFFC;
    return -6.0 + 125.0 * ((float)raw / 65536.0);
}

// --- WIFI CONFIG SERVER ---
void handleRoot() {
    String html = "<html><body><h1>MenoSense WiFi Setup</h1>";
    html += "<form action='/configure' method='POST'>";
    html += "SSID: <input type='text' name='ssid'><br>";
    html += "Password: <input type='password' name='pass'><br>";
    html += "<input type='submit' value='Connect'>";
    html += "</form></body></html>";
    server.send(200, "text/html", html);
}

void handleConfigure() {
    if (server.hasArg("ssid") && server.hasArg("pass")) {
        String ssid = server.arg("ssid");
        String pass = server.arg("pass");
        
        server.send(200, "text/plain", "Saving credentials and restarting...");
        delay(1000);
        
        Serial.printf("Connecting to %s...\n", ssid.c_str());
        WiFi.begin(ssid.c_str(), pass.c_str());
        
        // Wait up to 10s to see if it works before restarting to apply permanently
        int retry = 0;
        while (WiFi.status() != WL_CONNECTED && retry < 20) {
            delay(500);
            Serial.print(".");
            retry++;
        }
        
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("\nWiFi Connected! Restarting...");
            ESP.restart();
        } else {
            Serial.println("\nConnection failed. Please try again.");
        }
    }
}

// --- SETUP & LOOP ---
bool apMode = false;

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n\n--- MenoSense Hardware Init ---");

    pinMode(LED_STATUS, OUTPUT);
    digitalWrite(LED_STATUS, LOW);

    Wire.begin(SDA_PIN, SCL_PIN);
    
    // Check SHT20
    float testTemp = readTemperature();
    if (isnan(testTemp)) {
        Serial.println("SHT20: FAILED to detect!");
    } else {
        Serial.printf("SHT20: Detected. Current Temp: %.2f C\n", testTemp);
    }

    // Try saved WiFi
    Serial.print("WiFi: Connecting to saved credentials");
    WiFi.begin(); // Uses saved flash credentials
    
    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 30) {
        delay(500);
        Serial.print(".");
        retries++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi: Connected!");
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());
        digitalWrite(LED_STATUS, HIGH);
    } else {
        Serial.println("\nWiFi: Failed to connect. Starting AP Mode...");
        WiFi.mode(WIFI_AP);
        WiFi.softAP(AP_SSID_SETUP);
        Serial.print("AP Name: "); Serial.println(AP_SSID_SETUP);
        Serial.print("AP IP: "); Serial.println(WiFi.softAPIP());
        
        server.on("/", handleRoot);
        server.on("/configure", HTTP_POST, handleConfigure);
        server.begin();
        apMode = true;
    }
}

unsigned long lastPost = 0;
const unsigned long INTERVAL = 2000;

void loop() {
    // 1. Read Sensors (Always read for serial monitor)
    float temp = readTemperature();
    float humidity = readHumidity();
    int rawAdc = analogRead(ADC_PIN);
    float voltage = (rawAdc / 1023.0) * 3.3;
    bool edaAvailable = (rawAdc > 50);
    float conductance = edaAvailable ? voltageToConductance(voltage) : 0;
    String sensorStatus = edaAvailable ? "full-sensor" : "temperature-only";

    // 2. Serial Output (Always show)
    static unsigned long lastSerial = 0;
    if (millis() - lastSerial >= 2000) {
        lastSerial = millis();
        Serial.println("--- SENSOR DATA ---");
        Serial.printf("Mode: %s | WiFi: %d\n", apMode ? "AP (Setup)" : "Station", WiFi.status());
        if (isnan(temp)) Serial.println("SHT20: READ FAILURE");
        else Serial.printf("Temp: %.2f C | Humidity: %.2f %%\n", temp, humidity);
        Serial.printf("ADC: %d (%.3f V)\n", rawAdc, voltage);
        if (edaAvailable) Serial.printf("Conductance: %.3f uS\n", conductance);
        else Serial.println("EDA: Unavailable");
        Serial.println("-------------------");
    }

    if (apMode) {
        server.handleClient();
        static unsigned long lastBlink = 0;
        if (millis() - lastBlink > 500) {
            digitalWrite(LED_STATUS, !digitalRead(LED_STATUS));
            lastBlink = millis();
        }
        return;
    }

    if (millis() - lastPost >= INTERVAL) {
        lastPost = millis();
        // 3. HTTP POST (Only in station mode)
        HTTPClient http;
        // ... rest of the POST logic
        http.begin(wifiClient, BACKEND_URL);
        http.addHeader("Content-Type", "application/json");

        StaticJsonDocument<256> doc;
        if (edaAvailable) doc["conductance"] = conductance;
        else doc["conductance"] = nullptr;
        
        doc["temperature"] = isnan(temp) ? 0.0 : temp;
        doc["humidity"] = isnan(humidity) ? 0.0 : humidity;
        doc["batteryVoltage"] = 3.7; // Mock for now until divider is added
        doc["wifiConnected"] = true;
        doc["edaAvailable"] = edaAvailable;
        doc["sensorStatus"] = sensorStatus;

        String body;
        serializeJson(doc, body);
        int code = http.POST(body);
        
        Serial.printf("POST Result: %d\n\n", code);
        http.end();
        
        // Blink LED on success
        if (code == 200) {
            digitalWrite(LED_STATUS, LOW);
            delay(50);
            digitalWrite(LED_STATUS, HIGH);
        }
    }
}
