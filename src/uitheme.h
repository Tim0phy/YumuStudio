#pragma once
#include <QString>
#include <QColor>

class QApplication;

// Runtime-switchable design tokens -- Library / Archive edition.
// Warm ivory paper, charcoal ink, oak borders, amber accent.
// Reference: digital library (Computer Science collection) -- rounded cards,
// pill search, soft cream sidebar, editorial list + grid. Every widget colour
// must come from here so light (Ivory) and dark (Ink) stay consistent.
namespace UiTheme {
    enum class Mode { Dark, Light };

    void applyTheme(QApplication *app, const QString &name);
    void applyTheme(QApplication *app, Mode mode);

    Mode   mode();
    bool   isDark();
    QString styleSheet();

    QColor windowBg();      QColor topBarBg();
    QColor panelBg();       QColor panelHover();
    QColor inputBg();       QColor timelineBg();

    QColor border();        QColor borderSoft();    QColor inputBorder();

    QColor accent();        QColor accentHover();   QColor accentPressed();
    QColor pillBg();        QColor pillBorder();
    QColor navActiveBg();   QColor navActiveBorder(); QColor navActiveText();

    QColor text();          QColor textSecond();    QColor textMuted();

    QColor danger();        QColor dangerBg();      QColor dangerBgHover();
    QColor dangerText();    QColor dangerBorder();
    QColor success();       QColor successBorder();
    QColor warning();

    QColor wave();          QColor wavePlayed();    QColor grid();

    QColor menuBg();        QColor menuItem();
    QColor menuItemSelectedBg(); QColor menuItemSelectedText();
    QColor tooltipBg();     QColor tooltipText();
    QColor scrollHandle();  QColor scrollHandleHover();
    QColor popoverBg();     QColor popoverText();

    inline QString hex(const QColor &c) { return c.name(QColor::HexRgb); }
}
