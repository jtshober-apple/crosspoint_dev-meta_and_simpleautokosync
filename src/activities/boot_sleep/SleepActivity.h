#pragma once
#include <string>

#include "activities/Activity.h"

class Bitmap;
class HalFile;

class SleepActivity final : public Activity {
 public:
  explicit SleepActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool fromTimeout = false)
      : Activity("Sleep", renderer, mappedInput), fromTimeout(fromTimeout) {}
  void onEnter() override;

 private:
  void renderDefaultSleepScreen() const;
  void renderCustomSleepScreen() const;
  void renderCoverSleepScreen() const;
  void renderBitmapSleepScreen(const Bitmap& bitmap, bool preserveBackground = false) const;
  bool renderSleepOverlayFile(HalFile& file, const char* pathForLog) const;
  bool renderTransparentOverlayPng(const std::string& path) const;
  bool renderSleepOverlayPath(const std::string& path) const;
  void renderLastScreenSleepScreen() const;
  void renderTransparentCustomSleepScreen() const;
  void renderBlankSleepScreen() const;

  // Dispatches to the appropriate renderXxxSleepScreen() helper based on
  // settings and context (inverted frame, reader origin, etc.).
  void renderSleepScreenContent() const;
  // Draws the sync result indicator as a partial overlay after the main sleep
  // screen has already been committed to the display.
  // drawSyncPendingIndicator: minus sign — upload still pending or failed.
  // drawSyncSuccessIndicator: plus sign — upload completed this sleep.
  void drawSyncPendingIndicator() const;
  void drawSyncSuccessIndicator() const;
  // Full-screen verbose KoSync upload shown when sleeping from inside a book.
  // Draws per-network connection attempts and final result, then returns so the
  // normal sleep screen can render.  No-op when not sleeping from a book or
  // there is no pending upload.
  void doSleepKoSync();

  bool fromTimeout = false;
};
