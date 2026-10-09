#include "SilentKoSyncPush.h"

#include <Arduino.h>
#include <KOReaderCredentialStore.h>
#include <KOReaderDocumentId.h>
#include <KOReaderSyncClient.h>
#include <Logging.h>

#include "CrossPointState.h"
#include "network/WifiPowerSaveGuard.h"

enum class SyncAttemptResult { OK, AUTH_FAILED, TRANSIENT_FAILURE };

// One full bidirectional KoSync cycle (pull → push) inside an existing WiFi session.
static SyncAttemptResult trySync(const std::string& documentHash, const std::string& xpath, float percentage) {
  WifiPowerSaveGuard psGuard;

  // Pull first: validates auth and server reachability before the push,
  // mirroring the full KOReaderSyncActivity flow.
  KOReaderProgress remoteProgress;
  const auto pullResult = KOReaderSyncClient::getProgress(documentHash, remoteProgress);

  if (pullResult == KOReaderSyncClient::AUTH_FAILED) {
    LOG_ERR("KOSync", "Auto-sync pull: auth failed (http=%d) — check credentials", KOReaderSyncClient::lastHttpCode);
    return SyncAttemptResult::AUTH_FAILED;
  }
  if (pullResult == KOReaderSyncClient::NETWORK_ERROR) {
    LOG_ERR("KOSync", "Auto-sync pull: network error (http=%d)", KOReaderSyncClient::lastHttpCode);
    return SyncAttemptResult::TRANSIENT_FAILURE;
  }
  // NOT_FOUND is normal for a book the server hasn't seen yet — push creates the record.
  LOG_DBG("KOSync", "Auto-sync pull: result=%d http=%d remote=%.4f local=%.4f",
          pullResult, KOReaderSyncClient::lastHttpCode, remoteProgress.percentage, percentage);

  KOReaderProgress pushProgress;
  pushProgress.document = documentHash;
  pushProgress.progress = xpath;
  pushProgress.percentage = percentage;

  const auto pushResult = KOReaderSyncClient::updateProgress(pushProgress);
  LOG_DBG("KOSync", "Auto-sync push: result=%d http=%d", pushResult, KOReaderSyncClient::lastHttpCode);

  if (pushResult == KOReaderSyncClient::OK) return SyncAttemptResult::OK;
  if (pushResult == KOReaderSyncClient::AUTH_FAILED) return SyncAttemptResult::AUTH_FAILED;
  return SyncAttemptResult::TRANSIENT_FAILURE;
}

bool silentKoSyncUpload(const std::string& bookPath, const std::string& xpath, float percentage) {
  const DocumentMatchMethod method = KOREADER_STORE.getMatchMethod();
  const std::string documentHash = (method == DocumentMatchMethod::FILENAME)
                                       ? KOReaderDocumentId::calculateFromFilename(bookPath)
                                       : KOReaderDocumentId::calculate(bookPath);
  if (documentHash.empty()) {
    LOG_ERR("KOSync", "Auto-sync: could not compute document hash for %s", bookPath.c_str());
    return false;
  }

  auto result = trySync(documentHash, xpath, percentage);
  if (result == SyncAttemptResult::OK) {
    LOG_INF("KOSync", "Auto-sync succeeded for %s", bookPath.c_str());
    APP_STATE.kosyncUploadPending = false;
    APP_STATE.kosyncPendingXpath.clear();
    APP_STATE.kosyncPendingPct = 0.0f;
    APP_STATE.saveToFile();
    return true;
  }
  if (result == SyncAttemptResult::AUTH_FAILED) {
    // No point retrying — bad credentials won't get better on their own.
    return false;
  }

  // Transient failure: one retry after a short settle delay.
  LOG_DBG("KOSync", "Auto-sync: first attempt failed, retrying in 2s...");
  delay(2000);

  result = trySync(documentHash, xpath, percentage);
  if (result == SyncAttemptResult::OK) {
    LOG_INF("KOSync", "Auto-sync succeeded (retry) for %s", bookPath.c_str());
    APP_STATE.kosyncUploadPending = false;
    APP_STATE.kosyncPendingXpath.clear();
    APP_STATE.kosyncPendingPct = 0.0f;
    APP_STATE.saveToFile();
    return true;
  }

  LOG_ERR("KOSync", "Auto-sync failed after retry for %s", bookPath.c_str());
  return false;
}
