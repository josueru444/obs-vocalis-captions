#include "translator_settings_dialog.h"
#include "vector_icons.h"
#include "whisper_model_manager.h"
#include "../ai_audio_filter.h"
#include <obs.hpp>
#include <QMessageBox>
#include <QFileDialog>

TranslatorSettingsDialog::TranslatorSettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("Configuración del Traductor");
    resize(520, 440);
    setupUi();
    loadCurrentSettings();
}

void TranslatorSettingsDialog::setupUi()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    m_tabWidget = new QTabWidget(this);

    // ── Tab 1: Servidor Remoto (WebSocket) ──────────────────────────────────
    QWidget *tabRemote = new QWidget();
    QVBoxLayout *remoteLayout = new QVBoxLayout(tabRemote);

    m_chkUseRemote = new QCheckBox("Usar Servidor Remoto de Transcripción (WebSocket)", this);
    connect(m_chkUseRemote, &QCheckBox::toggled, this, &TranslatorSettingsDialog::onRemoteModeToggled);
    remoteLayout->addWidget(m_chkUseRemote);

    QGroupBox *grpRemoteConfig = new QGroupBox("Ajustes de Conexión", this);
    QFormLayout *formRemote = new QFormLayout(grpRemoteConfig);

    m_txtWsUrl = new QLineEdit(this);
    m_txtWsUrl->setPlaceholderText("ws://127.0.0.1:8765/ws");
    formRemote->addRow("URL del Servidor:", m_txtWsUrl);

    m_txtWsToken = new QLineEdit(this);
    m_txtWsToken->setEchoMode(QLineEdit::Password);
    m_txtWsToken->setPlaceholderText("Opcional");
    QAction *actionToggleToken = m_txtWsToken->addAction(VectorIcons::iconEye(QColor("#a0a0a0"), 16), QLineEdit::TrailingPosition);
    actionToggleToken->setToolTip("Mostrar / Ocultar token");
    connect(actionToggleToken, &QAction::triggered, this, [this, actionToggleToken]() {
        if (m_txtWsToken->echoMode() == QLineEdit::Password) {
            m_txtWsToken->setEchoMode(QLineEdit::Normal);
            actionToggleToken->setIcon(VectorIcons::iconEyeOff(QColor("#ffffff"), 16));
        } else {
            m_txtWsToken->setEchoMode(QLineEdit::Password);
            actionToggleToken->setIcon(VectorIcons::iconEye(QColor("#a0a0a0"), 16));
        }
    });
    formRemote->addRow("Token / Clave de Acceso:", m_txtWsToken);

    m_cmbPartialMode = new QComboBox(this);
    m_cmbPartialMode->addItem("Tiempo Real (500 ms - Fluidez continua)", "realtime");
    m_cmbPartialMode->addItem("Balanceado (1300 ms - Recomendado)", "balanced");
    m_cmbPartialMode->addItem("Alta Precisión (1800 ms - Frases completas)", "precision");
    m_cmbPartialMode->addItem("Personalizado (Elegir milisegundos - Mín 500 ms)", "custom");
    connect(m_cmbPartialMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &TranslatorSettingsDialog::onPartialModeChanged);
    formRemote->addRow("Muestreo de Texto:", m_cmbPartialMode);

    QLabel *lblSamplingWarning = new QLabel("⚠️ Advertencia: No se recomienda modificar el intervalo de muestreo del texto (por defecto es 1300 ms).", this);
    lblSamplingWarning->setWordWrap(true);
    lblSamplingWarning->setStyleSheet("color: #e67e22; font-size: 8pt; font-style: italic; margin-top: 2px;");
    formRemote->addRow("", lblSamplingWarning);

    m_widgetCustomInterval = new QWidget(this);
    QHBoxLayout *customIntervalLayout = new QHBoxLayout(m_widgetCustomInterval);
    customIntervalLayout->setContentsMargins(0, 0, 0, 0);
    customIntervalLayout->setSpacing(6);
    m_spnCustomInterval = new QSpinBox(m_widgetCustomInterval);
    m_spnCustomInterval->setRange(500, 5000);
    m_spnCustomInterval->setSingleStep(50);
    m_spnCustomInterval->setValue(1000);
    m_spnCustomInterval->setSuffix(" ms");
    m_spnCustomInterval->setToolTip("Mínimo permitido: 500 ms (protección contra sobrecarga de CPU)");
    customIntervalLayout->addWidget(m_spnCustomInterval);

    QLabel *lblMinInfo = new QLabel("(Mínimo: 500 ms — Máximo: 5000 ms)", m_widgetCustomInterval);
    lblMinInfo->setStyleSheet("color: #8c92a4; font-size: 8pt; font-style: italic;");
    customIntervalLayout->addWidget(lblMinInfo);
    customIntervalLayout->addStretch();

    m_widgetCustomInterval->setVisible(false);
    formRemote->addRow("Intervalo Exacto:", m_widgetCustomInterval);

    remoteLayout->addWidget(grpRemoteConfig);
    remoteLayout->addStretch();
    m_tabWidget->addTab(tabRemote, "Servidor Remoto");

    // ── Tab 2: Motor Local (Whisper) ────────────────────────────────────────
    QWidget *tabWhisper = new QWidget();
    QVBoxLayout *whisperLayout = new QVBoxLayout(tabWhisper);

    QGroupBox *grpModel = new QGroupBox("Modelo de Inteligencia Artificial", this);
    QVBoxLayout *modelLayout = new QVBoxLayout(grpModel);

    m_cmbModel = new QComboBox(this);
    const auto &models = WhisperModelManager::instance().getModels();
    for (const auto &m : models) {
        QIcon icon = m.isDownloaded ? VectorIcons::iconCheck(QColor("#4CAF50"), 16)
                                    : VectorIcons::iconDownload(QColor("#5B9BD5"), 16);
        QString statusText = m.isDownloaded ? QString("%1 (%2)  —  Listo").arg(m.name, m.sizeStr)
                                            : QString("%1 (%2)  —  Descargar").arg(m.name, m.sizeStr);
        m_cmbModel->addItem(icon, statusText, m.fileName);
    }
    modelLayout->addWidget(m_cmbModel);

    m_chkUseCustomModel = new QCheckBox("Usar modelo personalizado (.bin)", this);
    connect(m_chkUseCustomModel, &QCheckBox::toggled, this, &TranslatorSettingsDialog::onCustomModelToggled);
    modelLayout->addWidget(m_chkUseCustomModel);

    m_widgetCustomModel = new QWidget(this);
    QHBoxLayout *customModelLayout = new QHBoxLayout(m_widgetCustomModel);
    customModelLayout->setContentsMargins(0, 0, 0, 0);
    customModelLayout->setSpacing(4);

    m_txtCustomModelPath = new QLineEdit(m_widgetCustomModel);
    m_txtCustomModelPath->setPlaceholderText("Ruta del archivo de modelo GGML...");
    customModelLayout->addWidget(m_txtCustomModelPath);

    m_btnBrowseModel = new QPushButton(m_widgetCustomModel);
    m_btnBrowseModel->setIcon(VectorIcons::iconFolder(QColor("#cccccc"), 16));
    m_btnBrowseModel->setToolTip("Examinar archivo de modelo");
    m_btnBrowseModel->setFixedWidth(32);
    connect(m_btnBrowseModel, &QPushButton::clicked, this, &TranslatorSettingsDialog::onBrowseCustomModel);
    customModelLayout->addWidget(m_btnBrowseModel);

    modelLayout->addWidget(m_widgetCustomModel);
    whisperLayout->addWidget(grpModel);

    QGroupBox *grpHardware = new QGroupBox("Rendimiento y Hardware", this);
    QFormLayout *formHardware = new QFormLayout(grpHardware);

    m_chkUseGpu = new QCheckBox("Aceleración por Tarjeta Gráfica (GPU / Vulkan)", this);
    formHardware->addRow("GPU:", m_chkUseGpu);

    m_spnThreads = new QSpinBox(this);
    m_spnThreads->setRange(1, 16);
    m_spnThreads->setValue(4);
    formHardware->addRow("Hilos de CPU:", m_spnThreads);

    whisperLayout->addWidget(grpHardware);
    whisperLayout->addStretch();
    m_tabWidget->addTab(tabWhisper, "Motor Local (Whisper)");

    // ── Tab 3: Idiomas ──────────────────────────────────────────────────────
    QWidget *tabLanguages = new QWidget();
    QFormLayout *formLang = new QFormLayout(tabLanguages);

    m_cmbLangIn = new QComboBox(this);
    m_cmbLangIn->addItem("Detección Automática", "auto");
    m_cmbLangIn->addItem("Español", "es");
    m_cmbLangIn->addItem("Inglés", "en");
    m_cmbLangIn->addItem("Francés", "fr");
    m_cmbLangIn->addItem("Alemán", "de");
    m_cmbLangIn->addItem("Italiano", "it");
    m_cmbLangIn->addItem("Portugués", "pt");
    m_cmbLangIn->addItem("Polaco", "pl");
    m_cmbLangIn->addItem("Japonés", "ja");
    m_cmbLangIn->addItem("Chino", "zh");
    formLang->addRow("Idioma de Entrada (Habla):", m_cmbLangIn);

    m_cmbLangOut = new QComboBox(this);
    m_cmbLangOut->addItem("Original (Mismo que se habla)", "original");
    m_cmbLangOut->addItem("Inglés", "en");
    m_cmbLangOut->addItem("Español", "es");
    m_cmbLangOut->addItem("Francés", "fr");
    m_cmbLangOut->addItem("Alemán", "de");
    m_cmbLangOut->addItem("Italiano", "it");
    m_cmbLangOut->addItem("Portugués", "pt");
    m_cmbLangOut->addItem("Polaco", "pl");
    m_cmbLangOut->addItem("Japonés", "ja");
    m_cmbLangOut->addItem("Chino", "zh");
    formLang->addRow("Idioma de Salida (Traducción):", m_cmbLangOut);

    QLabel *lblLangNote = new QLabel("Nota: El motor local (Whisper) traduce directamente hacia el Inglés. El servidor remoto soporta cualquier combinación entre idiomas.", this);
    lblLangNote->setWordWrap(true);
    lblLangNote->setStyleSheet("color: #7f8c8d; font-size: 8.5pt; margin-top: 10px;");
    formLang->addRow(lblLangNote);

    m_tabWidget->addTab(tabLanguages, "Idiomas");

    // ── Tab 4: Detección de Voz y Visualización ────────────────────────────
    QWidget *tabVad = new QWidget();
    QVBoxLayout *vadLayout = new QVBoxLayout(tabVad);

    QGroupBox *grpDisplay = new QGroupBox("Visualización y Permanencia en Pantalla", this);
    QVBoxLayout *displayLayout = new QVBoxLayout(grpDisplay);

    m_chkAutoClear = new QCheckBox("Ocultar subtítulos automáticamente tras un periodo de silencio", this);
    connect(m_chkAutoClear, &QCheckBox::toggled, this, &TranslatorSettingsDialog::onAutoClearToggled);
    displayLayout->addWidget(m_chkAutoClear);

    m_widgetAutoClearTime = new QWidget(this);
    QHBoxLayout *autoClearTimeLayout = new QHBoxLayout(m_widgetAutoClearTime);
    autoClearTimeLayout->setContentsMargins(18, 0, 0, 0);
    autoClearTimeLayout->setSpacing(6);

    QLabel *lblAutoClearSecs = new QLabel("Tiempo de espera antes de ocultar:", m_widgetAutoClearTime);
    autoClearTimeLayout->addWidget(lblAutoClearSecs);

    m_spnAutoClearSeconds = new QSpinBox(m_widgetAutoClearTime);
    m_spnAutoClearSeconds->setRange(1, 60);
    m_spnAutoClearSeconds->setValue(5);
    m_spnAutoClearSeconds->setSuffix(" seg");
    autoClearTimeLayout->addWidget(m_spnAutoClearSeconds);
    autoClearTimeLayout->addStretch();
    displayLayout->addWidget(m_widgetAutoClearTime);

    m_lblAutoClearHelp = new QLabel("Nota: Si desmarca esta opción (o se establece en 0), los subtítulos permanecerán siempre visibles en pantalla hasta que se detecte una nueva frase o se pulse 'Limpiar'.", this);
    m_lblAutoClearHelp->setWordWrap(true);
    m_lblAutoClearHelp->setStyleSheet("color: #7f8c8d; font-size: 8.5pt; margin-top: 4px;");
    displayLayout->addWidget(m_lblAutoClearHelp);

    vadLayout->addWidget(grpDisplay);

    QGroupBox *grpVad = new QGroupBox("Parámetros de Detección de Voz (VAD)", this);
    QFormLayout *formVad = new QFormLayout(grpVad);

    m_spnVadThreshold = new QDoubleSpinBox(this);
    m_spnVadThreshold->setRange(0.001, 1.0);
    m_spnVadThreshold->setSingleStep(0.005);
    m_spnVadThreshold->setDecimals(3);
    m_spnVadThreshold->setValue(0.020);
    formVad->addRow("Umbral de Silencio (RMS):", m_spnVadThreshold);

    m_spnMinSpeechMs = new QSpinBox(this);
    m_spnMinSpeechMs->setRange(50, 2000);
    m_spnMinSpeechMs->setSingleStep(50);
    m_spnMinSpeechMs->setValue(250);
    m_spnMinSpeechMs->setSuffix(" ms");
    formVad->addRow("Duración mínima de voz:", m_spnMinSpeechMs);

    m_spnHangoverMs = new QSpinBox(this);
    m_spnHangoverMs->setRange(100, 3000);
    m_spnHangoverMs->setSingleStep(50);
    m_spnHangoverMs->setValue(400);
    m_spnHangoverMs->setSuffix(" ms");
    formVad->addRow("Margen de silencio tras hablar:", m_spnHangoverMs);

    vadLayout->addWidget(grpVad);
    vadLayout->addStretch();

    m_tabWidget->addTab(tabVad, "VAD y Pantalla");

    mainLayout->addWidget(m_tabWidget);

    // ── Bottom Action Buttons ───────────────────────────────────────────────
    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();

    m_btnCancel = new QPushButton("Cancelar", this);
    connect(m_btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(m_btnCancel);

    m_btnSave = new QPushButton("Guardar y Aplicar", this);
    m_btnSave->setDefault(true);
    m_btnSave->setStyleSheet("font-weight: bold; padding: 6px 16px;");
    connect(m_btnSave, &QPushButton::clicked, this, &TranslatorSettingsDialog::onSaveClicked);
    btnLayout->addWidget(m_btnSave);

    mainLayout->addLayout(btnLayout);
}

