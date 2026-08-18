#include "translator_dock.h"
#include "translator_settings_dialog.h"
#include "vector_icons.h"
#include "../ai_audio_filter.h"

#include <obs-frontend-api.h>
#include <obs.hpp>
#include <QMainWindow>
#include <QScrollArea>
#include <QFont>
#include <QFormLayout>
#include <QFileDialog>

static TranslatorDock *s_translator_dock = nullptr;

TranslatorDock::TranslatorDock(QWidget *parent)
    : QWidget(parent)
{
    setupUi();

    refreshFilterList();
    refreshSourceList();
    loadSettingsFromFilter();

    m_updateTimer = new QTimer(this);
    connect(m_updateTimer, &QTimer::timeout, this, &TranslatorDock::refreshStatus);
    m_updateTimer->start(250);

    refreshStatus();
}

TranslatorDock::~TranslatorDock()
{
    if (m_updateTimer) {
        m_updateTimer->stop();
    }
}

void TranslatorDock::setupUi()
{
    QVBoxLayout *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);

    QScrollArea *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    QWidget *container = new QWidget(scrollArea);
    QVBoxLayout *layout = new QVBoxLayout(container);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    const QString groupStyle =
        "QGroupBox {"
        "  font-weight: 600;"
        "  border: 1px solid rgba(255, 255, 255, 0.07);"
        "  border-radius: 5px;"
        "  margin-top: 6px;"
        "  padding-top: 8px;"
        "  background-color: rgba(255, 255, 255, 0.015);"
        "}"
        "QGroupBox::title {"
        "  subcontrol-origin: margin;"
        "  left: 8px;"
        "  padding: 0 4px;"
        "  color: #cfd3dc;"
        "}";

    // ── Warning Banner ──────────────────────────────────────────────────────
    m_lblWarning = new QLabel("Añada el filtro 'Traductor IA' a su fuente de micrófono en OBS.", container);
    m_lblWarning->setWordWrap(true);
    m_lblWarning->setStyleSheet("color: #cfd3dc; background-color: rgba(255, 255, 255, 0.03); border: 1px solid rgba(255, 255, 255, 0.08); border-radius: 5px; padding: 6px; font-size: 8.5pt;");
    m_lblWarning->setVisible(false);
    layout->addWidget(m_lblWarning);

    // ── 0. Group: Active Filter / Mic Selector ──────────────────────────────
    m_grpFilterSelector = new QGroupBox("Micrófono / Entrada de Audio", container);
    m_grpFilterSelector->setStyleSheet(groupStyle);
    QHBoxLayout *filterRow = new QHBoxLayout(m_grpFilterSelector);
    filterRow->setContentsMargins(8, 8, 8, 8);
    filterRow->setSpacing(6);

    m_cmbActiveFilter = new QComboBox(m_grpFilterSelector);
    m_cmbActiveFilter->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(m_cmbActiveFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &TranslatorDock::onActiveFilterChanged);
    filterRow->addWidget(m_cmbActiveFilter);

    m_btnRefreshFilters = new QPushButton(m_grpFilterSelector);
    m_btnRefreshFilters->setIcon(VectorIcons::iconRefresh(QColor("#b0b0b0"), 14));
    m_btnRefreshFilters->setToolTip("Actualizar lista de micrófonos con filtro");
    m_btnRefreshFilters->setFixedWidth(30);
    connect(m_btnRefreshFilters, &QPushButton::clicked, this, &TranslatorDock::refreshFilterList);
    filterRow->addWidget(m_btnRefreshFilters);

    layout->addWidget(m_grpFilterSelector);

    // ── 1. Top Card: Master Status & Quick Actions ──────────────────────────
    QFrame *cardTop = new QFrame(container);
    cardTop->setStyleSheet("QFrame { background-color: rgba(255, 255, 255, 0.03); border: 1px solid rgba(255, 255, 255, 0.07); border-radius: 5px; }");
    QHBoxLayout *topLayout = new QHBoxLayout(cardTop);
    topLayout->setContentsMargins(10, 6, 10, 6);
    topLayout->setSpacing(6);

    m_lblVadIcon = new QLabel(cardTop);
    m_lblVadIcon->setFixedSize(18, 18);
    m_lblVadIcon->setPixmap(VectorIcons::iconMic(QColor("#8c8c8c"), 18).pixmap(18, 18));
    topLayout->addWidget(m_lblVadIcon);

    m_lblVadText = new QLabel("Silencio", cardTop);
    m_lblVadText->setStyleSheet("font-size: 9pt; font-weight: 600; color: #8c8c8c;");
    m_lblVadText->setMinimumWidth(75);
    topLayout->addWidget(m_lblVadText);

    topLayout->addStretch();

    // Quick Clear Button (Panic Button)
    m_btnClear = new QPushButton("Limpiar", cardTop);
    m_btnClear->setIcon(VectorIcons::iconClear(QColor("#e0e0e0"), 14));
    m_btnClear->setToolTip("Limpiar subtítulos en pantalla de inmediato");
    m_btnClear->setFixedWidth(80);
    m_btnClear->setStyleSheet("QPushButton { padding: 4px 8px; font-weight: 600; border-radius: 4px; }");
    connect(m_btnClear, &QPushButton::clicked, this, &TranslatorDock::onClearSubtitles);
    topLayout->addWidget(m_btnClear);

    // Pause / Resume Button
    m_btnPause = new QPushButton("Pausar", cardTop);
    m_btnPause->setIcon(VectorIcons::iconPause(QColor("#e0e0e0"), 14));
    m_btnPause->setFixedWidth(85);
    m_btnPause->setStyleSheet("QPushButton { padding: 4px 8px; font-weight: 600; border-radius: 4px; }");
    connect(m_btnPause, &QPushButton::clicked, this, &TranslatorDock::onTogglePause);
    topLayout->addWidget(m_btnPause);

    layout->addWidget(cardTop);

    // ── 2. Group: Subtitle Output Target ────────────────────────────────────
    QGroupBox *grpTarget = new QGroupBox("Salida de Subtítulos", container);
    grpTarget->setStyleSheet(groupStyle);
    QVBoxLayout *targetLayout = new QVBoxLayout(grpTarget);
    targetLayout->setContentsMargins(8, 8, 8, 8);
    targetLayout->setSpacing(4);

    QLabel *lblTargetHelp = new QLabel("Mostrar subtítulos en la fuente:", grpTarget);
    lblTargetHelp->setStyleSheet("color: #8c92a4; font-size: 8.5pt; font-weight: normal;");
    targetLayout->addWidget(lblTargetHelp);

    QHBoxLayout *targetComboRow = new QHBoxLayout();
    m_cmbTargetSource = new QComboBox(grpTarget);
    m_cmbTargetSource->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(m_cmbTargetSource, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &TranslatorDock::onTargetSourceChanged);
    targetComboRow->addWidget(m_cmbTargetSource);

    m_btnRefreshSources = new QPushButton(grpTarget);
    m_btnRefreshSources->setIcon(VectorIcons::iconRefresh(QColor("#b0b0b0"), 14));
    m_btnRefreshSources->setToolTip("Actualizar lista de fuentes");
    m_btnRefreshSources->setFixedWidth(30);
    connect(m_btnRefreshSources, &QPushButton::clicked, this, &TranslatorDock::refreshSourceList);
    targetComboRow->addWidget(m_btnRefreshSources);

    targetLayout->addLayout(targetComboRow);
    layout->addWidget(grpTarget);

    // ── 3. Group: Languages ─────────────────────────────────────────────────
    QGroupBox *grpLanguages = new QGroupBox("Idiomas", container);
    grpLanguages->setStyleSheet(groupStyle);
    QFormLayout *langForm = new QFormLayout(grpLanguages);
    langForm->setContentsMargins(8, 8, 8, 8);
    langForm->setSpacing(6);

    m_cmbLangIn = new QComboBox(grpLanguages);
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
    connect(m_cmbLangIn, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &TranslatorDock::onLanguageChanged);
    langForm->addRow("Habla:", m_cmbLangIn);

    m_cmbLangOut = new QComboBox(grpLanguages);
    m_cmbLangOut->addItem("Original (Sin traducir)", "original");
    m_cmbLangOut->addItem("Inglés", "en");
    m_cmbLangOut->addItem("Español", "es");
    m_cmbLangOut->addItem("Francés", "fr");
    m_cmbLangOut->addItem("Alemán", "de");
    m_cmbLangOut->addItem("Italiano", "it");
    m_cmbLangOut->addItem("Portugués", "pt");
    m_cmbLangOut->addItem("Polaco", "pl");
    m_cmbLangOut->addItem("Japonés", "ja");
    m_cmbLangOut->addItem("Chino", "zh");
    connect(m_cmbLangOut, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &TranslatorDock::onLanguageChanged);
    langForm->addRow("Traducción:", m_cmbLangOut);

    layout->addWidget(grpLanguages);

    // ── 4. Group: AI Engine Mode ────────────────────────────────────
    QGroupBox *grpEngine = new QGroupBox("Motor de Transcripción", container);
    grpEngine->setStyleSheet(groupStyle);
    QVBoxLayout *engineLayout = new QVBoxLayout(grpEngine);
    engineLayout->setContentsMargins(8, 8, 8, 8);
    engineLayout->setSpacing(6);

    m_cmbEngineMode = new QComboBox(grpEngine);
    m_cmbEngineMode->addItem("Servidor Remoto (WebSocket)", "remote");
    m_cmbEngineMode->addItem("Whisper Local (IA en equipo)", "local");
    connect(m_cmbEngineMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &TranslatorDock::onEngineModeChanged);
    engineLayout->addWidget(m_cmbEngineMode);

    // Subpanel: Servidor Remoto
    m_panelRemote = new QFrame(grpEngine);
    m_panelRemote->setStyleSheet("QFrame { background-color: rgba(255, 255, 255, 0.02); border: 1px solid rgba(255, 255, 255, 0.06); border-radius: 5px; padding: 6px; }");
    QVBoxLayout *remoteSubLayout = new QVBoxLayout(m_panelRemote);
    remoteSubLayout->setContentsMargins(6, 6, 6, 6);
    remoteSubLayout->setSpacing(6);

    QHBoxLayout *connStatusRow = new QHBoxLayout();
    QLabel *lblStatusTitle = new QLabel("Estado:", m_panelRemote);
    lblStatusTitle->setStyleSheet("color: #8c92a4; font-size: 8.5pt;");
    connStatusRow->addWidget(lblStatusTitle);

    m_lblConnectionDot = new QLabel(m_panelRemote);
    m_lblConnectionDot->setFixedSize(16, 16);
    m_lblConnectionDot->setAlignment(Qt::AlignCenter);
    m_lblConnectionDot->setPixmap(VectorIcons::statusDot(QColor("#e57373"), 16));
    connStatusRow->addWidget(m_lblConnectionDot);

    m_lblConnectionText = new QLabel("Desconectado", m_panelRemote);
    m_lblConnectionText->setStyleSheet("font-size: 8.5pt; font-weight: 600; color: #e57373;");
    connStatusRow->addWidget(m_lblConnectionText);
    connStatusRow->addStretch();
    remoteSubLayout->addLayout(connStatusRow);

    m_txtWsUrl = new QLineEdit(m_panelRemote);
    m_txtWsUrl->setPlaceholderText("ws://127.0.0.1:8765/ws");
    connect(m_txtWsUrl, &QLineEdit::editingFinished, this, &TranslatorDock::onWsUrlEditingFinished);
    remoteSubLayout->addWidget(m_txtWsUrl);

    m_btnReconnect = new QPushButton("Reconectar Servidor", m_panelRemote);
    m_btnReconnect->setIcon(VectorIcons::iconRefresh(QColor("#e0e0e0"), 13));
    m_btnReconnect->setStyleSheet("QPushButton { padding: 4px 8px; font-weight: 600; }");
    connect(m_btnReconnect, &QPushButton::clicked, this, &TranslatorDock::onReconnect);
    remoteSubLayout->addWidget(m_btnReconnect);

    engineLayout->addWidget(m_panelRemote);

    // Subpanel: Local Whisper
    m_panelLocal = new QFrame(grpEngine);
    m_panelLocal->setStyleSheet("QFrame { background-color: rgba(255, 255, 255, 0.02); border: 1px solid rgba(255, 255, 255, 0.06); border-radius: 5px; padding: 6px; }");
    QVBoxLayout *localSubLayout = new QVBoxLayout(m_panelLocal);
    localSubLayout->setContentsMargins(6, 6, 6, 6);
    localSubLayout->setSpacing(6);

    QLabel *lblModel = new QLabel("Modelo Whisper:", m_panelLocal);
    lblModel->setStyleSheet("color: #8c92a4; font-size: 8.5pt;");
    localSubLayout->addWidget(lblModel);

    m_cmbModel = new QComboBox(m_panelLocal);
    m_cmbModel->addItem("Tiny (Rápido y Ligero)", "ggml-tiny.bin");
    m_cmbModel->addItem("Base (Balanceado)", "ggml-base.bin");
    m_cmbModel->addItem("Small (Alta Precisión)", "ggml-small.bin");
    connect(m_cmbModel, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &TranslatorDock::onModelChanged);
    localSubLayout->addWidget(m_cmbModel);

    m_chkUseCustomModel = new QCheckBox("Usar modelo personalizado (.bin)", m_panelLocal);
    connect(m_chkUseCustomModel, &QCheckBox::toggled, this, &TranslatorDock::onUseCustomModelToggled);
    localSubLayout->addWidget(m_chkUseCustomModel);

    m_widgetCustomModel = new QWidget(m_panelLocal);
    QHBoxLayout *customModelLayout = new QHBoxLayout(m_widgetCustomModel);
    customModelLayout->setContentsMargins(0, 0, 0, 0);
    customModelLayout->setSpacing(4);

    m_txtCustomModelPath = new QLineEdit(m_widgetCustomModel);
    m_txtCustomModelPath->setPlaceholderText("Ruta del archivo .bin...");
    connect(m_txtCustomModelPath, &QLineEdit::editingFinished, this, &TranslatorDock::onCustomModelPathEditingFinished);
    customModelLayout->addWidget(m_txtCustomModelPath);

    m_btnBrowseModel = new QPushButton(m_widgetCustomModel);
    m_btnBrowseModel->setIcon(VectorIcons::iconFolder(QColor("#b0b0b0"), 14));
    m_btnBrowseModel->setToolTip("Examinar archivo de modelo GGML");
    m_btnBrowseModel->setFixedWidth(30);
    connect(m_btnBrowseModel, &QPushButton::clicked, this, &TranslatorDock::onBrowseCustomModel);
    customModelLayout->addWidget(m_btnBrowseModel);

    localSubLayout->addWidget(m_widgetCustomModel);

    m_chkGpu = new QCheckBox("Aceleración por GPU (Vulkan)", m_panelLocal);
    connect(m_chkGpu, &QCheckBox::toggled, this, &TranslatorDock::onGpuToggled);
    localSubLayout->addWidget(m_chkGpu);

    engineLayout->addWidget(m_panelLocal);

    layout->addWidget(grpEngine);

    layout->addStretch();

    // ── 5. Bottom: Advanced Settings Button ─────────────────────────────────
    m_btnSettings = new QPushButton("Ajustes Avanzados...", container);
    m_btnSettings->setIcon(VectorIcons::iconSettings(QColor("#e0e0e0"), 14));
    m_btnSettings->setStyleSheet("QPushButton { padding: 5px 12px; font-weight: 600; border-radius: 4px; }");
    connect(m_btnSettings, &QPushButton::clicked, this, &TranslatorDock::onOpenSettings);
    layout->addWidget(m_btnSettings);

    scrollArea->setWidget(container);
    rootLayout->addWidget(scrollArea);
}

