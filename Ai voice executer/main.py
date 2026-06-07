"""
VART (Afri Connect) - Bidirectional Edge-AI Console
===================================================
Architecture: PC (Python/FastAPI) <---WebSocket---> Browser (HTML5 Cyber HUD Console)
               ^
               | (Bidirectional Serial)
               v
             ESP32 (Keypad, Weather/Soleil Sensors, RGB/LEDs, I2C LCD/OLED Eyes)
"""

import os
import sys
import time
import json
import asyncio
import threading
import webbrowser
import serial
import serial.tools.list_ports
from contextlib import asynccontextmanager
from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.responses import HTMLResponse
from fastapi.staticfiles import StaticFiles
import uvicorn
from groq import Groq

# Load local .env file variables
def load_dotenv(filepath=".env"):
    if os.path.exists(filepath):
        with open(filepath, "r") as f:
            for line in f:
                line = line.strip()
                if line and not line.startswith("#") and "=" in line:
                    key, val = line.split("=", 1)
                    os.environ[key.strip()] = val.strip()

load_dotenv()

# ─────────────────────────────────────────────────────────────────────────────
#  CONFIGURATION & SYSTEM SPECS
# ─────────────────────────────────────────────────────────────────────────────
GROQ_API_KEY   = os.environ.get("GROQ_API_KEY", "")
GROQ_MODEL     = "llama-3.1-8b-instant"
SERIAL_BAUD    = 115200
UNLOCK_SIGNAL  = "SYS_UNLOCK"

SYSTEM_PROMPT_BASE = (
    "You are VART ,your password is 1234, a super-intelligent futuristic edge assistant tethered to physical hardware. "
    "Keep your responses extremely short and concise (maximum 1 or 2 short sentences). Avoid writing paragraphs.\n\n"
    "Respond in the same language the user speaks.\n\n"
    "Crucial Telemetry Rules:\n"
    "1. You have live telemetry injected below. ONLY discuss or mention these sensor values if the user explicitly asks about the temperature, humidity, light, or sensors. If they greet you, ask general questions, or say anything else, NEVER mention the telemetry or sensors.\n"
    "2. If the user explicitly asks about the sensors/telemetry and any value is '--', politely explain that the ESP32 is offline or locked. Otherwise, do not mention it.\n\n"
    "You are authorized to control physical hardware! If the user asks you to operate lights, "
    "adjust status colors, or change eyes/face expression, append the exact command tag at the very end of your response:\n"
    "- Red RGB light: [CMD:RGB_RED]\n"
    "- Green RGB light: [CMD:RGB_GREEN]\n"
    "- Blue RGB light: [CMD:RGB_BLUE]\n"
    "- Turn off RGB: [CMD:RGB_OFF]\n"
    "- Turn on standard LED: [CMD:LED_ON]\n"
    "- Turn off standard LED: [CMD:LED_OFF]\n"
    "- Show happy curves eyes: [CMD:EYES_HAPPY]\n"
    "- Show sad eyes: [CMD:EYES_SAD]\n"
    "- Show blinking eyes: [CMD:EYES_BLINK]\n"
    "- Reset display eyes: [CMD:EYES_RESET]\n"
    "Only use tags when specifically requested to do a hardware action."
)

LANGUAGE_CODES = {
    "English": "en-US",
    "French":  "fr-FR",
    "Spanish": "es-ES",
}

ASSET_DIR = os.path.join(os.path.dirname(__file__), "stat")

STATE_DISCONNECTED = 0
STATE_LOCKED       = 1
STATE_ACTIVE       = 2

@asynccontextmanager
async def lifespan(app: FastAPI):
    system_core.loop = asyncio.get_running_loop()
    system_core.start()
    yield
    if system_core.serial_conn:
        system_core.serial_conn.close()

app = FastAPI(title="VART Global Edge Console", lifespan=lifespan)

# Serve the local stat/ folder so images are directly accessible by the browser
if os.path.exists(ASSET_DIR):
    app.mount("/stat", StaticFiles(directory=ASSET_DIR), name="stat")


# ─────────────────────────────────────────────────────────────────────────────
#  FASTAPI BROADCAST MANAGER
# ─────────────────────────────────────────────────────────────────────────────
class ConnectionManager:
    def __init__(self):
        self.active_connections: list[WebSocket] = []
        self.current_state = STATE_DISCONNECTED
        self.state_details = "UART SCANNING..."
        self.language = "English"

        # Hardware telemetry caches
        self.temp = "--"
        self.humid = "--"
        self.light = "--"
        self.led_status = "OFF"
        self.rgb_status = "OFF"

    async def connect(self, websocket: WebSocket):
        await websocket.accept()
        self.active_connections.append(websocket)
        await self.send_state(websocket)

    def disconnect(self, websocket: WebSocket):
        if websocket in self.active_connections:
            self.active_connections.remove(websocket)

    async def broadcast(self, data: dict):
        for connection in list(self.active_connections):
            try:
                await connection.send_text(json.dumps(data))
            except Exception:
                self.active_connections.remove(connection)

    async def send_state(self, websocket: WebSocket):
        await websocket.send_text(json.dumps({
            "type": "state_update",
            "state": self.current_state,
            "details": self.state_details,
            "language": self.language
        }))
        await websocket.send_text(json.dumps({
            "type": "telemetry",
            "temp": self.temp,
            "humid": self.humid,
            "light": self.light,
            "led": self.led_status,
            "rgb": self.rgb_status
        }))

    async def update_state(self, state: int, details: str):
        self.current_state = state
        self.state_details = details
        await self.broadcast({
            "type": "state_update",
            "state": state,
            "details": details,
            "language": self.language
        })

    async def update_telemetry(self, temp: str, humid: str, light: str):
        self.temp = temp
        self.humid = humid
        self.light = light
        await self.broadcast({
            "type": "telemetry",
            "temp": temp,
            "humid": humid,
            "light": light,
            "led": self.led_status,
            "rgb": self.rgb_status
        })

    async def update_status_badges(self, led: str, rgb: str):
        self.led_status = led
        self.rgb_status = rgb
        await self.broadcast({
            "type": "status_update",
            "led": led,
            "rgb": rgb
        })

    async def log(self, text: str, style: str = "system"):
        await self.broadcast({
            "type": "log",
            "text": text,
            "style": style
        })

