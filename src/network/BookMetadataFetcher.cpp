#include "BookMetadataFetcher.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Txt.h>
#include <WiFi.h>
#include <freertos/task.h>

#include <string>

#include "HttpDownloader.h"
#include "RecentBooksStore.h"
#include "WifiCredentialStore.h"
#include "util/UrlUtils.h"

// Open Library search endpoint – no API key required.
// Returns JSON with docs[] array; we take the first hit.
// Doc: https://openlibrary.org/dev/docs/api#anchor_search
static constexpr const char* kSearchBase = "https://openlibrary.org/search.json?limit=1&fields=title,author_name,cover_i&q=";

// Cover image CDN – /b/id/<id>-L.jpg (L = large, ~500px tall)
static constexpr const char* kCoverBase = "https://covers.openlibrary.org/b/id/";

// Temp path for the downloaded JPEG before BMP conversion.
static constexpr const char* kCoverTmpJpg = "/.crosspoint/meta_cover_tmp.jpg";

namespace {

// Percent-encode characters that break a URL query string.
// We only need spaces, +, &, = and non-ASCII here (all other title chars
// are safe in the query component).  Using + for space matches OL convention.
std::string encodeQuery(const std::string& raw) {
  std::string out;
  out.reserve(raw.size() * 3);
  for (const unsigned char c : raw) {
    if (c == ' ') {
      out += '+';
    } else if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
               c == '.' || c == '~') {
      out += static_cast<char>(c);
    } else {
      // Percent-encode everything else
      const char hex[] = "0123456789ABCDEF";
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 0xF];
    }
  }
  return out;
}

// Background fetch state — only one fetch runs at a time.
struct FetchTaskArgs {
  std::string query;
  std::string cachePath;
  std::string bookPath;
};

static volatile bool s_fetchRunning = false;

static void fetchTaskFn(void* arg) {
  auto* args = static_cast<FetchTaskArgs*>(arg);

  if (BookMetadataFetcher::ensureWifiConnected()) {
    const BookMetadata meta = BookMetadataFetcher::fetch(args->query, args->cachePath);
    if (!meta.title.empty()) {
      LOG_INF("BookMeta", "Background fetch complete: '%s' by '%s'", meta.title.c_str(), meta.author.c_str());
      RECENT_BOOKS.addBook(args->bookPath, meta.title, meta.author, meta.coverBmpPath);
    } else {
      LOG_DBG("BookMeta", "Background fetch returned no title for '%s'", args->query.c_str());
    }
  } else {
    LOG_DBG("BookMeta", "Background fetch: no WiFi available");
  }

  delete args;
  s_fetchRunning = false;
  vTaskDelete(nullptr);
}

}  // namespace