void TranslatorDock::refreshFilterList()
{
    m_isUpdatingUi = true;
    auto filters = get_active_filter_list();

    void *previousSelection = m_selectedFilterPtr;
    m_cmbActiveFilter->clear();

    int selectedIndex = -1;
    for (int i = 0; i < (int)filters.size(); ++i) {
        m_cmbActiveFilter->addItem(QString::fromUtf8(filters[i].display_name.c_str()),
                                   QVariant::fromValue(reinterpret_cast<quintptr>(filters[i].filter_ptr)));
        if (filters[i].filter_ptr == previousSelection) {
            selectedIndex = i;
        }
    }

    if (filters.empty()) {
        m_selectedFilterPtr = nullptr;
        m_grpFilterSelector->setVisible(false);
    } else {
        m_grpFilterSelector->setVisible(true);
        if (selectedIndex >= 0) {
            m_cmbActiveFilter->setCurrentIndex(selectedIndex);
            m_selectedFilterPtr = previousSelection;
        } else {
            m_cmbActiveFilter->setCurrentIndex(0);
            m_selectedFilterPtr = filters[0].filter_ptr;
        }
    }

    m_isUpdatingUi = false;
}

void TranslatorDock::onActiveFilterChanged(int index)
{
    if (m_isUpdatingUi) return;
    if (index >= 0) {
        quintptr ptrVal = m_cmbActiveFilter->itemData(index).value<quintptr>();
        m_selectedFilterPtr = reinterpret_cast<void*>(ptrVal);
        loadSettingsFromFilter();
        refreshStatus();
    }
}

