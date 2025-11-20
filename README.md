# 📡 DahuaEspEventStream

**DahuaEspEventStream** is a lightweight, ESP-optimized Arduino library for receiving **real-time events** from **Dahua VTO / Doorbell / NVR / IPC and other Dahua devices** using:

- `eventManager.cgi?action=attach`
- Digest Authentication
- Persistent streaming over HTTP or HTTPS

It supports:

- Multiple devices simultaneously  
- Doorbell detection  
- ESP32 & ESP32-C3  
- Background FreeRTOS tasks  
- Auto-reconnect  
- Optional debug logging (`setDebugStream()`)  
- Zero Serial output unless debugging is enabled  

---

## 🏠 Why This Library Exists

This project started as a practical solution to a real problem:

> **“I needed an ESP32 that listens to the Dahua event stream so I can trigger a physical doorbell (via a relay) whenever someone presses either of my two VTO panels.”**

Dahua’s event stream is **not limited to VTOs**:

- Dahua **IP cameras** also emit events (motion, IVS rules, smart events)  
- Dahua **NVRs** do too (channel alarms, door events, system notifications)  
- Many other Dahua devices expose the same `eventManager.cgi` API  

So this library isn’t just for doorbells — it’s a **general ESP32 Dahua event streaming client**.

You can use it to trigger:

- A relay connected to a physical chime  
- LEDs / buzzers  
- MQTT messages  
- Home Assistant automations  
- Alarm systems  
- Custom logic reacting to IPC/NVR events  

---

## ✨ Features

- 🔄 Continuous real-time event streaming  
- 🔐 Automatic Digest Authentication handshake  
- 🚪 Reliable doorbell event detection  
- 🖼️ Works with VTO, IPC cameras, NVR, and other Dahua devices  
- 📡 Multi-device support (each in its own task)  
- 💥 Non-blocking (runs async in FreeRTOS)  
- 🧠 Auto-selects `[All]` vs `%5BAll%5D`  
- 📶 HTTP or HTTPS (self-signed Dahua cert OK)  
- 🔧 Debug logging optional and redirectable (`setDebugStream()`)   

---

🔍 Debugging

Debug logging is off by default.
Enable only when needed:

```vto.setDebugStream(&Serial);```

Redirect to another UART:

```vto.setDebugStream(&Serial2);```


Disable again:

```vto.setDebugStream(nullptr);```

🔐 HTTPS Support
```
cfg.https = true;
cfg.port  = 443;
```

Dahua self-signed certs accepted automatically.

📡 Event Detection Logic



Supported doorbell indicators:

Code=Doorbell
action=Ring, action=Start, action=Pulse
JSON: "Action":"Invite" or "Ring"
JSON: "State":1

You can always parse raw lines to detect custom events.





