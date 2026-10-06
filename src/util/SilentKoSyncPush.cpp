#include "SilentKoSyncPush.h"

#include <KOReaderCredentialStore.h>
#include <KOReaderDocumentId.h>
#include <KOReaderSyncClient.h>
#include <Logging.h>

#include "CrossPointState.h"
#include "network/WifiPowerSaveGuard.h"

bool silentKoSyncUpload(const std::string& bookPath, const std::string& xpath, float percentage) {
  const DocumentMatchMethod method = KOREADER_STORE.getMatchMethod();
  const std::string documentHash = (method == DocumentMatchMethod::FILENAME)
                                       ? KOReaderDocumentId::calculateFromFilename(bookPath)
                                       : KOReaderDocumentId::calculate(bookPath);
  if (documentHash.empty()) {
    LOG_ERR("KOSync", "Silent upload: could not compute document hash for %s", bookPath.c_str());
    return false;
  }

  KOReaderProgress progress;
  progress.document = documentHash;
  progress.progress = xpath;
  progress.percentage = percentage;

  KOReaderSyncClient::Error result;
  {
    WifiPowerSaveGuard psGuard;
    result = KOReaderSyncClient::updateProgress(progress);
  }

  if (result == KOReaderSyncClient::OK) {
    LOG_INF("KOSync", "Silent sleep-upload succeeded for %s", bookPath.c_str());
    APP_STATE.kosyncUploadPending = false;
    APP_STATE.kosyncPendingXpath.clear();
    APP_STATE.kosyncPendingPct = 0.0f;
    APP_STATE.saveToFile();
    return true;
  }

  LOG_ERR("KOSync", "Silent sleep-upload failed (err=%d http=%d)", result, KOReaderSyncClient::lastHttpCode);
  return false;
}