void TranslatorDock::refreshSourceList()
{
    m_isUpdatingUi = true;
    QString currentSelection = m_cmbTargetSource->currentData().toString();

    m_cmbTargetSource->clear();
    m_cmbTargetSource->addItem("(Desactivado / No seleccionado)", "");

    obs_enum_sources(
        [](void *param, obs_source_t *source) {
            QComboBox *combo = static_cast<QComboBox *>(param);
            const char *id = obs_source_get_unversioned_id(source);
            if (id && strcmp(id, "fuente_subtitulos_ia") == 0) {
                const char *name = obs_source_get_name(source);
                if (name) {
                    combo->addItem(QString::fromUtf8(name), QString::fromUtf8(name));
                }
            }
            return true;
        },
        m_cmbTargetSource);

    // Restore selection or load from filter
    int idx = m_cmbTargetSource->findData(currentSelection);
    if (idx >= 0) {
        m_cmbTargetSource->setCurrentIndex(idx);
    } else {
        obs_source_t *source = get_active_filter_source(m_selectedFilterPtr);
        if (source) {
            obs_data_t *settings = obs_source_get_settings(source);
            if (settings) {
                const char *saved_target = obs_data_get_string(settings, "target_source_name");
                int saved_idx = m_cmbTargetSource->findData(saved_target ? saved_target : "");
                if (saved_idx >= 0) {
                    m_cmbTargetSource->setCurrentIndex(saved_idx);
                }
                obs_data_release(settings);
            }
        }
    }
    m_isUpdatingUi = false;
}