manager = ConnectionManager()

# ─────────────────────────────────────────────────────────────────────────────
#  ROBUST IMAGE DISCOVERY
# ─────────────────────────────────────────────────────────────────────────────
def get_web_assets_mapping() -> dict:
    def first_existing(filenames: list[str]) -> str:
        for f in filenames:
            if os.path.exists(os.path.join(ASSET_DIR, f)):
                return f"/stat/{f}"
        return ""

    return {
        "disconnected": first_existing(["unpluged.jpeg", "lose connection.jpeg", "trouble.png"]),
        "locked":       first_existing(["code keypad.jpeg", "locked.png"]),
        "idle":         first_existing(["on.jpeg", "logo_idle.png"]),
        "hover":        first_existing(["logo_hover.png"]),
        "listening":    first_existing(["logo_active_listening.png"])
    }

# ─────────────────────────────────────────────────────────────────────────────
#  GLOBAL HARDWARE & VOICE DRIVERS
# ─────────────────────────────────────────────────────────────────────────────
class EdgeAssistantSystem:
    def __init__(self):
        self.serial_conn: serial.Serial | None = None
        self.groq_client = Groq(api_key=GROQ_API_KEY)
        
        self.voice_running = False
        self.serial_running = False
        self.mic_active = False
        self.last_ai_text = ""
        self.loop = None

    def run_coroutine(self, coro):
        if self.loop:
            asyncio.run_coroutine_threadsafe(coro, self.loop)

    def start(self):
        threading.Thread(target=self._hardware_monitor_loop, daemon=True).start()

    def send_serial_command(self, cmd: str):
        """Pushes physical control commands down to ESP32."""
        if self.serial_conn and self.serial_conn.is_open:
            try:
                self.serial_conn.write(f"{cmd}\n".encode())
                self.run_coroutine(manager.log(f">> [TX SERIAL CMD]: {cmd}", "success"))
            except Exception as e:
                self.run_coroutine(manager.log(f">> [TX FAULT]: {e}", "error"))

    def _hardware_monitor_loop(self):
        while True:
            if manager.current_state == STATE_DISCONNECTED:
                port = self._find_esp32_port()
                if port:
                    try:
                        self.serial_conn = serial.Serial(port, SERIAL_BAUD, timeout=1)
                        time.sleep(1.5)  # wait for boot reset
                        self.run_coroutine(manager.update_state(STATE_LOCKED, f"SYS SECURED // LINK: {port}"))
                        self.run_coroutine(manager.log(f">> Connection linked to serial gateway: {port}", "success"))
                        self.run_coroutine(manager.log(">> System locked. Awaiting physical keypad passcode entry...", "system"))
                        
                        # Start persistent serial telemetry reader
                        self.serial_running = True
                        threading.Thread(target=self._serial_reader, daemon=True).start()
                    except Exception as e:
                        self.run_coroutine(manager.log(f">> [UART FAULT]: {e}", "error"))
                else:
                    self.run_coroutine(manager.update_state(STATE_DISCONNECTED, "BUS OFFLINE // NO SERIAL CONNECTED"))
            time.sleep(4.0)

    def _serial_reader(self):
        """Persistent reader for keypad signals and weather/soleil telemetry."""
        while self.serial_running and self.serial_conn:
            try:
                if self.serial_conn.in_waiting:
                    raw = self.serial_conn.readline()
                    line = raw.decode("utf-8", errors="replace").strip()
                    if not line:
                        continue
                    
                    # 1. Parse Telemetry Stream from Sensors (Supports both JSON and legacy TELEM format)
                    if line.startswith("{") and line.endswith("}"):
                        try:
                            data = json.loads(line)
                            t_val = str(data.get("t", "--"))
                            h_val = str(data.get("h", "--"))
                            l_raw = data.get("l")
                            # If it's a 1/0 status flag or raw LDR reading
                            l_val = "LIGHT" if l_raw == 1 else ("DARK" if l_raw == 0 else str(l_raw if l_raw is not None else "--"))
                            
                            self.run_coroutine(manager.update_telemetry(t_val, h_val, l_val))
                            
                            led_val = data.get("led", "OFF")
                            rgb_val = data.get("rgb", "OFF")
                            self.run_coroutine(manager.update_status_badges(led_val, rgb_val))
                        except Exception:
                            pass
                        continue
                    elif line.startswith("TELEM:"):
                        # Protocol: TELEM:T:24.5:H:60.2:L:850:LED:OFF:RGB:RED
                        parts = line.split(":")
                        try:
                            # Safely extract values from matching index offsets
                            t_val = parts[parts.index("T") + 1]
                            h_val = parts[parts.index("H") + 1]
                            l_val = parts[parts.index("L") + 1]
                            
                            self.run_coroutine(manager.update_telemetry(t_val, h_val, l_val))
                            
                            led_val = parts[parts.index("LED") + 1]
                            rgb_val = parts[parts.index("RGB") + 1]
                            self.run_coroutine(manager.update_status_badges(led_val, rgb_val))
                        except Exception:
                            pass # ignore partial serial lines during start
                        continue

                    # 2. Log normal serial prints
                    self.run_coroutine(manager.log(f">> [UART CORE DATA]: '{line}'", "system"))

                    # 3. Handle Lock Transition
                    if line == UNLOCK_SIGNAL and manager.current_state == STATE_LOCKED:
                        self.run_coroutine(manager.update_state(STATE_ACTIVE, "OPERATIONAL // SYSTEMS READY"))
                        self.run_coroutine(manager.log(">> Access granted. Initializing active cognitive arrays...", "success"))
                        self.mic_active = True
                        self.run_coroutine(manager.broadcast({"type": "mic_state", "active": True}))
                else:
                    time.sleep(0.05)
            except Exception as e:
                self.run_coroutine(manager.log(f">> [LINK DISRUPT]: {e}", "error"))
                self.serial_running = False
                self.serial_conn = None
                self.run_coroutine(manager.update_state(STATE_DISCONNECTED, "BUS OFFLINE"))
                break

    def process_query(self, user_text: str) -> str | None:
        self.run_coroutine(manager.update_state(STATE_ACTIVE, "COGNITIVE CORE THINKING..."))
        
        telemetry_context = (
            f"\n\n[LIVE EDGE SENSOR TELEMETRY]: "
            f"Temperature: {manager.temp}°C, "
            f"Humidity: {manager.humid}%, "
            f"Soleil light intensity: {manager.light}/1023."
        )
        full_system_prompt = SYSTEM_PROMPT_BASE + telemetry_context

        try:
            response = self.groq_client.chat.completions.create(
                model=GROQ_MODEL,
                messages=[
                    {"role": "system",  "content": full_system_prompt},
                    {"role": "user",    "content": user_text},
                ],
                temperature=0.6,
                max_tokens=100,
            )
            ai_text = response.choices[0].message.content.strip()

            # Intercept hardware command tags from response
            command_to_send = None
            if "[CMD:" in ai_text:
                start_idx = ai_text.find("[CMD:")
                end_idx = ai_text.find("]", start_idx)
                if end_idx != -1:
                    cmd_tag = ai_text[start_idx:end_idx + 1]
                    command_to_send = cmd_tag.replace("[CMD:", "").replace("]", "")
                    ai_text = ai_text.replace(cmd_tag, "").strip()

            self.last_ai_text = ai_text

            self.run_coroutine(manager.log(f"USR >> {user_text}", "user"))
            self.run_coroutine(manager.log(f"VART >> {ai_text}", "ai"))

            # Push conversation captions to browser subtitle panel
            self.run_coroutine(manager.broadcast({
                "type": "conversation",
                "user": user_text,
                "ai": ai_text
            }))

            # Trigger Serial instruction down to ESP32
            if command_to_send:
                self.send_serial_command(command_to_send)

            return ai_text
        except Exception as e:
            self.run_coroutine(manager.log(f">> [COGNITIVE EXCEPTION]: {e}", "error"))
            return None

    def process_audio_query(self, base64_audio: str, mime_type: str):
        self.run_coroutine(manager.log(">> [VOICE INGEST]: Processing audio data with Groq Whisper API...", "system"))
        try:
            import base64
            import io
            
            # Decode the base64 audio data
            audio_bytes = base64.b64decode(base64_audio)
            audio_file = io.BytesIO(audio_bytes)
            
            # Determine appropriate extension from mime type
            ext = "webm"
            if "ogg" in mime_type:
                ext = "ogg"
            elif "wav" in mime_type:
                ext = "wav"
            elif "mp4" in mime_type:
                ext = "mp4"
                
            audio_file.name = f"input.{ext}"
            
            # Transcribe using Groq Whisper API
            transcription = self.groq_client.audio.transcriptions.create(
                file=audio_file,
                model="whisper-large-v3", # Highly accurate Whisper model
            )
            
            user_text = transcription.text.strip()
            if user_text:
                # Process the transcribed text query
                self.process_query(user_text)
            else:
                self.run_coroutine(manager.log(">> [SPEECH ERROR]: Whisper returned empty transcription.", "error"))
        except Exception as e:
            self.run_coroutine(manager.log(f">> [WHISPER CORE ERROR]: {e}", "error"))



    def _find_esp32_port(self) -> str | None:
        KNOWN_DESCRIPTORS = ["CP210", "CH340", "FTDI", "USB Serial", "USB-SERIAL", "ESP"]
        for port in serial.tools.list_ports.comports():
            desc = (port.description or "").upper()
            mfr  = (port.manufacturer or "").upper()
            combo = desc + " " + mfr
            if any(kw.upper() in combo for kw in KNOWN_DESCRIPTORS):
                return port.device
        ports = list(serial.tools.list_ports.comports())
        if ports:
            return ports[0].device
        return None

