#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WebServer.h>

namespace wat {

struct App;

// HTTP API + embedded single-page UI (firmware/web/index.html).
// API contract is mirrored by tools/mock_server.py - keep both in sync.
class WebUi {
 public:
  WebUi() : server_(80) {}
  void begin(App &app);
  void loop();

 private:
  bool authorized();
  void sendJson(int code, JsonDocument &doc);
  void sendError(int code, const String &message);
  bool parseBody(JsonDocument &doc);

  void handleIndex();
  void handleStatus();
  void handleGetConfig();
  void handlePostConfig();
  void handleAction();
  void handleHistory();
  void handleLog();
  void handleTime();
  void handleUpdateDone();
  void handleUpdateUpload();
  void handleNotFound();

  App *app_ = nullptr;
  WebServer server_;
  uint32_t rebootAt_ = 0;
  bool otaRejected_ = false;
};

}  // namespace wat