void TranslatorDock::loadSettingsFromFilter()
{
    obs_source_t *source = get_active_filter_source(m_selectedFilterPtr);
    if (!source) return;

    obs_data_t *settings = obs_source_get_settings(source);
    if (!settings) return;

    m_isUpdatingUi = true;

    // Mode
    bool use_remote = obs_data_get_bool(settings, "use_remote_transcription");
    m_cmbEngineMode->setCurrentIndex(use_remote ? 0 : 1);
    m_panelRemote->setVisible(use_remote);
    m_panelLocal->setVisible(!use_remote);

    // Target Source
    const char *target = obs_data_get_string(settings, "target_source_name");
    int target_idx = m_cmbTargetSource->findData(target ? target : "");
    if (target_idx >= 0) m_cmbTargetSource->setCurrentIndex(target_idx);

    // Languages
    const char *lang_in = obs_data_get_string(settings, "lang_in");
    int in_idx = m_cmbLangIn->findData(lang_in ? lang_in : "auto");
    if (in_idx >= 0) m_cmbLangIn->setCurrentIndex(in_idx);

    const char *lang_out = obs_data_get_string(settings, "lang_out");
    int out_idx = m_cmbLangOut->findData(lang_out ? lang_out : "original");
    if (out_idx >= 0) m_cmbLangOut->setCurrentIndex(out_idx);

    // Remote
    if (!m_txtWsUrl->hasFocus()) {
        m_txtWsUrl->setText(obs_data_get_string(settings, "ws_url"));
    }

    // Local
    const char *model = obs_data_get_string(settings, "model_settings");
    int model_idx = m_cmbModel->findData(model ? model : "ggml-base.bin");
    if (model_idx >= 0) m_cmbModel->setCurrentIndex(model_idx);

    bool use_custom = obs_data_get_bool(settings, "use_custom_model");
    m_chkUseCustomModel->setChecked(use_custom);
    m_widgetCustomModel->setVisible(use_custom);
    m_cmbModel->setEnabled(!use_custom);

    if (!m_txtCustomModelPath->hasFocus()) {
        m_txtCustomModelPath->setText(obs_data_get_string(settings, "custom_model_path"));
    }

    m_chkGpu->setChecked(obs_data_get_bool(settings, "processing_mode"));

    m_isUpdatingUi = false;
    obs_data_release(settings);
}

