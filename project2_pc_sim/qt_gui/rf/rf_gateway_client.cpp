#include "rf_gateway_client.h"

#include "rf_utils.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
#include <QRegularExpression>

namespace {

constexpr int kPrepTimeoutMs = 120000;
constexpr int kPrepTimeoutSeconds = kPrepTimeoutMs / 1000;
constexpr qint64 kMinFirstRfTimeoutMs = 15000;
constexpr qint64 kFirstRfTimeoutGraceMs = 5000;

QString normalizePathIfExists(const QString &path) {
    const QFileInfo info(path);
    if (info.exists() && info.isFile()) {
        return info.absoluteFilePath();
    }
    return QString();
}

qint64 extractTimingValueLongLong(const QString &line, const QString &key, qint64 fallback = -1) {
    static const QRegularExpression re(QStringLiteral("([A-Za-z0-9_]+)=([^\\s]+)"));
    QRegularExpressionMatchIterator it = re.globalMatch(line);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        if (match.captured(1) != key) {
            continue;
        }

        bool ok = false;
        const qint64 value = match.captured(2).toLongLong(&ok);
        return ok ? value : fallback;
    }
    return fallback;
}

}  // namespace

namespace dashboard {

RFGatewayClient::RFGatewayClient(DashboardBackend *backend, const AppOptions &options, QObject *context)
    : backend_(backend),
      options_(options),
      context_(context == nullptr ? QCoreApplication::instance() : context) {
    prepTimeoutTimer_.setSingleShot(true);
    QObject::connect(&prepTimeoutTimer_, &QTimer::timeout, context_, [this]() {
        onPrepTimeout();
    });

    firstRfTimer_.setSingleShot(true);
    QObject::connect(&firstRfTimer_, &QTimer::timeout, context_, [this]() {
        onFirstRfTimeout();
    });
}

RFGatewayClient::~RFGatewayClient() {
    stop();
}

void RFGatewayClient::start() {
    if (backend_ == nullptr) {
        return;
    }

    startGatewayProcess();
    if (
        gatewayProcess_.state() != QProcess::Running &&
        prepProcess_.state() != QProcess::Running &&
        lastStartError_.isEmpty()
    ) {
        backend_->updateSerialStatus(false);
        backend_->addLog(
            "ERROR",
            "RF",
            QStringLiteral("RF pipeline startup failed; check --gateway/--wav-input/--python-bin arguments")
        );
    }
}

void RFGatewayClient::stop() {
    stopGatewayProcess();

    if (backend_ != nullptr) {
        backend_->updateSerialStatus(false);
    }
}

void RFGatewayClient::startGatewayProcess() {
    if (backend_ == nullptr) {
        return;
    }
    if (gatewayProcess_.state() == QProcess::Running) {
        return;
    }

    rfScreenshotReadyLogged_ = false;
    lastStartError_.clear();
    firstRfTimeoutMs_ = kMinFirstRfTimeoutMs;

    auto failStart = [this](const QString &msg) {
        lastStartError_ = msg;
        backend_->addLog("ERROR", "RF", msg);
    };

    const QString gatewayPath = resolveGatewayPath();
    if (gatewayPath.isEmpty()) {
        failStart(QStringLiteral("rf_gateway executable not found; pass --gateway with a valid path"));
        return;
    }

    if (options_.wavPath.trimmed().isEmpty()) {
        failStart(QStringLiteral("--wav-input is required in fixed-chain mode"));
        return;
    }

    pendingGatewayPath_ = gatewayPath;
    if (!prepareRealtimeTimeline()) {
        return;
    }
}

bool RFGatewayClient::prepareRealtimeTimeline() {
    if (backend_ == nullptr) {
        return false;
    }

    auto failStart = [this](const QString &msg) {
        lastStartError_ = msg;
        backend_->addLog("ERROR", "RF", msg);
    };

    const QString wavPath = resolveWavInputPath();
    if (wavPath.isEmpty()) {
        failStart(QStringLiteral("--wav-input points to a missing file"));
        return false;
    }

    const QString wavToPulses = resolveWavToPulsesPath();
    if (wavToPulses.isEmpty()) {
        failStart(QStringLiteral("python/wav_to_pulses.py not found"));
        return false;
    }

    const QString pythonBin = resolvePythonBin();
    if (pythonBin.isEmpty()) {
        failStart(QStringLiteral("Python interpreter not found; pass --python-bin"));
        return false;
    }

    const QString runtimeDirPath = QDir(QDir::tempPath()).filePath(QStringLiteral("project2_pc_sim_runtime"));
    QDir runtimeDir(runtimeDirPath);
    if (!runtimeDir.exists() && !QDir().mkpath(runtimeDirPath)) {
        failStart(QStringLiteral("Failed to create realtime replay temp directory"));
        return false;
    }

    runtimePulseTxtPath_ = runtimeDir.filePath(QStringLiteral("pulse_runtime.txt"));
    runtimePulseJsonPath_ = runtimeDir.filePath(QStringLiteral("pulse_runtime.json"));

    if (!prepConnected_) {
        QObject::connect(&prepProcess_, &QProcess::readyReadStandardOutput, context_, [this]() {
            onPrepStdout();
        });
        QObject::connect(&prepProcess_, &QProcess::readyReadStandardError, context_, [this]() {
            onPrepStderr();
        });
        QObject::connect(
            &prepProcess_,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            context_,
            [this](int exitCode, QProcess::ExitStatus exitStatus) {
                onPrepFinished(exitCode, exitStatus);
            }
        );
        prepConnected_ = true;
    }

    prepStdoutBuffer_.clear();
    prepStderrBuffer_.clear();
    prepProcess_.setProgram(pythonBin);
    const QStringList prepArgs = {
        wavToPulses,
        QStringLiteral("--wav"),
        wavPath,
        QStringLiteral("--mode"),
        QStringLiteral("timeline"),
        // Use a tolerant extraction profile for PC replay so RF events are preserved.
        QStringLiteral("--sync-us"),
        QStringLiteral("8000"),
        QStringLiteral("--min-frame-pulses"),
        QStringLiteral("50"),
        QStringLiteral("--smooth-window"),
        QStringLiteral("2"),
        QStringLiteral("--min-run-samples"),
        QStringLiteral("0"),
        QStringLiteral("--min-pulse-us"),
        QStringLiteral("80"),
        QStringLiteral("--max-pulse-us"),
        QStringLiteral("65535"),
        QStringLiteral("--hw-prefilter"),
        QStringLiteral("--fixed-frame-pulses"),
        QStringLiteral("50"),
        QStringLiteral("--fixed-pulses-tolerance"),
        QStringLiteral("0"),
        QStringLiteral("--require-first-low-longest"),
        QStringLiteral("--selector"),
        QStringLiteral("decode"),
        QStringLiteral("--decoder-min-frame-confidence"),
        QStringLiteral("0.20"),
        QStringLiteral("--decoder-min-occurrences"),
        QStringLiteral("1"),
        QStringLiteral("--decoder-min-burst-occurrences"),
        QStringLiteral("1"),
        QStringLiteral("--max-frames"),
        QStringLiteral("0"),
        QStringLiteral("--out-txt"),
        runtimePulseTxtPath_,
        QStringLiteral("--out-json"),
        runtimePulseJsonPath_
    };
    prepProcess_.setArguments(prepArgs);
    prepProcess_.setProcessChannelMode(QProcess::SeparateChannels);
    prepProcess_.start();

    if (!prepProcess_.waitForStarted(4000)) {
        failStart(
            QString("Failed to start WAV preprocessing: %1 (python=%2)")
                .arg(prepProcess_.errorString(), pythonBin)
        );
        return false;
    }

    prepStartedAtMs_ = QDateTime::currentMSecsSinceEpoch();
    prepTimeoutTimer_.start(kPrepTimeoutMs);
    backend_->addLog(
        "INFO",
        "RF",
        QString("WAV preprocessing (full duration): %1").arg(wavPath)
    );

    return true;
}

void RFGatewayClient::startGatewayWithRealtimeInput(const QString &gatewayPath) {
    if (backend_ == nullptr) {
        return;
    }

    auto failStart = [this](const QString &msg) {
        lastStartError_ = msg;
        backend_->addLog("ERROR", "RF", msg);
    };

    const QString replayScript = resolveTimelineReplayPath();
    if (replayScript.isEmpty()) {
        failStart(QStringLiteral("python/replay_pulse_timeline.py not found"));
        return;
    }

    const QString pythonBin = resolvePythonBin();
    if (pythonBin.isEmpty()) {
        failStart(QStringLiteral("Python interpreter not found; pass --python-bin"));
        return;
    }

    if (!gatewayConnected_) {
        QObject::connect(&gatewayProcess_, &QProcess::readyReadStandardOutput, context_, [this]() {
            onGatewayStdout();
        });
        QObject::connect(&gatewayProcess_, &QProcess::readyReadStandardError, context_, [this]() {
            onGatewayStderr();
        });
        QObject::connect(
            &gatewayProcess_,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            context_,
            [this](int exitCode, QProcess::ExitStatus exitStatus) {
                onGatewayFinished(exitCode, exitStatus);
            }
        );
        gatewayConnected_ = true;
    }

    if (!replayConnected_) {
        QObject::connect(&replayProcess_, &QProcess::readyReadStandardOutput, context_, [this]() {
            onReplayStdout();
        });
        QObject::connect(&replayProcess_, &QProcess::readyReadStandardError, context_, [this]() {
            onReplayStderr();
        });
        QObject::connect(
            &replayProcess_,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            context_,
            [this](int exitCode, QProcess::ExitStatus exitStatus) {
                onReplayFinished(exitCode, exitStatus);
            }
        );
        replayConnected_ = true;
    }

    QStringList gatewayArgs;
    gatewayArgs << QStringLiteral("--rf-input") << QStringLiteral("-");
    gatewayArgs << QStringLiteral("--stable-repeat") << QStringLiteral("1");
    gatewayArgs << QStringLiteral("--min-publish-confidence") << QStringLiteral("0.92");

    gatewayStdoutBuffer_.clear();
    gatewayStderrBuffer_.clear();
    replayStderrBuffer_.clear();
    replayWavSecByIdx_.clear();

    gatewayProcess_.setProgram(gatewayPath);
    gatewayProcess_.setArguments(gatewayArgs);
    gatewayProcess_.setProcessChannelMode(QProcess::SeparateChannels);

    replayProcess_.setProgram(pythonBin);
    QStringList replayArgs = {
        replayScript,
        QStringLiteral("--pulse-json"),
        runtimePulseJsonPath_,
        QStringLiteral("--speed"),
        QString::number(options_.wavSpeed <= 0.0 ? 1.0 : options_.wavSpeed, 'f', 3)
    };
    if (options_.wavLoop) {
        replayArgs << QStringLiteral("--loop");
    }
    replayProcess_.setArguments(replayArgs);
    replayProcess_.setProcessChannelMode(QProcess::SeparateChannels);

    gatewayProcess_.start();
    if (!gatewayProcess_.waitForStarted(4000)) {
        failStart(QString("rf_gateway startup failed: %1").arg(gatewayProcess_.errorString()));
        return;
    }

    replayProcess_.start();
    if (!replayProcess_.waitForStarted(4000)) {
        failStart(
            QString("Realtime WAV replay startup failed: %1 (python=%2)")
                .arg(replayProcess_.errorString(), pythonBin)
        );
        gatewayProcess_.terminate();
        (void)gatewayProcess_.waitForFinished(1000);
        return;
    }

    replayStartedAtMs_ = QDateTime::currentMSecsSinceEpoch();
    lastStartError_.clear();
    awaitingFirstRf_ = true;
    firstRfTimeoutMs_ = computeFirstRfTimeoutMs();
    firstRfTimer_.start(firstRfTimeoutMs_);

    backend_->updateSerialStatus(true, QStringLiteral("proc://rf_gateway/stdin"));
    backend_->addLog(
        "INFO",
        "RF",
        QString("Starting realtime WAV replay: frames=%1 speed=%2 loop=%3 first_event_timeout_ms=%4")
            .arg(timelineFrameCount_)
            .arg(QString::number(options_.wavSpeed <= 0.0 ? 1.0 : options_.wavSpeed, 'f', 3))
            .arg(options_.wavLoop ? QStringLiteral("on") : QStringLiteral("off"))
            .arg(firstRfTimeoutMs_)
    );
    backend_->addLog(
        "INFO",
        "RF",
        QString("Starting rf_gateway: %1 %2").arg(gatewayPath, gatewayArgs.join(' '))
    );
}

void RFGatewayClient::stopGatewayProcess() {
    prepTimeoutTimer_.stop();
    firstRfTimer_.stop();
    awaitingFirstRf_ = false;
    rfScreenshotReadyLogged_ = false;
    pendingGatewayPath_.clear();
    prepStartedAtMs_ = -1;
    replayStartedAtMs_ = -1;
    firstRfTimeoutMs_ = kMinFirstRfTimeoutMs;

    if (prepProcess_.state() != QProcess::NotRunning) {
        prepProcess_.terminate();
        if (!prepProcess_.waitForFinished(2000)) {
            prepProcess_.kill();
            (void)prepProcess_.waitForFinished(1000);
        }
    }

    if (replayProcess_.state() != QProcess::NotRunning) {
        replayProcess_.terminate();
        if (!replayProcess_.waitForFinished(2000)) {
            replayProcess_.kill();
            (void)replayProcess_.waitForFinished(1000);
        }
    }

    if (gatewayProcess_.state() != QProcess::NotRunning) {
        gatewayProcess_.terminate();
        if (!gatewayProcess_.waitForFinished(2000)) {
            gatewayProcess_.kill();
            (void)gatewayProcess_.waitForFinished(1000);
        }
    }

    gatewayStdoutBuffer_.clear();
    gatewayStderrBuffer_.clear();
    replayStderrBuffer_.clear();
    prepStdoutBuffer_.clear();
    prepStderrBuffer_.clear();
    replayWavSecByIdx_.clear();
}

void RFGatewayClient::onGatewayStdout() {
    const QByteArray chunk = gatewayProcess_.readAllStandardOutput();
    drainBuffer(&gatewayStdoutBuffer_, chunk, QStringLiteral("GATEWAY"));
}

void RFGatewayClient::onGatewayStderr() {
    const QByteArray chunk = gatewayProcess_.readAllStandardError();
    drainBuffer(&gatewayStderrBuffer_, chunk, QStringLiteral("GATEWAY_ERR"));
}

void RFGatewayClient::onGatewayFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    if (backend_ == nullptr) {
        return;
    }

