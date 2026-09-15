#include "subtitlestylecard.h"
#include "stylepreviewwidget.h"
#include "appconfig.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QPair>
#include <QFontComboBox>
#include <QStackedWidget>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QSlider>
#include <QLineEdit>
#include <QScrollArea>
#include <QButtonGroup>
#include <QFontDatabase>
#include <QColorDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QSettings>
#include <QSignalBlocker>
#include <QVariant>

namespace {
QString settingKey() { return QStringLiteral("SubtitleStylesCustom"); }
QSettings *settings() { return AppConfig::instance().settings(); }

QString colorHexWithAlpha(const QColor &c) {
    if (!c.isValid()) return QStringLiteral("#FF000000");
    return QStringLiteral("#%1%2%3%4")
        .arg(c.alpha(), 2, 16, QChar('0'))
        .arg(c.red(), 2, 16, QChar('0'))
        .arg(c.green(), 2, 16, QChar('0'))
        .arg(c.blue(), 2, 16, QChar('0'))
        .toUpper();
}

QColor hexColorWithAlpha(const QString &t, const QColor &fallback) {
    QString s = t.trimmed();
    if (s.startsWith('#')) s = s.mid(1);
    if (s.length() == 6) {
        bool ok = false;
        const int rgb = s.toInt(&ok, 16);
        if (!ok) return fallback;
        return QColor((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
    }
    if (s.length() != 8) return fallback;
    bool ok = false;
    const uint argb = s.toUInt(&ok, 16);
    if (!ok) return fallback;
    return QColor((argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF, (argb >> 24) & 0xFF);
}
}

QString SubtitleStyleCard::colorHex(const QColor &c) {
    if (!c.isValid()) return QStringLiteral("#000000");
    return QStringLiteral("#%1%2%3")
        .arg(c.red(), 2, 16, QChar('0'))
        .arg(c.green(), 2, 16, QChar('0'))
        .arg(c.blue(), 2, 16, QChar('0'))
        .toUpper();
}

QColor SubtitleStyleCard::hexColor(const QString &t, const QColor &fallback) {
    QString s = t.trimmed();
    if (s.startsWith('#')) s = s.mid(1);
    if (s.length() != 6) return fallback;
    bool ok = false;
    const int rgb = s.toInt(&ok, 16);
    if (!ok) return fallback;
    return QColor((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

QVector<SubtitleStyle> SubtitleStyleCard::builtinPresets() {
    QVector<SubtitleStyle> out;
    SubtitleStyle base;
    base.fontFamily = QString::fromUtf8("\xe5\xbe\xae\xe8\xbd\xaf\xe9\x9b\x85\xe9\xbb\x91");
    base.fontSize = 24;
    base.bold = false;
    base.italic = false;
    base.marginV = 20;
    base.posX = 0.5;
    base.posY = -1.0;
    base.position = 0;
    base.bilingualEnabled = false;
    base.bilingualFontSize = 20;
    base.bilingualColor = QColor(255, 255, 180);
    base.shadowWidth = 1;
    base.shadowColor = QColor(0, 0, 0);
    base.shadowOpacity = 50;

    SubtitleStyle p0 = base; // classic white on black edge
    p0.textColor = Qt::white; p0.outlineColor = Qt::black; p0.outlineWidth = 2; p0.showBg = false;
    out << p0;
    SubtitleStyle p1 = base; // movie
    p1.fontSize = 26; p1.textColor = QColor(255, 248, 231); p1.outlineColor = Qt::black; p1.outlineWidth = 2; p1.showBg = false;
    out << p1;
    SubtitleStyle p2 = base; // YouTube
    p2.textColor = Qt::white; p2.outlineWidth = 0; p2.showBg = true; p2.bgColor = QColor(0, 0, 0, 180);
    out << p2;
    SubtitleStyle p3 = base; // black bg white text
    p3.textColor = Qt::white; p3.outlineWidth = 0; p3.showBg = true; p3.bgColor = QColor(0, 0, 0, 220);
    out << p3;
    SubtitleStyle p4 = base; // fresh minimal
    p4.fontSize = 22; p4.textColor = Qt::white; p4.outlineColor = QColor(40, 40, 45); p4.outlineWidth = 1; p4.showBg = false;
    out << p4;
    SubtitleStyle p5 = base; // bold醒目
    p5.fontSize = 28; p5.bold = true; p5.textColor = QColor(255, 235, 59); p5.outlineColor = Qt::black; p5.outlineWidth = 3; p5.showBg = false;
    out << p5;
    return out;
}

SubtitleStyleCard::SubtitleStyleCard(QWidget *parent) : QWidget(parent) {
    m_style = builtinPresets().value(0);
    m_style.fontSize = 24;
    buildUI();
    refreshCustomList();
    syncUi();
}

static QLabel *sectionLabel(const QString &t, QWidget *p) {
    auto *l = new QLabel(t, p);
    l->setObjectName(QStringLiteral("yumuCardTitle"));
    return l;
}

// Grouped settings card: title row + content layout, visually distinct from
// the old flat section list.
static QFrame *groupCard(QWidget *parent, const QString &title, QVBoxLayout **outBody, QLabel **outTitle = nullptr) {
    auto *card = new QFrame(parent);
    card->setObjectName(QStringLiteral("modeCard"));
    auto *lay = new QVBoxLayout(card);
    lay->setContentsMargins(12, 10, 12, 12);
    lay->setSpacing(8);
    auto *t = new QLabel(title, card);
    t->setObjectName(QStringLiteral("yumuCardTitle"));
    lay->addWidget(t);
    if (outBody) *outBody = lay;
    if (outTitle) *outTitle = t;
    return card;
}

void SubtitleStyleCard::buildUI() {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(8);

    // Pinned live preview canvas: lives outside the scroll area so it stays
    // visible while the user scrolls the colour/font controls below.
    auto *canvas = new QFrame(this);
    canvas->setObjectName(QStringLiteral("previewCanvas"));
    auto *canvasLay = new QVBoxLayout(canvas);
    canvasLay->setContentsMargins(8, 8, 8, 8);
    m_preview = new StylePreviewWidget(canvas);
    m_preview->setMinimumHeight(140);
    m_preview->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    canvasLay->addWidget(m_preview);
    root->addWidget(canvas);
    m_canvasHint = new QLabel(tr("即時預覽 · 亦可喺影片畫面直接拖移字幕定位"), this);
    m_canvasHint->setWordWrap(true);
    m_canvasHint->setStyleSheet(QStringLiteral("color:#8E86A0; font-size:8pt; background:transparent;"));
    root->addWidget(m_canvasHint);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget;
    auto *lay = new QVBoxLayout(content);
    // Flush with the card: rightCard already pads 12H/8V (same as the toolbar
    // above), so page content left edges align across all three pages.
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(8);

    // Header: title + save (save lives next to the library it feeds).
    auto *headRow = new QHBoxLayout;
    m_headerTitle = sectionLabel(tr("字幕樣式"), content);
    headRow->addWidget(m_headerTitle, 1);
    m_saveStyleBtn = new QPushButton(tr("存為我的樣式"), content);
    m_saveStyleBtn->setObjectName(QStringLiteral("pillBtnSecondary"));
    m_saveStyleBtn->setCursor(Qt::PointingHandCursor);
    connect(m_saveStyleBtn, &QPushButton::clicked, this, &SubtitleStyleCard::saveCustomStyle);
    headRow->addWidget(m_saveStyleBtn);
    lay->addLayout(headRow);

    // Segmented library switch: presets vs my styles.
    auto *segRow = new QHBoxLayout;
    segRow->setSpacing(6);
    m_segPresetBtn = new QPushButton(tr("預設庫"), content);
    m_segMineBtn = new QPushButton(tr("我的樣式"), content);
    for (auto *b : {m_segPresetBtn, m_segMineBtn}) {
        b->setObjectName(QStringLiteral("segBtn"));
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
    }
    m_libGroup = new QButtonGroup(this);
    m_libGroup->setExclusive(true);
    m_libGroup->addButton(m_segPresetBtn, 0);
    m_libGroup->addButton(m_segMineBtn, 1);
    m_segPresetBtn->setChecked(true);
    segRow->addWidget(m_segPresetBtn);
    segRow->addWidget(m_segMineBtn);
    lay->addLayout(segRow);

    m_libStack = new QStackedWidget(content);
    // Page 0: preset gallery — 2-col cards with live-rendered thumbnails.
    auto *presetPage = new QWidget(m_libStack);
    auto *presetPageLay = new QVBoxLayout(presetPage);
    presetPageLay->setContentsMargins(0, 0, 0, 0);
    presetPageLay->setSpacing(8);
    m_presetGrid = new QGridLayout;
    m_presetGrid->setSpacing(8);
    const auto presets = builtinPresets();
    const QStringList names = { tr("經典白字黑邊"), tr("電影字幕"), tr("YouTube風格"),
                                tr("黑底白字"), tr("清新簡約"), tr("醒目加粗") };
    const QStringList descs = { tr("描邊 · 通用"), tr("描邊 · 暖白"), tr("背景框 · 平台感"),
                                tr("背景框 · 高對比"), tr("幼邊 · 乾淨"), tr("粗體 · 吸睛") };
    for (int i = 0; i < 6; ++i) {
        auto *b = new QPushButton(presetPage);
        b->setObjectName(QStringLiteral("presetCard"));
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setProperty("presetIdx", i);
        auto *card = new QVBoxLayout(b);
        card->setContentsMargins(10, 10, 10, 10);
        card->setSpacing(6);
        auto *thumb = new StylePreviewWidget(b);
        // Gallery thumbnail: same colours/outline/bg/weight via thumbnail
        // mode (pixel-sized, DPI-independent single line). Tall enough that
        // the style always displays completely.
        SubtitleStyle th = presets.value(i);
        th.bilingualEnabled = false;
        thumb->setStyle(th);
        thumb->setSampleText(tr("字幕 Aa"), {});
        thumb->setThumbnailMode(true);
        thumb->setMinimumHeight(96);
        thumb->setMaximumHeight(96);
        thumb->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        card->addWidget(thumb);
        m_presetThumbs << thumb;
        // Name above, tag below: full row width each with guaranteed heights
        // so neither can ever collapse into clipped slivers.
        auto *name = new QLabel(names.value(i), b);
        name->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        name->setWordWrap(false);
        name->setMinimumHeight(20);
        card->addWidget(name);
        m_presetNameLabels << name;
        auto *tag = new QLabel(descs.value(i), b);
        tag->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        tag->setWordWrap(false);
        tag->setMinimumHeight(16);
        tag->setStyleSheet(QStringLiteral("color:#8E86A0; font-size:8pt; background:transparent;"));
        card->addWidget(tag);
        m_presetTagLabels << tag;
        // QPushButton never auto-expands to its child layout: with empty text
        // it keeps the style-default short height, so the grid rows collapse
        // and the thumbnail/labels overflow invisibly behind later rows.
        // Pin an explicit minimum covering the real content stack
        // (thumb 96 + name 20 + tag 16 + margins 20 + spacing 12).
        b->setMinimumHeight(164);
        connect(b, &QPushButton::clicked, this, [this, i] { applyPreset(i); });
        m_presetBtns << b;
        m_presetGrid->addWidget(b, i / 2, i % 2);
    }
    presetPageLay->addLayout(m_presetGrid);
    // Page 1: my saved styles.
    m_minePage = new QWidget(m_libStack);
    auto *minePage = m_minePage;
    auto *minePageLay = new QVBoxLayout(minePage);
    minePageLay->setContentsMargins(0, 0, 0, 0);
    minePageLay->setSpacing(6);
    m_mineHint = new QLabel(tr("微調後點「存為我的樣式」保存，匯出時都能快速選用"), minePage);
    m_mineHint->setWordWrap(true);
    m_mineHint->setStyleSheet(QStringLiteral("color:#8E86A0; font-size:8pt; background:transparent;"));
    minePageLay->addWidget(m_mineHint);
    m_mineGridContainer = new QWidget(minePage);
    m_mineGridContainer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    m_mineGrid = new QGridLayout(m_mineGridContainer);
    m_mineGrid->setSpacing(8);
    m_mineGrid->setContentsMargins(0, 0, 0, 0);
    minePageLay->addWidget(m_mineGridContainer);
    m_libStack->addWidget(presetPage);
    m_libStack->addWidget(minePage);
    m_libStack->setCurrentIndex(0);
    updateLibraryHeight();
    connect(m_libGroup, &QButtonGroup::idClicked, this, [this](int id) {
        if (m_libStack) m_libStack->setCurrentIndex(id);
        if (id == 1) refreshCustomList(); // ensure "My Styles" list is up-to-date
        updateLibraryHeight();
    });
    lay->addWidget(m_libStack);

    // ── Grouped studio cards (replaces the old flat section list) ──
    // Card A: type (font + size + weight).
    QVBoxLayout *typeBody = nullptr;
    lay->addWidget(groupCard(content, tr("字體排印"), &typeBody, &m_typeGroupTitle));
    auto *fontRow = new QHBoxLayout;
    m_fontTitle = new QLabel(tr("字體"), content);
    m_sizeLabel = new QLabel(QStringLiteral("24px"), content);
    m_sizeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    fontRow->addWidget(m_fontTitle);
    fontRow->addStretch();
    // Weight toggles (new: bold/italic were preset-only before).
    m_boldBtn = new QPushButton(QStringLiteral("B"), content);
    m_boldBtn->setObjectName(QStringLiteral("weightBtn"));
    m_boldBtn->setCheckable(true);
    m_boldBtn->setCursor(Qt::PointingHandCursor);
    m_boldBtn->setToolTip(tr("粗體"));
    QFont boldF = m_boldBtn->font(); boldF.setBold(true); m_boldBtn->setFont(boldF);
    m_italicBtn = new QPushButton(QStringLiteral("I"), content);
    m_italicBtn->setObjectName(QStringLiteral("weightBtn"));
    m_italicBtn->setCheckable(true);
    m_italicBtn->setCursor(Qt::PointingHandCursor);
    m_italicBtn->setToolTip(tr("斜體"));
    QFont itF = m_italicBtn->font(); itF.setItalic(true); m_italicBtn->setFont(itF);
    connect(m_boldBtn, &QPushButton::clicked, this, [this](bool on) {
        if (m_updating) return;
        m_style.bold = on;
        emitChanged();
    });
    connect(m_italicBtn, &QPushButton::clicked, this, [this](bool on) {
        if (m_updating) return;
        m_style.italic = on;
        emitChanged();
    });
    fontRow->addWidget(m_boldBtn);
    fontRow->addWidget(m_italicBtn);
    fontRow->addWidget(m_sizeLabel);
    typeBody->addLayout(fontRow);
    auto *fontCtl = new QHBoxLayout;
    // QFontComboBox lists every installed family (no 60-item cap), renders
    // each entry in its own typeface, and supports keyboard letter-jump.
    m_fontCombo = new QFontComboBox(content);
    m_fontCombo->setEditable(false);
    connect(m_fontCombo, &QFontComboBox::currentFontChanged, this, [this](const QFont &f) {
        if (m_updating) return;
        m_style.fontFamily = f.family();
        emitChanged();
    });
    m_sizeSlider = new QSlider(Qt::Horizontal, content);
    m_sizeSlider->setRange(12, 72);
    m_sizeSlider->setValue(24);
    connect(m_sizeSlider, &QSlider::valueChanged, this, [this](int v) {
        if (m_updating) return;
        m_style.fontSize = v;
        m_sizeLabel->setText(QString::number(v) + QStringLiteral("px"));
        emitChanged();
    });
    fontCtl->addWidget(m_fontCombo, 1);
    fontCtl->addWidget(m_sizeSlider, 1);
    typeBody->addLayout(fontCtl);

    // Card B: colour (text + outline swatches).
    QVBoxLayout *colorBody = nullptr;
    lay->addWidget(groupCard(content, tr("色彩"), &colorBody, &m_colorGroupTitle));
    auto *tcRow = new QHBoxLayout;
    tcRow->addWidget(new QLabel(tr("字體"), content));
    m_textSwatch = new QPushButton(content);
    m_textSwatch->setFixedSize(44, 32);
    m_textSwatch->setCursor(Qt::PointingHandCursor);
    connect(m_textSwatch, &QPushButton::clicked, this, &SubtitleStyleCard::pickTextColor);
    m_textHex = new QLineEdit(content);
    m_textHex->setMaxLength(7);
    connect(m_textHex, &QLineEdit::editingFinished, this, [this] {
        if (m_updating) return;
        m_style.textColor = hexColor(m_textHex->text(), m_style.textColor);
        syncUi();
        emitChanged();
    });
    tcRow->addWidget(m_textSwatch);
    tcRow->addWidget(m_textHex, 1);
    colorBody->addLayout(tcRow);

    // Card C: placement (9-grid lives in a card now, with drag hint).
    QVBoxLayout *posBody = nullptr;
    lay->addWidget(groupCard(content, tr("位置"), &posBody, &m_posGroupTitle));
    m_posHint = new QLabel(tr("點選格子定位，亦可喺預覽畫面直接拖移"), content);
    m_posHint->setWordWrap(true);
    m_posHint->setStyleSheet(QStringLiteral("color:#8E86A0; font-size:8pt; background:transparent;"));
    posBody->addWidget(m_posHint);
    auto *posGrid = new QGridLayout;
    posGrid->setSpacing(6);
    for (int i = 0; i < 9; ++i) {
        auto *b = new QPushButton(QString::fromUtf8("\xe2\x80\xa2"), content);
        b->setObjectName(QStringLiteral("posBtn"));
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setFixedSize(48, 48);
        connect(b, &QPushButton::clicked, this, [this, i] {
            if (m_updating) return;
            const int row = i / 3, col = i % 3;
            m_style.posX = col == 0 ? 0.25 : (col == 1 ? 0.5 : 0.75);
            m_style.posY = row == 0 ? 0.15 : (row == 1 ? 0.5 : 0.85);
            m_style.position = row == 0 ? 1 : (row == 1 ? 2 : 0);
            m_freePosition = false;
            syncUi();
            emitChanged();
        });
        m_posBtns << b;
        posGrid->addWidget(b, i / 3, i % 3, Qt::AlignHCenter);
    }
    posBody->addLayout(posGrid);

    // Card D: effect (outline/bg mode + outline + shadow).
    QVBoxLayout *fxBody = nullptr;
    lay->addWidget(groupCard(content, tr("描邊．陰影．背景"), &fxBody, &m_fxGroupTitle));
    auto *modeRow = new QHBoxLayout;
    m_outlineCard = new QPushButton(tr("✒ 描邊"), content);
    m_outlineCard->setToolTip(tr("文字帶描邊和陰影，適合大多數視頻"));
    m_bgCard = new QPushButton(tr("▦ 背景框"), content);
    m_bgCard->setToolTip(tr("文字帶底色色塊，畫面雜亂時更清晰"));
    for (auto *b : {m_outlineCard, m_bgCard}) {
        b->setObjectName(QStringLiteral("presetCard"));
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setMinimumHeight(48);
    }
    m_modeGroup = new QButtonGroup(this);
    m_modeGroup->setExclusive(true);
    m_modeGroup->addButton(m_outlineCard, 0);
    m_modeGroup->addButton(m_bgCard, 1);
    connect(m_modeGroup, &QButtonGroup::idClicked, this, [this](int id) {
        if (m_updating) return;
        m_style.showBg = (id == 1);
        syncUi();
        emitChanged();
    });
    modeRow->addWidget(m_outlineCard);
    modeRow->addWidget(m_bgCard);
    fxBody->addLayout(modeRow);

    auto *ocRow = new QHBoxLayout;
    m_outlineColorLabel = new QLabel(tr("邊框顏色"), content);
    ocRow->addWidget(m_outlineColorLabel, 1);
    m_outlineWidthLabel = new QLabel(tr("描邊粗細"), content);
    ocRow->addWidget(m_outlineWidthLabel);
    m_outlineLabel = new QLabel(QStringLiteral("2"), content);
    ocRow->addWidget(m_outlineLabel);
    fxBody->addLayout(ocRow);
    auto *ocCtl = new QHBoxLayout;
    m_outlineSwatch = new QPushButton(content);
    m_outlineSwatch->setFixedSize(44, 32);
    m_outlineSwatch->setCursor(Qt::PointingHandCursor);
    connect(m_outlineSwatch, &QPushButton::clicked, this, &SubtitleStyleCard::pickOutlineColor);
    m_outlineHex = new QLineEdit(content);
    m_outlineHex->setMaxLength(7);
    connect(m_outlineHex, &QLineEdit::editingFinished, this, [this] {
        if (m_updating) return;
        m_style.outlineColor = hexColor(m_outlineHex->text(), m_style.outlineColor);
        syncUi();
        emitChanged();
    });
    m_outlineSlider = new QSlider(Qt::Horizontal, content);
    m_outlineSlider->setRange(0, 8);
    connect(m_outlineSlider, &QSlider::valueChanged, this, [this](int v) {
        if (m_updating) return;
        m_style.outlineWidth = v;
        m_outlineLabel->setText(QString::number(v));
        emitChanged();
    });
    ocCtl->addWidget(m_outlineSwatch);
    ocCtl->addWidget(m_outlineHex, 1);
    ocCtl->addWidget(m_outlineSlider, 1);
    fxBody->addLayout(ocCtl);

    auto *shRow = new QHBoxLayout;
    m_shadowTitleLabel = new QLabel(tr("陰影"), content);
    shRow->addWidget(m_shadowTitleLabel, 1);
    m_shadowLabel = new QLabel(QStringLiteral("1"), content);
    shRow->addWidget(m_shadowLabel);
    fxBody->addLayout(shRow);
    m_shadowSlider = new QSlider(Qt::Horizontal, content);
    m_shadowSlider->setRange(0, 10);
    connect(m_shadowSlider, &QSlider::valueChanged, this, [this](int v) {
        if (m_updating) return;
        m_style.shadowWidth = v;
        m_shadowLabel->setText(QString::number(v));
        emitChanged();
    });
    fxBody->addWidget(m_shadowSlider);
    auto *scRow = new QHBoxLayout;
    m_shadowColorLabel = new QLabel(tr("陰影顏色"), content);
    scRow->addWidget(m_shadowColorLabel, 1);
    m_shadowOpacityLabel = new QLabel(tr("陰影不透明度"), content);
    scRow->addWidget(m_shadowOpacityLabel);
    m_shadowAlphaLabel = new QLabel(QStringLiteral("50%"), content);
    scRow->addWidget(m_shadowAlphaLabel);
    fxBody->addLayout(scRow);
    auto *scCtl = new QHBoxLayout;
    m_shadowSwatch = new QPushButton(content);
    m_shadowSwatch->setFixedSize(44, 32);
    m_shadowSwatch->setCursor(Qt::PointingHandCursor);
    connect(m_shadowSwatch, &QPushButton::clicked, this, &SubtitleStyleCard::pickShadowColor);
    m_shadowHex = new QLineEdit(content);
    m_shadowHex->setMaxLength(7);
    connect(m_shadowHex, &QLineEdit::editingFinished, this, [this] {
        if (m_updating) return;
        QColor c = hexColor(m_shadowHex->text(), m_style.shadowColor);
        c.setAlpha(m_style.shadowColor.alpha());
        m_style.shadowColor = c;
        syncUi();
        emitChanged();
    });
    m_shadowAlphaSlider = new QSlider(Qt::Horizontal, content);
    m_shadowAlphaSlider->setRange(0, 100);
    connect(m_shadowAlphaSlider, &QSlider::valueChanged, this, [this](int v) {
        if (m_updating) return;
        m_style.shadowOpacity = v;
        m_shadowAlphaLabel->setText(QString::number(v) + QStringLiteral("%"));
        emitChanged();
    });
    scCtl->addWidget(m_shadowSwatch);
    scCtl->addWidget(m_shadowHex, 1);
    scCtl->addWidget(m_shadowAlphaSlider, 1);
    fxBody->addLayout(scCtl);
    lay->addStretch();

    scroll->setWidget(content);
    root->addWidget(scroll, 1);
}

void SubtitleStyleCard::retranslateUi() {
    if (m_canvasHint) m_canvasHint->setText(tr("即時預覽 · 亦可喺影片畫面直接拖移字幕定位"));
    if (m_headerTitle) m_headerTitle->setText(tr("字幕樣式"));
    if (m_saveStyleBtn) m_saveStyleBtn->setText(tr("存為我的樣式"));
    if (m_segPresetBtn) m_segPresetBtn->setText(tr("預設庫"));
    if (m_segMineBtn) m_segMineBtn->setText(tr("我的樣式"));
    if (m_mineHint) m_mineHint->setText(tr("微調後點「存為我的樣式」保存，匯出時都能快速選用"));

    const QStringList names = { tr("經典白字黑邊"), tr("電影字幕"), tr("YouTube風格"),
                                tr("黑底白字"), tr("清新簡約"), tr("醒目加粗") };
    const QStringList descs = { tr("描邊 · 通用"), tr("描邊 · 暖白"), tr("背景框 · 平台感"),
                                tr("背景框 · 高對比"), tr("幼邊 · 乾淨"), tr("粗體 · 吸睛") };
    for (int i = 0; i < m_presetNameLabels.size() && i < names.size(); ++i)
        m_presetNameLabels[i]->setText(names.value(i));
    for (int i = 0; i < m_presetTagLabels.size() && i < descs.size(); ++i)
        m_presetTagLabels[i]->setText(descs.value(i));
    for (auto *thumb : m_presetThumbs)
        thumb->setSampleText(tr("字幕 Aa"), {});

    if (m_preview) m_preview->retranslateUi();

    if (m_typeGroupTitle) m_typeGroupTitle->setText(tr("字體排印"));
    if (m_fontTitle) m_fontTitle->setText(tr("字體"));
    if (m_boldBtn) m_boldBtn->setToolTip(tr("粗體"));
    if (m_italicBtn) m_italicBtn->setToolTip(tr("斜體"));
    if (m_colorGroupTitle) m_colorGroupTitle->setText(tr("色彩"));
    if (m_posGroupTitle) m_posGroupTitle->setText(tr("位置"));
    if (m_posHint) m_posHint->setText(tr("點選格子定位，亦可喺預覽畫面直接拖移"));
    if (m_fxGroupTitle) m_fxGroupTitle->setText(tr("描邊．陰影．背景"));
    if (m_outlineCard) {
        m_outlineCard->setText(tr("✒ 描邊"));
        m_outlineCard->setToolTip(tr("文字帶描邊和陰影，適合大多數視頻"));
    }
    if (m_bgCard) {
        m_bgCard->setText(tr("▦ 背景框"));
        m_bgCard->setToolTip(tr("文字帶底色色塊，畫面雜亂時更清晰"));
    }
    if (m_outlineColorLabel) m_outlineColorLabel->setText(tr("邊框顏色"));
    if (m_outlineWidthLabel) m_outlineWidthLabel->setText(tr("描邊粗細"));
    if (m_shadowTitleLabel) m_shadowTitleLabel->setText(tr("陰影"));
    if (m_shadowColorLabel) m_shadowColorLabel->setText(tr("陰影顏色"));
    if (m_shadowOpacityLabel) m_shadowOpacityLabel->setText(tr("陰影不透明度"));
}

QVariantMap SubtitleStyleCard::styleToMap(const SubtitleStyle &st, const QString &name) const {
    QVariantMap m;
    m[QStringLiteral("name")] = name;
    m[QStringLiteral("font")] = st.fontFamily;
    m[QStringLiteral("size")] = st.fontSize;
    m[QStringLiteral("bold")] = st.bold;
    m[QStringLiteral("italic")] = st.italic;
    m[QStringLiteral("text")] = colorHex(st.textColor);
    m[QStringLiteral("outline")] = colorHex(st.outlineColor);
    m[QStringLiteral("ow")] = st.outlineWidth;
    m[QStringLiteral("sw")] = st.shadowWidth;
    m[QStringLiteral("sc")] = colorHex(st.shadowColor);
    m[QStringLiteral("so")] = st.shadowOpacity;
    m[QStringLiteral("bg")] = st.showBg;
    m[QStringLiteral("bgColor")] = colorHexWithAlpha(st.bgColor);
    m[QStringLiteral("marginV")] = st.marginV;
    m[QStringLiteral("position")] = st.position;
    m[QStringLiteral("px")] = st.posX;
    m[QStringLiteral("py")] = st.posY;
    m[QStringLiteral("bilingualEnabled")] = st.bilingualEnabled;
    m[QStringLiteral("bilingualFontSize")] = st.bilingualFontSize;
    m[QStringLiteral("bilingualColor")] = colorHex(st.bilingualColor);
    return m;
}

SubtitleStyle SubtitleStyleCard::styleFromMap(const QVariantMap &m, const SubtitleStyle &base) const {
    SubtitleStyle st = base;
    st.fontFamily = m.value(QStringLiteral("font"), st.fontFamily).toString();
    st.fontSize = m.value(QStringLiteral("size"), st.fontSize).toInt();
    st.bold = m.value(QStringLiteral("bold"), st.bold).toBool();
    st.italic = m.value(QStringLiteral("italic"), st.italic).toBool();
    st.textColor = hexColor(m.value(QStringLiteral("text")).toString(), st.textColor);
    st.outlineColor = hexColor(m.value(QStringLiteral("outline")).toString(), st.outlineColor);
    st.outlineWidth = m.value(QStringLiteral("ow"), st.outlineWidth).toInt();
    st.shadowWidth = m.value(QStringLiteral("sw"), st.shadowWidth).toInt();
    st.shadowColor = hexColor(m.value(QStringLiteral("sc")).toString(), st.shadowColor);
    st.shadowOpacity = m.value(QStringLiteral("so"), st.shadowOpacity).toInt();
    st.showBg = m.value(QStringLiteral("bg"), st.showBg).toBool();
    st.bgColor = hexColorWithAlpha(m.value(QStringLiteral("bgColor")).toString(), st.bgColor);
    st.marginV = m.value(QStringLiteral("marginV"), st.marginV).toInt();
    st.position = m.value(QStringLiteral("position"), st.position).toInt();
    st.posX = m.value(QStringLiteral("px"), st.posX).toDouble();
    st.posY = m.value(QStringLiteral("py"), st.posY).toDouble();
    st.bilingualEnabled = m.value(QStringLiteral("bilingualEnabled"), st.bilingualEnabled).toBool();
    st.bilingualFontSize = m.value(QStringLiteral("bilingualFontSize"), st.bilingualFontSize).toInt();
    st.bilingualColor = hexColor(m.value(QStringLiteral("bilingualColor")).toString(), st.bilingualColor);
    return st;
}

void SubtitleStyleCard::setStyle(const SubtitleStyle &s) {
    m_style = s;
    m_freePosition = (s.posY >= 0.0);
    syncUi();
}

void SubtitleStyleCard::markPositionCustom() {
    m_freePosition = true;
    // setChecked() only emits toggled(), which nothing connects, so this
    // cannot recurse into the grid click handler (that uses clicked()).
    for (auto *b : m_posBtns) b->setChecked(false);
}

void SubtitleStyleCard::emitChanged() {
    if (m_preview) m_preview->setStyle(m_style);
    emit styleChanged(m_style);
}

void SubtitleStyleCard::applyPreset(int idx) {
    const auto presets = builtinPresets();
    if (idx < 0 || idx >= presets.size()) return;
    SubtitleStyle keep = m_style;
    const bool wasFree = m_freePosition || keep.posY >= 0.0;
    const double keepX = keep.posX;
    const double keepY = keep.posY;
    const int keepPos = keep.position;
    m_style = presets.at(idx);
    // Keep bilingual - presets are style-only, not position (unless no custom drag yet)
    m_style.bilingualEnabled = keep.bilingualEnabled;
    m_style.bilingualFontSize = keep.bilingualFontSize;
    m_style.bilingualColor = keep.bilingualColor;
    if (wasFree) {
        m_style.posX = keepX;
        m_style.posY = keepY;
        m_style.position = keepPos;
        m_freePosition = true;
    } else {
        m_freePosition = false;
    }
    syncUi();
    emitChanged();
}

void SubtitleStyleCard::syncUi() {
    m_updating = true;
    const QSignalBlocker b1(m_fontCombo), b2(m_sizeSlider), b3(m_outlineSlider),
        b4(m_shadowSlider), b5(m_shadowAlphaSlider), b6(m_textHex),
        b7(m_outlineHex), b8(m_shadowHex),
        b9(m_boldBtn), b10(m_italicBtn);
    // QFontComboBox always contains the family, so the display can never
    // silently fall back to the wrong font.
    m_fontCombo->setCurrentFont(QFont(m_style.fontFamily));
    m_sizeSlider->setValue(qBound(12, m_style.fontSize, 72));
    m_sizeLabel->setText(QString::number(m_style.fontSize) + QStringLiteral("px"));
    m_boldBtn->setChecked(m_style.bold);
    m_italicBtn->setChecked(m_style.italic);
    m_textHex->setText(colorHex(m_style.textColor));
    m_textSwatch->setStyleSheet(QStringLiteral("background:%1; border:1px solid #B9A8D8; border-radius:6px;").arg(colorHex(m_style.textColor)));
    m_outlineHex->setText(colorHex(m_style.outlineColor));
    m_outlineSwatch->setStyleSheet(QStringLiteral("background:%1; border:1px solid #B9A8D8; border-radius:6px;").arg(colorHex(m_style.outlineColor)));
    m_outlineSlider->setValue(qBound(0, m_style.outlineWidth, 8));
    m_outlineLabel->setText(QString::number(m_style.outlineWidth));
    m_shadowSlider->setValue(qBound(0, m_style.shadowWidth, 10));
    m_shadowLabel->setText(QString::number(m_style.shadowWidth));
    m_shadowHex->setText(colorHex(m_style.shadowColor));
    m_shadowSwatch->setStyleSheet(QStringLiteral("background:%1; border:1px solid #B9A8D8; border-radius:6px;").arg(colorHex(m_style.shadowColor)));
    m_shadowAlphaSlider->setValue(qBound(0, m_style.shadowOpacity, 100));
    m_shadowAlphaLabel->setText(QString::number(m_style.shadowOpacity) + QStringLiteral("%"));
    m_outlineCard->setChecked(!m_style.showBg);
    m_bgCard->setChecked(m_style.showBg);
    // Position grid: a free (dragged) position clears the highlight instead
    // of snapping it to the nearest cell.
    int best = -1;
    if (!m_freePosition) {
        if (m_style.posY >= 0.0) {
            const int col = m_style.posX < 0.375 ? 0 : (m_style.posX < 0.625 ? 1 : 2);
            const int row = m_style.posY < 0.325 ? 0 : (m_style.posY < 0.675 ? 1 : 2);
            best = row * 3 + col;
        } else {
            best = m_style.position == 1 ? 1 : (m_style.position == 2 ? 4 : 7);
        }
    }
    for (int i = 0; i < m_posBtns.size(); ++i) m_posBtns[i]->setChecked(i == best);
    // Preset highlight: exact match on key fields
    const auto presets = builtinPresets();
    for (int i = 0; i < m_presetBtns.size() && i < presets.size(); ++i) {
        const SubtitleStyle &p = presets.at(i);
        const bool match = (p.fontSize == m_style.fontSize && p.bold == m_style.bold
            && p.textColor.rgb() == m_style.textColor.rgb()
            && p.outlineWidth == m_style.outlineWidth && p.showBg == m_style.showBg);
        m_presetBtns[i]->setChecked(match);
    }
    if (m_preview) m_preview->setStyle(m_style);
    m_updating = false;
}

void SubtitleStyleCard::pickTextColor() {
    const QColor c = QColorDialog::getColor(m_style.textColor, this, tr("字體顏色"));
    if (!c.isValid()) return;
    m_style.textColor = c;
    syncUi();
    emitChanged();
}

void SubtitleStyleCard::pickOutlineColor() {
    const QColor c = QColorDialog::getColor(m_style.outlineColor, this, tr("邊框顏色"));
    if (!c.isValid()) return;
    m_style.outlineColor = c;
    syncUi();
    emitChanged();
}

void SubtitleStyleCard::pickShadowColor() {
    QColor base = m_style.shadowColor;
    base.setAlpha(255);
    const QColor c = QColorDialog::getColor(base, this, tr("陰影顏色"));
    if (!c.isValid()) return;
    m_style.shadowColor = c;
    syncUi();
    emitChanged();
}

void SubtitleStyleCard::saveCustomStyle() {
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("存為我的樣式"), tr("樣式名稱："), QLineEdit::Normal, QString(), &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    QSettings *s = settings();
    if (!s->isWritable()) {
        QMessageBox::warning(this, tr("存為我的樣式"), tr("無法寫入設定檔，請檢查權限。"));
        return;
    }

    const QString key = settingKey();
    const QString trimmed = name.trimmed();

    // Read the whole custom-style library first so we can append or replace
    // without losing the other entries. QSettings arrays do not support
    // partial updates safely when size is inferred from the highest index.
    QVector<QVariantMap> styles;
    const int n = s->beginReadArray(key);
    styles.reserve(n);
    for (int i = 0; i < n; ++i) {
        s->setArrayIndex(i);
        QVariantMap m;
        for (const QString &k : s->allKeys()) m[k] = s->value(k);
        styles.append(m);
    }
    s->endArray();

    int replaceIdx = -1;
    for (int i = 0; i < styles.size(); ++i) {
        if (styles[i].value(QStringLiteral("name")).toString() == trimmed) {
            replaceIdx = i;
            break;
        }
    }

    if (replaceIdx >= 0)
        styles[replaceIdx] = styleToMap(m_style, trimmed);
    else
        styles.append(styleToMap(m_style, trimmed));

    s->beginWriteArray(key, styles.size());
    for (int i = 0; i < styles.size(); ++i) {
        s->setArrayIndex(i);
        const QVariantMap &m = styles.at(i);
        for (auto it = m.begin(); it != m.end(); ++it)
            s->setValue(it.key(), it.value());
    }
    s->endArray();
    s->sync();

    if (s->status() != QSettings::NoError) {
        QMessageBox::warning(this, tr("存為我的樣式"), tr("儲存樣式時發生錯誤。"));
    } else {
        refreshCustomList();
    }
}

void SubtitleStyleCard::refreshCustomList() {
    if (!m_mineGrid) return;

    // Clear existing custom-style cards.
    for (auto *b : m_customBtns) {
        m_mineGrid->removeWidget(b);
        delete b;
    }
    m_customBtns.clear();
    m_customThumbs.clear();
    m_customNameLabels.clear();
    m_customTagLabels.clear();

    QSettings *s = settings();
    QVector<QPair<QString, SubtitleStyle>> styles;
    const int n = s->beginReadArray(settingKey());
    for (int i = 0; i < n; ++i) {
        s->setArrayIndex(i);
        const QString name = s->value(QStringLiteral("name")).toString();
        if (name.isEmpty()) continue;
        QVariantMap m;
        for (const QString &k : s->allKeys()) m[k] = s->value(k);
        styles.append(qMakePair(name, styleFromMap(m, SubtitleStyle())));
    }
    s->endArray();

    const QString tag = tr("我的樣式");
    for (int i = 0; i < styles.size(); ++i) {
        const QString &name = styles[i].first;
        const SubtitleStyle &st = styles[i].second;
        auto *b = new QPushButton(m_mineGridContainer);
        b->setObjectName(QStringLiteral("presetCard"));
        b->setCursor(Qt::PointingHandCursor);
        b->setProperty("styleName", name);
        auto *card = new QVBoxLayout(b);
        card->setContentsMargins(10, 10, 10, 10);
        card->setSpacing(6);
        auto *thumb = new StylePreviewWidget(b);
        // Gallery thumbnail: render the saved style without bilingual line.
        SubtitleStyle th = st;
        th.bilingualEnabled = false;
        thumb->setStyle(th);
        thumb->setSampleText(tr("字幕 Aa"), {});
        thumb->setThumbnailMode(true);
        thumb->setMinimumHeight(96);
        thumb->setMaximumHeight(96);
        thumb->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        card->addWidget(thumb);
        m_customThumbs << thumb;
        auto *nameLabel = new QLabel(name, b);
        nameLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        nameLabel->setWordWrap(false);
        nameLabel->setMinimumHeight(20);
        card->addWidget(nameLabel);
        m_customNameLabels << nameLabel;
        auto *tagLabel = new QLabel(tag, b);
        tagLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        tagLabel->setWordWrap(false);
        tagLabel->setMinimumHeight(16);
        tagLabel->setStyleSheet(QStringLiteral("color:#8E86A0; font-size:8pt; background:transparent;"));
        card->addWidget(tagLabel);
        m_customTagLabels << tagLabel;
        // QPushButton with empty text collapses without an explicit minimum
        // height covering the thumbnail + labels + margins + spacing.
        b->setMinimumHeight(164);
        connect(b, &QPushButton::clicked, this, [this, name] { applyCustomStyle(name); });
        m_customBtns << b;
        m_mineGrid->addWidget(b, i / 2, i % 2);
    }
    updateLibraryHeight();
}

void SubtitleStyleCard::applyCustomStyle(const QString &name) {
    if (name.isEmpty()) return;
    QSettings *s = settings();
    const int n = s->beginReadArray(settingKey());
    for (int i = 0; i < n; ++i) {
        s->setArrayIndex(i);
        if (s->value(QStringLiteral("name")).toString() == name) {
            QVariantMap m;
            for (const QString &k : s->allKeys()) m[k] = s->value(k);
            m_style = styleFromMap(m, m_style);
            m_freePosition = (m_style.posY >= 0.0);
            syncUi();
            emitChanged();
            break;
        }
    }
    s->endArray();
}

void SubtitleStyleCard::updateLibraryHeight() {
    if (!m_libStack) return;
    QWidget *current = m_libStack->currentWidget();
    if (!current) return;

    int desiredHeight = 0;
    if (current == m_minePage) {
        const int count = m_customBtns.size();
        const int rows = (count + 1) / 2;
        const int gridSpacing = m_mineGrid ? m_mineGrid->spacing() : 8;
        const int gridHeight = rows * 164 + qMax(0, rows - 1) * gridSpacing;
        const int hintHeight = m_mineHint ? m_mineHint->sizeHint().height() : 0;
        const int layoutSpacing = current->layout() ? current->layout()->spacing() : 6;
        desiredHeight = hintHeight + layoutSpacing + gridHeight;
    } else {
        const int rows = 3;
        const int gridSpacing = m_presetGrid ? m_presetGrid->spacing() : 8;
        desiredHeight = rows * 164 + qMax(0, rows - 1) * gridSpacing;
    }

    m_libStack->setMaximumHeight(desiredHeight);
}
