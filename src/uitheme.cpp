#include "uitheme.h"
#include <QApplication>
#include <QPalette>
#include <QHash>
#include <QFile>
#include <QTextStream>

namespace UiTheme {
namespace {
    struct Tokens {
        QColor windowBg, topBarBg, panelBg, panelHover, inputBg, timelineBg;
        QColor border, borderSoft, inputBorder;
        QColor accent, accentHover, accentPressed;
        QColor pillBg, pillBorder;
        QColor navActiveBg, navActiveBorder, navActiveText;
        QColor text, textSecond, textMuted;
        QColor danger, dangerBg, dangerBgHover, dangerText, dangerBorder;
        QColor success, successBorder;
        QColor warning;
        QColor wave, wavePlayed, grid;
        QColor menuBg, menuItem, menuItemSelectedBg, menuItemSelectedText;
        QColor tooltipBg, tooltipText;
        QColor scrollHandle, scrollHandleHover;
        QColor popoverBg, popoverText;
    };

    Tokens makeLight() {
        Tokens t;
        // Yumu pastel redesign (see mockups): pink header, lavender cards,
        // purple accent, mint preview pills.
        t.windowBg   = "#F7F4FA"; t.topBarBg  = "#E8B4DC";
        t.panelBg    = "#ECE6F2"; t.panelHover = "#E0D6EE";
        t.inputBg    = "#FFFFFF"; t.timelineBg = "#ECE6F2";
        t.border     = "#D9CFE8"; t.borderSoft = "#E6DDF2"; t.inputBorder = "#C9BCE0";
        t.accent     = "#7B61B8"; t.accentHover = "#6A4FA8"; t.accentPressed = "#5A4196";
        t.pillBg       = "#DDE9E4"; t.pillBorder = "#B9CFC4";
        t.navActiveBg = "#7B61B8"; t.navActiveBorder = "#7B61B8"; t.navActiveText = "#FFFFFF";
        t.text       = "#2A2433"; t.textSecond = "#5A5266"; t.textMuted = "#8E86A0";
        t.danger     = "#C0392B"; t.dangerBg = "#FDF0EE"; t.dangerBgHover = "#FCE4E0";
        t.dangerText = "#8B1A1A"; t.dangerBorder = "#F5C6C0";
        t.success    = "#2E7D32"; t.successBorder = "#C8E6C9";
        t.warning    = "#E8A838";
        t.wave       = "#7B61B8"; t.wavePlayed = "#7B61B8"; t.grid = "#D9CFE8";
        t.menuBg     = "#F4EFF9"; t.menuItem = "#2A2433";
        t.menuItemSelectedBg = "#7B61B8"; t.menuItemSelectedText = "#FFFFFF";
        t.tooltipBg  = "#2A2433"; t.tooltipText = "#FFFFFF";
        t.scrollHandle = "#C9BCE0"; t.scrollHandleHover = "#7B61B8";
        t.popoverBg  = QColor(43, 36, 51, 242); t.popoverText = "#FFFFFF";
        return t;
    }

    Tokens makeDark() {
        Tokens t;
        // Dark-purple sibling of the pastel light theme (same design family).
        t.windowBg   = "#211C2E"; t.topBarBg  = "#372A55";
        t.panelBg    = "#2C2540"; t.panelHover = "#38304F";
        t.inputBg    = "#241F33"; t.timelineBg = "#241F33";
        t.border     = "#453A63"; t.borderSoft = "#372E52"; t.inputBorder = "#54487A";
        t.accent     = "#A78BFA"; t.accentHover = "#BBA6FB"; t.accentPressed = "#9370F5";
        t.pillBg     = "#3A3358"; t.pillBorder = "#54487A";
        t.navActiveBg = "#A78BFA"; t.navActiveBorder = "#A78BFA"; t.navActiveText = "#221D33";
        t.text       = "#EDE8F7"; t.textSecond = "#B6AACC"; t.textMuted = "#847A9E";
        t.danger     = "#F08A80"; t.dangerBg = "#3A2320"; t.dangerBgHover = "#4A2C28";
        t.dangerText = "#FFCDD2"; t.dangerBorder = "#6B3A36";
        t.success    = "#81C784"; t.successBorder = "#2E4A30";
        t.warning    = "#FFD54F";
        t.wave       = "#A78BFA"; t.wavePlayed = "#C4B5FD"; t.grid = "#453A63";
        t.menuBg     = "#2C2540"; t.menuItem = "#EDE8F7";
        t.menuItemSelectedBg = "#A78BFA"; t.menuItemSelectedText = "#221D33";
        t.tooltipBg  = "#EDE8F7"; t.tooltipText = "#221D33";
        t.scrollHandle = "#54487A"; t.scrollHandleHover = "#A78BFA";
        t.popoverBg  = QColor(34, 29, 51, 242); t.popoverText = "#EDE8F7";
        return t;
    }

    Mode g_mode = Mode::Dark;
    const Tokens &tok() {
        static const Tokens dark = makeDark();
        static const Tokens light = makeLight();
        return g_mode == Mode::Dark ? dark : light;
    }

    QString loadThemeTemplate() {
        QFile f(":/theme.qss");
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            return QString();
        QTextStream in(&f);
        in.setEncoding(QStringConverter::Utf8);
        return in.readAll();
    }

