#include "ui/main_window.h"

#include <QApplication>
#include <QFont>
#include <QFontDatabase>
#include <QIcon>
#include <QPalette>
#include <QTimer>
#include <QSystemTrayIcon>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("Ausyn"));
    QCoreApplication::setOrganizationName(QStringLiteral("Ausyn"));
    QCoreApplication::setApplicationVersion(QStringLiteral(AUSYN_VERSION));
    app.setWindowIcon(QIcon(QStringLiteral(":/brand/ausyn.ico")));

    if (app.arguments().contains(QStringLiteral("--ui-check")) || app.arguments().contains(QStringLiteral("--collector-check"))) {
        // Qt's offscreen platform has no Windows font discovery. Use the installed
        // system faces for faithful developer previews without bundling them.
        QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/segoeui.ttf"));
        QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/seguisb.ttf"));
        QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/seguisym.ttf"));
    }

    QFont appFont = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    appFont.setFamilies({QStringLiteral("Segoe UI Variable"), QStringLiteral("Segoe UI")});
    appFont.setPointSize(10);
    app.setFont(appFont);

    const QStringList args = app.arguments();
    if (args.contains(QStringLiteral("--priority-check-child"))) {
        // Disposable verification worker: no window, collectors or user settings.
        QTimer::singleShot(60'000, &app, &QCoreApplication::quit);
        return app.exec();
    }
    const int checkIndex = args.indexOf(QStringLiteral("--ui-check"));
    const int collectorIndex = args.indexOf(QStringLiteral("--collector-check"));
    const bool uiCheck = checkIndex >= 0 || collectorIndex >= 0;
    Ausyn::MainWindow window(nullptr, uiCheck);
    if (uiCheck) window.setAttribute(Qt::WA_DontShowOnScreen);
    if (uiCheck || !window.shouldStartHidden() || !QSystemTrayIcon::isSystemTrayAvailable()) window.show();
    if (checkIndex >= 0) {
        const QString output = checkIndex + 1 < args.size() ? args.at(checkIndex + 1) : QString();
        QTimer::singleShot(0, &app, [&app, &window, output] {
            app.exit(output.isEmpty() ? 2 : window.runUiCheck(output) ? 0 : 1);
        });
    } else if (collectorIndex >= 0) {
        const QString output = collectorIndex + 1 < args.size() ? args.at(collectorIndex + 1) : QString();
        QTimer::singleShot(0, &app, [&app, &window, output] {
            if (output.isEmpty()) app.exit(2);
            else window.startCollectorCheck(output);
        });
    }
    return app.exec();
}
