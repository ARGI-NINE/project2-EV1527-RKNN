#include "rf_gateway_client.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace dashboard {

namespace {

constexpr int kMaxBufferedDiagnosticsBytes = 8192;

QString fixedGatewayPath() {
    return QCoreApplication::applicationDirPath() + QStringLiteral("/rf_gateway");
}

QString scalarJsonString(const QJsonValue &value) {
    if (value.isString()) {
        return value.toString().trimmed();
    }
    if (value.isDouble()) {
        return QString::number(value.toDouble(), 'f', 0);
    }
    return QString();
}

}  // namespace

RFGatewayClient::RFGatewayClient(DashboardBackend *backend, const AppOptions &options, QObject *context)
    : backend_(backend),
      options_(options),
      context_(context == nullptr ? QCoreApplication::instance() : context) {
}

RFGatewayClient::~RFGatewayClient() {
    stop();
}

void RFGatewayClient::start() {
    if (backend_ == nullptr) {
        return;
    }

    startGateway();
}

void RFGatewayClient::stop() {
    if (gateway_ != nullptr) {
        if (gateway_->state() != QProcess::NotRunning) {
            gateway_->kill();
            gateway_->waitForFinished(500);
        }
        flushProtocolBuffer();
        gatewayStderrBuffer_.clear();
        gateway_->deleteLater();
        gateway_ = nullptr;
    }

    if (backend_ != nullptr) {
        backend_->updateSerialStatus(false);
    }
}

QString RFGatewayClient::resolveGatewayPath() const {
    const QFileInfo info(fixedGatewayPath());
    if (info.exists() && info.isFile()) {
        return info.absoluteFilePath();
    }
    return QString();
}

QString RFGatewayClient::resolvedRfInputPath() const {
    const QString defaultPath = defaultRFInputPath();
    if (options_.rfInput.isEmpty() || options_.rfInput == defaultPath) {
        return defaultPath;
    }
    return defaultPath;
}

QStringList RFGatewayClient::buildGatewayArgs() const {
    QStringList args;
    args << "--rf-input" << resolvedRfInputPath();
    return args;
}

void RFGatewayClient::startGateway() {
    if (backend_ == nullptr) {
        return;
    }

    const QString gatewayPath = resolveGatewayPath();
    const QString rfInputPath = resolvedRfInputPath();
    if (gatewayPath.isEmpty()) {
        backend_->updateSerialStatus(false);
        backend_->addLog(
            "ERROR",
            "SYSTEM",
            QString("未找到固定路径 rf_gateway: %1（板侧版本不允许 gateway 路径注入）")
                .arg(QFileInfo(fixedGatewayPath()).absoluteFilePath())
        );
        return;
    }
    if (!options_.rfInput.isEmpty() && options_.rfInput != rfInputPath) {
        backend_->addLog(
            "WARN",
            "SYSTEM",
            QString("检测到非法 RF 输入路径 %1，已强制回退到 %2")
                .arg(options_.rfInput, rfInputPath)
        );
    }

    if (gateway_ != nullptr) {
        gateway_->deleteLater();
        gateway_ = nullptr;
    }

    gateway_ = new QProcess(context_);
    gateway_->setProcessChannelMode(QProcess::SeparateChannels);
    gatewayStdoutBuffer_.clear();
    gatewayStderrBuffer_.clear();

    QObject::connect(gateway_, &QProcess::started, context_, [this, gatewayPath, rfInputPath]() {
        if (backend_ != nullptr) {
            backend_->updateSerialStatus(false, rfInputPath);
            backend_->addLog("INFO", "SYSTEM", QString("rf_gateway 已启动: %1").arg(gatewayPath));
        }
    });

    QObject::connect(gateway_, &QProcess::readyReadStandardOutput, context_, [this]() {
        if (gateway_ == nullptr) {
            return;
        }

        drainProtocolBuffer(gateway_->readAllStandardOutput());
    });

    QObject::connect(gateway_, &QProcess::readyReadStandardError, context_, [this]() {
        if (gateway_ == nullptr) {
            return;
        }

        appendDiagnosticChunk(gateway_->readAllStandardError());
    });

    QObject::connect(gateway_, &QProcess::errorOccurred, context_, [this](QProcess::ProcessError error) {
        if (backend_ != nullptr) {
            const QString diagnostics = takeBufferedDiagnostics();

            backend_->updateSerialStatus(false);
            backend_->addLog(
                "ERROR",
                "SYSTEM",
                diagnostics.isEmpty()
                    ? QString("rf_gateway 运行异常: %1").arg(static_cast<int>(error))
                    : QString("rf_gateway 运行异常: %1 stderr=%2").arg(static_cast<int>(error)).arg(diagnostics)
            );
        }
    });

    QObject::connect(gateway_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), context_, [this](int code, QProcess::ExitStatus status) {
        const QString diagnostics = takeBufferedDiagnostics();

        flushProtocolBuffer();
        if (backend_ != nullptr) {
            backend_->updateSerialStatus(false);
            if ((status != QProcess::NormalExit || code != 0) && !diagnostics.isEmpty()) {
                backend_->addLog("ERROR", "SYSTEM", QString("rf_gateway fatal stderr: %1").arg(diagnostics));
            }
            backend_->addLog(
                (status == QProcess::NormalExit && code == 0) ? "WARN" : "ERROR",
                "SYSTEM",
                QString("rf_gateway 已退出，code=%1").arg(code)
            );
        }
    });

    gateway_->setProgram(gatewayPath);
    gateway_->setArguments(buildGatewayArgs());
    gateway_->start();
}

