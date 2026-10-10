#include "CarouselTheme.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Utf8.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "components/icons/blocks.h"
#include "components/icons/book.h"
#include "components/icons/bookmark.h"
#include "components/icons/cover.h"
#include "components/icons/folder.h"
#include "components/icons/hotspot.h"
#include "components/icons/library.h"
#include "components/icons/recent.h"
#include "components/icons/settings2.h"
#include "components/icons/transfer.h"
#include "components/icons/wifi.h"
#include "fontIds.h"

namespace {
// Cover layout
constexpr int kCenterCoverMaxW = CarouselTheme::kCenterCoverW;
constexpr int kCenterCoverMaxH = CarouselTheme::kCenterCoverH;
constexpr int kCenterThumbW = CarouselTheme::kCenterThumbW;
constexpr int kCenterThumbH = CarouselTheme::kCenterThumbH;
constexpr int kSideCoverMaxW = CarouselTheme::kSideCoverW;
constexpr int kSideCoverMaxH = CarouselTheme::kSideCoverH;
constexpr int kCoverTopPad = 18;
constexpr int kCenterCoverVisualInset = CarouselTheme::kCenterCoverVisualInset;
constexpr int kCarouselVerticalLift = 8;
constexpr int kBaseDisplayCenterW = CarouselTheme::kBaseDisplayCenterW;
constexpr int kBaseDisplayCenterH = CarouselTheme::kBaseDisplayCenterH;
constexpr int kDisplayCenterW = CarouselTheme::kDisplayCenterW;
constexpr int kDisplayCenterH = CarouselTheme::kDisplayCenterH;
constexpr int kNearSideInnerH = (kBaseDisplayCenterH * 90) / 100;
constexpr int kSideOutlineW = 2;
constexpr int kSideCornerRadius = 5;
constexpr int kCoverStackLift = 15;
constexpr int kCenterCoverTopInset = (((kCenterCoverMaxH - kDisplayCenterH) / 2) > kCoverStackLift)
                                         ? ((kCenterCoverMaxH - kDisplayCenterH) / 2) - kCoverStackLift
                                         : 0;

constexpr int kTitleFontId = UI_12_FONT_ID;
constexpr int kMenuLabelFontId = SMALL_FONT_ID;
constexpr int kDotSize = 8;
constexpr int kDotGap = 6;
constexpr int kTitleTopClearance = 4;
constexpr int kTitleDrawOffset = 5;
constexpr int kTitleBottomGap = 8;
constexpr int kMenuLabelTopGap = 3;
constexpr int kMenuLabelBottomGap = 4;
constexpr int kMenuRowDrop = 31;

constexpr int kCornerRadius = 6;
constexpr int kThinOutlineW = 1;
constexpr int kSelectionLineW = 3;
constexpr int kCenterOutlineW = 4;

constexpr int kMenuIconSize = 32;
constexpr int kMenuIconPad = 14;
constexpr int kHighlightPad = 7;
constexpr size_t kMenuLabelBufferSize = 96;
constexpr int kButtonHintsH = CarouselMetrics::values.buttonHintsHeight;

struct MenuLayoutMetrics {
  int tileH;
  int tileW;
  int labelLineHeight;
  int rowY;
  int labelY;
};

MenuLayoutMetrics computeMenuLayout(const GfxRenderer& renderer, int buttonCount) {
  const int tileH = kMenuIconPad + kMenuIconSize + kMenuIconPad;
  const int labelLineHeight = renderer.getLineHeight(kMenuLabelFontId);
  const int rowY = renderer.getScreenHeight() - kButtonHintsH - tileH - kMenuLabelTopGap - labelLineHeight -
                   kMenuLabelBottomGap + kMenuRowDrop;
  return {
      tileH, renderer.getScreenWidth() / buttonCount, labelLineHeight, rowY, rowY - kMenuLabelTopGap - labelLineHeight,
  };
}

std::atomic<int> lastCarouselSelectorIndex{-1};
Rect lastCenterCoverRect{0, 0, 0, 0};
Rect cachedCenterCoverRects[CarouselMetrics::values.homeRecentBooksCount];

Rect shrinkCenterCoverRect(const Rect& rect) {
  const int insetWidth = rect.width - kCenterCoverVisualInset * 2;
  const int insetHeight = rect.height - kCenterCoverVisualInset * 2;
  const int width = std::max(0, insetWidth);
  const int height = std::max(0, insetHeight);
  return Rect{rect.x + (rect.width - width) / 2, rect.y + (rect.height - height) / 2, width, height};
}

void removeLastUtf8Codepoint(char* text) {
  if (!text) return;
  const size_t len = strlen(text);
  if (len == 0) return;

  size_t lead = len - 1;
  while (lead > 0 && (static_cast<unsigned char>(text[lead]) & 0xC0) == 0x80) {
    --lead;
  }
  text[lead] = '\0';
}

void fitMenuLabel(const GfxRenderer& renderer, const char* label, int maxWidth, char* out, size_t outSize) {
  if (!out || outSize == 0) return;
  out[0] = '\0';
  if (!label || maxWidth <= 0) return;

  snprintf(out, outSize, "%s", label);
  out[outSize - 1] = '\0';
  const int safeLen = utf8SafeTruncateBuffer(out, static_cast<int>(strlen(out)));
  out[safeLen] = '\0';

  if (renderer.getTextWidth(kMenuLabelFontId, out, EpdFontFamily::REGULAR) <= maxWidth) {
    return;
  }

  constexpr char ellipsis[] = "\xe2\x80\xa6";
  char candidate[kMenuLabelBufferSize];
  while (out[0] != '\0') {
    removeLastUtf8Codepoint(out);
    snprintf(candidate, sizeof(candidate), "%s%s", out, ellipsis);
    candidate[sizeof(candidate) - 1] = '\0';
    if (renderer.getTextWidth(kMenuLabelFontId, candidate, EpdFontFamily::REGULAR) <= maxWidth) {
      snprintf(out, outSize, "%s", candidate);
      out[outSize - 1] = '\0';
      return;
    }
  }

  snprintf(out, outSize, "%s", ellipsis);
  out[outSize - 1] = '\0';
  if (renderer.getTextWidth(kMenuLabelFontId, out, EpdFontFamily::REGULAR) > maxWidth) {
    out[0] = '\0';
  }
}

Rect computeCenterCoverSlotRect(const GfxRenderer& renderer, Rect rect,
                                const std::vector<RecentBook>& recentBooks) {
  if (recentBooks.empty()) {
    const int screenW = renderer.getScreenWidth();
    const int fallbackX = (screenW - kDisplayCenterW) / 2;
    const int fallbackY = rect.y + kCoverTopPad + kCenterCoverTopInset - kCarouselVerticalLift;
    return Rect{fallbackX, fallbackY, kDisplayCenterW, kDisplayCenterH};
  }

  const int screenW = renderer.getScreenWidth();
  const int titleLineHeight = renderer.getLineHeight(kTitleFontId);
  const int reservedTitleBlockHeight = titleLineHeight * 2;
  const int titleY = rect.y + kTitleTopClearance;
  const int centerTileY = std::max(rect.y + kCoverTopPad, titleY + reservedTitleBlockHeight + kTitleBottomGap);
  const int centerDrawY = centerTileY + kCenterCoverTopInset - kCarouselVerticalLift;
  const int centerX = (screenW - kDisplayCenterW) / 2;
  return Rect{centerX, centerDrawY, kDisplayCenterW, kDisplayCenterH};
}

void drawMenuBookmarkIcon(const GfxRenderer& renderer, int x, int y, bool selected) {
  constexpr int ribbonWidth = 16;
  constexpr int ribbonHeight = 22;
  constexpr int notchSize = 6;
  const int iconX = x + (kMenuIconSize - ribbonWidth) / 2;
  const int iconY = y + 4;
  const int centerX = iconX + ribbonWidth / 2;

  const int polyX[5] = {iconX, iconX + ribbonWidth, iconX + ribbonWidth, centerX, iconX};
  const int polyY[5] = {iconY, iconY, iconY + ribbonHeight, iconY + ribbonHeight - notchSize, iconY + ribbonHeight};
  renderer.fillPolygon(polyX, polyY, 5, !selected);
}

void drawPerspectiveOutline(const GfxRenderer& renderer, int x, int y, int width, int leftHeight, int rightHeight) {
  const int maxHeight = std::max(leftHeight, rightHeight);
  const int topLeft = (maxHeight - leftHeight) / 2;
  const int topRight = (maxHeight - rightHeight) / 2;
  const int bottomLeft = topLeft + leftHeight - 1;
  const int bottomRight = topRight + rightHeight - 1;
  const int rightX = x + width - 1;

  renderer.drawLine(x, y + topLeft, rightX, y + topRight, kSideOutlineW, true);
  renderer.drawLine(x, y + bottomLeft, rightX, y + bottomRight, kSideOutlineW, true);
  renderer.fillRect(x, y + topLeft, kSideOutlineW, leftHeight, true);
  renderer.fillRect(rightX - kSideOutlineW + 1, y + topRight, kSideOutlineW, rightHeight, true);
  renderer.fillRect(x, y + maxHeight + 1, width, 2, false);
}

void fillPerspectiveSilhouette(const GfxRenderer& renderer, int x, int y, int width, int leftHeight, int rightHeight) {
  const int maxHeight = std::max(leftHeight, rightHeight);
  renderer.fillRect(x, y, width, maxHeight, false);
  for (int dx = 0; dx < width; ++dx) {
    const int columnHeight = (width <= 1) ? leftHeight : (leftHeight + ((rightHeight - leftHeight) * dx) / (width - 1));
    const int top = y + (maxHeight - columnHeight) / 2;
    renderer.fillRect(x + dx, top, 1, columnHeight, true);
  }
}

const uint8_t* iconForName(UIIcon icon) {
  switch (icon) {
    case UIIcon::Folder:
      return FolderIcon;
    case UIIcon::Book:
      return BookIcon;
    case UIIcon::Recent:
      return RecentIcon;
    case UIIcon::Settings:
      return Settings2Icon;
    case UIIcon::Transfer:
      return TransferIcon;
    case UIIcon::Library:
      return LibraryIcon;
    case UIIcon::Plugins:
      return BlocksIcon;
    case UIIcon::Wifi:
      return WifiIcon;
    case UIIcon::Hotspot:
      return HotspotIcon;
    case UIIcon::Bookmark:
      return BookmarkIcon;
    case UIIcon::Blocks:
      return BlocksIcon;
    default:
      return nullptr;
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// Carousel cover strip
// ---------------------------------------------------------------------------
void CarouselTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect,
                                        const std::vector<RecentBook>& recentBooks, const int selectorIndex,
                                        bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                                        std::function<bool()> storeCoverBuffer) const {
  (void)bufferRestored;
  if (recentBooks.empty()) {
    drawEmptyRecents(renderer, rect);
    return;
  }

  const int bookCount = static_cast<int>(recentBooks.size());
  const bool inCarouselRow = (selectorIndex < bookCount);
  const int lastSelectorIndex = lastCarouselSelectorIndex.load(std::memory_order_relaxed);
  int centerIdx = inCarouselRow ? selectorIndex : (lastSelectorIndex >= 0 ? lastSelectorIndex : 0);

  if (centerIdx >= bookCount) {
    centerIdx = bookCount - 1;
    coverRendered = false;
    coverBufferStored = false;
  }

  if (centerIdx != lastSelectorIndex) {
    coverRendered = false;
    coverBufferStored = false;
  }

  const int screenW = renderer.getScreenWidth();
  const int textMaxWidth = std::min(screenW - 40, kCenterCoverMaxW + 40);
  const auto titleLines =
      renderer.wrappedText(kTitleFontId, recentBooks[centerIdx].title.c_str(), textMaxWidth, 2, EpdFontFamily::BOLD);
  const int titleLineHeight = renderer.getLineHeight(kTitleFontId);
  const int titleBlockHeight = titleLineHeight * static_cast<int>(titleLines.size());
  const int reservedTitleBlockHeight = titleLineHeight * 2;
  const int titleY = rect.y + kTitleTopClearance;
  const Rect centerCoverSlotRect = computeCenterCoverSlotRect(renderer, rect, recentBooks);
  const int centerDrawY = centerCoverSlotRect.y;
  const int sideTileY = centerDrawY + (kDisplayCenterH - kNearSideInnerH) / 2;

  const int centerX = centerCoverSlotRect.x;
  constexpr int kSideUnderlapPx = 12;  // pixels the side cover slides under the center cover
  const int leftSideX = 0;
  const int leftSideW = centerX + kSideUnderlapPx;
  const int rightSideX = centerX + kDisplayCenterW - kSideUnderlapPx;
  const int rightSideW = screenW - rightSideX;
  const int sideH = kNearSideInnerH;

  auto drawCenterCover = [&](int bookIdx, Rect& outRect) -> bool {
    if (bookIdx < 0 || bookIdx >= bookCount) return false;
    const RecentBook& book = recentBooks[bookIdx];
    outRect = shrinkCenterCoverRect(centerCoverSlotRect);

    if (!book.coverBmpPath.empty()) {
      const std::string thumbPath = UITheme::getCoverThumbPath(book.coverBmpPath, CarouselMetrics::values.homeCoverHeight);
      HalFile file;
      if (Storage.openFileForRead("HOME", thumbPath, file)) {
        Bitmap bitmap(file);
        if (bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.getWidth() > 0 && bitmap.getHeight() > 0) {
          const float srcW = static_cast<float>(bitmap.getWidth());
          const float srcH = static_cast<float>(bitmap.getHeight());
          const float srcRatio = srcW / srcH;
          const float safeTargetHeight = outRect.height == 0 ? 1.0f : static_cast<float>(outRect.height);
          const float targetRatio = static_cast<float>(outRect.width) / safeTargetHeight;
          float cropX = 0.0f;
          float cropY = 0.0f;

          if (srcRatio > targetRatio) {
            cropX = std::max(0.0f, 1.0f - (targetRatio / srcRatio));
          } else if (srcRatio < targetRatio) {
            cropY = std::max(0.0f, 1.0f - (srcRatio / targetRatio));
          }

          renderer.fillRect(outRect.x - kCenterOutlineW, outRect.y - kCenterOutlineW,
                            outRect.width + 2 * kCenterOutlineW, outRect.height + 2 * kCenterOutlineW, false);
          renderer.drawBitmap(bitmap, outRect.x, outRect.y, outRect.width, outRect.height, cropX, cropY);
          renderer.maskRoundedRectOutsideCorners(outRect.x, outRect.y, outRect.width, outRect.height, kCornerRadius,
                                                 Color::White);
          return true;
        }
      }
    }

    renderer.fillRect(outRect.x - kCenterOutlineW, outRect.y - kCenterOutlineW, outRect.width + 2 * kCenterOutlineW,
                      outRect.height + 2 * kCenterOutlineW, false);
    renderer.drawRoundedRect(outRect.x, outRect.y, outRect.width, outRect.height, 1, kCornerRadius, true);
    renderer.fillRoundedRect(outRect.x, outRect.y + outRect.height / 3, outRect.width, 2 * outRect.height / 3,
                             kCornerRadius, /*roundTopLeft=*/false, /*roundTopRight=*/false,
                             /*roundBottomLeft=*/true, /*roundBottomRight=*/true, Color::Black);
    constexpr int kFallbackTitlePadX = 14;
    constexpr int kFallbackTitlePadBottom = 14;
    constexpr int kFallbackIconGap = 10;
    const int iconX = outRect.x + outRect.width / 2 - 16;
    const int iconY = outRect.y + outRect.height / 3 + 14;
    renderer.drawIcon(CoverIcon, iconX, iconY, 32);

    const int fallbackTitleX = outRect.x + kFallbackTitlePadX;
    const int fallbackTitleY = iconY + 32 + kFallbackIconGap;
    const int fallbackTitleW = outRect.width - kFallbackTitlePadX * 2;
    const int fallbackTitleH = outRect.y + outRect.height - kFallbackTitlePadBottom - fallbackTitleY;
    const int fallbackLineHeight = renderer.getLineHeight(UI_10_FONT_ID);
    const int maxFallbackLines = std::clamp(fallbackTitleH / std::max(1, fallbackLineHeight), 1, 4);
    const auto fallbackTitleLines =
        renderer.wrappedText(UI_10_FONT_ID, book.title.c_str(), fallbackTitleW, maxFallbackLines, EpdFontFamily::BOLD);
    const int fallbackBlockH = fallbackLineHeight * static_cast<int>(fallbackTitleLines.size());
    int fallbackLineY = fallbackTitleY + std::max(0, (fallbackTitleH - fallbackBlockH) / 2);
    for (const auto& line : fallbackTitleLines) {
      const int lineW = renderer.getTextWidth(UI_10_FONT_ID, line.c_str(), EpdFontFamily::BOLD);
      renderer.drawText(UI_10_FONT_ID, outRect.x + (outRect.width - lineW) / 2, fallbackLineY, line.c_str(), false,
                        EpdFontFamily::BOLD);
      fallbackLineY += fallbackLineHeight;
    }
    return false;
  };

  auto drawSideCover = [&](int bookIdx, int x, int width, int leftHeight, int rightHeight) -> bool {
    if (bookIdx < 0 || bookIdx >= bookCount) return false;
    const RecentBook& book = recentBooks[bookIdx];

    if (!book.coverBmpPath.empty()) {
      const std::string thumbPath = UITheme::getCoverThumbPath(book.coverBmpPath, CarouselMetrics::values.homeCoverHeight);
      HalFile file;
      if (Storage.openFileForRead("HOME", thumbPath, file)) {
        Bitmap bitmap(file);
        if (bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.getWidth() > 0 && bitmap.getHeight() > 0) {
          const int sideHeight = std::max(leftHeight, rightHeight);
          const float srcW = static_cast<float>(bitmap.getWidth());
          const float srcH = static_cast<float>(bitmap.getHeight());
          const float srcRatio = srcW / srcH;
          const float safeTargetH = sideHeight == 0 ? 1.0f : static_cast<float>(sideHeight);
          const float targetRatio = static_cast<float>(width) / safeTargetH;
          float cropX = 0.0f;
          float cropY = 0.0f;
          if (srcRatio > targetRatio) {
            cropX = std::max(0.0f, 1.0f - (targetRatio / srcRatio));
          } else if (srcRatio < targetRatio) {
            cropY = std::max(0.0f, 1.0f - (srcRatio / targetRatio));
          }
          renderer.fillRect(x, sideTileY, width, sideHeight, false);
          renderer.drawBitmap(bitmap, x, sideTileY, width, sideHeight, cropX, cropY);
          renderer.maskRoundedRectOutsideCorners(x, sideTileY, width, sideHeight, kSideCornerRadius, Color::White);
          drawPerspectiveOutline(renderer, x, sideTileY, width, leftHeight, rightHeight);
          return true;
        }
      }
    }

    fillPerspectiveSilhouette(renderer, x, sideTileY, width, leftHeight, rightHeight);
    renderer.maskRoundedRectOutsideCorners(x, sideTileY, width, std::max(leftHeight, rightHeight), kSideCornerRadius,
                                           Color::White);
    return false;
  };

  if (!coverRendered) {
    lastCarouselSelectorIndex.store(centerIdx, std::memory_order_relaxed);

    renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);

    const int leftNearIdx = (centerIdx + bookCount - 1) % bookCount;
    const int rightNearIdx = (centerIdx + 1) % bookCount;
    if (bookCount >= 2) drawSideCover(leftNearIdx, leftSideX, leftSideW, sideH, sideH);
    if (bookCount >= 3) drawSideCover(rightNearIdx, rightSideX, rightSideW, sideH, sideH);

    Rect centerCoverRect{};
    drawCenterCover(centerIdx, centerCoverRect);
    lastCenterCoverRect = centerCoverRect;
    if (centerIdx >= 0 && centerIdx < CarouselMetrics::values.homeRecentBooksCount) {
      cachedCenterCoverRects[centerIdx] = centerCoverRect;
    }

    const int textCenterX = centerCoverRect.x + centerCoverRect.width / 2;
    const int titleVerticalInset = (reservedTitleBlockHeight - titleBlockHeight) / 2;
    int currentTitleY = titleY + titleVerticalInset + kTitleDrawOffset;
    for (const auto& titleLine : titleLines) {
      const int titleW = renderer.getTextWidth(kTitleFontId, titleLine.c_str(), EpdFontFamily::BOLD);
      renderer.drawText(kTitleFontId, textCenterX - titleW / 2, currentTitleY, titleLine.c_str(), true,
                        EpdFontFamily::BOLD);
      currentTitleY += titleLineHeight;
    }

    const int dotsY = centerCoverSlotRect.y + centerCoverSlotRect.height + 8;
    const int totalDotsW = bookCount * kDotSize + (bookCount - 1) * kDotGap;
    int dotX = centerCoverSlotRect.x + (centerCoverSlotRect.width - totalDotsW) / 2;
    for (int i = 0; i < bookCount; ++i) {
      if (i == centerIdx)
        renderer.fillRect(dotX, dotsY, kDotSize, kDotSize, true);
      else
        renderer.drawRect(dotX, dotsY, kDotSize, kDotSize, true);
      dotX += kDotSize + kDotGap;
    }

    // Author line below dots
    const int authorY = dotsY + kDotSize + 5;
    const std::string& authorStr = recentBooks[centerIdx].author;
    if (!authorStr.empty()) {
      const int authorW = renderer.getTextWidth(UI_12_FONT_ID, authorStr.c_str(), EpdFontFamily::REGULAR);
      renderer.drawText(UI_12_FONT_ID, textCenterX - authorW / 2, authorY, authorStr.c_str(), true,
                        EpdFontFamily::REGULAR);
    }

    coverBufferStored = storeCoverBuffer();
    coverRendered = coverBufferStored;
  } else if (lastCenterCoverRect.width <= 0 || lastCenterCoverRect.height <= 0) {
    lastCenterCoverRect = shrinkCenterCoverRect(centerCoverSlotRect);
  }

  const int outlineW = inCarouselRow ? kSelectionLineW : kThinOutlineW;
  renderer.drawRoundedRect(lastCenterCoverRect.x, lastCenterCoverRect.y, lastCenterCoverRect.width,
                           lastCenterCoverRect.height, outlineW, kCornerRadius, true);
}

// ---------------------------------------------------------------------------
// Touch layout helpers — mirror computeMenuLayout so HomeActivity can build
// a colTouch grid that exactly matches the drawn icon positions.
// ---------------------------------------------------------------------------
int CarouselTheme::getMenuTileWidth(const GfxRenderer& renderer, int buttonCount) const {
  return buttonCount > 0 ? renderer.getScreenWidth() / buttonCount : 0;
}

int CarouselTheme::getMenuRowTop(const GfxRenderer& renderer) const {
  const int tileH = kMenuIconPad + kMenuIconSize + kMenuIconPad;
  const int labelLineHeight = renderer.getLineHeight(kMenuLabelFontId);
  return renderer.getScreenHeight() - kButtonHintsH - tileH - kMenuLabelTopGap - labelLineHeight -
         kMenuLabelBottomGap + kMenuRowDrop;
}

// ---------------------------------------------------------------------------
// Horizontal icon-only menu row — anchored to bottom of screen
// ---------------------------------------------------------------------------
void CarouselTheme::drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                                   const std::function<std::string(int index)>& buttonLabel,
                                   const std::function<UIIcon(int index)>& rowIcon) const {
  if (buttonCount <= 0) return;

  const MenuLayoutMetrics metrics = computeMenuLayout(renderer, buttonCount);

  for (int i = 0; i < buttonCount; ++i) {
    const int tileX = i * metrics.tileW;
    const int iconX = tileX + (metrics.tileW - kMenuIconSize) / 2;
    const int iconY = metrics.rowY + kMenuIconPad;

    const bool selected = (selectedIndex == i);
    if (selected) {
      const int highlightSize = kMenuIconSize + 2 * kHighlightPad;
      const int highlightY = metrics.rowY + (metrics.tileH - highlightSize) / 2;
      renderer.fillRoundedRect(iconX - kHighlightPad, highlightY, highlightSize, highlightSize, kCornerRadius,
                               Color::LightGray);
    }

    if (rowIcon != nullptr) {
      const UIIcon icon = rowIcon(i);
      if (icon == UIIcon::Bookmark) {
        drawMenuBookmarkIcon(renderer, iconX, iconY, selected);
      } else {
        const uint8_t* bmp = iconForName(icon);
        if (bmp != nullptr) {
          renderer.drawIcon(bmp, iconX, iconY, kMenuIconSize);
        }
      }
    }
  }

  renderer.fillRect(0, metrics.labelY, renderer.getScreenWidth(), metrics.labelLineHeight, false);
  if (selectedIndex >= 0 && selectedIndex < buttonCount && buttonLabel != nullptr) {
    const std::string label = buttonLabel(selectedIndex);
    char centeredLabel[kMenuLabelBufferSize];
    fitMenuLabel(renderer, label.c_str(), renderer.getScreenWidth() - 40, centeredLabel, sizeof(centeredLabel));
    const int labelWidth = renderer.getTextWidth(kMenuLabelFontId, centeredLabel, EpdFontFamily::REGULAR);
    renderer.drawText(kMenuLabelFontId, (renderer.getScreenWidth() - labelWidth) / 2, metrics.labelY + 2,
                      centeredLabel, true, EpdFontFamily::REGULAR);
  }
}
