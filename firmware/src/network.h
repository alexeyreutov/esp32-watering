#pragma once

#include <Arduino.h>
#include <DNSServer.h>

namespace wat {

struct App;

// Station mode with saved credentials; falls back to an access point
// "Watering-XXXX" (192.168.4.1, captive DNS) so the web UI is always
// reachable for first setup. Station keeps retrying in the background.
class Network {
 public:
  // stationOnly: silent NTP sync on a scheduled wake - no AP fallback,
  // no mDNS, nothing visible.
  void begin(App &app, bool stationOnly = false);
  void loop();
  void reconnect();  // after credentials change
  void shutdown();   // before deep sleep
  bool started() const { return app_ != nullptr; }

  bool stationConnected() const;
  bool apActive() const { return apActive_; }
  String apSsid() const;
  String stationIp() const;

 private:
  void startAp();
  void stopAp();

  App *app_ = nullptr;
  DNSServer dns_;
  bool apActive_ = false;
  bool wasConnected_ = false;
  uint32_t connectedSince_ = 0;
  uint32_t disconnectedSince_ = 0;
  uint32_t apDelayMs_ = 0;
  bool stationOnly_ = false;
};

}  // namespace wat
