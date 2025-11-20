#include "DahuaEspEventStream.h"
#include <Arduino.h>
#include <esp_log.h>

// Optional JSON parsing of data={...}. Comment these 3 lines out if you don't want ArduinoJson.
#include <ArduinoJson.h>
static StaticJsonDocument<1024> s_doc;

// ---------- Debug helpers ----------
void DahuaEspEventStream::_debug(const String& s) {
  if (_dbg) _dbg->print(s);
}
void DahuaEspEventStream::_debugln(const String& s) {
  if (_dbg) _dbg->println(s);
}
void DahuaEspEventStream::_debugf(const char* fmt, ...) {
  if (!_dbg) return;
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  _dbg->print(buf);
}

// ---------- ctor/dtor ----------
DahuaEspEventStream::DahuaEspEventStream() {}

DahuaEspEventStream::~DahuaEspEventStream() {
  stop();
}

// ---------- public API ----------
void DahuaEspEventStream::setConfig(const DahuaEspEventStreamConfig& cfg) {
  _cfg = cfg;
}

void DahuaEspEventStream::onEvent(DahuaEspEventCallback cb, void* userCtx) {
  _cb = cb;
  _cbCtx = userCtx;
}

bool DahuaEspEventStream::testGetSystemInfo(String* outBody, int* outStatus) {
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
  if (_cfg.https) {
    cfg.skip_cert_common_name_check = true;
    cfg.cert_pem = NULL;
  }

  esp_http_client_handle_t c = esp_http_client_init(&cfg);
  if (!c) {
    _debugln(F("[DahuaEspEventStream] testGetSystemInfo: init failed"));
    return false;
  }

  esp_err_t err = esp_http_client_perform(c);
  bool ok = false;
  if (err == ESP_OK) {
    int code = esp_http_client_get_status_code(c);
    if (outStatus) *outStatus = code;
    char buf[1025]; int r = esp_http_client_read(c, buf, 1024);
    if (r > 0 && outBody) {
      buf[r] = 0;
      *outBody = String(buf);
    }
    ok = (code == 200);
  } else {
    _debugf("[DahuaEspEventStream] testGetSystemInfo error: %s\n", esp_err_to_name(err));
  }

  esp_http_client_cleanup(c);
  return ok;
}

bool DahuaEspEventStream::start() {
  if (_running) return true;
  _running = true;
  BaseType_t r = xTaskCreate(_taskEntry, "dahua_vto", 12288, this, 1, &_task);
  if (r != pdPASS) {
    _running = false;
    _task = nullptr;
    _debugln(F("[DahuaEspEventStream] start: xTaskCreate failed"));
    return false;
  }
  return true;
}

void DahuaEspEventStream::stop() {
  if (!_running) return;
  _running = false;
  if (_task) {
    TaskHandle_t t = _task;
    _task = nullptr;
    // Cooperative wait for task to delete itself
    for (int i = 0; i < 50 && eTaskGetState(t) != eDeleted; ++i) {
      delay(20);
    }
  }
}

// ---------- internal helpers ----------
void DahuaEspEventStream::_taskEntry(void* arg) {
  DahuaEspEventStream* self = reinterpret_cast<DahuaEspEventStream*>(arg);
  if (self) self->_runLoop();
  vTaskDelete(nullptr);
}

static String makeAttachUrl(const DahuaEspEventStreamConfig& c, bool encoded) {
  String url = c.https ? "https://" : "http://";
  url += c.host;
  if ((c.https && c.port != 443) || (!c.https && c.port != 80)) {
    url += ":"; url += String(c.port);
  }
  url += "/cgi-bin/eventManager.cgi?action=attach&codes=";
  url += encoded ? "%5BAll%5D" : "[All]";
  url += "&heartbeat="; url += c.heartbeat_s;
  return url;
}

