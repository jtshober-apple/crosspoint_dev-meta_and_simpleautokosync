#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <cstdint>
#include <string>

class CrossPointState : public PersistableStore<CrossPointState> {
  CrossPointState() = default;

  friend class PersistableStore<CrossPointState>;

 public:
  static constexpr uint8_t SLEEP_RECENT_COUNT = 16;

  std::string openEpubPath;
  uint16_t recentSleepImages[SLEEP_RECENT_COUNT] = {};
  uint8_t recentSleepPos = 0;
  uint8_t recentSleepFill = 0;
  uint16_t recentOverlaySleepImages[SLEEP_RECENT_COUNT] = {};
  uint8_t recentOverlaySleepPos = 0;
  uint8_t recentOverlaySleepFill = 0;
  uint8_t readerActivityLoadCount = 0;
  bool lastSleepFromReader = false;
  // Set when the device sleeps from inside a book with KoSync credentials
  // configured and cleared once the upload succeeds. SleepActivity reads this
  // to draw the pending-sync X indicator and attempt a silent upload.
  bool kosyncUploadPending = false;
  // Pre-computed KoReader progress for the pending sleep-upload (xpath + percentage).
  // Valid only while kosyncUploadPending is true.
  std::string kosyncPendingXpath;
  float kosyncPendingPct = 0.0f;
  // MD5 document hash for the KoReader sync server, pre-computed on the main task
  // (so SleepActivity and any background logic never needs to re-read the EPUB from SD).
  std::string kosyncPendingDocHash;
  // Epub path and reader position for the pending sleep-upload.
  // Used by ActivityManager::goToSleep() to construct a KOReaderSyncActivity
  // that completes the upload before showing the sleep screen.
  std::string kosyncPendingEpubPath;
  std::string kosyncPendingChapterName;
  int kosyncPendingSpineIndex = 0;
  int kosyncPendingPage = 0;
  int kosyncPendingPageCount = 0;
  bool showBootScreen = true;
  // Transient (not serialized): set by KOReaderSyncActivity on a successful
  // manual upload so the next EpubReaderActivity skips the on-open auto-sync
  // and shows OK instead of immediately attempting (and likely failing) a
  // redundant re-sync right after the user just synced manually.
  bool kosyncJustSynced = false;

  static const char* getFilePath() { return "/.crosspoint/state.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  bool isRecentSleep(uint16_t idx, uint8_t checkCount) const;
  bool isRecentOverlaySleep(uint16_t idx, uint8_t checkCount) const;

  void pushRecentSleep(uint16_t idx);
  void pushRecentOverlaySleep(uint16_t idx);
};

#define APP_STATE CrossPointState::getInstance()
