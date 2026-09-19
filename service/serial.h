#pragma once
#include "model.h"
#include "rpc.h"
#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <termios.h>
class QSocketNotifier;
namespace omaflip {
// One bounded, read-only CLI transaction. Never owns a port while idle.
class SerialProbe : public QObject {
    Q_OBJECT
public:
    explicit SerialProbe(QObject* parent = nullptr);
    ~SerialProbe() override;
    void start(const QString& port);
    void cancel();
signals:
    void finished(const QJsonObject& info, const QJsonObject& power, const QJsonObject& rpc, const QString& warning);
    void failed(const QJsonObject& error);
private:
    enum class Phase { Banner, DeviceInfo, PowerInfo, StartRpc, RpcPing, RpcVersion, RpcStorageExt, RpcStorageInt, Done };
    void readReady();
    void send(const QByteArray& command);
    void drainWrite();
    void fail(const QJsonObject& error);
    void complete(const QString& warning = {});
    void sendRpc(const PB::Main& message);
    void handleRpc(const PB::Main& message);
    bool rpcPhase() const;
    int fd_ = -1;
    bool saved_ = false;
    bool exclusive_ = false;
    termios previous_{};
    QString port_;
    Phase phase_ = Phase::Done;
    QSocketNotifier* reader_ = nullptr;
    QSocketNotifier* writer_ = nullptr;
    QTimer timeout_, dtrTimer_;
    QByteArray input_, output_, pingPayload_;
    QJsonObject info_, power_, rpc_;
    QString warning_;
    quint32 commandId_ = 0;
    quint32 rpcMajor_ = 0, rpcMinor_ = 0;
    bool pingOk_ = false, extPresent_ = false, intPresent_ = false;
    quint64 extTotal_ = 0, extFree_ = 0, intTotal_ = 0, intFree_ = 0;
    QElapsedTimer probeClock_;
    QElapsedTimer pingClock_;
    qint64 probeMs_ = 0;
    qint64 pingMs_ = 0;
};
}
