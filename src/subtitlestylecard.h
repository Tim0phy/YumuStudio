#pragma once
#include <QWidget>
#include <QVector>
#include "subtitlemodel.h"

class QComboBox;
class QFontComboBox;
class QSlider;
class QLabel;
class QLineEdit;
class QPushButton;
class QButtonGroup;
class QGridLayout;
class QStackedWidget;
class StylePreviewWidget;

// Right-card page: subtitle style studio.
// Live preview canvas on top, segmented library (presets / mine), then
// grouped cards (type / colour / effect / placement). All edits emit
// styleChanged live so MainWindow can write back to SubtitleModel +
// VideoPreview.
class SubtitleStyleCard : public QWidget {
    Q_OBJECT
public:
    explicit SubtitleStyleCard(QWidget *parent = nullptr);
    void setStyle(const SubtitleStyle &s);
    void retranslateUi();
    SubtitleStyle style() const { return m_style; }
    // Called when the user drags the subtitle on the video preview: the
    // position is now free-form, so the 9-grid highlight is cleared (the
    // coordinates themselves are kept). Cleared by any explicit grid pick.
    void markPositionCustom();
    static QVector<SubtitleStyle> builtinPresets();

signals:
    void styleChanged(const SubtitleStyle &s);

private:
    void buildUI();
    void syncUi();
    void emitChanged();
    void applyPreset(int idx);
    void saveCustomStyle();
    void refreshCustomList();
    void applyCustomStyle(const QString &name);
    void updateLibraryHeight();
    void pickTextColor();
    void pickOutlineColor();
    void pickShadowColor();
    static QString colorHex(const QColor &c);
    static QColor hexColor(const QString &t, const QColor &fallback);
    QVariantMap styleToMap(const SubtitleStyle &st, const QString &name) const;
    SubtitleStyle styleFromMap(const QVariantMap &m, const SubtitleStyle &base) const;

    SubtitleStyle m_style;
    bool m_updating = false;
    bool m_freePosition = false;
    StylePreviewWidget *m_preview = nullptr;
    QVector<QPushButton*> m_presetBtns;
    QVector<StylePreviewWidget*> m_presetThumbs;
    QVector<QLabel*> m_presetNameLabels;
    QVector<QLabel*> m_presetTagLabels;
    QButtonGroup *m_libGroup = nullptr;
    QPushButton *m_segPresetBtn = nullptr;
    QPushButton *m_segMineBtn = nullptr;
    QStackedWidget *m_libStack = nullptr;
    QFontComboBox *m_fontCombo = nullptr;
    QSlider *m_sizeSlider = nullptr;
    QLabel *m_sizeLabel = nullptr;
    QPushButton *m_boldBtn = nullptr;
    QPushButton *m_italicBtn = nullptr;
    QLabel *m_fontTitle = nullptr;
    QPushButton *m_textSwatch = nullptr;
    QLineEdit *m_textHex = nullptr;
    QVector<QPushButton*> m_posBtns;
    QButtonGroup *m_modeGroup = nullptr;
    QPushButton *m_outlineCard = nullptr;
    QPushButton *m_bgCard = nullptr;
    QPushButton *m_outlineSwatch = nullptr;
    QLineEdit *m_outlineHex = nullptr;
    QSlider *m_outlineSlider = nullptr;
    QLabel *m_outlineLabel = nullptr;
    QSlider *m_shadowSlider = nullptr;
    QLabel *m_shadowLabel = nullptr;
    QPushButton *m_shadowSwatch = nullptr;
    QLineEdit *m_shadowHex = nullptr;
    QSlider *m_shadowAlphaSlider = nullptr;
    QLabel *m_shadowAlphaLabel = nullptr;
    QWidget *m_minePage = nullptr;
    QWidget *m_mineGridContainer = nullptr;
    QGridLayout *m_mineGrid = nullptr;
    QGridLayout *m_presetGrid = nullptr;
    QVector<QPushButton*> m_customBtns;
    QVector<StylePreviewWidget*> m_customThumbs;
    QVector<QLabel*> m_customNameLabels;
    QVector<QLabel*> m_customTagLabels;

    // Retranslatable labels
    QLabel *m_canvasHint = nullptr;
    QLabel *m_headerTitle = nullptr;
    QPushButton *m_saveStyleBtn = nullptr;
    QLabel *m_mineHint = nullptr;
    QLabel *m_typeGroupTitle = nullptr;
    QLabel *m_colorGroupTitle = nullptr;
    QLabel *m_posGroupTitle = nullptr;
    QLabel *m_posHint = nullptr;
    QLabel *m_fxGroupTitle = nullptr;
    QLabel *m_outlineColorLabel = nullptr;
    QLabel *m_outlineWidthLabel = nullptr;
    QLabel *m_shadowTitleLabel = nullptr;
    QLabel *m_shadowColorLabel = nullptr;
    QLabel *m_shadowOpacityLabel = nullptr;
};
