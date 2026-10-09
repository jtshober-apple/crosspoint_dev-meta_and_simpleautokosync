#pragma once

#include <string>

// Attempts a silent KoSync progress upload using a pre-computed document hash.
// The hash must be computed by the caller on the main task (not from a background
// FreeRTOS task) to avoid SD-card contention with the EPUB renderer.
// Must be called while WiFi is already connected (WL_CONNECTED).
// On success the pending-upload flag in APP_STATE is cleared and state is saved to SD.
// Returns true if the upload succeeded.
bool silentKoSyncUpload(const std::string& documentHash, const std::string& xpath, float percentage);