    if (replayProcess_.state() != QProcess::NotRunning) {
        replayProcess_.terminate();
        if (!replayProcess_.waitForFinished(1000)) {
            replayProcess_.kill();
            (void)replayProcess_.waitForFinished(500);
        }
    }

    firstRfTimer_.stop();
    awaitingFirstRf_ = false;

    backend_->updateSerialStatus(false);
    backend_->addLog(
        "WARN",
        "RF",
        QString("rf_gateway exited: code=%1 status=%2")
            .arg(exitCode)
            .arg(exitStatus == QProcess::NormalExit ? QStringLiteral("normal") : QStringLiteral("crash"))
    );
}

void RFGatewayClient::onReplayStderr() {
    const QByteArray chunk = replayProcess_.readAllStandardError();
    drainBuffer(&replayStderrBuffer_, chunk, QStringLiteral("REPLAY_ERR"));
}

void RFGatewayClient::onReplayStdout() {
    const QByteArray chunk = replayProcess_.readAllStandardOutput();
    if (chunk.isEmpty() || gatewayProcess_.state() != QProcess::Running) {
        return;
    }
    qint64 totalWritten = 0;
    while (totalWritten < chunk.size()) {
        const qint64 wrote = gatewayProcess_.write(chunk.constData() + totalWritten, chunk.size() - totalWritten);
        if (wrote <= 0) {
            if (backend_ != nullptr) {
                backend_->addLog(
                    "WARN",
                    "RF",
                    QString("Failed to write replay data into gateway: wrote=%1 pending=%2")
                        .arg(wrote)
                        .arg(gatewayProcess_.bytesToWrite())
                );
            }
            if (!gatewayProcess_.waitForBytesWritten(1000)) {
                break;
            }
            continue;
        }
        totalWritten += wrote;
    }
}