void RFGatewayClient::drainProtocolBuffer(const QByteArray &chunk) {
    if (chunk.isEmpty()) {
        return;
    }

    gatewayStdoutBuffer_.append(chunk);
    while (true) {
        const int newlinePos = gatewayStdoutBuffer_.indexOf('\n');
        QByteArray lineBytes;
        QString line;

        if (newlinePos < 0) {
            break;
        }

        lineBytes = gatewayStdoutBuffer_.left(newlinePos);
        gatewayStdoutBuffer_.remove(0, newlinePos + 1);
        if (!lineBytes.isEmpty() && lineBytes.endsWith('\r')) {
            lineBytes.chop(1);
        }

        line = QString::fromUtf8(lineBytes).trimmed();
        if (line.isEmpty()) {
            continue;
        }

        handleProtocolLine(line);
    }
}

void RFGatewayClient::flushProtocolBuffer() {
    QString line;

    if (gatewayStdoutBuffer_.isEmpty()) {
        return;
    }

    line = QString::fromUtf8(gatewayStdoutBuffer_).trimmed();
    gatewayStdoutBuffer_.clear();
    if (line.isEmpty()) {
        return;
    }

    handleProtocolLine(line);
}

void RFGatewayClient::appendDiagnosticChunk(const QByteArray &chunk) {
    if (chunk.isEmpty()) {
        return;
    }

    gatewayStderrBuffer_.append(chunk);
    if (gatewayStderrBuffer_.size() > kMaxBufferedDiagnosticsBytes) {
        gatewayStderrBuffer_.remove(0, gatewayStderrBuffer_.size() - kMaxBufferedDiagnosticsBytes);
    }
}

QString RFGatewayClient::takeBufferedDiagnostics() {
    QString diagnostics;

    if (gatewayStderrBuffer_.isEmpty()) {
        return QString();
    }

    diagnostics = QString::fromUtf8(gatewayStderrBuffer_).trimmed();
    diagnostics.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    diagnostics.replace(QLatin1Char('\n'), QLatin1String(" | "));
    gatewayStderrBuffer_.clear();
    return diagnostics;
}

bool RFGatewayClient::parseProtocolEnvelope(
    const QString &line,
    QString *type,
    QString *topic,
    bool *mqttPublished,
    QJsonObject *payload
) const {
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8(), &parseError);
    const QJsonObject root = doc.object();

    if (type == nullptr || topic == nullptr || mqttPublished == nullptr || payload == nullptr) {
        return false;
    }
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return false;
    }

    *type = scalarJsonString(root.value(QStringLiteral("type")));
    *topic = scalarJsonString(root.value(QStringLiteral("topic")));
    *mqttPublished = root.value(QStringLiteral("mqtt_published")).toBool(false);
    if (type->isEmpty() || !root.value(QStringLiteral("payload")).isObject()) {
        return false;
    }

    *payload = root.value(QStringLiteral("payload")).toObject();
    return true;
}

