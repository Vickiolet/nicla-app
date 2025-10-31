from fastapi import FastAPI
from api.endpoints import logs  
from fastapi.middleware.cors import CORSMiddleware

app = FastAPI(title="Nicla Backend", description="Arduino Nicla 设备管理后端")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["http://localhost:3000"],  
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# 注册路由
# app.include_router(device.router)
# app.include_router(files.router)
# app.include_router(sensor.router)
app.include_router(logs.router)  

@app.get("/")
def read_root():
    return {"message": "Welcome to Nicla Backend API"}

if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host="0.0.0.0", port=8000)