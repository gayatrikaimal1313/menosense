from fastapi import FastAPI, Request
from fastapi.staticfiles import StaticFiles
from fastapi.responses import FileResponse
from pydantic import BaseModel
from typing import List, Optional
import time
import uvicorn
import math

app = FastAPI()

class SensorData(BaseModel):
    conductance: Optional[float] = None
    temperature: float
    humidity: float
    batteryVoltage: float
    wifiConnected: bool
    edaAvailable: bool
    sensorStatus: str # "temperature-only", "full-sensor", "no-data"

data_history = []
max_history = 1000
calibration_params = {"offset": 0.0, "multiplier": 1.0}
last_post_timestamp = 0

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
    global last_post_timestamp
    entry = data.dict()
    
    # Apply calibration if EDA is available
    if entry.get("edaAvailable") and entry.get("conductance") is not None:
        entry["conductance"] = (entry["conductance"] * calibration_params.get("multiplier", 1.0)) + calibration_params.get("offset", 0.0)
    else:
        entry["conductance"] = None
        
    entry["timestamp"] = time.time()
    last_post_timestamp = entry["timestamp"]
    
    data_history.append(entry)
    if len(data_history) > max_history:
        data_history.pop(0)
    return {"status": "success"}

def run_inference(history):
    if not history:
        return {"state": "Waiting for ESP", "source": "None"}
        
    if len(history) < 5:
        return {"state": "Initializing", "source": "None"}
    
    latest = history[-1]
    
    # 1. EDA Logic (if available)
    if latest.get("edaAvailable") and latest.get("conductance") is not None:
        if len(history) >= 10:
            prev_short = history[-10] 
            if prev_short.get("conductance") is not None:
                eda_diff = latest["conductance"] - prev_short["conductance"]
                
                # Check if EDA is likely active/valid (safety check)
                eda_values = [h["conductance"] for h in history[-30:] if h.get("conductance") is not None]
                if len(eda_values) >= 10:
                    eda_failed = max(eda_values) - min(eda_values) < 0.0001
                    if not eda_failed and eda_diff > 0.5:
                        return {"state": "Hot Flash Detected", "source": "EDA Sensor", "confidence": "High"}
    
    # 2. Temperature Logic (Primary Fallback)
    # Hot flashes often show a rise of >0.5C over 30s-2mins
    if len(history) >= 15: # ~30 seconds at 2s interval
        prev_long = history[-15]
        temp_rise = latest["temperature"] - prev_long["temperature"]
        if temp_rise >= 0.5:
            return {"state": "Hot Flash Detected", "source": "Temperature Spike", "confidence": "Moderate"}

    return {"state": "Stable", "source": "Hardware Data"}

@app.get("/api/data")
async def get_data():
    inference = run_inference(data_history)
    return {
        "history": data_history, 
        "inference": inference,
        "lastSync": last_post_timestamp,
        "deviceConnected": (time.time() - last_post_timestamp) < 10 if last_post_timestamp > 0 else False
    }

app.mount("/static", StaticFiles(directory="backend/static"), name="static")

@app.get("/")
async def root():
    return FileResponse("backend/static/index.html")

if __name__ == "__main__":
    uvicorn.run(app, host="0.0.0.0", port=8000)

