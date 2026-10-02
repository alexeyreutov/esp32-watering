#include "config.h"

#include <LittleFS.h>

namespace wat {

namespace {
constexpr const char *kPath = "/config.json";
constexpr const char *kTmpPath = "/config.json.tmp";
constexpr uint8_t kConfigVersion = 1;

template <typename T>
bool readNumber(JsonObjectConst obj, const char *key, long lo, long hi, T &out, String &error,
                const char *label) {
  JsonVariantConst v = obj[key];
  if (v.isNull()) {
    return true;
  }
  if (!v.is<long>()) {
    error = String(label) + ": ожидается целое число";
    return false;
  }
  const long n = v.as<long>();
  if (n < lo || n > hi) {
    error = String(label) + ": допустимо " + lo + "…" + hi;
    return false;
  }
  out = static_cast<T>(n);
  return true;
}

bool readBool(JsonObjectConst obj, const char *key, bool &out) {
  JsonVariantConst v = obj[key];
  if (v.is<bool>()) {
    out = v.as<bool>();
  }
  return true;
}

bool readString(JsonObjectConst obj, const char *key, size_t maxBytes, String &out, String &error,
                const char *label) {
  JsonVariantConst v = obj[key];
  if (v.isNull()) {
    return true;
  }
  if (!v.is<const char *>()) {
    error = String(label) + ": ожидается строка";
    return false;
  }
  const char *s = v.as<const char *>();
  if (strlen(s) > maxBytes) {
    error = String(label) + ": слишком длинно (макс. " + maxBytes + " байт)";
    return false;
  }
  out = s;
  return true;
}

bool applyModule(JsonObjectConst in, ModuleConfig &m, uint16_t maxPumpSec, String &error) {
  readBool(in, "enabled", m.enabled);
  readBool(in, "lineSense", m.lineSense);
  if (!readString(in, "name", kMaxNameBytes, m.name, error, "Имя модуля")) {
    return false;
  }
  JsonVariantConst mode = in["mode"];
  if (!mode.isNull() && !moduleModeFromKey(mode.as<const char *>(), m.mode)) {
    error = "Неизвестный режим модуля";
    return false;
  }
  JsonVariantConst time = in["time"];
  if (!time.isNull()) {
    uint16_t minute = 0;
    if (!parseMinuteOfDay(time.as<const char *>(), minute)) {
      error = "Время полива: формат ЧЧ:ММ";
      return false;
    }
    m.minuteOfDay = minute;
  }
  return readNumber(in, "everyDays", 1, 30, m.everyNDays, error, "Период, дней") &&
         readNumber(in, "threshold", 0, 100, m.thresholdPct, error, "Порог влажности") &&
         readNumber(in, "duration", 1, maxPumpSec, m.durationSec, error, "Длительность полива") &&
         readNumber(in, "dryRaw", -32768, 32767, m.dryRaw, error, "Калибровка «сухо»") &&
         readNumber(in, "wetRaw", -32768, 32767, m.wetRaw, error, "Калибровка «мокро»");
}

void moduleToJson(const ModuleConfig &m, JsonObject out) {
  out["enabled"] = m.enabled;
  out["name"] = m.name;
  out["mode"] = moduleModeKey(m.mode);
  out["time"] = formatMinuteOfDay(m.minuteOfDay);
  out["everyDays"] = m.everyNDays;
  out["threshold"] = m.thresholdPct;
  out["duration"] = m.durationSec;
  out["dryRaw"] = m.dryRaw;
  out["wetRaw"] = m.wetRaw;
  out["calibrated"] = calibrationValid(m.calibration());
  out["lineSense"] = m.lineSense;
}

void serializeFull(const AppConfig &c, JsonDocument &doc) {
  configToJson(c, doc.to<JsonObject>());
  doc["version"] = kConfigVersion;
  doc["wifi"]["password"] = c.wifiPassword;
  doc["wifi"]["apPassword"] = c.apPassword;
  doc["security"]["adminPassword"] = c.adminPassword;
}
}  // namespace

String formatMinuteOfDay(uint16_t minuteOfDay) {
  char buf[6];
  snprintf(buf, sizeof(buf), "%02u:%02u", static_cast<unsigned>(minuteOfDay / 60 % 24),
           static_cast<unsigned>(minuteOfDay % 60));
  return String(buf);
}

bool parseMinuteOfDay(const char *text, uint16_t &out) {
  if (text == nullptr) {
    return false;
  }
  unsigned h = 0;
  unsigned m = 0;
  char tail = 0;
  if (sscanf(text, "%u:%u%c", &h, &m, &tail) != 2 || h > 23 || m > 59) {
    return false;
  }
  out = static_cast<uint16_t>(h * 60 + m);
  return true;
}

void configToJson(const AppConfig &c, JsonObject out) {
  JsonArray modules = out["modules"].to<JsonArray>();
  for (const ModuleConfig &m : c.modules) {
    moduleToJson(m, modules.add<JsonObject>());
  }

  JsonObject wifi = out["wifi"].to<JsonObject>();
  wifi["ssid"] = c.wifiSsid;
  wifi["passwordSet"] = !c.wifiPassword.isEmpty();
  wifi["hostname"] = c.hostname;

  JsonObject time = out["time"].to<JsonObject>();
  time["tz"] = c.timezone;
  time["ntp"] = c.ntpServer;

  JsonObject battery = out["battery"].to<JsonObject>();
  battery["check"] = c.batteryCheck;
  battery["minMv"] = c.batteryMinMv;
  battery["abortMv"] = c.batteryAbortMv;
  battery["calibration"] = c.batteryCalibration;

  JsonObject water = out["water"].to<JsonObject>();
  water["check"] = c.waterCheck;
  water["presentHigh"] = c.waterPresentHigh;

  JsonObject pump = out["pump"].to<JsonObject>();
  pump["maxSec"] = c.maxPumpSec;
  pump["valveLeadMs"] = c.valveLeadMs;
  pump["valveTailMs"] = c.valveTailMs;
  pump["catchUpMin"] = c.catchUpMinutes;

  JsonObject sleep = out["sleep"].to<JsonObject>();
  sleep["wifiMin"] = c.wifiSessionMinutes;
  sleep["ntpDays"] = c.ntpEveryDays;

  JsonObject security = out["security"].to<JsonObject>();
  security["adminPasswordSet"] = !c.adminPassword.isEmpty();
}

bool configFromJson(JsonObjectConst in, AppConfig &config, String &error) {
  AppConfig next = config;

  JsonObjectConst pump = in["pump"];
  if (!pump.isNull()) {
    if (!readNumber(pump, "maxSec", 5, 1800, next.maxPumpSec, error, "Макс. работа насоса") ||
        !readNumber(pump, "valveLeadMs", 0, 5000, next.valveLeadMs, error, "Задержка клапан→насос") ||
        !readNumber(pump, "valveTailMs", 0, 5000, next.valveTailMs, error, "Задержка насос→клапан") ||
        !readNumber(pump, "catchUpMin", 0, 720, next.catchUpMinutes, error, "Окно догоняющего полива")) {
      return false;
    }
  }

  JsonArrayConst modules = in["modules"];
  if (!modules.isNull()) {
    if (modules.size() > kMaxModules) {
      error = "Слишком много модулей";
      return false;
    }
    uint8_t i = 0;
    for (JsonObjectConst m : modules) {
      if (!m.isNull() && !applyModule(m, next.modules[i], next.maxPumpSec, error)) {
        error = String("Модуль ") + (i + 1) + ": " + error;
        return false;
      }
      ++i;
    }
  }
  for (ModuleConfig &m : next.modules) {
    if (m.durationSec > next.maxPumpSec) {
      m.durationSec = next.maxPumpSec;
    }
  }

  JsonObjectConst wifi = in["wifi"];
  if (!wifi.isNull()) {
    if (!readString(wifi, "ssid", 32, next.wifiSsid, error, "SSID") ||
        !readString(wifi, "password", 64, next.wifiPassword, error, "Пароль Wi-Fi") ||
        !readString(wifi, "apPassword", 64, next.apPassword, error, "Пароль точки доступа") ||
        !readString(wifi, "hostname", 31, next.hostname, error, "Имя хоста")) {
      return false;
    }
    if (next.apPassword.length() < 8) {
      error = "Пароль точки доступа: минимум 8 символов (WPA2)";
      return false;
    }
    if (next.hostname.isEmpty()) {
      next.hostname = "watering";
    }
  }

  JsonObjectConst time = in["time"];
  if (!time.isNull()) {
    if (!readString(time, "tz", 47, next.timezone, error, "Часовой пояс") ||
        !readString(time, "ntp", 63, next.ntpServer, error, "NTP-сервер")) {
      return false;
    }
  }

  JsonObjectConst battery = in["battery"];
  if (!battery.isNull()) {
    readBool(battery, "check", next.batteryCheck);
    if (!readNumber(battery, "minMv", 3000, 4200, next.batteryMinMv, error, "Мин. заряд для старта") ||
        !readNumber(battery, "abortMv", 2800, 4100, next.batteryAbortMv, error, "Порог аварийной остановки")) {
      return false;
    }
    JsonVariantConst cal = battery["calibration"];
    if (!cal.isNull()) {
      const float f = cal.as<float>();
      if (f < 0.8f || f > 1.2f) {
        error = "Калибровка АКБ: допустимо 0.8…1.2";
        return false;
      }
      next.batteryCalibration = f;
    }
  }
  if (next.batteryAbortMv >= next.batteryMinMv) {
    error = "Порог аварийной остановки должен быть ниже порога старта";
    return false;
  }

  JsonObjectConst water = in["water"];
  if (!water.isNull()) {
    readBool(water, "check", next.waterCheck);
    readBool(water, "presentHigh", next.waterPresentHigh);
  }

  JsonObjectConst sleep = in["sleep"];
  if (!sleep.isNull()) {
    if (!readNumber(sleep, "wifiMin", 1, 60, next.wifiSessionMinutes, error, "Wi-Fi после кнопки") ||
        !readNumber(sleep, "ntpDays", 0, 30, next.ntpEveryDays, error, "Синхронизация времени")) {
      return false;
    }
  }

  JsonObjectConst security = in["security"];
  if (!security.isNull() &&
      !readString(security, "adminPassword", 32, next.adminPassword, error, "Пароль администратора")) {
    return false;
  }

  config = next;
  return true;
}

bool loadConfig(AppConfig &config) {
  const bool ok = [&config]() {
    File f = LittleFS.open(kPath, "r");
    if (!f) {
      Serial.println("[config] no config.json, using defaults");
      return false;
    }
    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
      Serial.printf("[config] config.json is corrupt (%s), using defaults\n", err.c_str());
      return false;
    }
    String error;
    if (!configFromJson(doc.as<JsonObjectConst>(), config, error)) {
      Serial.printf("[config] config.json rejected: %s\n", error.c_str());
      return false;
    }
    Serial.println("[config] loaded");
    return true;
  }();
  for (uint8_t i = 0; i < kMaxModules; ++i) {
    if (config.modules[i].name.isEmpty()) {
      config.modules[i].name = String("Модуль ") + (i + 1);
    }
  }
  return ok;
}

bool saveConfig(const AppConfig &config) {
  JsonDocument doc;
  serializeFull(config, doc);
  File f = LittleFS.open(kTmpPath, "w");
  if (!f) {
    Serial.println("[config] cannot open tmp file");
    return false;
  }
  const size_t written = serializeJson(doc, f);
  f.close();
  if (written == 0) {
    LittleFS.remove(kTmpPath);
    return false;
  }
  // Write-then-rename: a power cut mid-save leaves the old file intact.
  if (!LittleFS.rename(kTmpPath, kPath)) {
    LittleFS.remove(kPath);
    if (!LittleFS.rename(kTmpPath, kPath)) {
      Serial.println("[config] rename failed");
      return false;
    }
  }
  return true;
}

}  // namespace wat
