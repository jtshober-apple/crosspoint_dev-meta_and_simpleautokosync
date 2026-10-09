#pragma once

// Silent WiFi connection helpers for auto-sync paths.

// Non-blocking: puts the radio into STA mode and calls WiFi.begin() (NVS last
// network).  Returns immediately — the ESP32 WiFi stack associates in the
// background.  The caller polls WiFi.status() == WL_CONNECTED and should call
// WiFi.disconnect() once it is done if it called this.
void silentWifiBegin();

// Blocking aggressive path for sleep sync: NVS last network first (8s), then
// every credential in WIFI_STORE (8s each) as a last-ditch sweep.
// Sets weConnected=true only when this call established the connection (so the
// caller can WiFi.disconnect() afterwards without dropping a live session).
bool silentWifiConnectAggressive(bool& weConnected);