void RFGatewayClient::onReplayFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    if (backend_ == nullptr) {
        return;
    }

    if (gatewayProcess_.state() == QProcess::Running) {
        gatewayProcess_.closeWriteChannel();
    }

    backend_->addLog(
        "INFO",
        "RF",
        QString("WAV replay process finished: code=%1 status=%2")
            .arg(exitCode)
            .arg(exitStatus == QProcess::NormalExit ? QStringLiteral("normal") : QStringLiteral("crash"))
    );
}

void RFGatewayClient::onPrepStdout() {
    if (backend_ == nullptr) {
        return;
    }
    const QByteArray chunk = prepProcess_.readAllStandardOutput();
    drainBuffer(&prepStdoutBuffer_, chunk, QStringLiteral("PREP"));
}

void RFGatewayClient::onPrepStderr() {
    if (backend_ == nullptr) {
        return;
    }
    const QByteArray chunk = prepProcess_.readAllStandardError();
    drainBuffer(&prepStderrBuffer_, chunk, QStringLiteral("PREP_ERR"));
}

void RFGatewayClient::onPrepFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    if (backend_ == nullptr) {
        return;
    }

    prepTimeoutTimer_.stop();
    if (prepStdoutBuffer_.contains('\n')) {
        drainBuffer(&prepStdoutBuffer_, QByteArray("\n"), QStringLiteral("PREP"));
    }
    if (prepStderrBuffer_.contains('\n')) {
        drainBuffer(&prepStderrBuffer_, QByteArray("\n"), QStringLiteral("PREP_ERR"));
    }

    auto failStart = [this](const QString &msg) {
        lastStartError_ = msg;
        backend_->addLog("ERROR", "RF", msg);
        backend_->updateSerialStatus(false);
        pendingGatewayPath_.clear();
    };

    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        failStart(QString("WAV preprocessing failed: code=%1").arg(exitCode));
        return;
    }

    QFile pulseJson(runtimePulseJsonPath_);
    if (!pulseJson.exists() || !pulseJson.open(QIODevice::ReadOnly)) {
        failStart(QStringLiteral("pulse_runtime.json was not generated"));
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(pulseJson.readAll(), &parseError);
    pulseJson.close();
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        failStart(QStringLiteral("Failed to parse pulse_runtime.json"));
        return;
    }

    const QJsonArray frames = doc.object().value(QStringLiteral("frames")).toArray();
    timelineFrameCount_ = frames.size();
    if (timelineFrameCount_ <= 0) {
        failStart(QStringLiteral("No replayable frames extracted from WAV"));
        return;
    }
    const qint64 prepFinishedAtMs = QDateTime::currentMSecsSinceEpoch();
    qint64 prepDurationMs = -1;
    if (prepStartedAtMs_ > 0 && prepFinishedAtMs >= prepStartedAtMs_) {
        prepDurationMs = prepFinishedAtMs - prepStartedAtMs_;
        const QString timingLine =
            QString("RF_TIMING stage=wav_preprocess duration_ms=%1 frames=%2")
                .arg(prepDurationMs)
                .arg(timelineFrameCount_);
        backend_->addLog(
            "INFO",
            "RF",
            timingLine
        );
    }
    backend_->addLog(
        "INFO",
        "RF",
        QString("WAV timeline prepared: frames=%1").arg(QString::number(timelineFrameCount_))
    );

    const QString gatewayPath = pendingGatewayPath_;
    pendingGatewayPath_.clear();
    if (gatewayPath.isEmpty()) {
        failStart(QStringLiteral("Internal state error: gateway path is empty"));
        return;
    }
    startGatewayWithRealtimeInput(gatewayPath);
}

