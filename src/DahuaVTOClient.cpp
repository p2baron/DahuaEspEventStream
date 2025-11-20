#include "DahuaVTOClient.h"
#include <Arduino.h>
#include <esp_log.h>

// Optional JSON parsing of data={...}. Comment these 2 lines if you don't want ArduinoJson.
#include <ArduinoJson.h>
static StaticJsonDocument<1024> s_doc;

DahuaVTOClient::DahuaVTOClient() {}

DahuaVTOClient::~DahuaVTOClient() { stop(); }

void DahuaVTOClient::setConfig(const DahuaVTOConfig& cfg) { _cfg = cfg; }

void DahuaVTOClient::onEvent(DahuaEventCallback cb, void* userCtx) {
  _cb = cb; _cbCtx = userCtx;
}

bool DahuaVTOClient::testGetSystemInfo(String* outBody, int* outStatus) {
  String url = _cfg.https ? "https://" : "http://";
  url += _cfg.host;
  if ((_cfg.https && _cfg.port != 443) || (!_cfg.https && _cfg.port != 80)) {
    url += ":"; url += String(_cfg.port);
  }
  url += "/cgi-bin/magicBox.cgi?action=getSystemInfo";

  esp_http_client_config_t cfg = {};
  cfg.url = url.c_str();
  cfg.method = HTTP_METHOD_GET;
  cfg.username = _cfg.user.c_str();
  cfg.password = _cfg.pass.c_str();
  cfg.auth_type = HTTP_AUTH_TYPE_DIGEST;
  cfg.timeout_ms = 8000;
  cfg.transport_type = _cfg.https ? HTTP_TRANSPORT_OVER_SSL : HTTP_TRANSPORT_OVER_TCP;
  if (_cfg.https) { cfg.skip_cert_common_name_check = true; cfg.cert_pem = NULL; }

  esp_http_client_handle_t c = esp_http_client_init(&cfg);
  if (!c) return false;
  esp_err_t err = esp_http_client_perform(c);
  bool ok = false;
  if (err == ESP_OK) {
    int code = esp_http_client_get_status_code(c);
    if (outStatus) *outStatus = code;
    char buf[1025]; int r = esp_http_client_read(c, buf, 1024);
    if (r > 0 && outBody) { buf[r] = 0; *outBody = String(buf); }
    ok = (code == 200);
  }
  esp_http_client_cleanup(c);
  return ok;
}

bool DahuaVTOClient::start() {
  if (_running) return true;
  _running = true;
  BaseType_t r = xTaskCreate(_taskEntry, "dahua_vto", 12288, this, 1, &_task);
  if (r != pdPASS) { _running = false; _task = nullptr; return false; }
  return true;
}

void DahuaVTOClient::stop() {
  if (!_running) return;
  _running = false;
  if (_task) {
    // wait for task to end
    TaskHandle_t t = _task; _task = nullptr;
    // Cooperative wait
    for (int i=0; i<50 && eTaskGetState(t) != eDeleted; ++i) { delay(20); }
  }
}

void DahuaVTOClient::_taskEntry(void* arg) {
  reinterpret_cast<DahuaVTOClient*>(arg)->_runLoop();
  vTaskDelete(nullptr);
}

static String makeAttachUrl(const DahuaVTOConfig& c, bool encoded) {
  String url = c.https ? "https://" : "http://";
  url += c.host;
  if ((c.https && c.port != 443) || (!c.https && c.port != 80)) { url += ":"; url += String(c.port); }
  url += "/cgi-bin/eventManager.cgi?action=attach&codes=";
  url += encoded ? "%5BAll%5D" : "[All]";
  url += "&heartbeat="; url += c.heartbeat_s;
  return url;
}

void DahuaVTOClient::_runLoop() {
  // esp_log_level_set("*", ESP_LOG_VERBOSE); // optional
  while (_running) {
    for (int attempt = 0; attempt < 2 && _running; ++attempt) {
      bool encoded = (attempt == 0) ? _cfg.useEncodedAll : !_cfg.useEncodedAll;
      String url = makeAttachUrl(_cfg, encoded);
      Serial.printf("[VTO %s] Connecting to %s\n", _cfg.host.c_str(), url.c_str());

      esp_http_client_config_t cfg = {};
      cfg.url = url.c_str();
      cfg.method = HTTP_METHOD_GET;
      cfg.username = _cfg.user.c_str();
      cfg.password = _cfg.pass.c_str();
      cfg.auth_type = HTTP_AUTH_TYPE_DIGEST;
      cfg.timeout_ms = _cfg.stream_timeout_ms;
      cfg.event_handler = _httpEvtTrampoline;
      cfg.user_data = this; // pass 'this' to event handler
      cfg.transport_type = _cfg.https ? HTTP_TRANSPORT_OVER_SSL : HTTP_TRANSPORT_OVER_TCP;
      if (_cfg.https) { cfg.skip_cert_common_name_check = true; cfg.cert_pem = NULL; }

      _lineBuf = "";
      _sawAnyData = false;

      esp_http_client_handle_t client = esp_http_client_init(&cfg);
      if (!client) {
        Serial.printf("[VTO %s] http_client_init failed\n", _cfg.host.c_str());
        delay(_cfg.reconnect_delay_ms);
        continue;
      }

      esp_http_client_set_header(client, "User-Agent", "ESP32");
      esp_http_client_set_header(client, "Accept", "*/*");
      esp_http_client_set_header(client, "Connection", "keep-alive");

      esp_err_t err = esp_http_client_perform(client);
      if (err == ESP_OK) {
        int code = esp_http_client_get_status_code(client);
        Serial.printf("[VTO %s] perform finished, HTTP code: %d\n", _cfg.host.c_str(), code);
      } else {
        Serial.printf("[VTO %s] perform error: %s\n", _cfg.host.c_str(), esp_err_to_name(err));
      }

      esp_http_client_cleanup(client);

      if (_sawAnyData) {
        _cfg.useEncodedAll = encoded; // remember working variant
      }

      if (!_running) break;
      Serial.printf("[VTO %s] Reconnecting in %u ms...\n", _cfg.host.c_str(), _cfg.reconnect_delay_ms);
      delay(_cfg.reconnect_delay_ms);
    }
  }
}