bool RFGatewayClient::parseRFEventPayload(const QJsonObject &payload, RFEvent *event, QVector<int> *pulses) const {
    const QString address = scalarJsonString(payload.value(QStringLiteral("addr")));
    const QString key = scalarJsonString(payload.value(QStringLiteral("key")));
    const QString source = scalarJsonString(payload.value(QStringLiteral("src")));
    const QJsonValue confValue = payload.value(QStringLiteral("conf"));
    const QJsonValue pulseArrayValue = payload.value(QStringLiteral("pulse_us"));
    const QJsonArray pulseArray = pulseArrayValue.toArray();

    if (event == nullptr || pulses == nullptr) {
        return false;
    }
    if (address.isEmpty() || key.isEmpty() || source.isEmpty() || !confValue.isDouble() || !pulseArrayValue.isArray()) {
        return false;
    }

    event->timestamp = QDateTime::currentDateTime();
    event->address = address;
    event->key = key;
    event->confidence = confValue.toDouble();
    event->source = source;
    event->frameSeq = payload.value(QStringLiteral("seq")).isDouble()
        ? static_cast<qint64>(payload.value(QStringLiteral("seq")).toDouble(-1.0))
        : -1;
    event->decodeUs = payload.value(QStringLiteral("decode_us")).isDouble()
        ? static_cast<qint64>(payload.value(QStringLiteral("decode_us")).toDouble(-1.0))
        : -1;

    pulses->clear();
    pulses->reserve(pulseArray.size());
    for (const QJsonValue &value : pulseArray) {
        const int pulseUs = value.toInt(-1);
        if (!value.isDouble() || pulseUs <= 0) {
            pulses->clear();
            return false;
        }
        pulses->append(pulseUs);
    }

    return true;
}

void RFGatewayClient::handleProtocolLine(const QString &line) {
    QString type;
    QString topic;
    QJsonObject payload;
    bool mqttPublished = false;

    if (backend_ == nullptr) {
        return;
    }

    if (!parseProtocolEnvelope(line, &type, &topic, &mqttPublished, &payload)) {
        backend_->incrementParseError();
        backend_->addLog("WARN", "RF", QString("Invalid rf_gateway protocol JSON: %1").arg(line));
        return;
    }

    if (type == QStringLiteral("rf_event")) {
        RFEvent event;
        QVector<int> pulses;

        if (!parseRFEventPayload(payload, &event, &pulses)) {
            backend_->incrementParseError();
            backend_->addLog("WARN", "RF", QString("Invalid rf_event payload: %1").arg(line));
            return;
        }

        backend_->updateSerialStatus(true, scalarJsonString(payload.value(QStringLiteral("rf_input"))).isEmpty()
            ? resolvedRfInputPath()
            : scalarJsonString(payload.value(QStringLiteral("rf_input"))));
        backend_->addRFEvent(event, pulses);
        backend_->addLog("INFO", "RF", line);
        if (mqttPublished && !topic.isEmpty()) {
            backend_->addMqttPublishLog(topic, QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact)));
        }
        return;
    }

    if (type == QStringLiteral("device_status")) {
        const QString rfInputPath = scalarJsonString(payload.value(QStringLiteral("rf_input")));
        const bool rfOnline = payload.value(QStringLiteral("rf_online")).toBool(false);
        const int crcErrors = payload.value(QStringLiteral("driver_crc_err")).toInt(-1);
        const int driverDropFrames = payload.value(QStringLiteral("app_drv_drop")).toInt(-1);

        backend_->updateSerialStatus(rfOnline, rfInputPath.isEmpty() ? resolvedRfInputPath() : rfInputPath);
        backend_->updateProtocolStats(crcErrors, -1, driverDropFrames);
        backend_->addLog("INFO", "RF", line);
        if (mqttPublished && !topic.isEmpty()) {
            backend_->addMqttPublishLog(topic, QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact)));
        }
        return;
    }

    if (type == QStringLiteral("rf_stats")) {
        const int crcErrors = payload.value(QStringLiteral("driver_crc_err")).toInt(-1);
        const int parseErrors =
            payload.value(QStringLiteral("decode_no_frame")).toInt(0) +
            payload.value(QStringLiteral("decode_err")).toInt(0);
        const int driverDropFrames = payload.value(QStringLiteral("drv_drop")).toInt(-1);

        if (payload.contains(QStringLiteral("driver_online"))) {
            backend_->updateSerialStatus(
                payload.value(QStringLiteral("driver_online")).toBool(false),
                scalarJsonString(payload.value(QStringLiteral("rf_input"))).isEmpty()
                    ? resolvedRfInputPath()
                    : scalarJsonString(payload.value(QStringLiteral("rf_input")))
            );
        }
        backend_->updateProtocolStats(crcErrors, parseErrors, driverDropFrames);
        backend_->addLog("INFO", "RF", line);
        if (mqttPublished && !topic.isEmpty()) {
            backend_->addMqttPublishLog(topic, QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact)));
        }
        return;
    }

    backend_->incrementParseError();
    backend_->addLog("WARN", "RF", QString("Unknown rf_gateway protocol type: %1").arg(line));
}

}  // namespace dashboard