void RFGatewayClient::onPrepTimeout() {
    if (backend_ == nullptr) {
        return;
    }
    if (prepProcess_.state() == QProcess::NotRunning) {
        return;
    }

    prepProcess_.kill();
    (void)prepProcess_.waitForFinished(1000);
    lastStartError_ = QString("WAV preprocessing timed out (%1s)").arg(kPrepTimeoutSeconds);
    backend_->addLog("ERROR", "RF", lastStartError_);
    backend_->updateSerialStatus(false);
    firstRfTimer_.stop();
    awaitingFirstRf_ = false;
    pendingGatewayPath_.clear();
}

void RFGatewayClient::onFirstRfTimeout() {
    if (backend_ == nullptr || !awaitingFirstRf_) {
        return;
    }

    awaitingFirstRf_ = false;
    const QString gatewayState = QString::number(static_cast<int>(gatewayProcess_.state()));
    const QString replayState = QString::number(static_cast<int>(replayProcess_.state()));
    backend_->addLog(
        "WARN",
        "RF",
        QString("Timed out waiting first RF event (%1 ms): gatewayState=%2 replayState=%3")
            .arg(firstRfTimeoutMs_)
            .arg(gatewayState, replayState)
    );
    backend_->addLog(
        "WARN",
        "RF",
        QStringLiteral("Check AA55 pulse stream input and WAV extraction thresholds")
    );
}