    QString interpolate(QString qss) {
        const Tokens &t = tok();
        const QHash<QString, QColor> map = {
            {"windowBg", t.windowBg}, {"topBarBg", t.topBarBg},
            {"panelBg", t.panelBg}, {"panelHover", t.panelHover},
            {"inputBg", t.inputBg}, {"timelineBg", t.timelineBg},
            {"border", t.border}, {"borderSoft", t.borderSoft},
            {"inputBorder", t.inputBorder},
            {"accent", t.accent}, {"accentHover", t.accentHover},
            {"accentPressed", t.accentPressed},
            {"pillBg", t.pillBg}, {"pillBorder", t.pillBorder},
            {"navActiveBg", t.navActiveBg}, {"navActiveBorder", t.navActiveBorder},
            {"navActiveText", t.navActiveText},
            {"text", t.text}, {"textSecond", t.textSecond}, {"textMuted", t.textMuted},
            {"danger", t.danger}, {"dangerBg", t.dangerBg},
            {"dangerBgHover", t.dangerBgHover}, {"dangerText", t.dangerText},
            {"dangerBorder", t.dangerBorder},
            {"success", t.success}, {"successBorder", t.successBorder},
            {"warning", t.warning},
            {"menuBg", t.menuBg}, {"menuItem", t.menuItem},
            {"menuItemSelectedBg", t.menuItemSelectedBg},
            {"menuItemSelectedText", t.menuItemSelectedText},
            {"tooltipBg", t.tooltipBg}, {"tooltipText", t.tooltipText},
            {"scrollHandle", t.scrollHandle}, {"scrollHandleHover", t.scrollHandleHover}
        };
        for (auto it = map.constBegin(); it != map.constEnd(); ++it)
            qss.replace(QStringLiteral("%") + it.key() + QStringLiteral("%"), it.value().name(QColor::HexRgb));
        return qss;
    }
}
void applyTheme(QApplication *app, Mode m) {
    g_mode = m;
    if (!app) return;
    const Tokens &t = tok();
    QPalette pal;
    pal.setColor(QPalette::Window, t.windowBg);
    pal.setColor(QPalette::WindowText, t.text);
    pal.setColor(QPalette::Base, t.inputBg);
    pal.setColor(QPalette::AlternateBase, t.panelBg);
    pal.setColor(QPalette::ToolTipBase, t.tooltipBg);
    pal.setColor(QPalette::ToolTipText, t.tooltipText);
    pal.setColor(QPalette::Text, t.text);
    pal.setColor(QPalette::Button, t.panelHover);
    pal.setColor(QPalette::ButtonText, t.text);
    pal.setColor(QPalette::Highlight, t.accent);
    pal.setColor(QPalette::HighlightedText, g_mode == Mode::Dark ? QColor(34,29,51) : QColor(255,255,255));
    pal.setColor(QPalette::Disabled, QPalette::Text, t.textMuted);
    app->setPalette(pal);
    app->setStyleSheet(styleSheet());
}
void applyTheme(QApplication *app, const QString &name) {
    applyTheme(app, name.compare("light", Qt::CaseInsensitive) == 0 ? Mode::Light : Mode::Dark);
}
Mode mode() { return g_mode; }
bool isDark() { return g_mode == Mode::Dark; }
QColor windowBg()  { return tok().windowBg; }
QColor topBarBg()  { return tok().topBarBg; }
QColor panelBg()   { return tok().panelBg; }
QColor panelHover(){ return tok().panelHover; }
QColor inputBg()   { return tok().inputBg; }
QColor timelineBg(){ return tok().timelineBg; }
QColor border()      { return tok().border; }
QColor borderSoft()  { return tok().borderSoft; }
QColor inputBorder() { return tok().inputBorder; }
QColor accent()         { return tok().accent; }
QColor accentHover()    { return tok().accentHover; }
QColor accentPressed()  { return tok().accentPressed; }
QColor pillBg()         { return tok().pillBg; }
QColor pillBorder()     { return tok().pillBorder; }
QColor navActiveBg()    { return tok().navActiveBg; }
QColor navActiveBorder(){ return tok().navActiveBorder; }
QColor navActiveText()  { return tok().navActiveText; }
QColor text()       { return tok().text; }
QColor textSecond() { return tok().textSecond; }
QColor textMuted()  { return tok().textMuted; }
QColor danger()        { return tok().danger; }
QColor dangerBg()      { return tok().dangerBg; }
QColor dangerBgHover() { return tok().dangerBgHover; }
QColor dangerText()    { return tok().dangerText; }
QColor dangerBorder()  { return tok().dangerBorder; }
QColor success()       { return tok().success; }
QColor successBorder() { return tok().successBorder; }
QColor warning()       { return tok().warning; }
QColor wave()       { return tok().wave; }
QColor wavePlayed() { return tok().wavePlayed; }
QColor grid()       { return tok().grid; }
QColor menuBg()               { return tok().menuBg; }
QColor menuItem()             { return tok().menuItem; }
QColor menuItemSelectedBg()   { return tok().menuItemSelectedBg; }
QColor menuItemSelectedText() { return tok().menuItemSelectedText; }
QColor tooltipBg()            { return tok().tooltipBg; }
QColor tooltipText()          { return tok().tooltipText; }
QColor scrollHandle()         { return tok().scrollHandle; }
QColor scrollHandleHover()    { return tok().scrollHandleHover; }
QColor popoverBg()            { return tok().popoverBg; }
QColor popoverText()          { return tok().popoverText; }
QString styleSheet() {
    QString tmpl = loadThemeTemplate();
    if (tmpl.isEmpty())
        return QString(); // fallback: empty stylesheet
    return interpolate(tmpl);
}
} // namespace UiTheme
