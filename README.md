# DahuaEspEventStream
ESP32 Arduino library for streaming real-time events from Dahua VTO devices using Digest authentication. (eventManager.cgi)

**DahuaEspEventStream** is a lightweight, ESP-optimized Arduino library for connecting to **Dahua VTO / Doorbell / NVR / IPC event streams** via:

- `eventManager.cgi?action=attach`
- Digest Authentication (401 → MD5 handshake)
- Persistent streaming over HTTP or HTTPS

It supports:

- Multiple VTO devices simultaneously  
- Doorbell event detection  
- ESP32 / ESP32-C3 (Arduino framework)  
- Auto-reconnect, auto-detect `[All]` encoding  
- Very stable long-running connections  

No webserver required.  
No bloat — just clean, reliable event streaming.

---

## ✨ Features

- 🔄 Real-time continuous event streaming  
- 🔐 Automatic Digest Authentication  
- 🚪 Built-in doorbell/call detection  
- 💥 Non-blocking (runs in background FreeRTOS tasks)  
- 📡 Supports multiple Dahua devices at once  
- 📶 HTTP or HTTPS (self-signed OK)  
- 🧠 Auto-switch between `[All]` and `%5BAll%5D`  
- ⚙️ Lightweight and dependency-minimal  

---