void RFGatewayClient::drainBuffer(QByteArray *buffer, const QByteArray &chunk, const QString &source) {
    if (buffer == nullptr || chunk.isEmpty()) {
        return;
    }

    buffer->append(chunk);
    while (true) {
        const int newline = buffer->indexOf('\n');
        if (newline < 0) {
            break;
        }

        QByteArray line = buffer->left(newline);
        buffer->remove(0, newline + 1);
        if (!line.isEmpty() && line.endsWith('\r')) {
            line.chop(1);
        }

        handleGatewayLine(QString::fromLocal8Bit(line), source);
    }
}

void RFGatewayClient::handleGatewayLine(const QString &line, const QString &source) {
    if (backend_ == nullptr) {
        return;
    }

    const QString text = line.trimmed();
    if (text.isEmpty()) {
        return;
    }

    if (
        text.startsWith(QStringLiteral("[RF]"), Qt::CaseInsensitive) &&
        (
            text.contains(QStringLiteral("decode failed"), Qt::CaseInsensitive) ||
            text.contains(QStringLiteral("decode_failed"), Qt::CaseInsensitive)
        )
    ) {
        return;
    }

    if (source == QStringLiteral("REPLAY_ERR")) {
        int frameIndex = -1;
        double wavSec = -1.0;
        if (parseReplayFrameMeta(text, &frameIndex, &wavSec)) {
            if (frameIndex > 0 && wavSec >= 0.0) {
                replayWavSecByIdx_.insert(frameIndex, wavSec);
            }
            return;
        }
        if (parseReplayFrameMarker(text, &frameIndex)) {
            return;
        }
    }

    QString enrichedText = text;
    RFEvent event;
    if (parseRfLine(text, &event)) {
        if (awaitingFirstRf_) {
            awaitingFirstRf_ = false;
            firstRfTimer_.stop();
        }

        event.frameSeq = extractTimingValueLongLong(text, QStringLiteral("seq"), -1);
        if (event.frameSeq > 0) {
            const int idx = static_cast<int>(event.frameSeq);
            if (replayWavSecByIdx_.contains(idx)) {
                event.candidateWavSec = replayWavSecByIdx_.value(idx, -1.0);
            }
        }
        event.decodeUs = extractTimingValueLongLong(text, QStringLiteral("decode_us"), -1);

        const uint32_t rawCode = parseRawCode(event.address);

        backend_->addRFEvent(event);
        backend_->updateWaveform(buildWaveformFromRawCode(rawCode));
        backend_->addLog("INFO", "RF", enrichedText);

        QJsonObject payloadObj;
        payloadObj.insert(QStringLiteral("addr"), event.address);
        payloadObj.insert(QStringLiteral("key"), event.key);
        payloadObj.insert(QStringLiteral("conf"), event.confidence);
        payloadObj.insert(QStringLiteral("src"), event.source);
        if (event.decodeUs >= 0) {
            payloadObj.insert(QStringLiteral("decode_us"), event.decodeUs);
        }

        backend_->addMqttPublishLog(
            QStringLiteral("home/rf433/report"),
            QString::fromUtf8(QJsonDocument(payloadObj).toJson(QJsonDocument::Compact))
        );
        return;
    }

    if (text.contains(QStringLiteral("crc"), Qt::CaseInsensitive)) {
        backend_->incrementCrcError();
    }
    if (text.contains(QStringLiteral("drop"), Qt::CaseInsensitive)) {
        backend_->incrementDrop();
    }

    const QString level =
        (source == QStringLiteral("GATEWAY_ERR") || source == QStringLiteral("REPLAY_ERR"))
            ? QStringLiteral("WARN")
            : QStringLiteral("INFO");
    backend_->addLog(level, source, enrichedText);
}

