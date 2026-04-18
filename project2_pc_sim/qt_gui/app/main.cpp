#include "app_options.h"
#include "app_palette.h"
#include "main_window.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("rf_dashboard_qt5"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Qt5 C++ Dashboard for RF Gateway + Vision monitor"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption gatewayOption(
        QStringLiteral("gateway"),
        QStringLiteral("Path to rf_gateway executable."),
        QStringLiteral("path")
    );
    QCommandLineOption wavInputOption(
        QStringLiteral("wav-input"),
        QStringLiteral("Real WAV input path for fixed full-duration RF playback."),
        QStringLiteral("path")
    );
    QCommandLineOption wavLoopOption(
        QStringLiteral("wav-loop"),
        QStringLiteral("Loop WAV timeline playback after reaching end.")
    );
    QCommandLineOption wavSpeedOption(
        QStringLiteral("wav-speed"),
        QStringLiteral("WAV timeline playback speed multiplier (1.0 means realtime)."),
        QStringLiteral("factor"),
        QStringLiteral("1.0")
    );
    QCommandLineOption videoInputOption(
        QStringLiteral("video-input"),
        QStringLiteral("Video file path for real vision display (for example test.mp4)."),
        QStringLiteral("path")
    );
    QCommandLineOption pythonBinOption(
        QStringLiteral("python-bin"),
        QStringLiteral("Python interpreter path passed to rf_gateway."),
        QStringLiteral("path")
    );
    QCommandLineOption visionHostOption(
        QStringLiteral("vision-host"),
        QStringLiteral("WSL vision bridge host (optional, for RKNN telemetry)."),
        QStringLiteral("host"),
        QStringLiteral("127.0.0.1")
    );
    QCommandLineOption visionPortOption(
        QStringLiteral("vision-port"),
        QStringLiteral("WSL vision bridge TCP port (0 disables bridge)."),
        QStringLiteral("port"),
        QStringLiteral("0")
    );
    parser.addOption(gatewayOption);
    parser.addOption(wavInputOption);
    parser.addOption(wavLoopOption);
    parser.addOption(wavSpeedOption);
    parser.addOption(videoInputOption);
    parser.addOption(pythonBinOption);
    parser.addOption(visionHostOption);
    parser.addOption(visionPortOption);

    parser.process(app);

    dashboard::AppOptions options;
    options.gatewayPath = parser.value(gatewayOption).trimmed();
    options.wavPath = parser.value(wavInputOption).trimmed();
    options.wavLoop = parser.isSet(wavLoopOption);
    options.wavSpeed = parser.value(wavSpeedOption).toDouble();
    options.videoPath = parser.value(videoInputOption).trimmed();
    options.pythonBin = parser.value(pythonBinOption).trimmed();
    options.visionHost = parser.value(visionHostOption).trimmed();
    options.visionPort = parser.value(visionPortOption).toInt();

    dashboard::applyDarkPalette(app);

    dashboard::MainWindow window(options);
    window.show();
    return app.exec();
}