void TranslatorDock::saveSettingString(const char *key, const QString &val)
{
    if (m_isUpdatingUi) return;
    obs_source_t *source = get_active_filter_source(m_selectedFilterPtr);
    if (!source) return;

    obs_data_t *settings = obs_source_get_settings(source);
    if (!settings) return;

    obs_data_set_string(settings, key, val.toUtf8().constData());
    obs_source_update(source, settings);
    obs_data_release(settings);
}

void TranslatorDock::saveSettingBool(const char *key, bool val)
{
    if (m_isUpdatingUi) return;
    obs_source_t *source = get_active_filter_source(m_selectedFilterPtr);
    if (!source) return;

    obs_data_t *settings = obs_source_get_settings(source);
    if (!settings) return;

    obs_data_set_bool(settings, key, val);
    obs_source_update(source, settings);
    obs_data_release(settings);
}

void TranslatorDock::onTargetSourceChanged(int index)
{
    if (index >= 0) {
        saveSettingString("target_source_name", m_cmbTargetSource->itemData(index).toString());
    }
}

void TranslatorDock::onEngineModeChanged(int index)
{
    bool is_remote = (index == 0);
    m_panelRemote->setVisible(is_remote);
    m_panelLocal->setVisible(!is_remote);
    saveSettingBool("use_remote_transcription", is_remote);
}