void DahuaEspEventStream::_runLoop() {
  // esp_log_level_set("*", ESP_LOG_VERBOSE); // optional, not routed to Serial

  while (_running) {
    for (int attempt = 0; attempt < 2 && _running; ++attempt) {
      bool encoded = (attempt == 0) ? _cfg.useEncodedAll : !_cfg.useEncodedAll;
      String url = makeAttachUrl(_cfg, encoded);
      _debugf("[DahuaEspEventStream %s] Connecting to %s\n",
              _cfg.host.c_str(), url.c_str());

      esp_http_client_config_t cfg = {};
      cfg.url = url.c_str();
      cfg.method = HTTP_METHOD_GET;
      cfg.username = _cfg.user.c_str();
      cfg.password = _cfg.pass.c_str();
      cfg.auth_type = HTTP_AUTH_TYPE_DIGEST;
      cfg.timeout_ms = _cfg.stream_timeout_ms;
      cfg.event_handler = _httpEvtTrampoline;
      cfg.user_data = this;
      cfg.transport_type = _cfg.https ? HTTP_TRANSPORT_OVER_SSL : HTTP_TRANSPORT_OVER_TCP;
      if (_cfg.https) {
        cfg.skip_cert_common_name_check = true;
        cfg.cert_pem = NULL;
      }

      _lineBuf = "";
      _sawAnyData = false;

      esp_http_client_handle_t client = esp_http_client_init(&cfg);
      if (!client) {
        _debugf("[DahuaEspEventStream %s] http_client_init failed\n", _cfg.host.c_str());
        delay(_cfg.reconnect_delay_ms);
        continue;
      }

      esp_http_client_set_header(client, "User-Agent", "ESP32");
      esp_http_client_set_header(client, "Accept", "*/*");
      esp_http_client_set_header(client, "Connection", "keep-alive");

      esp_err_t err = esp_http_client_perform(client);
      if (err == ESP_OK) {
        int code = esp_http_client_get_status_code(client);
        _debugf("[DahuaEspEventStream %s] perform finished, HTTP code: %d\n",
                _cfg.host.c_str(), code);
      } else {
        _debugf("[DahuaEspEventStream %s] perform error: %s\n",
                _cfg.host.c_str(), esp_err_to_name(err));
      }

      esp_http_client_cleanup(client);

      if (_sawAnyData) {
        _cfg.useEncodedAll = encoded; // remember working variant
      }

      if (!_running) break;
      _debugf("[DahuaEspEventStream %s] Reconnecting in %u ms...\n",
              _cfg.host.c_str(), _cfg.reconnect_delay_ms);
      delay(_cfg.reconnect_delay_ms);
    }
  }
}

esp_err_t DahuaEspEventStream::_httpEvtTrampoline(esp_http_client_event_t* evt) {
  DahuaEspEventStream* self = reinterpret_cast<DahuaEspEventStream*>(evt->user_data);
  return self ? self->_httpEvt(evt) : ESP_OK;
}

esp_err_t DahuaEspEventStream::_httpEvt(esp_http_client_event_t* evt) {
  switch (evt->event_id) {
    case HTTP_EVENT_ON_CONNECTED:
      _debugf("[DahuaEspEventStream %s] Connected\n", _cfg.host.c_str());
      break;
    case HTTP_EVENT_ON_HEADER: {
      int status = esp_http_client_get_status_code(evt->client);
      _debugf("[DahuaEspEventStream %s] HTTP code: %d\n", _cfg.host.c_str(), status);
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
      _debugf("[DahuaEspEventStream %s] HTTP finish\n", _cfg.host.c_str());
      break;
    case HTTP_EVENT_DISCONNECTED:
      _debugf("[DahuaEspEventStream %s] Disconnected\n", _cfg.host.c_str());
      break;
    default:
      break;
  }
  return ESP_OK;
}

// Very lightweight heuristics for doorbell press.
// You can extend this or inspect raw lines in your callback.
void DahuaEspEventStream::_maybeFireEvent(const String& line) {
  _debugf("[DahuaEspEventStream %s] %s\n", _cfg.host.c_str(), line.c_str());

  String L = line; L.toLowerCase();
  bool doorbell = false;

  if (L.indexOf("code=callnoanswered") >= 0 &&
      (L.indexOf("action=start") >= 0 || L.indexOf("action=pulse") >= 0)) doorbell = true;

  if (L.indexOf("code=call") >= 0 &&
      (L.indexOf("action=start") >= 0 || L.indexOf("action=pulse") >= 0)) doorbell = true;

  if (L.indexOf("code=doorbell") >= 0 &&
      (L.indexOf("action=start") >= 0 || L.indexOf("action=ring") >= 0 || L.indexOf("action=pulse") >= 0)) doorbell = true;

  if (L.indexOf("code=_dotalkaction_") >= 0 &&
      (L.indexOf("invite") >= 0 || L.indexOf("ring") >= 0)) doorbell = true;

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
    _cb(doorbell ? DahuaEspEventKind::DoorbellPress : DahuaEspEventKind::Unknown,
        line, _cbCtx);
  }
}