void TranslatorSettingsDialog::onCustomModelToggled(bool checked)
{
    m_widgetCustomModel->setVisible(checked);
    m_cmbModel->setEnabled(!checked);
}

void TranslatorSettingsDialog::onBrowseCustomModel()
{
    QString filePath = QFileDialog::getOpenFileName(
        this,
        "Seleccionar Modelo Whisper",
        m_txtCustomModelPath->text(),
        "Modelos GGML (*.bin *.gguf);;Todos los archivos (*.*)");

    if (!filePath.isEmpty()) {
        m_txtCustomModelPath->setText(filePath);
    }
}

void TranslatorSettingsDialog::onRemoteModeToggled(bool checked)
{
    m_tabWidget->setTabEnabled(0, true);
    m_tabWidget->setTabEnabled(1, true);
}

void TranslatorSettingsDialog::onAutoClearToggled(bool checked)
{
    if (m_widgetAutoClearTime) {
        m_widgetAutoClearTime->setVisible(checked);
    }
}

void TranslatorSettingsDialog::onPartialModeChanged(int index)
{
    bool isCustom = (m_cmbPartialMode->itemData(index).toString() == "custom");
    m_widgetCustomInterval->setVisible(isCustom);
}

void TranslatorSettingsDialog::loadCurrentSettings()
{
    obs_source_t *source = get_active_filter_source();
    if (!source) return;

    obs_data_t *settings = obs_source_get_settings(source);
    if (!settings) return;

    m_chkUseRemote->setChecked(obs_data_get_bool(settings, "use_remote_transcription"));
    m_txtWsUrl->setText(obs_data_get_string(settings, "ws_url"));
    m_txtWsToken->setText(obs_data_get_string(settings, "ws_token"));

    const char *mode = obs_data_get_string(settings, "partial_mode");
    int mode_idx = m_cmbPartialMode->findData(mode ? mode : "balanced");
    if (mode_idx >= 0) m_cmbPartialMode->setCurrentIndex(mode_idx);

    int custom_ms = (int)obs_data_get_int(settings, "custom_partial_interval_ms");
    if (custom_ms < 500) custom_ms = 500;
    if (custom_ms > 5000) custom_ms = 5000;
    m_spnCustomInterval->setValue(custom_ms);

    bool isCustom = (m_cmbPartialMode->currentData().toString() == "custom");
    m_widgetCustomInterval->setVisible(isCustom);

    const char *model = obs_data_get_string(settings, "model_settings");
    int model_idx = m_cmbModel->findData(model ? model : "ggml-tiny.bin");
    if (model_idx >= 0) m_cmbModel->setCurrentIndex(model_idx);

    bool use_custom = obs_data_get_bool(settings, "use_custom_model");
    m_chkUseCustomModel->setChecked(use_custom);
    m_widgetCustomModel->setVisible(use_custom);
    m_cmbModel->setEnabled(!use_custom);
    m_txtCustomModelPath->setText(obs_data_get_string(settings, "custom_model_path"));

    m_chkUseGpu->setChecked(obs_data_get_bool(settings, "processing_mode"));
    m_spnThreads->setValue((int)obs_data_get_int(settings, "whisper_threads"));

    const char *lang_in = obs_data_get_string(settings, "lang_in");
    int in_idx = m_cmbLangIn->findData(lang_in ? lang_in : "auto");
    if (in_idx >= 0) m_cmbLangIn->setCurrentIndex(in_idx);

    const char *lang_out = obs_data_get_string(settings, "lang_out");
    int out_idx = m_cmbLangOut->findData(lang_out ? lang_out : "original");
    if (out_idx >= 0) m_cmbLangOut->setCurrentIndex(out_idx);

    m_spnVadThreshold->setValue(obs_data_get_double(settings, "vad_rms"));
    m_spnMinSpeechMs->setValue((int)obs_data_get_int(settings, "vad_min_speech"));
    m_spnHangoverMs->setValue((int)obs_data_get_int(settings, "vad_hangover"));

    int autoClearSecs = (int)obs_data_get_int(settings, "auto_clear_seconds");
    if (autoClearSecs <= 0) {
        m_chkAutoClear->setChecked(false);
        m_widgetAutoClearTime->setVisible(false);
        m_spnAutoClearSeconds->setValue(5);
    } else {
        m_chkAutoClear->setChecked(true);
        m_widgetAutoClearTime->setVisible(true);
        m_spnAutoClearSeconds->setValue(autoClearSecs);
    }

    obs_data_release(settings);
}

