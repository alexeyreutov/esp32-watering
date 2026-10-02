#include "network.h"

#include <ESPmDNS.h>
#include <WiFi.h>

#include "app.h"
#include "board_config.h"

namespace wat {

namespace {
constexpr uint32_t kInitialConnectMs = 15000;
constexpr uint32_t kApAfterDisconnectMs = 5UL * 60 * 1000;
constexpr uint32_t kApOffAfterConnectMs = 60000;
}  // namespace

String Network::apSsid() const {
  const uint64_t mac = ESP.getEfuseMac();
  char buf[20];
  snprintf(buf, sizeof(buf), "Watering-%04X", static_cast<unsigned>((mac >> 32) & 0xFFFF));
  return String(buf);
}

bool Network::stationConnected() const { return WiFi.status() == WL_CONNECTED; }

String Network::stationIp() const { return stationConnected() ? WiFi.localIP().toString() : String(); }

void Network::startAp() {
  if (apActive_) {
    return;
  }
  WiFi.mode(app_->config.wifiSsid.isEmpty() ? WIFI_AP : WIFI_AP_STA);
  WiFi.softAP(apSsid().c_str(), app_->config.apPassword.c_str());
  dns_.start(53, "*", WiFi.softAPIP());
  apActive_ = true;
  app_->log.add(Event::WifiApStarted, LogLevel::Info);
  Serial.printf("[net] AP %s at %s\n", apSsid().c_str(), WiFi.softAPIP().toString().c_str());
}

void Network::stopAp() {
  if (!apActive_) {
    return;
  }
  dns_.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  apActive_ = false;
}

void Network::begin(App &app, bool stationOnly) {
  shutdown();  // a silent NTP session can turn into a button session
  app_ = &app;
  stationOnly_ = stationOnly;
  // Non-blocking: Wi-Fi may be switched on by the button mid-watering, and
  // the controller loop must keep watching the pump meanwhile.
  apDelayMs_ = kInitialConnectMs;
  disconnectedSince_ = millis();
  wasConnected_ = false;
  WiFi.persistent(false);
  WiFi.setHostname(app_->config.hostname.c_str());
  if (stationOnly_) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(app_->config.wifiSsid.c_str(), app_->config.wifiPassword.c_str());
    return;
  }
  if (app_->config.wifiSsid.isEmpty()) {
    startAp();
  } else {
    WiFi.mode(WIFI_STA);
    if (board::kLimitWifiTxPower) {
      WiFi.setTxPower(WIFI_POWER_8_5dBm);
    }
    WiFi.setAutoReconnect(true);
    WiFi.begin(app_->config.wifiSsid.c_str(), app_->config.wifiPassword.c_str());
  }
  if (MDNS.begin(app_->config.hostname.c_str())) {
    MDNS.addService("http", "tcp", 80);
  }
}

void Network::reconnect() {
  WiFi.disconnect();
  if (app_->config.wifiSsid.isEmpty()) {
    startAp();
    return;
  }
  if (apActive_) {
    WiFi.mode(WIFI_AP_STA);
  }
  WiFi.begin(app_->config.wifiSsid.c_str(), app_->config.wifiPassword.c_str());
}

void Network::shutdown() {
  if (!started()) {
    return;
  }
  dns_.stop();
  if (!stationOnly_) {
    MDNS.end();
  }
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  apActive_ = false;
}

void Network::loop() {
  if (apActive_) {
    dns_.processNextRequest();
  }
  const uint32_t now = millis();
  const bool connected = stationConnected();
  if (connected && !wasConnected_) {
    connectedSince_ = now;
    app_->log.add(Event::WifiConnected, LogLevel::Info, kNoModule, WiFi.RSSI());
    Serial.printf("[net] station %s\n", WiFi.localIP().toString().c_str());
  }
  if (!connected && wasConnected_) {
    disconnectedSince_ = now;
    apDelayMs_ = kApAfterDisconnectMs;
  }
  wasConnected_ = connected;
  if (stationOnly_) {
    return;
  }

  // Keep the AP only while nobody is using it: a phone on the AP would lose
  // the page mid-configuration otherwise.
  if (connected && apActive_ && now - connectedSince_ > kApOffAfterConnectMs &&
      WiFi.softAPgetStationNum() == 0) {
    stopAp();
  }
  if (!connected && !apActive_ && now - disconnectedSince_ > apDelayMs_) {
    Serial.println("[net] no station connection, starting AP");
    startAp();
  }
}

}  // namespace wat