void TranslatorDock::onLanguageChanged()
{
    saveSettingString("lang_in", m_cmbLangIn->currentData().toString());
    saveSettingString("lang_out", m_cmbLangOut->currentData().toString());
}

void TranslatorDock::onModelChanged(int index)
{
    if (index >= 0) {
        saveSettingString("model_settings", m_cmbModel->itemData(index).toString());
    }
}

void TranslatorDock::onUseCustomModelToggled(bool checked)
{
    m_widgetCustomModel->setVisible(checked);
    m_cmbModel->setEnabled(!checked);
    saveSettingBool("use_custom_model", checked);
}

void TranslatorDock::onCustomModelPathEditingFinished()
{
    saveSettingString("custom_model_path", m_txtCustomModelPath->text().trimmed());
}

void TranslatorDock::onBrowseCustomModel()
{
    QString filePath = QFileDialog::getOpenFileName(
        this,
        "Seleccionar Modelo Whisper",
        m_txtCustomModelPath->text(),
        "Modelos GGML (*.bin *.gguf);;Todos los archivos (*.*)");

    if (!filePath.isEmpty()) {
        m_txtCustomModelPath->setText(filePath);
        saveSettingString("custom_model_path", filePath);
    }
}

void TranslatorDock::onGpuToggled(bool checked)
{
    saveSettingBool("processing_mode", checked);
}

void TranslatorDock::onWsUrlEditingFinished()
{
    saveSettingString("ws_url", m_txtWsUrl->text().trimmed());
}

