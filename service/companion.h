#pragma once

#include "companion_protocol.h"
#include "rpc.h"
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <array>
#include <termios.h>

class QSocketNotifier;

namespace omaflip {
class CompanionSession : public QObject {
    Q_OBJECT
public:
    explicit CompanionSession(QObject* parent = nullptr);
    ~CompanionSession() override;
    void start(const QString& port);
    void stop();
    void ping();
    QJsonObject snapshot() const;
signals:
    void ready();
    void changed();
    void failed(const QJsonObject& error);
    void stopped();
private:
    enum class Phase { Banner, StartRpc, Ping, Launch, WaitHello, Ready, Stopping, Done };
    void readReady();
    void send(const QByteArray& bytes);
    void drainWrite();
    void sendRpc(const PB::Main& message);
    void handleRpc(const PB::Main& message);
    void handleFrame(const CompanionFrame& frame);
    void sendFrame(CompanionMessage type, quint16 requestId, const QByteArray& payload = {}, quint8 flags = 0);
    void sendState();
    void readTelemetry(int index, quint64 generation);
    void finishTelemetry(int index, quint64 generation, bool ok);
    void publishTelemetry();
    void cancelTelemetry();
    void runAction(quint8 action, quint16 requestId);
    void startActionCommand(QStringList command);
    void readActionOutput();
    void cancelAction();
    void finishAction(bool success, const QString& detail);
    void fail(const QJsonObject& error);
    void closePort();
    int fd_ = -1;
    bool saved_ = false;
    bool exclusive_ = false;
    termios previous_{};
    QString port_;
    Phase phase_ = Phase::Done;
    QSocketNotifier* reader_ = nullptr;
    QSocketNotifier* writer_ = nullptr;
    QTimer timeout_, dtrTimer_, refreshTimer_, actionTimer_;
    QProcess* actionProcess_ = nullptr;
    QByteArray actionOutput_;
    QString currentTheme_;
    int actionStep_ = 0;
    quint64 actionGeneration_ = 0;
    struct TelemetryResult { bool ok = false; QByteArray out; };
    std::array<TelemetryResult, 6> telemetry_;
    std::array<QProcess*, 6> telemetryProcesses_{};
    QTimer telemetryTimer_;
    quint64 telemetryGeneration_ = 0;
    int telemetryRemaining_ = 0;
    bool telemetryInFlight_ = false;
    QByteArray input_, output_;
    quint32 commandId_ = 0;
    quint16 actionRequestId_ = 0;
    quint8 actionId_ = 0;
    quint16 nextPingId_ = 0;
    quint16 pendingPingId_ = 0;
    int pingsSent_ = 0;
    int pingsReceived_ = 0;
    QString status_;
    QString lastAction_;
};
}
