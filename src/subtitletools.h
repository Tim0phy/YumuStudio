#pragma once
#include <QDialog>
#include <QVector>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QSpinBox;
class CutDetector;
struct RefineParams;

// "Smart segmentation": split long cues by reading rhythm, characters per line
// and on-screen dwell time.
class SmartSplitDialog : public QDialog {
    Q_OBJECT
public:
    explicit SmartSplitDialog(int subtitleCount, QWidget *parent = nullptr);

signals:
    void applied(const RefineParams &params);

private:
    void apply();

    QSpinBox *m_charsPerLine = nullptr;
    QSpinBox *m_maxLines = nullptr;
    QSpinBox *m_maxSeconds = nullptr;
};

// Scene-cut detection plus magnetic alignment of subtitle boundaries.
class CutAlignDialog : public QDialog {
    Q_OBJECT
public:
    CutAlignDialog(const QString &videoPath, const QString &ffmpegPath, qint64 durationMs,
                   int subtitleCount, QWidget *parent = nullptr);

signals:
    void requestAlign(const QVector<qint64> &cutPointsMs, qint64 toleranceMs);

private slots:
    void detect();
    void align();

private:
    void updateSummary();

    QString m_videoPath;
    QString m_ffmpegPath;
    qint64 m_durationMs = 0;
    CutDetector *m_detector = nullptr;
    QVector<qint64> m_cuts;

    QDoubleSpinBox *m_threshold = nullptr;
    QSpinBox *m_toleranceMs = nullptr;
    QProgressBar *m_progress = nullptr;
    QLabel *m_summary = nullptr;
    QPushButton *m_detectButton = nullptr;
    QPushButton *m_alignButton = nullptr;
};
