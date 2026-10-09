#pragma once

#include "components/themes/lyra/LyraTheme.h"

class GfxRenderer;

namespace CarouselMetrics {
constexpr ThemeMetrics makeValues() {
  ThemeMetrics v = LyraMetrics::values;
  v.listRowHeight = 35;
  v.menuRowHeight = 64;
  v.menuSpacing = 8;
  v.homeTopPadding = 28;
  v.homeCoverHeight = 600;
  v.homeCoverTileHeight = 660;
  v.homeRecentBooksCount = 3;
  v.keyboardKeyHeight = 56;
  v.keyboardCenteredText = true;
  return v;
}
constexpr ThemeMetrics values = makeValues();
}  // namespace CarouselMetrics

class CarouselTheme : public LyraTheme {
 public:
  static constexpr int kCenterCoverW = 340;
  static constexpr int kCenterCoverH = CarouselMetrics::values.homeCoverHeight - 60;  // 540
  static constexpr int kCenterCoverVisualInset = 10;
  static constexpr int kBaseDisplayCenterW = (kCenterCoverW * 86) / 100;
  static constexpr int kBaseDisplayCenterH = (kCenterCoverH * 86) / 100;
  static constexpr int kDisplayCenterW =
      ((kBaseDisplayCenterW + 24) < kCenterCoverW) ? (kBaseDisplayCenterW + 24) : kCenterCoverW;
  static constexpr int kDisplayCenterH =
      ((kBaseDisplayCenterH + 24) < kCenterCoverH) ? (kBaseDisplayCenterH + 24) : kCenterCoverH;
  static constexpr int kCenterThumbW = kDisplayCenterW - kCenterCoverVisualInset * 2;
  static constexpr int kCenterThumbH = kDisplayCenterH - kCenterCoverVisualInset * 2;
  static constexpr int kSideCoverW = 200;
  static constexpr int kSideCoverH = CarouselMetrics::values.homeCoverHeight - 210;  // 390

  void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           const int selectorIndex, bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                           std::function<bool()> storeCoverBuffer) const override;
  void drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                      const std::function<std::string(int index)>& buttonLabel,
                      const std::function<UIIcon(int index)>& rowIcon) const override;
};