esp_err_t DahuaVTOClient::_httpEvtTrampoline(esp_http_client_event_t* evt) {
  DahuaVTOClient* self = reinterpret_cast<DahuaVTOClient*>(evt->user_data);
  return self ? self->_httpEvt(evt) : ESP_OK;
}

esp_err_t DahuaVTOClient::_httpEvt(esp_http_client_event_t* evt) {
  switch (evt->event_id) {
    case HTTP_EVENT_ON_CONNECTED:
      Serial.printf("[VTO %s] Connected\n", _cfg.host.c_str());
      break;
    case HTTP_EVENT_ON_HEADER: {
      int status = esp_http_client_get_status_code(evt->client);
      Serial.printf("[VTO %s] HTTP code: %d\n", _cfg.host.c_str(), status);
      break;
    }
    case HTTP_EVENT_ON_DATA: {
      _sawAnyData = true;
      const char* data = (const char*)evt->data;
      int len = evt->data_len;
      for (int i = 0; i < len; ++i) {
        char c = data[i];
        if (c == '\r') continue;
        if (c == '\n') {
          if (_lineBuf.length()) {
            // Dispatch raw line
            _maybeFireEvent(_lineBuf);
            _lineBuf = "";
          }
        } else {
          _lineBuf += c;
          if (_lineBuf.length() > 4096) _lineBuf.remove(0, 2048);
        }
      }
      break;
    }
    case HTTP_EVENT_ON_FINISH:
      Serial.printf("[VTO %s] HTTP finish\n", _cfg.host.c_str());
      break;
    case HTTP_EVENT_DISCONNECTED:
      Serial.printf("[VTO %s] Disconnected\n", _cfg.host.c_str());
      break;
    default: break;
  }
  return ESP_OK;
}

// Very lightweight heuristics for doorbell press.
// You can extend this or surface raw lines to your app and decide there.
void DahuaVTOClient::_maybeFireEvent(const String& line) {
  Serial.printf("[VTO %s] %s\n", _cfg.host.c_str(), line.c_str());

  // Quick detection based on prior experiments
  String L = line; L.toLowerCase();
  bool doorbell = false;

  if (L.indexOf("code=callnoanswered") >= 0 && (L.indexOf("action=start") >= 0 || L.indexOf("action=pulse") >= 0)) doorbell = true;
  if (L.indexOf("code=call") >= 0       && (L.indexOf("action=start") >= 0 || L.indexOf("action=pulse") >= 0)) doorbell = true;
  if (L.indexOf("code=doorbell") >= 0   && (L.indexOf("action=start") >= 0 || L.indexOf("action=ring") >= 0 || L.indexOf("action=pulse") >= 0)) doorbell = true;
  if (L.indexOf("code=_dotalkaction_") >= 0 && (L.indexOf("invite") >= 0 || L.indexOf("ring") >= 0)) doorbell = true;

  // Optional: parse data={...} JSON; infer from Action/State
  if (!doorbell) {
    int dp = L.indexOf("data=");
    if (dp >= 0) {
      int js = L.indexOf('{', dp), je = L.lastIndexOf('}');
      if (js >= 0 && je > js) {
        String jsn = line.substring(js, je + 1); // original case
        DeserializationError er = deserializeJson(s_doc, jsn);
        if (!er) {
          String action = s_doc["Action"] | s_doc["action"] | "";
          if (action.equalsIgnoreCase("Invite") || action.equalsIgnoreCase("Ring")) doorbell = true;
          if (!doorbell) {
            int st = s_doc["State"] | s_doc["state"] | -1;
            if (st == 1) doorbell = true;
          }
        }
      }
    }
  }

  if (_cb) {
    _cb(doorbell ? DahuaEventKind::DoorbellPress : DahuaEventKind::Unknown, line, _cbCtx);
  }
}
