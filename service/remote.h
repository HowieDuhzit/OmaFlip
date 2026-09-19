#pragma once
#include "rpc.h"
#include "screen.h"
#include <QObject>
#include <QTimer>
#include <termios.h>
class QSocketNotifier;
namespace omaflip {
QString remoteKeyFromVisual(const QString& visual, int orientation);

class RemoteSession : public QObject {
    Q_OBJECT
public:
    explicit RemoteSession(QObject* parent = nullptr);
    ~RemoteSession() override;
    void start(const QString& port);
    void stop();
    void sendInput(const QString& button, const QString& type = "short");
    QImage lastImage() const { return lastImage_; }
signals:
    void ready();
    void frame(const QByteArray& png, int orientation);
    void failed(const QJsonObject& error);
    void stopped();
private:
    enum class Phase { Banner, StartRpc, Ping, StartStream, Streaming, Stopping, Done };
    void readReady();
    void send(const QByteArray& bytes);
    void drainWrite();
    void sendRpc(const PB::Main& message);
    void handleRpc(const PB::Main& message);
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
    QTimer timeout_, dtrTimer_;
    QByteArray input_, output_;
    quint32 commandId_ = 0;
    QImage lastImage_;
    int orientation_ = 0;
};
}
