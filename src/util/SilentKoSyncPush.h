#pragma once

#include <string>

// Attempts a silent KoSync progress upload for a book whose progress was
// pre-computed and stored in APP_STATE before sleep.  Must be called while
// WiFi is already connected (WL_CONNECTED).  On success the pending-upload
// flag in APP_STATE is cleared and state is saved to SD.  Returns true if
// the upload succeeded.
bool silentKoSyncUpload(const std::string& bookPath, const std::string& xpath, float percentage);
