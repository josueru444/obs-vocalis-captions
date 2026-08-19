#pragma once

#include <QDialog>
#include <QTabWidget>
#include <QLineEdit>
#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>

class TranslatorSettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit TranslatorSettingsDialog(QWidget *parent = nullptr);
    ~TranslatorSettingsDialog() override = default;

private slots:
    void onSaveClicked();
    void onRemoteModeToggled(bool checked);
    void onCustomModelToggled(bool checked);
    void onBrowseCustomModel();
    void onPartialModeChanged(int index);
    void onAutoClearToggled(bool checked);

private:
    void setupUi();
    void loadCurrentSettings();
    void saveSettings();

    QTabWidget *m_tabWidget{nullptr};

    // Tab 1: Remote Server & Sampling
    QCheckBox *m_chkUseRemote{nullptr};
    QLineEdit *m_txtWsUrl{nullptr};
    QLineEdit *m_txtWsToken{nullptr};
    QComboBox *m_cmbPartialMode{nullptr};
    QWidget *m_widgetCustomInterval{nullptr};
    QSpinBox *m_spnCustomInterval{nullptr};

    // Tab 2: Local Whisper
    QComboBox *m_cmbModel{nullptr};
    QCheckBox *m_chkUseCustomModel{nullptr};
    QWidget *m_widgetCustomModel{nullptr};
    QLineEdit *m_txtCustomModelPath{nullptr};
    QPushButton *m_btnBrowseModel{nullptr};
    QCheckBox *m_chkUseGpu{nullptr};
    QSpinBox *m_spnThreads{nullptr};

    // Tab 3: Languages
    QComboBox *m_cmbLangIn{nullptr};
    QComboBox *m_cmbLangOut{nullptr};
    QCheckBox *m_chkLocalTranslation{nullptr};

    // Tab 4: VAD & Subtitle Display
    QDoubleSpinBox *m_spnVadThreshold{nullptr};
    QSpinBox *m_spnMinSpeechMs{nullptr};
    QSpinBox *m_spnHangoverMs{nullptr};
    QCheckBox *m_chkAutoClear{nullptr};
    QWidget *m_widgetAutoClearTime{nullptr};
    QSpinBox *m_spnAutoClearSeconds{nullptr};
    QLabel *m_lblAutoClearHelp{nullptr};

    // Dialog buttons
    QPushButton *m_btnSave{nullptr};
    QPushButton *m_btnCancel{nullptr};
};
