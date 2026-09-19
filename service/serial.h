#pragma once
#include "model.h"
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
    void finished(const QJsonObject& info, const QJsonObject& power, const QString& warning);
    void failed(const QJsonObject& error);
private:
    enum class Phase { Banner, DeviceInfo, PowerInfo, Done };
    void readReady();
    void send(const QByteArray& command);
    void drainWrite();
    void fail(const QJsonObject& error);
    void complete(const QString& warning = {});
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
    QJsonObject info_, power_;
};
}
