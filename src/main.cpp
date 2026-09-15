#include <QApplication>
#include <QIcon>
#include <QStyleFactory>
#include "appconfig.h"
#include "mainwindow.h"
#include "uilanguage.h"
#include "uitheme.h"
#include "yumutooltip.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setWindowIcon(QIcon(":/app.ico"));
    app.setApplicationName("Yumu Studio");
    app.setApplicationVersion("2.0.0");
    app.setOrganizationName("YumuStudio");

    AppConfig::instance().load();
    // Apply hardware decode preference globally before any QMediaPlayer is
    // created. HW ON: d3d11va chain; HW OFF: unset -> Qt default (SW + fallback)
    {
        const bool hw = AppConfig::instance().preview.hardwarePreview;
        if (hw) {
            qputenv("QT_FFMPEG_DECODING_HW_DEVICE_TYPES", "d3d11va,d3d12va,dxva2");
            qputenv("QT_DISABLE_HW_TEXTURES_CONVERSION", "0");
        } else {
            qunsetenv("QT_FFMPEG_DECODING_HW_DEVICE_TYPES");
            qunsetenv("QT_DISABLE_HW_TEXTURES_CONVERSION");
        }
        qputenv("QT_FFMPEG_HW_ALLOW_PROFILE_MISMATCH", "0");
    }
    UiLanguage::installTranslator(&app);

    app.setStyle(QStyleFactory::create("Fusion"));

    // Dark / light theme from persisted settings (Settings → Appearance).
    UiTheme::applyTheme(&app, AppConfig::instance().system.theme);
    // Self-drawn rounded tooltips replace the native rectangular ones.
    YumuToolTipFilter::installOn(&app);

    MainWindow w;
    w.show();
    return app.exec();
}
