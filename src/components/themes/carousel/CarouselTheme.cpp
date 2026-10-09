#include "CarouselTheme.h"

#include <GfxRenderer.h>
#include <HalStorage.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "components/icons/cover.h"
#include "fontIds.h"

// Internal constants
namespace {
// Center cover occupies this fraction of the available width.
constexpr int CENTER_WIDTH_PERCENT = 60;
// Flanking covers are drawn at this fraction of the center cover size.
constexpr int FLANK_SCALE_PERCENT = 65;
// Visible strip of each flanking cover (fraction of its total width).
constexpr int FLANK_VISIBLE_PERCENT = 45;
// Gap between center cover and the flanking covers.
constexpr int COVER_GAP = 10;
// Inner padding around each cover border.
constexpr int COVER_BORDER = 2;
// Corner radius for the selection highlight.
constexpr int SELECTION_RADIUS = 8;
// Vertical drop of flanking covers relative to the center cover.
constexpr int FLANK_VERTICAL_OFFSET_PERCENT = 5;
}  // namespace

int CarouselTheme::homeCoverThumbHeight(const GfxRenderer& renderer) const {
  const int availW = renderer.getScreenWidth() - 2 * CarouselMetrics::values.contentSidePadding;
  const int centerW = availW * CENTER_WIDTH_PERCENT / 100;
  // Request a thumb tall enough to fill the center slot at a 0.65 aspect;
  // covers narrower than the slot are centered and cropped vertically.
  return std::max(CarouselMetrics::values.homeCoverHeight, centerW * 3 / 2 + 2);
}

// Draws one cover slot (center or flanker) from the BMP cache.
// slot       — the full rectangular slot that contains the cover image.
// clipSlot   — the visible sub-region (for partially-offscreen flankers).
// book       — source data; empty coverBmpPath → placeholder.
// thumbH     — requested BMP height (passed to getCoverThumbPath).
static void drawOneCover(GfxRenderer& renderer, Rect slot, Rect clipSlot, const RecentBook& book,
                         const int thumbH) {
  bool hasCover = !book.coverBmpPath.empty();
  if (hasCover) {
    const std::string bmpPath = UITheme::getCoverThumbPath(book.coverBmpPath, thumbH);
    HalFile file;
    if (Storage.openFileForRead("HOME", bmpPath, file)) {
      Bitmap bitmap(file);
      if (bitmap.parseHeaders() == BmpReaderError::Ok) {
        // Apply clip so the bitmap draw is confined to the visible strip.
        const auto prevClip = renderer.getClipRect();
        renderer.setClipRect(clipSlot.x, clipSlot.y, clipSlot.width, clipSlot.height);
        BaseTheme::drawCoverThumbFill(renderer, bitmap, slot);
        renderer.setClipRect(prevClip[0], prevClip[1], prevClip[2], prevClip[3]);
      } else {
        hasCover = false;
      }
      file.close();
    } else {
      hasCover = false;
    }
  }

  // Always draw the border rect (only within the visible clip).
  {
    const auto prevClip = renderer.getClipRect();
    renderer.setClipRect(clipSlot.x, clipSlot.y, clipSlot.width, clipSlot.height);
    renderer.drawRect(slot.x, slot.y, slot.width, slot.height, true);
    if (!hasCover) {
      BaseTheme::drawCoverPlaceholder(renderer, slot);
    }
    renderer.setClipRect(prevClip[0], prevClip[1], prevClip[2], prevClip[3]);
  }
}

void CarouselTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                                        const int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                                        bool& bufferRestored, std::function<bool()> storeCoverBuffer) const {
  if (recentBooks.empty()) {
    drawEmptyRecents(renderer, rect);
    return;
  }

  const int availW = rect.width;
  const int thumbH = homeCoverThumbHeight(renderer);

  // Center cover geometry
  const int centerW = availW * CENTER_WIDTH_PERCENT / 100;
  const int centerH = CarouselMetrics::values.homeCoverHeight;
  const int centerX = rect.x + (availW - centerW) / 2;
  const int centerY = rect.y;

  // Flanking cover geometry (scaled down from center)
  const int flankW = centerW * FLANK_SCALE_PERCENT / 100;
  const int flankH = centerH * FLANK_SCALE_PERCENT / 100;
  const int flankVertOffset = centerH * FLANK_VERTICAL_OFFSET_PERCENT / 100;
  const int flankY = centerY + (centerH - flankH) / 2 + flankVertOffset;

  // Visible strip width for each flanking cover
  const int flankVisible = flankW * FLANK_VISIBLE_PERCENT / 100;

  // Left flanker: right edge touches center left minus gap
  const int leftFlankRightEdge = centerX - COVER_GAP;
  const int leftFlankX = leftFlankRightEdge - flankW;
  const int leftClipX = std::max(rect.x, leftFlankRightEdge - flankVisible);

  // Right flanker: left edge touches center right plus gap
  const int rightFlankX = centerX + centerW + COVER_GAP;
  const int rightClipX = rightFlankX;
  const int rightClipRight = std::min(rect.x + rect.width, rightFlankX + flankVisible);

  if (!coverRendered) {
    // --- Draw left flanker (book[2] if available, else book[0]) ---
    if (recentBooks.size() >= 2) {
      const Rect leftSlot{leftFlankX, flankY, flankW, flankH};
      const Rect leftClip{leftClipX, flankY, leftFlankRightEdge - leftClipX, flankH};
      if (leftClip.width > 0) {
        drawOneCover(renderer, leftSlot, leftClip, recentBooks[1], thumbH);
      }
    }

    // --- Draw right flanker (book[1] if available) ---
    if (recentBooks.size() >= 3) {
      const Rect rightSlot{rightFlankX, flankY, flankW, flankH};
      const Rect rightClip{rightClipX, flankY, rightClipRight - rightClipX, flankH};
      if (rightClip.width > 0) {
        drawOneCover(renderer, rightSlot, rightClip, recentBooks[2], thumbH);
      }
    }

    // --- Draw center cover (always book[0]) ---
    {
      const Rect centerSlot{centerX, centerY, centerW, centerH};
      drawOneCover(renderer, centerSlot, centerSlot, recentBooks[0], thumbH);
    }

    coverBufferStored = storeCoverBuffer();
    coverRendered = coverBufferStored;
  }

  // --- Selection highlight on center cover ---
  const bool centerSelected = (selectorIndex == 0);
  if (centerSelected) {
    renderer.fillRoundedRect(centerX - COVER_BORDER, centerY - COVER_BORDER, centerW + 2 * COVER_BORDER,
                             centerH + 2 * COVER_BORDER, SELECTION_RADIUS, true, true, true, true, Color::LightGray);
    // Re-draw the center cover image over the highlight
    {
      const Rect centerSlot{centerX, centerY, centerW, centerH};
      drawOneCover(renderer, centerSlot, centerSlot, recentBooks[0], thumbH);
    }
  }

  // --- Title and author below center cover ---
  {
    const int textX = centerX;
    const int textW = centerW;
    const int textY = centerY + centerH + CarouselMetrics::values.verticalSpacing;

    auto titleLines = renderer.wrappedText(UI_12_FONT_ID, recentBooks[0].title.c_str(), textW, 2, EpdFontFamily::BOLD);
    const int titleLineH = renderer.getLineHeight(UI_12_FONT_ID);
    int y = textY;
    for (const auto& line : titleLines) {
      const int lw = renderer.getTextWidth(UI_12_FONT_ID, line.c_str(), EpdFontFamily::BOLD);
      renderer.drawText(UI_12_FONT_ID, textX + (textW - lw) / 2, y, line.c_str(), true, EpdFontFamily::BOLD);
      y += titleLineH;
    }
    if (!recentBooks[0].author.empty()) {
      y += 2;
      auto author = renderer.truncatedText(UI_10_FONT_ID, recentBooks[0].author.c_str(), textW);
      const int aw = renderer.getTextWidth(UI_10_FONT_ID, author.c_str());
      renderer.drawText(UI_10_FONT_ID, textX + (textW - aw) / 2, y, author.c_str(), true);
    }
  }

}
