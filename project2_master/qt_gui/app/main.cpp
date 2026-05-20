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

    const QString defaultRfInput = dashboard::defaultRFInputPath();

    QCommandLineOption rfInputOption(
        QStringLiteral("rf-input"),
        QStringLiteral("RF input path passed to rf_gateway (default: %1; master only supports this path).")
            .arg(defaultRfInput),
        QStringLiteral("path")
    );
    const QString defaultVisionDevice = dashboard::defaultVisionDevicePath();
    QCommandLineOption visionDeviceOption(
        QStringLiteral("vision-device"),
        QStringLiteral("Board-side vision input passed to the local runtime "
                       "(default: %1; accepts /dev/video* or a readable local video file).")
            .arg(defaultVisionDevice),
        QStringLiteral("input"),
        defaultVisionDevice
    );
    parser.addOption(rfInputOption);
    parser.addOption(visionDeviceOption);

    parser.process(app);

    dashboard::AppOptions options;
    options.rfInput = parser.value(rfInputOption).trimmed();
    if (options.rfInput.isEmpty()) {
        options.rfInput = defaultRfInput;
    }
    options.visionDevice = parser.value(visionDeviceOption).trimmed();
    if (options.visionDevice.isEmpty()) {
        options.visionDevice = defaultVisionDevice;
    }
    if (options.rfInput != defaultRfInput) {
        QTextStream(stderr)
            << "Invalid --rf-input: " << options.rfInput
            << " (master only supports " << defaultRfInput << ")\n";
        return 1;
    }
    if (!dashboard::isAllowedVisionInputPath(options.visionDevice)) {
        QTextStream(stderr)
            << "Invalid --vision-device: " << options.visionDevice
            << " (master supports local /dev/video* devices or readable local video files)\n";
        return 1;
    }

    dashboard::applyDarkPalette(app);

    dashboard::MainWindow window(options);
    window.show();
    return app.exec();
}
