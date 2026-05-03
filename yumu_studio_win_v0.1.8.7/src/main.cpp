#include <QApplication>
#include <QStyleFactory>
#include "mainwindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("Yumu Studio");
    app.setApplicationVersion("8.7.0");
    app.setOrganizationName("YumuStudio");

    app.setStyle(QStyleFactory::create("Fusion"));

    QPalette dark;
    dark.setColor(QPalette::Window,          QColor(40,40,48));
    dark.setColor(QPalette::WindowText,      QColor(220,220,225));
    dark.setColor(QPalette::Base,            QColor(28,28,36));
    dark.setColor(QPalette::AlternateBase,   QColor(34,34,44));
    dark.setColor(QPalette::ToolTipBase,     Qt::white);
    dark.setColor(QPalette::ToolTipText,     Qt::white);
    dark.setColor(QPalette::Text,            QColor(220,220,225));
    dark.setColor(QPalette::Button,          QColor(52,52,64));
    dark.setColor(QPalette::ButtonText,      QColor(220,220,225));
    dark.setColor(QPalette::Highlight,       QColor(60,140,220));
    dark.setColor(QPalette::HighlightedText, Qt::white);
    app.setPalette(dark);

    MainWindow w;
    w.show();
    return app.exec();
}
