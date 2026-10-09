#include "SilentWifiConnect.h"

#include <Arduino.h>
#include <Logging.h>
#include <WiFi.h>

#include "WifiCredentialStore.h"

// Wait up to timeoutMs for WL_CONNECTED, polling every 100 ms.
static bool waitForConnect(unsigned long timeoutMs) {
  const unsigned long deadline = millis() + timeoutMs;
  while (WiFi.status() != WL_CONNECTED && millis() < deadline) {
    delay(100);
  }
  return WiFi.status() == WL_CONNECTED;
}

void silentWifiBegin() {
  // WiFi.begin() is non-blocking: the ESP32 WiFi stack associates in the
  // background.  The caller polls WiFi.status() for WL_CONNECTED.
  WiFi.mode(WIFI_STA);
  WiFi.begin();  // reconnects using NVS-stored SSID + passphrase
  LOG_DBG("WiFiConnect", "Begin (NVS last network, non-blocking)");
}

bool silentWifiConnectAggressive(bool& weConnected) {
  weConnected = false;

  if (WiFi.status() == WL_CONNECTED) return true;

  // NVS last network first.
  WiFi.mode(WIFI_STA);
  WiFi.begin();
  LOG_DBG("WiFiConnect", "Aggressive: NVS last network (8s)");
  if (waitForConnect(8000)) {
    LOG_INF("WiFiConnect", "Aggressive: connected via NVS");
    weConnected = true;
    return true;
  }
  WiFi.disconnect();
  delay(200);

  // Fall through to WIFI_STORE credentials.
  const size_t count = WIFI_STORE.getCredentialCount();
  if (count == 0) {
    LOG_DBG("WiFiConnect", "Aggressive: no stored credentials");
    return false;
  }

  const std::string lastSsid = WIFI_STORE.getLastConnectedSsid();
  auto tryCredential = [](const WifiCredential& cred, unsigned long timeoutMs) -> bool {
    LOG_DBG("WiFiConnect", "Trying: %s", cred.ssid.c_str());
    WiFi.begin(cred.ssid.c_str(), cred.password.c_str());
    if (waitForConnect(timeoutMs)) return true;
    WiFi.disconnect();
    delay(200);
    return false;
  };

  if (!lastSsid.empty()) {
    const auto cred = WIFI_STORE.findCredential(lastSsid);
    if (cred && tryCredential(*cred, 8000)) {
      LOG_INF("WiFiConnect", "Aggressive: connected (last: %s)", cred->ssid.c_str());
      weConnected = true;
      return true;
    }
  }

  for (size_t i = 0; i < count; ++i) {
    const auto cred = WIFI_STORE.getCredentialAt(i);
    if (!cred || cred->ssid == lastSsid) continue;
    if (tryCredential(*cred, 8000)) {
      WIFI_STORE.setLastConnectedSsid(cred->ssid);
      LOG_INF("WiFiConnect", "Aggressive: connected (%s)", cred->ssid.c_str());
      weConnected = true;
      return true;
    }
  }

  LOG_DBG("WiFiConnect", "Aggressive: all %zu credentials failed", count);
  return false;
}
