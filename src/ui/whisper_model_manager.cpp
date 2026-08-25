#include "whisper_model_manager.h"
#include <obs-module.h>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QUrl>
#include <QMetaObject>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#endif

WhisperModelManager &WhisperModelManager::instance()
{
    static WhisperModelManager s_instance;
    return s_instance;
}

WhisperModelManager::WhisperModelManager(QObject *parent)
    : QObject(parent)
{
    initCatalog();
    refreshModelStatuses();
}

WhisperModelManager::~WhisperModelManager()
{
    cancelDownload(m_currentDownloadingFile);
    if (m_downloadThread.joinable()) {
        m_downloadThread.join();
    }
}

void WhisperModelManager::initCatalog()
{
    const QString baseUrl = "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/";

    m_models = {
        {"tiny", "Tiny (Rápido)", "ggml-tiny.bin", "75 MB", baseUrl + "ggml-tiny.bin", "Multilingüe", false, false, 0, 0, 0},
        {"base", "Base (Recomendado)", "ggml-base.bin", "142 MB", baseUrl + "ggml-base.bin", "Multilingüe", false, false, 0, 0, 0},
        {"small", "Small (Alta Calidad)", "ggml-small.bin", "466 MB", baseUrl + "ggml-small.bin", "Multilingüe", false, false, 0, 0, 0},
        {"medium", "Medium (Preciso)", "ggml-medium.bin", "1.5 GB", baseUrl + "ggml-medium.bin", "Multilingüe", false, false, 0, 0, 0},
        {"large-v3-turbo", "Large v3 Turbo (Óptimo)", "ggml-large-v3-turbo.bin", "1.5 GB", baseUrl + "ggml-large-v3-turbo.bin", "Multilingüe", false, false, 0, 0, 0},
        {"large-v3", "Large v3 (Máxima Calidad)", "ggml-large-v3.bin", "2.9 GB", baseUrl + "ggml-large-v3.bin", "Multilingüe", false, false, 0, 0, 0},

        // Quantized models
        {"base-q5_1", "Base Q5_1 (Cuantizado)", "ggml-base-q5_1.bin", "57 MB", baseUrl + "ggml-base-q5_1.bin", "Cuantizado", false, false, 0, 0, 0},
        {"small-q5_1", "Small Q5_1 (Cuantizado)", "ggml-small-q5_1.bin", "182 MB", baseUrl + "ggml-small-q5_1.bin", "Cuantizado", false, false, 0, 0, 0},
        {"medium-q5_0", "Medium Q5_0 (Cuantizado)", "ggml-medium-q5_0.bin", "515 MB", baseUrl + "ggml-medium-q5_0.bin", "Cuantizado", false, false, 0, 0, 0},
        {"large-v3-turbo-q5_0", "Large v3 Turbo Q5_0", "ggml-large-v3-turbo-q5_0.bin", "547 MB", baseUrl + "ggml-large-v3-turbo-q5_0.bin", "Cuantizado", false, false, 0, 0, 0},

        // English only
        {"tiny.en", "Tiny.en (Solo Inglés)", "ggml-tiny.en.bin", "75 MB", baseUrl + "ggml-tiny.en.bin", "Solo Inglés", false, false, 0, 0, 0},
        {"base.en", "Base.en (Solo Inglés)", "ggml-base.en.bin", "142 MB", baseUrl + "ggml-base.en.bin", "Solo Inglés", false, false, 0, 0, 0},
        {"small.en", "Small.en (Solo Inglés)", "ggml-small.en.bin", "466 MB", baseUrl + "ggml-small.en.bin", "Solo Inglés", false, false, 0, 0, 0}
    };
}

QString WhisperModelManager::getModelsDirectory() const
{
    // 1. User config path (always writable without admin privileges)
    char *cfgDir = obs_module_config_path("models");
    if (cfgDir) {
        QString dir = QString::fromUtf8(cfgDir);
        bfree(cfgDir);
        QDir().mkpath(dir);
        return dir;
    }

    // 2. Standard user directory in AppData (always writable)
    QString appDataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QString fallbackDir = appDataDir + "/obs-plugins/obs-vocalis-captions/models";
    QDir().mkpath(fallbackDir);
    return fallbackDir;
}

const std::vector<WhisperModelInfo> &WhisperModelManager::getModels() const
{
    return m_models;
}

WhisperModelInfo WhisperModelManager::getModelInfo(const QString &fileName) const
{
    for (const auto &m : m_models) {
        if (m.fileName == fileName || m.id == fileName) {
            return m;
        }
    }
    return WhisperModelInfo();
}

bool WhisperModelManager::isModelDownloaded(const QString &fileName) const
{
    // 1. Check user writable download directory (config_path)
    QString userPath = getModelsDirectory() + "/" + fileName;
    QFileInfo fiUser(userPath);
    if (fiUser.exists() && fiUser.size() > 1024 * 1024) {
        return true;
    }

    // 2. Check bundled OBS module directory (Program Files or bundled data)
    char rel[256];
    snprintf(rel, sizeof(rel), "models/%s", fileName.toUtf8().constData());
    char *abs = obs_module_file(rel);
    if (abs) {
        QFileInfo fiBundle(QString::fromUtf8(abs));
        bfree(abs);
        if (fiBundle.exists() && fiBundle.size() > 1024 * 1024) {
            return true;
        }
    }

    return false;
}