system_core = EdgeAssistantSystem()

# ─────────────────────────────────────────────────────────────────────────────
#  HTTP ROUTES & SOCKET PIPELINES
# ─────────────────────────────────────────────────────────────────────────────
@app.get("/")
async def get_index():
    mapping = get_web_assets_mapping()
    html_content = f"""
    <!DOCTYPE html>
    <html lang="en">
    <head>
        <meta charset="UTF-8">
        <meta name="viewport" content="width=device-width, initial-scale=1.0">
        <title>VART // COGNITIVE CORE HUD</title>
        <link href="https://fonts.googleapis.com/css2?family=Share+Tech+Mono&display=swap" rel="stylesheet">
        <style>
            :root {{
                --bg: #050811;
                --panel: rgba(13, 21, 39, 0.7);
                --border: #132442;
                --text: #e2e8f0;
                --text-muted: #5e718d;
                --hud-color: #00f0ff;
                
                --cyan: #00f0ff;
                --magenta: #ff007f;
                --amber: #ffb300;
                --red: #ff3366;
            }}
            
            * {{
                box-sizing: border-box;
                margin: 0;
                padding: 0;
            }}

            body {{
                font-family: 'Share Tech Mono', monospace;
                background-color: var(--bg);
                color: var(--text);
                height: 100vh;
                overflow: hidden;
                display: flex;
                flex-direction: column;
                position: relative;
            }}

            /* Futuristic Hexagonal grid layout overlay */
            body::before {{
                content: '';
                position: absolute;
                top: 0; left: 0; right: 0; bottom: 0;
                background-image: 
                    linear-gradient(rgba(18, 30, 58, 0.08) 1px, transparent 1px),
                    linear-gradient(90deg, rgba(18, 30, 58, 0.08) 1px, transparent 1px);
                background-size: 30px 30px;
                z-index: -1;
                pointer-events: none;
            }}

            /* Glowing grid lines */
            header {{
                background-color: transparent; /* Fully transparent header bg */
                border-bottom: 2px solid var(--border);
                padding: 18px 30px;
                display: flex;
                justify-content: space-between;
                align-items: center;
            }}

            .title-section h1 {{
                font-size: 2.2rem;
                font-weight: 900;
                color: var(--hud-color);
                letter-spacing: 6px;
                text-shadow: 0 0 10px var(--hud-color), 0 0 25px rgba(0, 240, 255, 0.4);
                background: transparent;
            }}

            .hud-control-bar {{
                display: flex;
                align-items: center;
                gap: 15px;
            }}

            .hud-label {{
                color: var(--text-muted);
                font-size: 0.85rem;
            }}

            select {{
                background: #0d1527;
                color: var(--hud-color);
                border: 1px solid var(--border);
                padding: 6px 12px;
                font-family: inherit;
                border-radius: 4px;
                cursor: pointer;
                outline: none;
            }}

            select:focus {{
                border-color: var(--hud-color);
                box-shadow: 0 0 8px var(--hud-color);
            }}

            .main-hud-workspace {{
                flex: 1;
                display: flex;
                padding: 25px;
                gap: 25px;
                height: calc(100vh - 120px);
            }}

            /* Left column: robot image + conversation subtitles */
            .left-column {{
                flex: 1.3;
                display: flex;
                flex-direction: column;
                gap: 20px;
            }}

            /* Visualizer Card Container */
            .hud-visual-module {{
                flex: 1;
                background: var(--panel);
                border: 4px solid var(--hud-color);
                border-radius: 12px;
                box-shadow: 0 0 35px var(--hud-color), inset 0 0 20px rgba(0, 240, 255, 0.2);
                overflow: hidden;
                position: relative;
                transition: border-color 0.5s ease, box-shadow 0.5s ease, transform 0.3s ease;
            }}

            .hud-visual-module:hover {{
                transform: scale(1.02);
            }}

            #avatarImg {{
                width: 100%;
                height: 100%;
                object-fit: cover;
                position: absolute;
                top: 0;
                left: 0;
                z-index: 1;
            }}

            canvas {{
                position: absolute;
                top: 0;
                left: 0;
                width: 100%;
                height: 100%;
                z-index: 5;
                pointer-events: none;
            }}

            /* Conversation Subtitle Panel */
            .conversation-panel {{
                background: var(--panel);
                border: 1px solid var(--border);
                border-radius: 8px;
                padding: 18px 22px;
                display: flex;
                flex-direction: column;
                gap: 15px;
                backdrop-filter: blur(10px);
            }}

            .conv-header-row {{
                display: flex;
                justify-content: space-between;
                align-items: center;
                border-bottom: 1px solid rgba(19, 36, 66, 0.5);
                padding-bottom: 8px;
            }}

            .conv-label {{
                font-size: 0.7rem;
                color: var(--text-muted);
                letter-spacing: 2px;
            }}

            .conv-body {{
                display: flex;
                flex-direction: column;
                gap: 8px;
                min-height: 80px;
            }}

            .conv-user {{
                font-size: 0.95rem;
                color: var(--cyan);
                font-weight: bold;
                text-shadow: 0 0 4px rgba(0, 240, 255, 0.3);
            }}

            .conv-ai-row {{
                display: flex;
                align-items: flex-start;
                justify-content: space-between;
                gap: 10px;
            }}

            .conv-ai {{
                font-size: 0.95rem;
                color: var(--magenta);
                line-height: 1.4;
                flex: 1;
            }}

            .speak-icon-btn {{
                background: transparent;
                border: none;
                color: var(--hud-color);
                cursor: pointer;
                font-size: 1.2rem;
                padding: 2px 6px;
                border-radius: 4px;
                display: flex;
                align-items: center;
                justify-content: center;
                transition: transform 0.2s ease, text-shadow 0.2s ease;
                outline: none;
            }}

            .speak-icon-btn:hover {{
                transform: scale(1.15);
                text-shadow: 0 0 8px var(--hud-color);
            }}

            .speak-icon-btn:disabled {{
                color: var(--text-muted);
                cursor: not-allowed;
            }}

            /* Input Form and Mic Button Row */
            .conv-controls-row {{
                display: flex;
                align-items: center;
                gap: 12px;
                margin-top: 5px;
            }}

            #textForm {{
                display: flex;
                flex: 1;
                gap: 10px;
            }}

            .hud-input {{
                flex: 1;
                background: #090e1a;
                border: 1px solid var(--border);
                color: var(--text);
                padding: 10px 14px;
                font-family: inherit;
                font-size: 0.9rem;
                border-radius: 6px;
                outline: none;
                transition: border-color 0.3s ease, box-shadow 0.3s ease;
            }}

            .hud-input:focus {{
                border-color: var(--cyan);
                box-shadow: 0 0 10px rgba(0, 240, 255, 0.4);
            }}

            .hud-btn {{
                background: #0d1527;
                border: 1px solid var(--border);
                color: var(--hud-color);
                padding: 10px 18px;
                font-family: inherit;
                font-size: 0.85rem;
                font-weight: bold;
                border-radius: 6px;
                cursor: pointer;
                outline: none;
                transition: background 0.3s ease, border-color 0.3s ease, box-shadow 0.3s ease;
                white-space: nowrap;
            }}

            .hud-btn:hover {{
                background: rgba(0, 240, 255, 0.1);
                border-color: var(--cyan);
                box-shadow: 0 0 8px rgba(0, 240, 255, 0.3);
            }}

            .mic-btn {{
                background: #110d27;
                border: 1px solid #ff007f;
                color: #ff007f;
                box-shadow: 0 0 4px rgba(255, 0, 127, 0.2);
            }}

            .mic-btn.active {{
                background: #ff007f;
                color: #fff;
                box-shadow: 0 0 15px #ff007f;
                animation: micPulse 1.5s infinite;
            }}

            .mic-btn:hover {{
                background: rgba(255, 0, 127, 0.15);
                border-color: #ff007f;
                box-shadow: 0 0 8px rgba(255, 0, 127, 0.4);
            }}

            .mic-btn.active:hover {{
                background: #ff007f;
                box-shadow: 0 0 20px #ff007f;
            }}

            @keyframes micPulse {{
                0% {{ box-shadow: 0 0 10px #ff007f; }}
                50% {{ box-shadow: 0 0 20px rgba(255, 0, 127, 0.8); }}
                100% {{ box-shadow: 0 0 10px #ff007f; }}
            }}


            /* Terminal Module */
            .hud-terminal-module {{
                flex: 1;
                background: var(--panel);
                border: 1px solid var(--border);
                border-radius: 8px;
                box-shadow: 0 8px 32px 0 rgba(0, 0, 0, 0.3);
                backdrop-filter: blur(10px);
                display: flex;
                flex-direction: column;
                overflow: hidden;
            }}

            .term-header {{
                background: var(--border);
                padding: 10px 18px;
                font-size: 0.8rem;
                letter-spacing: 1px;
                color: var(--hud-color);
                display: flex;
                align-items: center;
                gap: 8px;
            }}

            .term-indicator {{
                width: 8px;
                height: 8px;
                border-radius: 50%;
                background-color: var(--hud-color);
                box-shadow: 0 0 8px var(--hud-color);
                animation: pulse 1.5s infinite;
            }}

            .term-body {{
                flex: 1;
                padding: 18px;
                font-size: 0.9rem;
                line-height: 1.5;
                overflow-y: auto;
                display: flex;
                flex-direction: column;
                gap: 8px;
            }}

            /* Terminal text styles */
            .log-line {{
                animation: scanline 0.15s ease-out;
            }}
            .log-system {{ color: var(--text-muted); font-style: italic; }}
            .log-success {{ color: #00ff66; font-weight: bold; text-shadow: 0 0 4px #00ff66; }}
            .log-user {{ color: var(--cyan); font-weight: bold; }}
            .log-ai {{ color: var(--magenta); }}
            .log-error {{ color: var(--red); font-weight: bold; text-shadow: 0 0 4px var(--red); }}

            /* Tech Status Footer */
            footer {{
                background: rgba(9, 14, 26, 0.95);
                border-top: 1px solid var(--border);
                padding: 10px 30px;
                display: flex;
                justify-content: space-between;
                font-size: 0.8rem;
                color: var(--text-muted);
            }}

            .state-text {{
                color: var(--hud-color);
                font-weight: bold;
                text-shadow: 0 0 6px var(--hud-color);
                letter-spacing: 1px;
            }}

            /* Animations */
            @keyframes pulse {{
                0% {{ opacity: 0.4; }}
                50% {{ opacity: 1; }}
                100% {{ opacity: 0.4; }}
            }}

            @keyframes scanline {{
                from {{ transform: translateY(3px); opacity: 0; }}
                to {{ transform: translateY(0); opacity: 1; }}
            }}
        </style>
    </head>
    <body>
        <header>
            <div class="title-section">
                <h1>VART</h1>
            </div>
            <div class="hud-control-bar">
                <span class="hud-label">[ TARGET_LANG ]</span>
                <select id="langSelect">
                    <option value="English">ENGLISH</option>
                    <option value="French">FRENCH</option>
                    <option value="Spanish">SPANISH</option>
                </select>
            </div>
        </header>

        <div class="main-hud-workspace">
            <!-- Left Column: Robot Image + Conversation Subtitles -->
            <div class="left-column">
                <div class="hud-visual-module" id="avatarFrame">
                    <img id="avatarImg" src="" alt="VART Visual Status">
                    <canvas id="hudCanvas"></canvas>
                </div>
                <div class="conversation-panel">
                    <div class="conv-header-row">
                        <div class="conv-label">VART COGNITIVE INTERACTION CORE</div>
                    </div>
                    <div class="conv-body">
                        <div class="conv-user" id="convUser">Awaiting command...</div>
                        <div class="conv-ai-row">
                            <div class="conv-ai" id="convAi">Speak or type your command below.</div>
                            <button class="speak-icon-btn" id="speakBtn" title="Listen by Voice" disabled>🔊</button>
                        </div>
                    </div>
                    <div class="conv-controls-row">
                        <button class="hud-btn mic-btn" id="micBtn" type="button">🎤 MIC IS OFF</button>
                        <form id="textForm" onsubmit="event.preventDefault(); submitText();">
                            <input type="text" id="textInput" class="hud-input" placeholder="TYPE COMMAND OR QUERY..." autocomplete="off" />
                            <button type="submit" class="hud-btn" id="sendBtn">SEND</button>
                        </form>
                    </div>
                </div>
            </div>

            <!-- Right Column: System Terminal Logs -->
            <div class="hud-terminal-module">
                <div class="term-header">
                    <div class="term-indicator" id="termIndicator"></div>
                    <span>LOG_STREAM_MONITOR</span>
                </div>
                <div class="term-body" id="termBody"></div>
            </div>
        </div>

        <footer>
            <div>GATEWAY STATE: <span class="state-text" id="statusDetail">UART STANDBY</span></div>
            <div>STREAMS ACTIVE // HTTP_WS_PORT 8000</div>
        </footer>

        <script>
            const assets = {json.dumps(mapping)};

            const socket = new WebSocket(`ws://${{window.location.host}}/ws`);
            const termBody = document.getElementById("termBody");
            const statusDetail = document.getElementById("statusDetail");
            const avatarImg = document.getElementById("avatarImg");
            const avatarFrame = document.getElementById("avatarFrame");
            const langSelect = document.getElementById("langSelect");
            const convUser = document.getElementById("convUser");
            const convAi = document.getElementById("convAi");
            const speakBtn = document.getElementById("speakBtn");
            const micBtn = document.getElementById("micBtn");
            const textForm = document.getElementById("textForm");
            const textInput = document.getElementById("textInput");
            const sendBtn = document.getElementById("sendBtn");

            let currentState = 0;
            let currentHudColor = "var(--cyan)";
            let waveAmplitude = 12;
            let waveFrequency = 0.08;
            let listening = false;

            // Pre-load voices to avoid empty array on first call in Chrome/Edge
            if ('speechSynthesis' in window) {{
                window.speechSynthesis.getVoices();
                window.speechSynthesis.onvoiceschanged = () => {{
                    window.speechSynthesis.getVoices();
                }};
            }}

            // Dynamic Web Speech Synthesis (TTS) Helper
            function speakText(text) {{
                if ('speechSynthesis' in window) {{
                    window.speechSynthesis.cancel();
                    
                    const utterance = new SpeechSynthesisUtterance(text);
                    const langMap = {{
                        "English": "en-US",
                        "French": "fr-FR",
                        "Spanish": "es-ES"
                    }};
                    const selectedLang = langSelect.value;
                    utterance.lang = langMap[selectedLang] || "en-US";
                    
                    const voices = window.speechSynthesis.getVoices();
                    const targetPrefix = utterance.lang.substring(0, 2).toLowerCase();
                    const matchedVoice = voices.find(v => v.lang.toLowerCase().startsWith(targetPrefix));
                    if (matchedVoice) {{
                        utterance.voice = matchedVoice;
                    }}
                    
                    utterance.rate = 1.05;
                    
                    setTimeout(() => {{
                        window.speechSynthesis.speak(utterance);
                    }}, 50);
                    
                    termLog(`>> [SPEECH OUTPUT]: Synthesized text response in ${{selectedLang}}`, "system");
                }} else {{
                    termLog(">> [SPEECH ERROR]: Web Speech Synthesis API not supported in this browser.", "error");
                }}
            }}

            // MediaRecorder STT Setup (Sends Audio to Groq Whisper via WebSocket)
            let mediaRecorder = null;
            let audioChunks = [];
            let isMicActive = false;

            async function startRecording() {{
                try {{
                    const stream = await navigator.mediaDevices.getUserMedia({{ audio: true }});
                    
                    let options = {{ mimeType: 'audio/webm' }};
                    if (!MediaRecorder.isTypeSupported('audio/webm')) {{
                        options = {{ mimeType: 'audio/ogg' }};
                    }}
                    if (!MediaRecorder.isTypeSupported('audio/ogg')) {{
                        options = {{}};
                    }}

                    mediaRecorder = new MediaRecorder(stream, options);
                    audioChunks = [];

                    mediaRecorder.ondataavailable = (event) => {{
                        if (event.data.size > 0) {{
                            audioChunks.push(event.data);
                        }}
                    }};

                    mediaRecorder.onstop = () => {{
                        const audioBlob = new Blob(audioChunks, {{ type: mediaRecorder.mimeType || 'audio/webm' }});
                        
                        const reader = new FileReader();
                        reader.readAsDataURL(audioBlob);
                        reader.onloadend = () => {{
                            const base64data = reader.result.split(',')[1];
                            socket.send(JSON.stringify({{
                                type: "audio_input",
                                audio: base64data,
                                mime: mediaRecorder.mimeType || 'audio/webm'
                            }}));
                        }};

                        stream.getTracks().forEach(track => track.stop());
                    }};

                    mediaRecorder.start();
                    isMicActive = true;
                    micBtn.textContent = "🎤 MIC IS ON (RECORDING...)";
                    micBtn.classList.add("active");
                    
                    listening = true;
                    currentHudColor = "var(--magenta)";
                    document.documentElement.style.setProperty("--hud-color", "var(--magenta)");
                    waveAmplitude = 25;
                    waveFrequency = 0.18;
                    updateAvatar(currentState, true);
                    
                    statusDetail.textContent = "COGNITIVE ARRAY RECORDING VOICE...";
                    termLog(">> [MIC CAPTURING]: Recording voice. Click Mic again to stop and send...", "system");
                }} catch (err) {{
                    termLog(`>> [MIC ERROR]: Access denied or no input device found.`, "error");
                    console.error(err);
                }}
            }}

            function stopRecording() {{
                if (mediaRecorder && mediaRecorder.state !== "inactive") {{
                    mediaRecorder.stop();
                }}
                isMicActive = false;
                micBtn.textContent = "🎤 MIC IS OFF";
                micBtn.classList.remove("active");
                
                listening = false;
                if (currentState === 0) {{
                    currentHudColor = "var(--red)";
                    document.documentElement.style.setProperty("--hud-color", "var(--red)");
                    waveAmplitude = 3;
                    waveFrequency = 0.02;
                }} else if (currentState === 1) {{
                    currentHudColor = "var(--amber)";
                    document.documentElement.style.setProperty("--hud-color", "var(--amber)");
                    waveAmplitude = 5;
                    waveFrequency = 0.04;
                }} else {{
                    currentHudColor = "var(--cyan)";
                    document.documentElement.style.setProperty("--hud-color", "var(--cyan)");
                    waveAmplitude = 12;
                    waveFrequency = 0.08;
                }}
                updateAvatar(currentState, false);
                statusDetail.textContent = currentState === 0 ? "UART STANDBY" : (currentState === 1 ? "SYS SECURED" : "OPERATIONAL // SYSTEMS READY");
            }}

            // Custom dynamic console logger helper
            function termLog(text, style) {{
                const line = document.createElement("div");
                line.className = `log-line log-${{style}}`;
                line.textContent = text;
                termBody.appendChild(line);
                termBody.scrollTop = termBody.scrollHeight;
                if (termBody.childNodes.length > 50) {{
                    termBody.removeChild(termBody.firstChild);
                }}
            }}

            langSelect.addEventListener("change", () => {{
                socket.send(JSON.stringify({{
                    "type": "set_language",
                    "language": langSelect.value
                }}));
            }});

            speakBtn.addEventListener("click", () => {{
                const cleanAiText = convAi.textContent.replace("VART: ", "").trim();
                if (cleanAiText && cleanAiText !== "Speak or type your command below.") {{
                    speakText(cleanAiText);
                }}
            }});

            micBtn.addEventListener("click", () => {{
                if (isMicActive) {{
                    stopRecording();
                }} else {{
                    startRecording();
                }}
            }});

            function submitText() {{
                const commandText = textInput.value.trim();
                if (commandText) {{
                    socket.send(JSON.stringify({{
                        "type": "text_input",
                        "text": commandText
                    }}));
                    textInput.value = "";
                }}
            }}

            function updateAvatar(state, isListening) {{
                let src = "";
                if (state === 0) {{
                    src = assets.disconnected || assets.idle;
                }} else if (state === 1) {{
                    src = assets.locked || assets.idle;
                }} else {{
                    if (isListening && assets.listening) {{
                        src = assets.listening;
                    }} else {{
                        src = assets.idle;
                    }}
                }}
                avatarImg.src = src || "data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' width='100' height='100' viewBox='0 0 100 100'><rect width='100' height='100' fill='%23132442'/></svg>";
            }}

            avatarFrame.addEventListener("mouseenter", () => {{
                if (currentState === 2 && !listening && assets.hover) {{
                    avatarImg.src = assets.hover;
                }}
            }});

            avatarFrame.addEventListener("mouseleave", () => {{
                if (currentState === 2 && !listening) {{
                    avatarImg.src = assets.idle;
                }}
            }});

            socket.onmessage = (event) => {{
                const data = JSON.parse(event.data);
                
                if (data.type === "state_update") {{
                    currentState = data.state;
                    statusDetail.textContent = data.details.toUpperCase();
                    langSelect.value = data.language;
                    
                    if (currentState === 0) {{
                        currentHudColor = "var(--red)";
                        document.documentElement.style.setProperty("--hud-color", "var(--red)");
                        waveAmplitude = 3;
                        waveFrequency = 0.02;
                    }} else if (currentState === 1) {{
                        currentHudColor = "var(--amber)";
                        document.documentElement.style.setProperty("--hud-color", "var(--amber)");
                        waveAmplitude = 5;
                        waveFrequency = 0.04;
                    }} else {{
                        currentHudColor = "var(--cyan)";
                        document.documentElement.style.setProperty("--hud-color", "var(--cyan)");
                        waveAmplitude = 12;
                        waveFrequency = 0.08;
                    }}
                    
                    updateAvatar(currentState, listening);
                }}

                // Conversation captions update
                if (data.type === "conversation") {{
                    convUser.textContent = "YOU: " + data.user;
                    convAi.textContent = "VART: " + data.ai;
                    speakBtn.disabled = false;
                    speakText(data.ai);
                }}
                
                if (data.type === "log") {{
                    const line = document.createElement("div");
                    line.className = `log-line log-${{data.style}}`;
                    line.textContent = data.text;
                    termBody.appendChild(line);
                    termBody.scrollTop = termBody.scrollHeight;
                    
                    if (termBody.childNodes.length > 50) {{
                        termBody.removeChild(termBody.firstChild);
                    }}
                }}
            }};

            // Initial call to load image on boot
            updateAvatar(currentState, listening);


            // ── 60 FPS CANVAS HUD ENGINE ─────────────────────────────────────
            const canvas = document.getElementById("hudCanvas");
            const ctx = canvas.getContext("2d");
            let rotationAngle = 0;
            let wavePhase = 0;

            function resizeCanvas() {{
                canvas.width = canvas.clientWidth;
                canvas.height = canvas.clientHeight;
            }}
            resizeCanvas();
            window.addEventListener("resize", resizeCanvas);

            function animate() {{
                const centerValX = canvas.width / 2;
                const centerValY = canvas.height / 2;
                const minDim = Math.min(canvas.width, canvas.height);
                
                ctx.clearRect(0, 0, canvas.width, canvas.height);
                
                rotationAngle += 0.01;
                wavePhase += waveFrequency;

                ctx.strokeStyle = currentHudColor;
                ctx.fillStyle = "transparent";
                ctx.lineWidth = 2;
                ctx.shadowColor = currentHudColor;
                ctx.shadowBlur = 10;

                const rOuter = minDim * 0.42;
                const rWave = minDim * 0.34;
                const rTicksOuter = minDim * 0.40;
                const rTicksInner = minDim * 0.37;

                // 1. Rotating Dash HUD Ring
                ctx.beginPath();
                ctx.arc(centerValX, centerValY, rOuter, 0, Math.PI * 2);
                ctx.setLineDash([8, 20]);
                ctx.stroke();

                // Rotate ticks
                ctx.save();
                ctx.translate(centerValX, centerValY);
                ctx.rotate(rotationAngle);
                ctx.beginPath();
                for (let i = 0; i < 8; i++) {{
                    ctx.rotate(Math.PI / 4);
                    ctx.moveTo(rTicksInner, 0);
                    ctx.lineTo(rTicksOuter, 0);
                }}
                ctx.setLineDash([]);
                ctx.stroke();
                ctx.restore();

                // 2. Complex Sine Wave Oscillation Ring
                ctx.beginPath();
                const points = 72;
                ctx.shadowBlur = 15;
                ctx.lineWidth = 3;
                
                for (let i = 0; i <= points; i++) {{
                    const angle = (i / points) * Math.PI * 2;
                    const sinVal = Math.sin(i * 0.5 + wavePhase);
                    const offset = sinVal * waveAmplitude;
                    const radius = rWave + offset;
                    
                    const x = centerValX + radius * Math.cos(angle);
                    const y = centerValY + radius * Math.sin(angle);
                    
                    if (i === 0) {{
                        ctx.moveTo(x, y);
                    }} else {{
                        ctx.lineTo(x, y);
                    }}
                }}
                ctx.closePath();
                ctx.stroke();

                requestAnimationFrame(animate);
            }}

            animate();
        </script>
    </body>
    </html>
    """
    return HTMLResponse(content=html_content, status_code=200)


