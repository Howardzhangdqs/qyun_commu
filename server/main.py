from fastapi import FastAPI, WebSocket, HTTPException, status
from fastapi.responses import JSONResponse
from typing import Dict, Set
import asyncio

app = FastAPI()

class ChannelManager:
    def __init__(self):
        self.channels: Dict[str, Channel] = {}
    
    def create_channel(self, name: str):
        if name in self.channels:
            raise ValueError("Channel already exists")
        self.channels[name] = Channel()

class Channel:
    def __init__(self):
        self.connections: Set[WebSocket] = set()
        self.lock = asyncio.Lock()

channel_manager = ChannelManager()

@app.get("/channel/create/{name}")
async def create_channel(name: str):
    try:
        channel_manager.create_channel(name)
        return {"status": "success", "message": f"Channel '{name}' created"}
    except ValueError as e:
        return JSONResponse(
            status_code=400,
            content={"status": "error", "message": str(e)}
        )

@app.websocket("/channel/listen/{name}")
async def websocket_listen(websocket: WebSocket, name: str):
    if name not in channel_manager.channels:
        await websocket.close(code=status.WS_1008_POLICY_VIOLATION)
        return
    
    channel = channel_manager.channels[name]
    await websocket.accept()
    
    async with channel.lock:
        channel.connections.add(websocket)
    
    try:
        while True:
            await websocket.receive_text()  # 保持连接开放
    except:
        async with channel.lock:
            channel.connections.remove(websocket)

@app.get("/channel/send/{name}/{message}")
async def send_message(name: str, message: str):
    if name not in channel_manager.channels:
        return JSONResponse(
            status_code=404,
            content={"status": "error", "message": "Channel not found"}
        )
    
    channel = channel_manager.channels[name]
    async with channel.lock:
        disconnected = []
        for ws in channel.connections:
            try:
                await ws.send_text(message)
            except Exception:
                disconnected.append(ws)
        
        for ws in disconnected:
            channel.connections.remove(ws)

    return {"status": "success", "message": "Message broadcasted"}