QString WhisperModelManager::getModelPath(const QString &fileName) const
{
    // 1. If present in user config dir, return that
    QString userPath = getModelsDirectory() + "/" + fileName;
    QFileInfo fiUser(userPath);
    if (fiUser.exists() && fiUser.size() > 1024 * 1024) {
        return userPath;
    }

    // 2. If present in bundled obs module dir, return that
    char rel[256];
    snprintf(rel, sizeof(rel), "models/%s", fileName.toUtf8().constData());
    char *abs = obs_module_file(rel);
    if (abs) {
        QString bundlePath = QString::fromUtf8(abs);
        bfree(abs);
        if (QFile::exists(bundlePath)) {
            return bundlePath;
        }
    }

    return userPath;
}

void WhisperModelManager::refreshModelStatuses()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto &m : m_models) {
        m.isDownloaded = isModelDownloaded(m.fileName);
        m.isDownloading = (m_isDownloading.load() && m.fileName == m_currentDownloadingFile);
    }
    emit modelListChanged();
}

bool WhisperModelManager::startDownload(const QString &fileName)
{
    if (m_isDownloading.load()) {
        return false; // Only 1 concurrent download
    }

    auto it = std::find_if(m_models.begin(), m_models.end(), [&](const WhisperModelInfo &m) {
        return m.fileName == fileName;
    });
    if (it == m_models.end()) {
        return false;
    }

    if (m_downloadThread.joinable()) {
        m_downloadThread.join();
    }

    QString dir = getModelsDirectory();
    QDir().mkpath(dir);

    QString finalPath = dir + "/" + fileName;
    QString tempPath = finalPath + ".part";
    QString url = it->url;

    m_cancelRequested = false;
    m_isDownloading = true;
    m_currentDownloadingFile = fileName;

    it->isDownloading = true;
    it->progressPercent = 0;
    it->bytesReceived = 0;
    it->bytesTotal = 0;

    refreshModelStatuses();

    m_downloadThread = std::thread(&WhisperModelManager::downloadWorker, this, fileName, url, finalPath, tempPath);
    return true;
}

void WhisperModelManager::cancelDownload(const QString &fileName)
{
    if (m_isDownloading.load() && (fileName.isEmpty() || fileName == m_currentDownloadingFile)) {
        m_cancelRequested = true;
    }
}

bool WhisperModelManager::deleteModel(const QString &fileName)
{
    cancelDownload(fileName);

    bool removed = false;

    // 1. Remove from user models directory (config path)
    QString userPath = getModelsDirectory() + "/" + fileName;
    if (QFile::exists(userPath)) {
        removed = QFile::remove(userPath) || removed;
    }

    // 2. Remove from bundled module directory if present and writable
    char rel[256];
    snprintf(rel, sizeof(rel), "models/%s", fileName.toUtf8().constData());
    char *abs = obs_module_file(rel);
    if (abs) {
        QString bundlePath = QString::fromUtf8(abs);
        bfree(abs);
        if (QFile::exists(bundlePath)) {
            removed = QFile::remove(bundlePath) || removed;
        }
    }

    refreshModelStatuses();
    return removed;
}

