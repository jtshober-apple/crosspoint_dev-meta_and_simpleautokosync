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
// Uses the `title` parameter (not `q`) for a title-field-specific search,
// which returns far more precise matches than full-text.
// Doc: https://openlibrary.org/dev/docs/api#anchor_search
static constexpr const char* kSearchBase =
    "https://openlibrary.org/search.json?limit=1&fields=title,author_name,cover_i,cover_edition_key&title=";

// Cover image CDN.
// Preferred: /b/olid/<edition-olid>-L.jpg — edition-matched cover.
// Fallback:  /b/id/<cover_i>-L.jpg — work-level cover (any edition).
static constexpr const char* kCoverOlidBase = "https://covers.openlibrary.org/b/olid/";
static constexpr const char* kCoverIdBase   = "https://covers.openlibrary.org/b/id/";

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

  const size_t count = WIFI_STORE.getCredentialCount();
  if (count == 0) {
    LOG_DBG("BookMeta", "No saved WiFi credentials");
    return false;
  }

  WiFi.mode(WIFI_STA);

  // Try the last-connected network first for speed, then fall through to all others.
  const std::string lastSsid = WIFI_STORE.getLastConnectedSsid();
  auto tryCredential = [](const WifiCredential& cred, unsigned long timeoutMs) -> bool {
    LOG_DBG("BookMeta", "Trying WiFi: %s", cred.ssid.c_str());
    WiFi.begin(cred.ssid.c_str(), cred.password.c_str());
    const unsigned long deadline = millis() + timeoutMs;
    while (WiFi.status() != WL_CONNECTED && millis() < deadline) {
      delay(100);
    }
    if (WiFi.status() == WL_CONNECTED) return true;
    WiFi.disconnect();
    delay(200);
    return false;
  };

  if (!lastSsid.empty()) {
    const auto cred = WIFI_STORE.findCredential(lastSsid);
    if (cred && tryCredential(*cred, 10000)) {
      LOG_INF("BookMeta", "WiFi connected: %s", cred->ssid.c_str());
      return true;
    }
  }

  for (size_t i = 0; i < count; ++i) {
    const auto cred = WIFI_STORE.getCredentialAt(i);
    if (!cred || cred->ssid == lastSsid) continue;
    if (tryCredential(*cred, 8000)) {
      WIFI_STORE.setLastConnectedSsid(cred->ssid);
      LOG_INF("BookMeta", "WiFi connected: %s", cred->ssid.c_str());
      return true;
    }
  }

  LOG_DBG("BookMeta", "All %zu WiFi credentials failed", count);
  return false;
}

static constexpr const char* kMetaPendingFile = "/meta_pending";

void BookMetadataFetcher::setMetadataPending(const std::string& cachePath, const std::string& query) {
  if (cachePath.empty()) return;
  if (!Storage.exists(cachePath.c_str()) && !Storage.mkdir(cachePath.c_str())) {
    LOG_ERR("BookMeta", "Cannot create cache dir for pending flag: %s", cachePath.c_str());
    return;
  }
  const std::string flagPath = cachePath + kMetaPendingFile;
  HalFile f;
  if (Storage.openFileForWrite("BookMeta", flagPath.c_str(), f)) {
    f.write(reinterpret_cast<const uint8_t*>(query.c_str()), query.size());
  } else {
    LOG_ERR("BookMeta", "Failed to write pending flag: %s", flagPath.c_str());
  }
}

void BookMetadataFetcher::clearMetadataPending(const std::string& cachePath) {
  if (cachePath.empty()) return;
  Storage.remove((cachePath + kMetaPendingFile).c_str());
}

bool BookMetadataFetcher::hasMetadataPending(const std::string& cachePath) {
  if (cachePath.empty()) return false;
  return Storage.exists((cachePath + kMetaPendingFile).c_str());
}

std::string BookMetadataFetcher::readPendingQuery(const std::string& cachePath) {
  if (cachePath.empty()) return "";
  const std::string flagPath = cachePath + kMetaPendingFile;
  HalFile f;
  if (!Storage.openFileForRead("BookMeta", flagPath.c_str(), f)) return "";
  char buf[256];
  const int n = f.read(buf, sizeof(buf) - 1);
  if (n <= 0) return "";
  buf[n] = '\0';
  return buf;
}