void TranslatorSettingsDialog::onSaveClicked()
{
    saveSettings();
    accept();
}

void TranslatorSettingsDialog::saveSettings()
{
    obs_source_t *source = get_active_filter_source();
    if (!source) return;

    obs_data_t *settings = obs_source_get_settings(source);
    if (!settings) return;

    obs_data_set_bool(settings, "use_remote_transcription", m_chkUseRemote->isChecked());
    obs_data_set_string(settings, "ws_url", m_txtWsUrl->text().trimmed().toUtf8().constData());
    obs_data_set_string(settings, "ws_token", m_txtWsToken->text().trimmed().toUtf8().constData());
    obs_data_set_string(settings, "partial_mode", m_cmbPartialMode->currentData().toString().toUtf8().constData());
    int custom_val = m_spnCustomInterval->value();
    if (custom_val < 500) custom_val = 500;
    if (custom_val > 5000) custom_val = 5000;
    obs_data_set_int(settings, "custom_partial_interval_ms", custom_val);

    obs_data_set_string(settings, "model_settings", m_cmbModel->currentData().toString().toUtf8().constData());
    obs_data_set_bool(settings, "use_custom_model", m_chkUseCustomModel->isChecked());
    obs_data_set_string(settings, "custom_model_path", m_txtCustomModelPath->text().trimmed().toUtf8().constData());

    obs_data_set_bool(settings, "processing_mode", m_chkUseGpu->isChecked());
    obs_data_set_int(settings, "whisper_threads", m_spnThreads->value());

    obs_data_set_string(settings, "lang_in", m_cmbLangIn->currentData().toString().toUtf8().constData());
    obs_data_set_string(settings, "lang_out", m_cmbLangOut->currentData().toString().toUtf8().constData());

    obs_data_set_double(settings, "vad_rms", m_spnVadThreshold->value());
    obs_data_set_int(settings, "vad_min_speech", m_spnMinSpeechMs->value());
    obs_data_set_int(settings, "vad_hangover", m_spnHangoverMs->value());

    int autoClearVal = m_chkAutoClear->isChecked() ? m_spnAutoClearSeconds->value() : 0;
    obs_data_set_int(settings, "auto_clear_seconds", autoClearVal);

    obs_source_update(source, settings);
    obs_data_release(settings);
}
