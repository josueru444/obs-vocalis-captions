#include "translator_ui.h"
#include <QString>
#include <QFont>

// Function declared in ai_audio_filter.cpp to get real-time status
extern std::string get_filter_connection_status(void* data);

TranslatorUI::TranslatorUI(ai_filter_data* filter_data, QWidget *parent)
    : QDialog(parent), m_filter_data(filter_data)
{
    setWindowTitle("Interfaz de Traducción");
    resize(400, 300);

    QVBoxLayout* layout = new QVBoxLayout(this);

    QLabel* titleLabel = new QLabel("Traductor - Panel de Control", this);
    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(16);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    layout->addWidget(titleLabel);

    m_statusLabel = new QLabel("Estado: Obteniendo...", this);
    QFont statusFont = m_statusLabel->font();
    statusFont.setPointSize(12);
    m_statusLabel->setFont(statusFont);
    layout->addWidget(m_statusLabel);

    layout->addStretch();

    m_closeButton = new QPushButton("Cerrar", this);
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::accept);
    layout->addWidget(m_closeButton);

    // Setup timer for real-time updates (every 500ms)
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &TranslatorUI::updateStatus);
    m_timer->start(500);

    // Initial update
    updateStatus();
}

TranslatorUI::~TranslatorUI()
{
}

void TranslatorUI::updateStatus()
{
    if (m_filter_data) {
        std::string status = get_filter_connection_status(m_filter_data);
        m_statusLabel->setText(QString::fromStdString("Estado: " + status));
    }
}
