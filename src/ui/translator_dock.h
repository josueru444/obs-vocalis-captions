#pragma once

#ifdef __cplusplus

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QLineEdit>
#include <QCheckBox>
#include <QGroupBox>
#include <QTimer>
#include <QFrame>

class TranslatorDock : public QWidget {
    Q_OBJECT

public:
    explicit TranslatorDock(QWidget *parent = nullptr);
    ~TranslatorDock() override;

private slots:
    void refreshStatus();
    void refreshSourceList();
    void onTogglePause();
    void onReconnect();
    void onOpenSettings();
    void onBrowseCustomModel();

    // Direct filter settings modification from Dock
    void onTargetSourceChanged(int index);
    void onEngineModeChanged(int index);
    void onLanguageChanged();
    void onModelChanged(int index);
    void onUseCustomModelToggled(bool checked);
    void onCustomModelPathEditingFinished();
    void onGpuToggled(bool checked);
    void onWsUrlEditingFinished();

private:
    void setupUi();
    void loadSettingsFromFilter();
    void saveSettingString(const char *key, const QString &val);
    void saveSettingBool(const char *key, bool val);

    bool m_isUpdatingUi{false};
    void *m_lastActiveSource{nullptr};
    bool m_lastHadActiveFilter{false};

    // Header & VAD
    QLabel *m_lblVadIcon{nullptr};
    QLabel *m_lblVadText{nullptr};
    QPushButton *m_btnPause{nullptr};

    // Target Subtitle Source
    QComboBox *m_cmbTargetSource{nullptr};
    QPushButton *m_btnRefreshSources{nullptr};

    // Languages
    QComboBox *m_cmbLangIn{nullptr};
    QComboBox *m_cmbLangOut{nullptr};

    // Engine Mode
    QComboBox *m_cmbEngineMode{nullptr};

    // Remote settings container
    QFrame *m_panelRemote{nullptr};
    QLabel *m_lblConnectionDot{nullptr};
    QLabel *m_lblConnectionText{nullptr};
    QLineEdit *m_txtWsUrl{nullptr};
    QPushButton *m_btnReconnect{nullptr};

    // Local Whisper settings container
    QFrame *m_panelLocal{nullptr};
    QComboBox *m_cmbModel{nullptr};
    QCheckBox *m_chkUseCustomModel{nullptr};
    QWidget *m_widgetCustomModel{nullptr};
    QLineEdit *m_txtCustomModelPath{nullptr};
    QPushButton *m_btnBrowseModel{nullptr};
    QCheckBox *m_chkGpu{nullptr};

    // Warning
    QLabel *m_lblWarning{nullptr};

    // Bottom
    QPushButton *m_btnSettings{nullptr};

    QTimer *m_updateTimer{nullptr};
};

extern "C" {
#endif

void init_translator_dock(void);
void free_translator_dock(void);

#ifdef __cplusplus
}
#endif