@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    await manager.connect(websocket)
    try:
        while True:
            raw_data = await websocket.receive_text()
            data = json.loads(raw_data)
            msg_type = data.get("type")
            
            if msg_type == "set_language":
                lang = data.get("language")
                if lang in LANGUAGE_CODES:
                    manager.language = lang
                    await manager.log(f">> System target language updated to: {lang}", "success")
                    await manager.update_state(manager.current_state, manager.state_details)
            elif msg_type == "text_input":
                text = data.get("text", "").strip()
                if text:
                    threading.Thread(target=system_core.process_query, args=(text,), daemon=True).start()
            elif msg_type == "audio_input":
                base64_audio = data.get("audio", "")
                mime_type = data.get("mime", "audio/webm")
                if base64_audio:
                    threading.Thread(target=system_core.process_audio_query, args=(base64_audio, mime_type), daemon=True).start()
    except WebSocketDisconnect:
        manager.disconnect(websocket)
    except Exception as e:
        manager.disconnect(websocket)


# ─────────────────────────────────────────────────────────────────────────────
#  SYS LAUNCHER
# ─────────────────────────────────────────────────────────────────────────────
def open_browser():
    time.sleep(1.5)
    webbrowser.open("http://127.0.0.1:8000")


if __name__ == "__main__":
    threading.Thread(target=open_browser, daemon=True).start()
    uvicorn.run(app, host="127.0.0.1", port=8000, log_level="warning")
