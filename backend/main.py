from fastapi import FastAPI, Request
from fastapi.staticfiles import StaticFiles
from fastapi.responses import FileResponse
from pydantic import BaseModel
from typing import List
import time
import uvicorn
import math

app = FastAPI()

class SensorData(BaseModel):
    conductance: float
    temperature: float
    humidity: float
    batteryVoltage: float
    wifiConnected: bool

data_history = []
max_history = 1000
calibration_params = {"offset": 0.0, "multiplier": 1.0}

class CalibrationData(BaseModel):
    offset: float
    multiplier: float

@app.post("/api/calibrate")
async def set_calibration(data: CalibrationData):
    global calibration_params
    calibration_params = data.dict()
    return {"status": "success", "params": calibration_params}

@app.get("/api/calibrate")
async def get_calibration():
    return calibration_params

@app.post("/api/data")
async def receive_data(data: SensorData):
    entry = data.dict()
    # Apply calibration
    entry["conductance"] = (entry["conductance"] * calibration_params.get("multiplier", 1.0)) + calibration_params.get("offset", 0.0)
    entry["timestamp"] = time.time()
    data_history.append(entry)
    if len(data_history) > max_history:
        data_history.pop(0)
    return {"status": "success"}

def run_inference(history):
    if len(history) < 10:
        return {"state": "Stable", "source": "None"}
    
    latest = history[-1]
    # Look back ~30 seconds for immediate trend, but ~5 mins for temperature
    prev_short = history[-10] 
    
    eda_diff = latest["conductance"] - prev_short["conductance"]
    
    # Check if EDA is likely active/valid
    # If it's near zero or perfectly constant for 60 seconds, it's "failed"
    eda_values = [h["conductance"] for h in history[-30:]]
    eda_failed = max(eda_values) - min(eda_values) < 0.001 or latest["conductance"] < 0.05
    
    if not eda_failed and eda_diff > 0.5:
        return {"state": "Hot Flash Detected", "source": "EDA Sensor", "confidence": "High"}
    
    # Temperature Fallback Logic
    # Hot flashes often show a rise of >0.5C over a few minutes
    if len(history) > 60:
        prev_long = history[-60] # ~2 minutes ago
        temp_rise = latest["temperature"] - prev_long["temperature"]
        if temp_rise > 0.4:
            return {"state": "Hot Flash Detected", "source": "Temperature Fallback", "confidence": "Moderate"}

    return {"state": "Stable", "source": "Multi-modal Monitoring"}

def get_mock_history():
    mock_history = []
    now = time.time()
    for i in range(100, 0, -1):
        t = now - i*2
        cond = 0.59
        temp = 24.5
        if 15 < i < 45:
            cond += 3.0 * math.exp(-((i-30)**2)/100.0)
            temp += 1.2 * math.exp(-((i-30)**2)/200.0)
        mock_history.append({
            "timestamp": t,
            "conductance": cond,
            "temperature": temp,
            "humidity": 45.0,
            "batteryVoltage": 3.7,
            "wifiConnected": True
        })
    return mock_history

@app.get("/api/data")
async def get_data():
    current_history = data_history if data_history else get_mock_history()
    inference = run_inference(current_history)
    return {"history": current_history, "inference": inference}

app.mount("/static", StaticFiles(directory="static"), name="static")

@app.get("/")
async def root():
    return FileResponse("static/index.html")

if __name__ == "__main__":
    uvicorn.run(app, host="0.0.0.0", port=8000)

