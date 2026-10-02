#include "web_ui.h"

#include <Update.h>
#include <WiFi.h>

#include "app.h"
#include "core/battery.h"

extern const char kIndexHtml[] asm("_binary_web_index_html_start");
extern const char kIndexHtmlEnd[] asm("_binary_web_index_html_end");

namespace wat {

namespace {
constexpr const char *kFirmwareVersion = "0.1.0";
constexpr const char *kAdminUser = "admin";
constexpr size_t kChunkBytes = 1024;
constexpr uint16_t kDefaultLogLimit = 200;

bool readModule(JsonDocument &doc, uint8_t &out) {
  JsonVariant v = doc["module"];
  if (!v.is<int>()) {
    return false;
  }
  const int m = v.as<int>();
  if (m < 0 || m >= kMaxModules) {
    return false;
  }
  out = static_cast<uint8_t>(m);
  return true;
}
}  // namespace

void WebUi::begin(App &app) {
  app_ = &app;
  server_.on("/", HTTP_GET, [this] { handleIndex(); });
  server_.on("/api/status", HTTP_GET, [this] { handleStatus(); });
  server_.on("/api/config", HTTP_GET, [this] { handleGetConfig(); });
  server_.on("/api/config", HTTP_POST, [this] { handlePostConfig(); });
  server_.on("/api/action", HTTP_POST, [this] { handleAction(); });
  server_.on("/api/history", HTTP_GET, [this] { handleHistory(); });
  server_.on("/api/log", HTTP_GET, [this] { handleLog(); });
  server_.on("/api/time", HTTP_POST, [this] { handleTime(); });
  server_.on(
      "/api/update", HTTP_POST, [this] { handleUpdateDone(); }, [this] { handleUpdateUpload(); });
  server_.onNotFound([this] { handleNotFound(); });
  server_.begin();
}

void WebUi::loop() {
  server_.handleClient();
  if (rebootAt_ != 0 && static_cast<int32_t>(millis() - rebootAt_) >= 0) {
    app_->controller.emergencyStop();
    ESP.restart();
  }
}

bool WebUi::authorized() {
  app_->power.keepAwake();
  const String &pass = app_->config.adminPassword;
  if (pass.isEmpty() || server_.authenticate(kAdminUser, pass.c_str())) {
    return true;
  }
  server_.requestAuthentication(BASIC_AUTH, "Watering");
  return false;
}

void WebUi::sendJson(int code, JsonDocument &doc) {
  String body;
  serializeJson(doc, body);
  server_.sendHeader("Cache-Control", "no-store");
  server_.send(code, "application/json", body);
}

void WebUi::sendError(int code, const String &message) {
  JsonDocument doc;
  doc["ok"] = false;
  doc["error"] = message;
  sendJson(code, doc);
}

bool WebUi::parseBody(JsonDocument &doc) {
  if (deserializeJson(doc, server_.arg("plain")) != DeserializationError::Ok || !doc.is<JsonObject>()) {
    sendError(400, "Некорректный JSON");
    return false;
  }
  return true;
}

void WebUi::handleIndex() {
  if (!authorized()) {
    return;
  }
  server_.sendHeader("Cache-Control", "no-cache");
  // embed_txtfiles appends a NUL; it is not part of the page.
  server_.send_P(200, "text/html; charset=utf-8", kIndexHtml, kIndexHtmlEnd - kIndexHtml - 1);
}

void WebUi::handleStatus() {
  if (!authorized()) {
    return;
  }
  App &a = *app_;
  JsonDocument doc;
  doc["fw"] = kFirmwareVersion;
  doc["uptime"] = millis() / 1000;

  JsonObject time = doc["time"].to<JsonObject>();
  time["valid"] = a.time.valid();
  time["epoch"] = static_cast<uint32_t>(a.time.now());
  time["tz"] = a.config.timezone;
  if (a.time.valid()) {
    time["local"] = a.time.format(a.time.now());
  }

  const uint16_t mv = a.battery.readMillivolts(a.config.batteryCalibration);
  JsonObject battery = doc["battery"].to<JsonObject>();
  battery["mv"] = mv;
  battery["present"] = mv >= kBatteryAbsentMv;
  battery["pct"] = liIonPercent(mv);
  battery["check"] = a.config.batteryCheck;
  battery["minMv"] = a.config.batteryMinMv;
  // Under pump load the reading sags; the UI shows it as "under load".
  battery["underLoad"] = a.pump.isOn();

  JsonObject water = doc["water"].to<JsonObject>();
  water["present"] = a.water.waterPresent(a.config.waterPresentHigh);
  water["check"] = a.config.waterCheck;

  JsonObject ctl = doc["controller"].to<JsonObject>();
  ctl["state"] = a.controller.stateKey();
  ctl["module"] = a.controller.activeModule();
  ctl["kind"] = static_cast<int>(a.controller.activeKind());
  ctl["remainingSec"] = a.controller.remainingSeconds();
  ctl["queued"] = a.controller.queued();
  ctl["pumpOn"] = a.pump.isOn();
  ctl["lastBlock"] = pumpBlockKey(a.controller.lastBlock());

  JsonObject net = doc["net"].to<JsonObject>();
  net["sta"] = a.net.stationConnected();
  net["ssid"] = a.config.wifiSsid;
  net["ip"] = a.net.stationIp();
  net["rssi"] = a.net.stationConnected() ? WiFi.RSSI() : 0;
  net["ap"] = a.net.apActive();
  net["apSsid"] = a.net.apSsid();
  net["hostname"] = a.config.hostname;

  JsonObject power = doc["power"].to<JsonObject>();
  power["mode"] = a.power.modeKey();
  power["sleepInSec"] = a.power.secondsUntilSleep();
  const int32_t next = a.garden.secondsUntilNextCheck();
  power["nextWakeSec"] = next;

  JsonObject alarms = doc["alarms"].to<JsonObject>();
  alarms["noWater"] = a.power.noWaterAlarm();
  alarms["lowBattery"] = a.power.lowBatteryAlarm();

  JsonArray modules = doc["modules"].to<JsonArray>();
  for (uint8_t i = 0; i < kMaxModules; ++i) {
    const ModuleRuntime &r = a.garden.runtime(i);
    const ModuleConfig &m = a.config.modules[i];
    JsonObject o = modules.add<JsonObject>();
    o["index"] = i;
    o["address"] = ads::kBaseAddress + i;
    o["name"] = m.name;
    o["enabled"] = m.enabled;
    o["mode"] = moduleModeKey(m.mode);
    o["present"] = r.present;
    o["readingOk"] = r.readingOk;
    o["raw"] = r.raw;
    o["pct"] = r.pct;
    o["threshold"] = m.thresholdPct;
    o["calibrated"] = calibrationValid(m.calibration());
    o["lastMeasureTs"] = r.lastMeasureTs;
    o["lastWaterTs"] = r.lastWaterTs;
    o["lastWaterSec"] = r.lastWaterSec;
    o["nextCheckTs"] = a.garden.nextCheckEpoch(i);
    o["valveOpen"] = a.bus.openValveIndex() == static_cast<int8_t>(i);
  }
  sendJson(200, doc);
}

void WebUi::handleGetConfig() {
  if (!authorized()) {
    return;
  }
  JsonDocument doc;
  configToJson(app_->config, doc.to<JsonObject>());
  sendJson(200, doc);
}

void WebUi::handlePostConfig() {
  if (!authorized()) {
    return;
  }
  JsonDocument body;
  if (!parseBody(body)) {
    return;
  }
  const AppConfig before = app_->config;
  String error;
  if (!configFromJson(body.as<JsonObjectConst>(), app_->config, error)) {
    sendError(400, error);
    return;
  }
  if (!saveConfig(app_->config)) {
    sendError(500, "Настройки применены, но не сохранились во flash");
    return;
  }
  app_->log.add(Event::ConfigSaved, LogLevel::Info);

  const AppConfig &now = app_->config;
  if (now.timezone != before.timezone || now.ntpServer != before.ntpServer) {
    app_->time.begin(now.timezone, now.ntpServer);
  }
  const bool wifiChanged = now.wifiSsid != before.wifiSsid || now.wifiPassword != before.wifiPassword;

  JsonDocument doc;
  doc["ok"] = true;
  doc["wifiReconnect"] = wifiChanged;
  sendJson(200, doc);
  if (wifiChanged) {
    app_->net.reconnect();
  }
}

void WebUi::handleAction() {
  if (!authorized()) {
    return;
  }
  JsonDocument body;
  if (!parseBody(body)) {
    return;
  }
  App &a = *app_;
  const String cmd = body["cmd"] | "";
  uint8_t module = 0;

  if (cmd == "water" || cmd == "valve_test") {
    if (!readModule(body, module)) {
      sendError(400, "Не указан модуль");
      return;
    }
    const bool test = cmd == "valve_test";
    const uint16_t seconds = body["seconds"] | a.config.modules[module].durationSec;
    const WateringJob job{module, seconds, test ? JobKind::ValveTest : JobKind::Manual,
                          a.garden.runtime(module).pct};
    if (!a.controller.enqueue(job)) {
      sendError(409, "Не удалось поставить в очередь: модуль уже в очереди, очередь полна или авария клапана");
      return;
    }
  } else if (cmd == "measure") {
    if (!a.controller.idle()) {
      sendError(409, "Идёт полив — измерение после его окончания");
      return;
    }
    if (body["module"].is<int>() && body["module"].as<int>() >= 0) {
      if (!readModule(body, module)) {
        sendError(400, "Нет такого модуля");
        return;
      }
      a.garden.measure(module, true, kHistManual);
    } else {
      a.garden.measureAll(true);
    }
  } else if (cmd == "calibrate") {
    if (!readModule(body, module)) {
      sendError(400, "Не указан модуль");
      return;
    }
    if (!a.controller.idle()) {
      sendError(409, "Идёт полив — калибровка после его окончания");
      return;
    }
    const String point = body["point"] | "";
    if (point != "dry" && point != "wet") {
      sendError(400, "point: dry или wet");
      return;
    }
    String error;
    if (!a.garden.calibrate(module, point == "wet", error)) {
      sendError(500, error);
      return;
    }
  } else if (cmd == "stop") {
    a.controller.stop();
  } else if (cmd == "reboot") {
    rebootAt_ = millis() + 500;
  } else if (cmd == "sleep") {
    a.power.requestSleep();
  } else if (cmd == "clear_history") {
    a.history.clear();
  } else if (cmd == "clear_log") {
    a.log.clear();
  } else {
    sendError(400, "Неизвестная команда");
    return;
  }
  JsonDocument doc;
  doc["ok"] = true;
  sendJson(200, doc);
}

void WebUi::handleHistory() {
  if (!authorized()) {
    return;
  }
  const int moduleFilter = server_.hasArg("module") ? server_.arg("module").toInt() : -1;
  const uint32_t days = server_.hasArg("days") ? server_.arg("days").toInt() : 30;
  const bool timeValid = app_->time.valid();
  const uint32_t now = static_cast<uint32_t>(app_->time.now());
  const uint32_t since = (timeValid && days > 0 && now > days * 86400UL) ? now - days * 86400UL : 0;

  server_.sendHeader("Cache-Control", "no-store");
  server_.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server_.send(200, "application/json", "");
  String chunk;
  chunk.reserve(kChunkBytes + 96);
  chunk = String("{\"now\":") + now + ",\"timeValid\":" + (timeValid ? "true" : "false") + ",\"records\":[";
  bool first = true;
  app_->history.store().forEach([&](const void *p) {
    const HistoryRecord &r = *static_cast<const HistoryRecord *>(p);
    if (moduleFilter >= 0 && r.module != moduleFilter) {
      return true;
    }
    const bool uptime = r.flags & kHistUptimeStamp;
    if (since > 0 && (uptime || r.ts < since)) {
      return true;
    }
    char row[72];
    snprintf(row, sizeof(row), "%s[%lu,%u,%u,%d,%u,%u,%u]", first ? "" : ",", static_cast<unsigned long>(r.ts),
             r.module, r.moisturePct, r.raw, r.batteryMv, r.wateredSec, r.flags);
    first = false;
    chunk += row;
    if (chunk.length() >= kChunkBytes) {
      server_.sendContent(chunk);
      chunk = "";
    }
    return true;
  });
  chunk += "]}";
  server_.sendContent(chunk);
  server_.sendContent("");
}

void WebUi::handleLog() {
  if (!authorized()) {
    return;
  }
  const uint32_t limit = server_.hasArg("limit") ? server_.arg("limit").toInt() : kDefaultLogLimit;
  const uint32_t total = app_->log.store().count();
  const uint32_t skip = total > limit ? total - limit : 0;

  server_.sendHeader("Cache-Control", "no-store");
  server_.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server_.send(200, "application/json", "");
  String chunk;
  chunk.reserve(kChunkBytes + 96);
  chunk = String("{\"now\":") + static_cast<uint32_t>(app_->time.now()) + ",\"uptime\":" + millis() / 1000 +
          ",\"records\":[";
  uint32_t index = 0;
  bool first = true;
  app_->log.store().forEach([&](const void *p) {
    if (index++ < skip) {
      return true;
    }
    const LogRecord &r = *static_cast<const LogRecord *>(p);
    char row[80];
    snprintf(row, sizeof(row), "%s[%lu,%u,%u,%d,%ld,%ld]", first ? "" : ",", static_cast<unsigned long>(r.ts),
             r.code, r.level, r.module == kNoModule ? -1 : r.module, static_cast<long>(r.a),
             static_cast<long>(r.b));
    first = false;
    chunk += row;
    if (chunk.length() >= kChunkBytes) {
      server_.sendContent(chunk);
      chunk = "";
    }
    return true;
  });
  chunk += "]}";
  server_.sendContent(chunk);
  server_.sendContent("");
}

void WebUi::handleTime() {
  if (!authorized()) {
    return;
  }
  JsonDocument body;
  if (!parseBody(body)) {
    return;
  }
  const uint32_t epoch = body["epoch"] | 0UL;
  if (epoch < 1704067200UL) {
    sendError(400, "Некорректное время");
    return;
  }
  app_->time.setFromBrowser(static_cast<time_t>(epoch));
  app_->log.add(Event::TimeSynced, LogLevel::Info, kNoModule, 1);
  JsonDocument doc;
  doc["ok"] = true;
  sendJson(200, doc);
}

void WebUi::handleUpdateUpload() {
  HTTPUpload &up = server_.upload();
  if (up.status == UPLOAD_FILE_START) {
    // No response may be sent mid-upload; the verdict is reported in
    // handleUpdateDone().
    const String &pass = app_->config.adminPassword;
    otaRejected_ = !pass.isEmpty() && !server_.authenticate(kAdminUser, pass.c_str());
    if (otaRejected_) {
      return;
    }
    // Valves closed and pump off before flashing: the update ends in a reboot.
    app_->controller.emergencyStop();
    app_->log.add(Event::OtaStarted, LogLevel::Info);
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      Update.printError(Serial);
    }
  } else if (otaRejected_) {
    return;
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (Update.write(up.buf, up.currentSize) != up.currentSize) {
      Update.printError(Serial);
    }
  } else if (up.status == UPLOAD_FILE_END) {
    if (!Update.end(true)) {
      Update.printError(Serial);
    }
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
  }
}

void WebUi::handleUpdateDone() {
  if (otaRejected_) {
    otaRejected_ = false;
    server_.requestAuthentication(BASIC_AUTH, "Watering");
    return;
  }
  if (Update.hasError() || !Update.isFinished()) {
    app_->log.add(Event::OtaFailed, LogLevel::Error, kNoModule, Update.getError());
    sendError(500, String("Ошибка обновления: ") + Update.errorString());
    return;
  }
  app_->log.add(Event::OtaFinished, LogLevel::Info);
  JsonDocument doc;
  doc["ok"] = true;
  sendJson(200, doc);
  rebootAt_ = millis() + 1000;
}

void WebUi::handleNotFound() {
  // Captive portal: phones probe random hosts after joining the AP.
  if (app_->net.apActive() && server_.hostHeader() != WiFi.softAPIP().toString()) {
    server_.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
    server_.send(302, "text/plain", "");
    return;
  }
  sendError(404, "Не найдено");
}

}  // namespace wat
