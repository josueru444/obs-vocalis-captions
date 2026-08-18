#pragma once

#include <QDialog>
#include <QLabel>
#include <QVBoxLayout>
#include <QPushButton>
#include <QTimer>

// Forward declaration of the filter data so we can interact with it if needed
struct ai_filter_data;

class TranslatorUI : public QDialog {
    Q_OBJECT

public:
    explicit TranslatorUI(ai_filter_data* filter_data, QWidget *parent = nullptr);
    ~TranslatorUI() override;

private slots:
    void updateStatus();

private:
    ai_filter_data* m_filter_data;
    QLabel* m_statusLabel;
    QPushButton* m_closeButton;
    QTimer* m_timer;
};