bool RFGatewayClient::parseRfLine(const QString &line, RFEvent *event) const {
    if (event == nullptr) {
        return false;
    }

    static const QRegularExpression re(
        QStringLiteral("^\\[RF\\]\\s+addr=([^\\s]+)\\s+key=([^\\s]+)\\s+conf=([0-9]*\\.?[0-9]+)\\s+source=([^\\s]+).*$")
    );
    const QRegularExpressionMatch m = re.match(line);
    if (!m.hasMatch()) {
        return false;
    }

    bool confOk = false;
    const double conf = m.captured(3).toDouble(&confOk);
    if (!confOk) {
        return false;
    }

    event->timestamp = QDateTime::currentDateTime();
    event->address = m.captured(1);
    event->key = m.captured(2);
    event->confidence = conf;
    event->source = m.captured(4);

    return true;
}

bool RFGatewayClient::parseReplayFrameMarker(
    const QString &line,
    int *frameIndex) const {
    if (frameIndex == nullptr) {
        return false;
    }

    static const QRegularExpression markerRe(
        QStringLiteral(
            "^FRAME_TS\\s+idx=(\\d+)$"
        )
    );
    const QRegularExpressionMatch m = markerRe.match(line);
    if (!m.hasMatch()) {
        return false;
    }

    bool idxOk = false;
    const int idx = m.captured(1).toInt(&idxOk);
    if (!idxOk) {
        return false;
    }

    *frameIndex = idx;
    return true;
}