BookMetadataFetcher::BookSearchResult BookMetadataFetcher::fetchSearchResult(const std::string& query) {
  BookSearchResult result;

  // TLS heap sanity check (wolfSSL needs ~40KB of contiguous free RAM)
  if (ESP.getFreeHeap() < HttpDownloader::MIN_TLS_FREE_HEAP ||
      ESP.getMaxAllocHeap() < HttpDownloader::MIN_TLS_MAX_ALLOC) {
    LOG_INF("BookMeta", "Heap too low for TLS fetch (free=%u maxAlloc=%u)", (unsigned)ESP.getFreeHeap(),
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

  // ── 2. Parse JSON ────────────────────────────────────────────────────────
  // { "numFound": N, "docs": [ { "title": "…", "author_name": ["…"],
  //                              "cover_i": 12345, "cover_edition_key": "OL12345M" } ] }
  JsonDocument filter;
  filter["numFound"] = true;
  filter["docs"][0]["title"] = true;
  filter["docs"][0]["author_name"][0] = true;
  filter["docs"][0]["cover_i"] = true;
  filter["docs"][0]["cover_edition_key"] = true;

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
  result.coverId = hit["cover_i"] | 0;
  result.coverEditionKey = hit["cover_edition_key"] | "";

  LOG_INF("BookMeta", "Hit: '%s' by '%s' (cover_i=%d editionKey='%s')",
          result.title.c_str(), result.author.c_str(), result.coverId, result.coverEditionKey.c_str());

  if (result.title.empty()) {
    LOG_INF("BookMeta", "Result has empty title – discarding");
    result = BookSearchResult{};
  }
  return result;
}

static constexpr const char* kCoverBmpFilename = "/cover_legacy_v2.bmp";

std::string BookMetadataFetcher::getCachedCoverBmpPath(const std::string& cachePath) {
  if (cachePath.empty()) return "";
  const std::string path = cachePath + kCoverBmpFilename;
  return Storage.exists(path.c_str()) ? path : "";
}

bool BookMetadataFetcher::downloadCover(int coverId, const std::string& coverEditionKey,
                                        const std::string& cachePath, std::string& outCoverBmpPath) {
  outCoverBmpPath.clear();
  const bool hasOlid = !coverEditionKey.empty();
  if (!hasOlid && coverId <= 0) return false;
  if (cachePath.empty() || !Storage.exists(cachePath.c_str())) return false;

  // Match Epub::getCoverBmpPath() so the home cover grid picks it up.
  const std::string destBmp = cachePath + kCoverBmpFilename;

  // If an embedded cover was already extracted during a prior book open, use it.
  // Never overwrite it with an OL cover — embedded art is always the right edition.
  if (Storage.exists(destBmp.c_str())) {
    LOG_INF("BookMeta", "Embedded cover exists; skipping OL fetch");
    outCoverBmpPath = destBmp;
    return true;
  }

  // Prefer the edition OLID URL (edition-matched cover) over the work-level cover_i.
  std::string coverUrl;
  if (hasOlid) {
    coverUrl = std::string(kCoverOlidBase) + coverEditionKey + "-L.jpg";
  } else {
    coverUrl = std::string(kCoverIdBase) + std::to_string(coverId) + "-L.jpg";
  }
  LOG_DBG("BookMeta", "Downloading cover: %s", coverUrl.c_str());

  Storage.remove(kCoverTmpJpg);
  const auto dlResult = HttpDownloader::downloadToFile(coverUrl, kCoverTmpJpg);
  if (dlResult == HttpDownloader::OK) {
    Storage.remove(destBmp.c_str());
    if (Txt::convertCoverImageToBmp(kCoverTmpJpg, destBmp)) {
      outCoverBmpPath = destBmp;
      LOG_INF("BookMeta", "Cover saved: %s", destBmp.c_str());
    } else {
      LOG_INF("BookMeta", "Cover BMP conversion failed");
    }
    Storage.remove(kCoverTmpJpg);
  } else {
    LOG_INF("BookMeta", "Cover download failed (err=%d)", dlResult);
    // Fall back to any existing cover so the field is not left empty.
    if (Storage.exists(destBmp.c_str())) outCoverBmpPath = destBmp;
  }
  return !outCoverBmpPath.empty();
}

BookMetadata BookMetadataFetcher::fetch(const std::string& query, const std::string& cachePath) {
  BookMetadata result;
  const BookSearchResult sr = fetchSearchResult(query);
  result.title = sr.title;
  result.author = sr.author;
  if (!sr.title.empty()) {
    downloadCover(sr.coverId, sr.coverEditionKey, cachePath, result.coverBmpPath);
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
