#pragma once

// Silent WiFi connection helpers for auto-sync paths.
// Both functions set weConnected=true only when THIS call connected the radio
// (so the caller can disconnect it afterwards without dropping a session it
// didn't start).  Returns true when WiFi is usable (already up, or we joined).

// Fast path for book-open / 15-page sync: try ESP32 NVS last network only,
// 5-second timeout.
bool silentWifiConnectFast(bool& weConnected);

// Aggressive path for sleep sync: NVS last network first (5s), then every
// credential in WIFI_STORE (8s each).
bool silentWifiConnectAggressive(bool& weConnected);
