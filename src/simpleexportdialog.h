#pragma once
#include <QDialog>
#include "subtitlemodel.h"

class QComboBox;
class QSpinBox;
class QCheckBox;
class QPushButton;
class QLabel;
class StylePreviewWidget;

class SimpleExportDialog : public QDialog {
    Q_OBJECT
public:
    enum class Action { Cancelled, ExportSRT, ExportVTT, ExportTXT, ExportMP4, ExportTransparent };
    struct Result {
        Action action = Action::Cancelled;
        SubtitleStyle style;
        int qualityIndex = 1; // 0=高畫質 1=標準 2=流暢
        bool bilingual = false;
    };

    explicit SimpleExportDialog(SubtitleModel *model,
                                const QString &videoPath,
                                qint64 durationMs,
                                QWidget *parent = nullptr);
    Result result() const { return m_result; }

private slots:
    void onDownloadSRT();
    void onDownloadVTT();
    void onDownloadTXT();
    void onExportMP4();
    void onExportTransparent();

private:
    void buildUI();
    QString estimateTime() const;

    Result m_result;
    SubtitleModel *m_model = nullptr;
    QString m_videoPath;
    qint64 m_durationMs = 0;

    QComboBox *m_qualityCombo = nullptr;
    QCheckBox *m_bilingualCb = nullptr;
    QLabel *m_estimateLabel = nullptr;
    SubtitleStyle m_editStyle;
};
