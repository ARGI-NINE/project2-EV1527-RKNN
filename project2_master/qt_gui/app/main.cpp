#include "app_options.h"
#include "app_palette.h"
#include "main_window.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QTextStream>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("rf_dashboard_qt5"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Qt5 C++ Dashboard for RF Gateway + Vision monitor"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption rfInputOption(
        QStringLiteral("rf-input"),
        QStringLiteral("RF input path passed to rf_gateway (master only supports /dev/ttyS9)."),
        QStringLiteral("path")
    );
    parser.addOption(rfInputOption);

    parser.process(app);

    dashboard::AppOptions options;
    options.rfInput = parser.value(rfInputOption).trimmed();
    if (options.rfInput.isEmpty()) {
        options.rfInput = dashboard::defaultRFInputPath();
    }
    if (options.rfInput != dashboard::defaultRFInputPath()) {
        QTextStream(stderr)
            << "Invalid --rf-input: " << options.rfInput
            << " (master only supports " << dashboard::defaultRFInputPath() << ")\n";
        return 1;
    }

    dashboard::applyDarkPalette(app);

    dashboard::MainWindow window(options);
    window.show();
    return app.exec();
}
