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

bool silentWifiConnectFast(bool& weConnected) {
  weConnected = false;

  if (WiFi.status() == WL_CONNECTED) return true;

  // Use the ESP32 NVS last-connected network — no credential store needed.
  WiFi.mode(WIFI_STA);
  WiFi.begin();  // reconnects using NVS-stored SSID + passphrase
  LOG_DBG("WiFiConnect", "Fast: trying NVS last network (5s)");

  if (waitForConnect(5000)) {
    LOG_INF("WiFiConnect", "Fast: connected via NVS");
    weConnected = true;
    return true;
  }

  WiFi.disconnect();
  LOG_DBG("WiFiConnect", "Fast: NVS connect failed");
  return false;
}

bool silentWifiConnectAggressive(bool& weConnected) {
  weConnected = false;

  if (WiFi.status() == WL_CONNECTED) return true;

  // Try NVS last network first (fastest).
  WiFi.mode(WIFI_STA);
  WiFi.begin();
  LOG_DBG("WiFiConnect", "Aggressive: trying NVS last network (5s)");

  if (waitForConnect(5000)) {
    LOG_INF("WiFiConnect", "Aggressive: connected via NVS");
    weConnected = true;
    return true;
  }
  WiFi.disconnect();
  delay(200);

  // Fall through to WIFI_STORE credentials — wider net for sleep sync.
  const size_t count = WIFI_STORE.getCredentialCount();
  if (count == 0) {
    LOG_DBG("WiFiConnect", "Aggressive: no stored credentials");
    return false;
  }

  // Try last-connected SSID from the store first (may differ from NVS if the
  // user connected via the SSID picker rather than ESP32 native join).
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