void TranslatorDock::refreshStatus()
{
    FilterStatusInfo info = get_active_filter_status(m_selectedFilterPtr);

    bool filterSourceChanged = (!m_lastHadActiveFilter && info.has_active_filter) ||
                               (m_lastActiveSource != info.source_context);
    m_lastHadActiveFilter = info.has_active_filter;
    m_lastActiveSource = info.source_context;

    if (!info.has_active_filter) {
        m_lblWarning->setVisible(true);
        m_grpFilterSelector->setVisible(false);
        m_lblVadIcon->setPixmap(VectorIcons::iconMic(QColor("#7f8c8d"), 18).pixmap(18, 18));
        m_lblVadText->setText("Inactivo");
        m_lblVadText->setStyleSheet("color: #7f8c8d; font-size: 9pt; font-weight: 600;");
        m_btnPause->setEnabled(false);
        m_btnClear->setEnabled(false);
        return;
    }

    if (filterSourceChanged) {
        refreshFilterList();
        loadSettingsFromFilter();
    }

    m_lblWarning->setVisible(false);
    m_grpFilterSelector->setVisible(true);
    m_btnPause->setEnabled(true);
    m_btnClear->setEnabled(true);

    // VAD Status, Mute & Pause State
    if (info.is_muted) {
        m_lblVadIcon->setPixmap(VectorIcons::iconMicMuted(QColor("#d4883b"), 18).pixmap(18, 18));
        m_lblVadText->setText("Muteado (OBS)");
        m_lblVadText->setStyleSheet("color: #d4883b; font-size: 9pt; font-weight: 600;");
    } else if (info.is_paused) {
        m_lblVadIcon->setPixmap(VectorIcons::iconPause(QColor("#d4883b"), 18).pixmap(18, 18));
        m_lblVadText->setText("En pausa");
        m_lblVadText->setStyleSheet("color: #d4883b; font-size: 9pt; font-weight: 600;");
        m_btnPause->setText("Reanudar");
        m_btnPause->setIcon(VectorIcons::iconPlay(QColor("#e0e0e0"), 14));
    } else {
        m_btnPause->setText("Pausar");
        m_btnPause->setIcon(VectorIcons::iconPause(QColor("#e0e0e0"), 14));
        if (info.in_speech) {
            m_lblVadIcon->setPixmap(VectorIcons::iconMic(QColor("#4caf50"), 18).pixmap(18, 18));
            m_lblVadText->setText("Hablando");
            m_lblVadText->setStyleSheet("color: #4caf50; font-size: 9pt; font-weight: 600;");
        } else {
            m_lblVadIcon->setPixmap(VectorIcons::iconMic(QColor("#7f8c8d"), 18).pixmap(18, 18));
            m_lblVadText->setText("Silencio");
            m_lblVadText->setStyleSheet("color: #8c8c8c; font-size: 9pt; font-weight: 600;");
        }
    }

    // Remote Connection Status
    if (info.is_remote) {
        if (info.connection_status.find("Conectado") != std::string::npos || info.connection_status.find("🟢") != std::string::npos) {
            m_lblConnectionDot->setPixmap(VectorIcons::statusDot(QColor("#4caf50"), 16));
            m_lblConnectionText->setText("Conectado");
            m_lblConnectionText->setStyleSheet("color: #4caf50; font-weight: 600; font-size: 8.5pt;");
        } else if (info.connection_status.find("Conectando") != std::string::npos || info.connection_status.find("🟡") != std::string::npos) {
            m_lblConnectionDot->setPixmap(VectorIcons::statusDot(QColor("#fbc02d"), 16));
            m_lblConnectionText->setText("Conectando");
            m_lblConnectionText->setStyleSheet("color: #fbc02d; font-weight: 600; font-size: 8.5pt;");
        } else {
            m_lblConnectionDot->setPixmap(VectorIcons::statusDot(QColor("#e57373"), 16));
            m_lblConnectionText->setText("Desconectado");
            m_lblConnectionText->setStyleSheet("color: #e57373; font-weight: 600; font-size: 8.5pt;");
        }
    }
}

void TranslatorDock::onTogglePause()
{
    toggle_active_filter_pause(m_selectedFilterPtr);
    refreshStatus();
}

void TranslatorDock::onClearSubtitles()
{
    clear_active_filter_subtitles(m_selectedFilterPtr);
}

void TranslatorDock::onReconnect()
{
    saveSettingString("ws_url", m_txtWsUrl->text().trimmed());
    trigger_active_filter_reconnect(m_selectedFilterPtr);
    refreshStatus();
}

void TranslatorDock::onOpenSettings()
{
    TranslatorSettingsDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        loadSettingsFromFilter();
    }
    refreshStatus();
}

static void frontend_event_handler(enum obs_frontend_event event, void *private_data)
{
    (void)private_data;
    if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
        if (!s_translator_dock) {
            s_translator_dock = new TranslatorDock();
            obs_frontend_add_dock_by_id("ai_translator_dock", "Traductor IA", s_translator_dock);
        }
    }
}

static void open_settings_tools_cb(void *private_data)
{
    (void)private_data;
    QMainWindow *main_window = (QMainWindow *)obs_frontend_get_main_window();
    TranslatorSettingsDialog dialog(main_window);
    dialog.exec();
}

extern "C" void init_translator_dock(void)
{
    obs_frontend_add_event_callback(frontend_event_handler, nullptr);
    obs_frontend_add_tools_menu_item("Ajustes de Traductor IA...", open_settings_tools_cb, nullptr);
}

extern "C" void free_translator_dock(void)
{
    s_translator_dock = nullptr;
}