bool RFGatewayClient::parseReplayFrameMeta(
    const QString &line,
    int *frameIndex,
    double *wavSec) const {
    if (frameIndex == nullptr || wavSec == nullptr) {
        return false;
    }

    static const QRegularExpression markerRe(
        QStringLiteral("^FRAME_META\\s+idx=(\\d+)\\s+wav_sec=([0-9]*\\.?[0-9]+)$")
    );
    const QRegularExpressionMatch m = markerRe.match(line);
    if (!m.hasMatch()) {
        return false;
    }

    bool idxOk = false;
    bool secOk = false;
    const int idx = m.captured(1).toInt(&idxOk);
    const double sec = m.captured(2).toDouble(&secOk);
    if (!idxOk || !secOk) {
        return false;
    }

    *frameIndex = idx;
    *wavSec = sec;
    return true;
}





qint64 RFGatewayClient::computeFirstRfTimeoutMs() const {
    const double replaySpeed = options_.wavSpeed <= 0.0 ? 1.0 : options_.wavSpeed;
    // Rough estimate: assume ~50ms per frame at speed=1
    const qint64 estimatedMs = static_cast<qint64>(timelineFrameCount_ * 50.0 / replaySpeed);
    return qMax(kMinFirstRfTimeoutMs, estimatedMs + kFirstRfTimeoutGraceMs);
}

QString RFGatewayClient::resolvePythonBin() const {
    return normalizePathIfExists(options_.pythonBin.trimmed());
}

QString RFGatewayClient::resolveGatewayPath() const {
    return normalizePathIfExists(options_.gatewayPath.trimmed());
}

QString RFGatewayClient::resolveWavInputPath() const {
    return normalizePathIfExists(options_.wavPath.trimmed());
}

QString RFGatewayClient::resolveWavToPulsesPath() const {
    const QString appDir = QCoreApplication::applicationDirPath();
    return normalizePathIfExists(QDir(appDir).filePath(QStringLiteral("../../python/wav_to_pulses.py")));
}

QString RFGatewayClient::resolveTimelineReplayPath() const {
    const QString appDir = QCoreApplication::applicationDirPath();
    return normalizePathIfExists(QDir(appDir).filePath(QStringLiteral("../../python/replay_pulse_timeline.py")));
}

}  // namespace dashboard




