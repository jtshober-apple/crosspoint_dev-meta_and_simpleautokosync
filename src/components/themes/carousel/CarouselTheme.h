#pragma once

#include "components/themes/lyra/LyraTheme.h"

class GfxRenderer;

namespace CarouselMetrics {
constexpr ThemeMetrics values = [] {
  ThemeMetrics v = LyraMetrics::values;
  // Three-slot carousel: center cover fills most of the screen width;
  // flanking covers are drawn at reduced scale and partially clipped.
  v.homeRecentBooksCount = 3;
  v.homeCoverHeight = 360;
  v.homeCoverTileHeight = 380;
  return v;
}();
}  // namespace CarouselMetrics

class CarouselTheme : public LyraTheme {
 public:
  void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           const int selectorIndex, bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                           std::function<bool()> storeCoverBuffer) const override;
  int homeCoverThumbHeight(const GfxRenderer& renderer) const override;
};
