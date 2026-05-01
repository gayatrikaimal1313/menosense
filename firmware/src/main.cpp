#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <Wire.h>
#include <SHT2x.h>
#include <ArduinoJson.h>

const char* ap_ssid = "MenoSense_Internal";
const char* backend_url = "http://192.168.4.2:8000/api/data";

SHT2x sht;

const int LED_WIFI = 15;
const int ADC_PIN = A0;

// Calibration Table: Voltage -> Conductance (uS)
struct CalPoint {
    float voltage;
    float conductance;
};

// Sorted by voltage ascending
CalPoint calTable[] = {
    {0.050, 20.000},
    {0.360, 4.545},
    {0.380, 10.000}, // Preserving original data order anomaly by sorting voltage
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
            
            // Linear interpolation
            return c1 + (voltage - v1) * (c2 - c1) / (v2 - v1);
        }
    }
    return 0.0; // fallback
}

void setup() {
    Serial.begin(115200);
    pinMode(LED_WIFI, OUTPUT);
    digitalWrite(LED_WIFI, LOW);

    Wire.begin(4, 5); // SDA = GPIO4, SCL = GPIO5
    sht.begin();

    Serial.println("Configuring Access Point...");
    WiFi.softAP(ap_ssid);
    
    IPAddress IP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(IP);
    Serial.println("Network Ready. Connect computer to 'MenoSense_Internal'");
    
    digitalWrite(LED_WIFI, HIGH); // AP is active
}

void loop() {
    // 1. Read SHT20
    sht.read();
    float temp = sht.getTemperature();
    float humidity = sht.getHumidity();

    // 2. Read ADC for mock EDA data
    int rawAdc = analogRead(ADC_PIN); 
    // Simulate battery voltage and mock EDA voltage
    // ESP8266 ADC is 0-1V normally, but Wemos D1 mini has a voltage divider to support 0-3.3V mapping to 0-1023.
    // Let's assume the ADC is measuring EDA directly (0-1V internal mapped from 0-3.3V).
    float edaVoltage = (rawAdc / 1023.0) * 3.3; 
    
    // Since EDA is not connected, let's inject a mock oscillation or random noise if voltage is 0
    if (rawAdc < 10) {
        // Mock a baseline with occasional peaks
        static float mockVolts = 0.59;
        static int cycle = 0;
        cycle++;
        if (cycle > 50 && cycle < 60) {
            mockVolts -= 0.05; // rapid drop in voltage -> rapid rise in conductance
        } else if (cycle >= 60) {
            mockVolts += 0.005; // slow recovery
            if (mockVolts > 0.59) {
                mockVolts = 0.59;
                cycle = 0;
            }
        }
        edaVoltage = mockVolts;
    }

    float conductance = voltageToConductance(edaVoltage);

    // Mock battery voltage 
    float batteryVoltage = 3.7; // Fixed for now, realistically would be measured on another pin or multiplexed

    // 3. Send to Backend
    WiFiClient client;
    HTTPClient http;
    
    http.begin(client, backend_url);
    http.addHeader("Content-Type", "application/json");

    StaticJsonDocument<200> doc;
    doc["conductance"] = conductance;
    doc["temperature"] = temp;
    doc["humidity"] = humidity;
    doc["batteryVoltage"] = batteryVoltage;
    doc["wifiConnected"] = true;

    String requestBody;
    serializeJson(doc, requestBody);

    int httpResponseCode = http.POST(requestBody);
    
    if (httpResponseCode > 0) {
        Serial.printf("HTTP POST Success: %d\n", httpResponseCode);
    } else {
        Serial.printf("HTTP POST Error: %s (Check if computer is connected to ESP Wi-Fi)\n", http.errorToString(httpResponseCode).c_str());
    }
    http.end();

    delay(2000); // Sample every 2 seconds
}
