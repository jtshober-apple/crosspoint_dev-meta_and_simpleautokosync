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
  std::string title;         // empty if lookup failed / no result
  std::string author;        // empty if not found in result
  std::string coverBmpPath;  // path to written BMP file; empty if no cover
};

namespace BookMetadataFetcher {

// Result of the JSON search step only — no cover download.
struct BookSearchResult {
  std::string title;            // empty on failure or no match
  std::string author;           // empty if not in result
  int coverId = 0;              // Open Library cover_i (work-level); 0 = none
  std::string coverEditionKey;  // OLID of the edition whose cover to use (e.g. "OL12345M");
                                // preferred over coverId when non-empty
};

/**
 * Phase 1: HTTP + JSON search only. Returns title/author/coverId without
 * touching the filesystem. Caller can show a progress screen between phases.
 */
BookSearchResult fetchSearchResult(const std::string& query);

/**
 * Phase 2: Download and convert the Open Library cover image to BMP.
 * @param coverId          Open Library cover_i (work-level fallback); 0 = skip.
 * @param coverEditionKey  OLID of the matched edition (preferred; e.g. "OL12345M"); empty = use coverId.
 * @param cachePath        Book's cache directory; must already exist.
 * @param outCoverBmpPath  Set to the saved BMP path on success.
 * @return true if the cover was downloaded and converted successfully.
 */
bool downloadCover(int coverId, const std::string& coverEditionKey,
                   const std::string& cachePath, std::string& outCoverBmpPath);

/**
 * Convenience wrapper: calls fetchSearchResult() then downloadCover().
 * Prefer the two-phase calls when a progress screen is needed between steps.
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

/**
 * Fire-and-forget background fetch: joins WiFi if needed, queries Open Library,
 * and on success calls RECENT_BOOKS.addBook() to promote the online title/author/cover
 * over whatever embedded metadata was stored at rename time.
 *
 * Spawns a FreeRTOS task (8 KB stack) and returns immediately — never blocks the
 * caller. Only one background fetch runs at a time; duplicate calls while a fetch
 * is in flight are silently dropped.
 *
 * @param query      Search terms (typically the user-typed filename stem).
 * @param cachePath  The book's cache directory; must already exist for cover download.
 * @param bookPath   The book's new path after rename (used as the RECENT_BOOKS key).
 */
void launchBackgroundFetch(const std::string& query, const std::string& cachePath,
                           const std::string& bookPath);

/**
 * Pending-metadata flag: written when a blocking fetch fails (no WiFi / no OL result),
 * cleared on success. The flag file stores the original search query so the retry
 * can use the same terms on next book open.
 *
 * @param cachePath  The book's cache directory (/.crosspoint/epub_<hash>).
 * @param query      The search query to retry (typically the user-typed stem).
 */
void setMetadataPending(const std::string& cachePath, const std::string& query);
void clearMetadataPending(const std::string& cachePath);
bool hasMetadataPending(const std::string& cachePath);
std::string readPendingQuery(const std::string& cachePath);

}  // namespace BookMetadataFetcher