void WhisperModelManager::downloadWorker(const QString &fileName, const QString &url, const QString &finalPath, const QString &tempPath)
{
    bool success = false;
    QString errorMsg;

#ifdef _WIN32
    HINTERNET hSession = WinHttpOpen(L"OBS-AI-Translator/1.0",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        errorMsg = "No se pudo inicializar la conexión HTTPS (WinHTTP).";
    } else {
        DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
        WinHttpSetOption(hSession, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));

        QString currentUrl = url;
        int redirectCount = 0;
        const int maxRedirects = 10;

        QFile outFile(tempPath);
        if (!outFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            errorMsg = "No se pudo crear el archivo temporal de destino en AppData.";
        } else {
            while (redirectCount < maxRedirects && !m_cancelRequested.load()) {
                QUrl qurl(currentUrl);
                std::wstring host = qurl.host().toStdWString();
                std::wstring path = (qurl.path() + (qurl.hasQuery() ? "?" + qurl.query() : "")).toStdWString();
                INTERNET_PORT port = qurl.port(qurl.scheme().toLower() == "https" ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT);
                bool isHttps = (qurl.scheme().toLower() == "https");

                HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), port, 0);
                if (!hConnect) {
                    errorMsg = QString("Error al conectar con el servidor: %1").arg(qurl.host());
                    break;
                }

                DWORD reqFlags = isHttps ? WINHTTP_FLAG_SECURE : 0;
                HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(),
                                                        NULL, WINHTTP_NO_REFERER,
                                                        WINHTTP_DEFAULT_ACCEPT_TYPES, reqFlags);
                if (!hRequest) {
                    WinHttpCloseHandle(hConnect);
                    errorMsg = "Error al abrir la solicitud HTTP.";
                    break;
                }

                // Send request
                if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                        WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
                    !WinHttpReceiveResponse(hRequest, NULL)) {
                    WinHttpCloseHandle(hRequest);
                    WinHttpCloseHandle(hConnect);
                    errorMsg = "Error al enviar solicitud o recibir respuesta del servidor.";
                    break;
                }

                DWORD statusCode = 0;
                DWORD statusSize = sizeof(statusCode);
                WinHttpQueryHeaders(hRequest,
                                    WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                    WINHTTP_HEADER_NAME_BY_INDEX,
                                    &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);

                // Handle Redirects (301, 302, 303, 307, 308)
                if (statusCode == 301 || statusCode == 302 || statusCode == 303 || statusCode == 307 || statusCode == 308) {
                    DWORD locSize = 0;
                    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
                                        NULL, &locSize, WINHTTP_NO_HEADER_INDEX);
                    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
                        std::vector<wchar_t> locBuf(locSize / sizeof(wchar_t) + 1, 0);
                        if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
                                                locBuf.data(), &locSize, WINHTTP_NO_HEADER_INDEX)) {
                            QString nextUrl = QString::fromWCharArray(locBuf.data());
                            if (nextUrl.startsWith("/")) {
                                nextUrl = QString("%1://%2%3").arg(qurl.scheme(), qurl.host(), nextUrl);
                            }
                            currentUrl = nextUrl;
                            redirectCount++;
                            WinHttpCloseHandle(hRequest);
                            WinHttpCloseHandle(hConnect);
                            continue;
                        }
                    }
                    WinHttpCloseHandle(hRequest);
                    WinHttpCloseHandle(hConnect);
                    errorMsg = "Error al seguir la redirección del archivo de descarga.";
                    break;
                }

                if (statusCode != 200) {
                    WinHttpCloseHandle(hRequest);
                    WinHttpCloseHandle(hConnect);
                    errorMsg = QString("El servidor respondió con error HTTP %1").arg(statusCode);
                    break;
                }

                // Query Content-Length
                qint64 totalBytes = 0;
                DWORD clSize = 0;
                WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX,
                                    NULL, &clSize, WINHTTP_NO_HEADER_INDEX);
                if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
                    std::vector<wchar_t> clBuf(clSize / sizeof(wchar_t) + 1, 0);
                    if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX,
                                            clBuf.data(), &clSize, WINHTTP_NO_HEADER_INDEX)) {
                        totalBytes = QString::fromWCharArray(clBuf.data()).toLongLong();
                    }
                }

                // Read streamed data
                qint64 receivedBytes = 0;
                std::vector<char> buffer(64 * 1024); // 64 KB
                bool readSuccess = true;

                while (!m_cancelRequested.load()) {
                    DWORD bytesAvailable = 0;
                    if (!WinHttpQueryDataAvailable(hRequest, &bytesAvailable)) {
                        readSuccess = false;
                        errorMsg = "Error al consultar datos de descarga.";
                        break;
                    }

                    if (bytesAvailable == 0) {
                        break; // EOF
                    }

                    DWORD bytesToRead = (std::min)(bytesAvailable, (DWORD)buffer.size());
                    DWORD bytesRead = 0;
                    if (!WinHttpReadData(hRequest, buffer.data(), bytesToRead, &bytesRead)) {
                        readSuccess = false;
                        errorMsg = "Error al leer datos de la conexión.";
                        break;
                    }

                    if (bytesRead > 0) {
                        outFile.write(buffer.data(), bytesRead);
                        receivedBytes += bytesRead;

                        int percent = (totalBytes > 0) ? (int)((receivedBytes * 100) / totalBytes) : 0;
                        QMetaObject::invokeMethod(this, [this, fileName, receivedBytes, totalBytes, percent]() {
                            emit downloadProgress(fileName, receivedBytes, totalBytes, percent);
                        }, Qt::QueuedConnection);
                    }
                }

                WinHttpCloseHandle(hRequest);
                WinHttpCloseHandle(hConnect);

                if (m_cancelRequested.load()) {
                    errorMsg = "Descarga cancelada";
                    break;
                }

                if (readSuccess && receivedBytes > 0) {
                    success = true;
                }
                break;
            }

            outFile.flush();
            outFile.close();

            if (success && !m_cancelRequested.load()) {
                QFile::remove(finalPath);
                if (!QFile::rename(tempPath, finalPath)) {
                    success = false;
                    errorMsg = "Error al renombrar el archivo descargado a su ubicación final.";
                }
            } else {
                QFile::remove(tempPath);
            }
        }

        WinHttpCloseHandle(hSession);
    }
#else
    errorMsg = "Plataforma no soportada para descarga directa.";
#endif

    m_isDownloading = false;
    m_currentDownloadingFile.clear();

    QMetaObject::invokeMethod(this, [this, fileName, success, errorMsg]() {
        refreshModelStatuses();
        emit downloadFinished(fileName, success, errorMsg);
    }, Qt::QueuedConnection);
}
