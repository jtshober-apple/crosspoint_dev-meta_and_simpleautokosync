#pragma once

#include <string>

/**
 * Online book metadata lookup via Open Library search API.
 *
 * Attempts to retrieve authoritative title, author, and cover image for a
 * book given a search query (typically the user-typed filename stem).
 * Network is required; the caller is responsible for ensuring WiFi is up
 * before calling fetch().
 *
 * Cover images are converted to BMP and written into the book's existing
 * CrossPoint cache directory (the same directory Epub uses for thumb_*.bmp),
 * so they are immediately available to the cover-grid home screen.
 */
struct BookMetadata {
  std::string title;   // empty if lookup failed / no result
  std::string author;  // empty if not found in result
  std::string coverBmpPath;  // path to written BMP file; empty if no cover
};

namespace BookMetadataFetcher {

/**
 * Query Open Library for the best matching book given a title search string.
 * Downloads and converts the cover image to BMP at <cachePath>/cover_legacy_v2.bmp
 * (matching the path Epub::getCoverBmpPath() produces) so the home cover grid
 * picks it up without any extra plumbing.
 *
 * @param query       Search terms — typically the user-typed filename stem.
 * @param cachePath   The book's CrossPoint cache directory (e.g. /.crosspoint/epub_<hash>).
 *                    Must already exist or cover download is skipped.
 * @return            Populated BookMetadata. title is empty on failure.
 */
BookMetadata fetch(const std::string& query, const std::string& cachePath);

/**
 * Attempt to join the best saved WiFi network if not already connected.
 * Uses the last-connected SSID from WifiCredentialStore with a 10-second
 * timeout. Returns true if WiFi is (or was already) connected.
 */
bool ensureWifiConnected();

}  // namespace BookMetadataFetcher
