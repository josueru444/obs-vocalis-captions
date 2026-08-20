#pragma once

#include <QObject>
#include <QString>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>

struct WhisperModelInfo {
    QString id;
    QString name;
    QString fileName;
    QString sizeStr;
    QString url;
    QString category;
    bool isDownloaded{false};
    bool isDownloading{false};
    int progressPercent{0};
    qint64 bytesReceived{0};
    qint64 bytesTotal{0};
};

class WhisperModelManager : public QObject {
    Q_OBJECT

public:
    static WhisperModelManager &instance();

    QString getModelsDirectory() const;
    const std::vector<WhisperModelInfo> &getModels() const;
    WhisperModelInfo getModelInfo(const QString &fileName) const;
    bool isModelDownloaded(const QString &fileName) const;
    QString getModelPath(const QString &fileName) const;

    void refreshModelStatuses();
    bool startDownload(const QString &fileName);
    void cancelDownload(const QString &fileName);
    bool deleteModel(const QString &fileName);

signals:
    void modelListChanged();
    void downloadProgress(const QString &fileName, qint64 bytesReceived, qint64 bytesTotal, int percent);
    void downloadFinished(const QString &fileName, bool success, const QString &errorMsg);

private:
    explicit WhisperModelManager(QObject *parent = nullptr);
    ~WhisperModelManager() override;

    void initCatalog();
    void downloadWorker(const QString &fileName, const QString &url, const QString &finalPath, const QString &tempPath);

    std::vector<WhisperModelInfo> m_models;
    std::thread m_downloadThread;
    std::atomic<bool> m_cancelRequested{false};
    std::atomic<bool> m_isDownloading{false};
    QString m_currentDownloadingFile;
    std::mutex m_mutex;
};
