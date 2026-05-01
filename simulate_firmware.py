import requests
import time
import math
import random

BACKEND_URL = "http://localhost:8000/api/data"

def voltage_to_conductance(voltage):
    # Calibration Table from main.cpp
    cal_table = [
        (0.050, 20.000),
        (0.360, 4.545),
        (0.380, 10.000), 
        (0.530, 2.128),
        (0.590, 1.000),
        (0.650, 0.455),
        (0.653, 0.213),
        (0.662, 0.100)
    ]
    
    if voltage <= cal_table[0][0]: return cal_table[0][1]
    if voltage >= cal_table[-1][0]: return cal_table[-1][1]

    for i in range(len(cal_table) - 1):
        v1, c1 = cal_table[i]
        v2, c2 = cal_table[i+1]
        if v1 <= voltage <= v2:
            return c1 + (voltage - v1) * (c2 - c1) / (v2 - v1)
    return 0.0

def simulate_firmware():
    print(f"Starting firmware simulation... sending data to {BACKEND_URL}")
    
    cycle = 0
    base_temp = 24.5
    base_humidity = 45.0
    mock_volts = 0.59
    
    while True:
        try:
            # Replicate the logic in main.cpp loop()
            
            # 1. Mock SHT20 data
            temp = base_temp + math.sin(time.time() / 60.0) * 0.5 + random.uniform(-0.1, 0.1)
            humidity = base_humidity + math.cos(time.time() / 60.0) * 2.0 + random.uniform(-0.5, 0.5)
            
            # 2. Mock EDA data with occasional peaks (Hot Flash simulation)
            cycle += 1
            if 50 < (cycle % 100) < 60:
                mock_volts -= 0.05 # rapid drop in voltage -> rapid rise in conductance
            elif (cycle % 100) >= 60:
                mock_volts += 0.005 # slow recovery
                if mock_volts > 0.59:
                    mock_volts = 0.59
            else:
                mock_volts = 0.59
                
            conductance = voltage_to_conductance(mock_volts)
            
            # 3. Prepare payload
            payload = {
                "conductance": conductance,
                "temperature": temp,
                "humidity": humidity,
                "batteryVoltage": 3.8 + random.uniform(-0.05, 0.05),
                "wifiConnected": True
            }
            
            # 4. Send POST request
            response = requests.post(BACKEND_URL, json=payload)
            if response.status_code == 200:
                print(f"[{time.strftime('%H:%M:%S')}] Data sent: C={conductance:.2f}uS, T={temp:.1f}C, H={humidity:.1f}%")
            else:
                print(f"[{time.strftime('%H:%M:%S')}] Failed to send data: {response.status_code}")
                
        except Exception as e:
            print(f"Error: {e}")
            
        time.sleep(2) # Sample every 2 seconds as in main.cpp

if __name__ == "__main__":
    simulate_firmware()
