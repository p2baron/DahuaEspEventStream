#pragma once
/*
  DahuaEspEventStream.h
  - Minimal client for Dahua VTO event stream (eventManager.cgi?action=attach)
  - Supports multiple concurrent VTOs (one FreeRTOS task per instance)
  - Uses esp_http_client_perform() so Digest 401 handshake works
  - No Serial printing by default; optional debug via setDebugStream()
*/

#include <Arduino.h>
#include <esp_http_client.h>
#include <functional>

// ----- Compatibility shims for older ESP32 cores -----
#ifndef HTTP_EVENT_ERROR
  #define HTTP_EVENT_ERROR            0
  #define HTTP_EVENT_ON_CONNECTED     1
  #define HTTP_EVENT_HEADER_SENT      2
  #define HTTP_EVENT_ON_HEADER        3
  #define HTTP_EVENT_ON_DATA          4
  #define HTTP_EVENT_ON_FINISH        5
  #define HTTP_EVENT_DISCONNECTED     6
#endif
// -----------------------------------------------------

struct DahuaEspEventStreamConfig {
  String host;          // e.g. "10.11.12.20"
  uint16_t port = 80;   // 80 or 443 or custom
  bool https = false;   // false->HTTP, true->HTTPS
  String user = "admin";
  String pass = "password";
  uint16_t heartbeat_s = 5;
  bool useEncodedAll = true;          // true => codes=%5BAll%5D, false => codes=[All]
  uint32_t reconnect_delay_ms = 3000;
  uint32_t stream_timeout_ms = 120000; // perform timeout; server usually keeps open
};

enum class DahuaEspEventKind : uint8_t {
  Unknown = 0,
  DoorbellPress,
};

using DahuaEspEventCallback =
  std::function<void(DahuaEspEventKind kind, const String& rawLine, void* userCtx)>;

class DahuaEspEventStream {
public:
  DahuaEspEventStream();
  ~DahuaEspEventStream();

  // Configure once before start()
  void setConfig(const DahuaEspEventStreamConfig& cfg);
  const DahuaEspEventStreamConfig& config() const { return _cfg; }

  // Event callback (doorbell + raw lines)
  void onEvent(DahuaEspEventCallback cb, void* userCtx = nullptr);

  // Optional simple GET to verify credentials/connectivity
  bool testGetSystemInfo(String* outBody = nullptr, int* outStatus = nullptr);

  // Control
  bool start();  // spawns a FreeRTOS task for this instance
  void stop();   // stops and joins the task
  bool running() const { return _running; }

  // Debug
  // Pass &Serial or any Stream; nullptr (default) disables all debug output.
  void setDebugStream(Stream* s) { _dbg = s; }

private:
  DahuaEspEventStreamConfig _cfg;
  DahuaEspEventCallback _cb = nullptr;
  void* _cbCtx = nullptr;

  TaskHandle_t _task = nullptr;
  bool _running = false;
  bool _sawAnyData = false;
  String _lineBuf;

  Stream* _dbg = nullptr;  // debug output, e.g. &Serial (or nullptr for silent)

  // internal helpers
  static void _taskEntry(void* arg);
  void _runLoop();
  static esp_err_t _httpEvtTrampoline(esp_http_client_event_t* evt);
  esp_err_t _httpEvt(esp_http_client_event_t* evt);
  void _maybeFireEvent(const String& line);

  // debug helpers (safe when _dbg == nullptr)
  void _debug(const String& s);
  void _debugln(const String& s);
  void _debugf(const char* fmt, ...);
};