bool BookMetadataFetcher::ensureWifiConnected() {
  if (WiFi.status() == WL_CONNECTED) return true;

  const auto cred = WIFI_STORE.findCredential(WIFI_STORE.getLastConnectedSsid());
  if (!cred) {
    LOG_DBG("BookMeta", "No saved WiFi credential – skipping online lookup");
    return false;
  }

  LOG_DBG("BookMeta", "Joining %s for metadata lookup…", cred->ssid.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.begin(cred->ssid.c_str(), cred->password.c_str());

  const unsigned long deadline = millis() + 10000;
  while (WiFi.status() != WL_CONNECTED && millis() < deadline) {
    delay(100);
  }

  if (WiFi.status() == WL_CONNECTED) {
    LOG_INF("BookMeta", "WiFi connected for metadata lookup");
    return true;
  }
  LOG_DBG("BookMeta", "WiFi join timed out – falling back to embedded metadata");
  return false;
}

BookMetadata BookMetadataFetcher::fetch(const std::string& query, const std::string& cachePath) {
  BookMetadata result;

  // TLS heap sanity check (wolfSSL needs ~40KB of contiguous free RAM)
  if (ESP.getFreeHeap() < HttpDownloader::MIN_TLS_FREE_HEAP ||
      ESP.getMaxAllocHeap() < HttpDownloader::MIN_TLS_MAX_ALLOC) {
    LOG_INF("BookMeta", "Heap too low for TLS metadata fetch (free=%u maxAlloc=%u)", (unsigned)ESP.getFreeHeap(),
             (unsigned)ESP.getMaxAllocHeap());
    return result;
  }

  // ── 1. Build and fire the search request ────────────────────────────────
  const std::string url = std::string(kSearchBase) + encodeQuery(query);
  LOG_DBG("BookMeta", "Search: %s", url.c_str());

  // Buffer the whole JSON response; OL search responses are typically 1–3 KB
  // with our field filter.  Cap at 8 KB to avoid blowing the heap.
  constexpr size_t kMaxJsonBytes = 8192;
  std::string jsonBuf;
  jsonBuf.reserve(512);
  bool overflow = false;

  const bool fetched = HttpDownloader::fetchUrl(url, [&](const uint8_t* data, size_t len) -> bool {
    if (jsonBuf.size() + len > kMaxJsonBytes) {
      overflow = true;
      return false;  // abort — response is suspiciously large
    }
    jsonBuf.append(reinterpret_cast<const char*>(data), len);
    return true;
  });

  if (!fetched && !overflow) {
    LOG_ERR("BookMeta", "HTTP search failed");
    return result;
  }
  if (overflow) {
    LOG_INF("BookMeta", "Search response too large (>%zu B) – truncated; trying to parse anyway", kMaxJsonBytes);
  }

  // ── 2. Parse JSON – pull just what we need ──────────────────────────────
  // {
  //   "numFound": N,
  //   "docs": [ { "title": "…", "author_name": ["…"], "cover_i": 12345 } ]
  // }
  JsonDocument filter;
  filter["numFound"] = true;
  filter["docs"][0]["title"] = true;
  filter["docs"][0]["author_name"][0] = true;
  filter["docs"][0]["cover_i"] = true;

  JsonDocument doc;
  const DeserializationError err =
      deserializeJson(doc, jsonBuf.c_str(), jsonBuf.size(), DeserializationOption::Filter(filter));

  if (err || !doc["numFound"].as<int>()) {
    LOG_INF("BookMeta", "No results for query '%s' (err=%s numFound=%d)", query.c_str(), err.c_str(),
             doc["numFound"].as<int>());
    return result;
  }

  const JsonObject hit = doc["docs"][0];
  result.title = hit["title"] | "";
  result.author = hit["author_name"][0] | "";
  const int coverId = hit["cover_i"] | 0;

  LOG_INF("BookMeta", "Hit: '%s' by '%s' (cover_i=%d)", result.title.c_str(), result.author.c_str(), coverId);

  if (result.title.empty()) {
    // OL returned a result but without a title – treat as no-match
    LOG_INF("BookMeta", "Result has empty title – discarding");
    result = BookMetadata{};
    return result;
  }

  // ── 3. Download and convert cover image ─────────────────────────────────
  if (coverId > 0 && !cachePath.empty() && Storage.exists(cachePath.c_str())) {
    // Match Epub::getCoverBmpPath() so the home cover grid picks it up.
    const std::string destBmp = cachePath + "/cover_legacy_v2.bmp";

    if (Storage.exists(destBmp.c_str())) {
      // A cover BMP already exists (from a prior epub parse); keep it.
      result.coverBmpPath = destBmp;
      LOG_DBG("BookMeta", "Existing cover BMP kept: %s", destBmp.c_str());
    } else {
      const std::string coverUrl =
          std::string(kCoverBase) + std::to_string(coverId) + "-L.jpg";
      LOG_DBG("BookMeta", "Downloading cover: %s", coverUrl.c_str());

      Storage.remove(kCoverTmpJpg);
      const auto dlResult = HttpDownloader::downloadToFile(coverUrl, kCoverTmpJpg);
      if (dlResult == HttpDownloader::OK) {
        if (Txt::convertCoverImageToBmp(kCoverTmpJpg, destBmp)) {
          result.coverBmpPath = destBmp;
          LOG_INF("BookMeta", "Cover saved: %s", destBmp.c_str());
        } else {
          LOG_INF("BookMeta", "Cover BMP conversion failed");
        }
        Storage.remove(kCoverTmpJpg);
      } else {
        LOG_INF("BookMeta", "Cover download failed (err=%d)", dlResult);
      }
    }
  }

  return result;
}

void BookMetadataFetcher::launchBackgroundFetch(const std::string& query, const std::string& cachePath,
                                                const std::string& bookPath) {
  if (s_fetchRunning) {
    LOG_DBG("BookMeta", "Background fetch already in flight – skipping for '%s'", query.c_str());
    return;
  }

  auto* args = new (std::nothrow) FetchTaskArgs{query, cachePath, bookPath};
  if (!args) {
    LOG_ERR("BookMeta", "OOM: FetchTaskArgs for '%s'", query.c_str());
    return;
  }

  s_fetchRunning = true;
  TaskHandle_t handle = nullptr;
  if (xTaskCreate(fetchTaskFn, "BookMetaFetch", 8192, args, 1, &handle) != pdPASS) {
    LOG_ERR("BookMeta", "Failed to create background fetch task");
    delete args;
    s_fetchRunning = false;
  } else {
    LOG_DBG("BookMeta", "Background fetch launched for '%s'", query.c_str());
  }
}
