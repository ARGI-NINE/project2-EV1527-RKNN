#include "rf_gateway_client.h"

#include "rf_utils.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QRegularExpression>

namespace dashboard {

namespace {

QString fixedGatewayPath() {
    return QCoreApplication::applicationDirPath() + QStringLiteral("/rf_gateway");
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
    if (options_.rfInput.isEmpty()) {
        return defaultPath;
    }
    if (options_.rfInput == defaultPath) {
        return options_.rfInput;
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
    gateway_->setProcessChannelMode(QProcess::MergedChannels);

    QObject::connect(gateway_, &QProcess::started, context_, [this, gatewayPath, rfInputPath]() {
        if (backend_ != nullptr) {
            backend_->updateSerialStatus(true, rfInputPath);
            backend_->addLog("INFO", "SYSTEM", QString("rf_gateway 已启动: %1").arg(gatewayPath));
        }
    });

    QObject::connect(gateway_, &QProcess::readyReadStandardOutput, context_, [this]() {
        if (gateway_ == nullptr) {
            return;
        }

        gatewayBuffer_.append(QString::fromLocal8Bit(gateway_->readAllStandardOutput()));
        int newlinePos = gatewayBuffer_.indexOf('\n');
        while (newlinePos >= 0) {
            const QString line = gatewayBuffer_.left(newlinePos).trimmed();
            gatewayBuffer_.remove(0, newlinePos + 1);
            if (!line.isEmpty()) {
                handleGatewayLine(line);
            }
            newlinePos = gatewayBuffer_.indexOf('\n');
        }
    });

    QObject::connect(gateway_, &QProcess::errorOccurred, context_, [this](QProcess::ProcessError error) {
        if (backend_ != nullptr) {
            backend_->updateSerialStatus(false);
            backend_->addLog("ERROR", "SYSTEM", QString("rf_gateway 运行异常: %1").arg(static_cast<int>(error)));
        }
    });

    QObject::connect(gateway_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), context_, [this](int code, QProcess::ExitStatus status) {
        Q_UNUSED(status);
        if (backend_ != nullptr) {
            backend_->updateSerialStatus(false);
            backend_->addLog("WARN", "SYSTEM", QString("rf_gateway 已退出，code=%1").arg(code));
        }
    });

    gateway_->setProgram(gatewayPath);
    gateway_->setArguments(buildGatewayArgs());
    gateway_->start();
}

void RFGatewayClient::handleGatewayLine(const QString &line) {
    if (backend_ == nullptr) {
        return;
    }

    static const QRegularExpression rfExpr(
        R"(\[RF\]\s+addr=([^\s]+)\s+key=([^\s]+)\s+conf=([0-9.]+)\s+source=([^\s]+)\s+pulses=([0-9]+)(?:\s+seq=([0-9]+))?(?:\s+decode_us=([0-9]+))?)"
    );
    static const QRegularExpression runningExpr(R"(rf_gateway\s+running:\s+rf=([^\s]+))");
    static const QRegularExpression parseStatsExpr(
        R"(\[RF_PARSE_STATS\]\s+crc_errors=([0-9]+)\s+parse_errors=([0-9]+)\s+last_error=([^\s]+))"
    );
    static const QRegularExpression finalStatsExpr(
        R"(\[RF_STATS\].*?\bproto_crc_err=([0-9]+)\b.*?\bproto_parse_err=([0-9]+)\b)"
    );

    const QRegularExpressionMatch runningMatch = runningExpr.match(line);
    if (runningMatch.hasMatch()) {
        backend_->updateSerialStatus(true, runningMatch.captured(1));
        backend_->addLog("INFO", "SYSTEM", line);
        return;
    }

    const QRegularExpressionMatch parseStatsMatch = parseStatsExpr.match(line);
    if (parseStatsMatch.hasMatch()) {
        backend_->updateProtocolStats(parseStatsMatch.captured(1).toInt(), parseStatsMatch.captured(2).toInt());
        backend_->addLog("WARN", "RF", line);
        return;
    }

    const QRegularExpressionMatch finalStatsMatch = finalStatsExpr.match(line);
    if (finalStatsMatch.hasMatch()) {
        backend_->updateProtocolStats(finalStatsMatch.captured(1).toInt(), finalStatsMatch.captured(2).toInt());
        backend_->addLog("INFO", "RF", line);
        return;
    }

    const QRegularExpressionMatch rfMatch = rfExpr.match(line);
    if (rfMatch.hasMatch()) {
        RFEvent event;
        event.timestamp = QDateTime::currentDateTime();
        event.address = rfMatch.captured(1);
        event.key = rfMatch.captured(2);
        event.confidence = rfMatch.captured(3).toDouble();
        event.source = rfMatch.captured(4);
        if (!rfMatch.captured(6).isEmpty())
            event.frameSeq = rfMatch.captured(6).toLongLong();
        if (!rfMatch.captured(7).isEmpty())
            event.decodeUs = rfMatch.captured(7).toLongLong();

        backend_->addRFEvent(event);
        backend_->updateWaveform(buildWaveformFromRawCode(parseRawCode(event.address)));
        backend_->addLog("INFO", "RF", line);
        return;
    }

    if (line.contains("decode failed", Qt::CaseInsensitive)) {
        backend_->incrementDrop();
        backend_->addLog("WARN", "RF", line);
        return;
    }

    if (line.contains("parse", Qt::CaseInsensitive) && line.contains("fail", Qt::CaseInsensitive)) {
        backend_->incrementParseError();
        backend_->addLog("WARN", "RF", line);
        return;
    }

    if (line.contains("CRC", Qt::CaseInsensitive)) {
        backend_->incrementCrcError();
        backend_->addLog("WARN", "RF", line);
        return;
    }

    if (line.contains("[MQTT", Qt::CaseInsensitive)) {
        backend_->addLog("INFO", "MQTT", line);
        return;
    }

    backend_->addLog("INFO", "SYSTEM", line);
}

}  // namespace dashboard
