#pragma once
#include <QDialog>
#include "subtitlemodel.h"

class QComboBox; class QSpinBox; class QCheckBox;
class QRadioButton;

class ExportDialog : public QDialog {
    Q_OBJECT
public:
    explicit ExportDialog(SubtitleStyle defaultStyle, QWidget *parent = nullptr);

    struct Result {
        bool     accepted      = false;
        bool     burnSubtitles = true;
        bool     bilingualBurn = false;
        bool     srtOnly       = false;
        QString  videoCodec    = "libx264";
        int      crf           = 23;
        QString  preset        = "fast";
        int      audioBitrate  = 192;
        SubtitleStyle style;
    };
    Result result() const { return m_result; }

private:
    void buildUI();
    Result        m_result;
    SubtitleStyle m_style;

    QRadioButton       *m_burnRadio       = nullptr;
    QRadioButton       *m_srtRadio        = nullptr;
    QCheckBox          *m_bilingualCb     = nullptr;
    QComboBox          *m_codecCombo      = nullptr;
    QComboBox          *m_presetCombo     = nullptr;
    QSpinBox           *m_crfSpin         = nullptr;
    QSpinBox           *m_audioBrSpin     = nullptr;
};
