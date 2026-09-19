#pragma once
#include "files.h"
#include <QFile>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <termios.h>
class QSocketNotifier;
namespace omaflip {
constexpr int kMaxDevLog = 64 * 1024;

QString sanitizeAppId(const QString& id);
QJsonObject parseApplicationFam(const QByteArray& text);
QStringList ufbtUpdateArgs(const QString& origin, const QString& version);
QString findUfbt(const QStringList& extraDirs = {});
QStringList ufbtCommand(const QStringList& extraDirs = {});
QString defaultDevProject();
QString findBuiltFap(const QString& projectDir);
bool inspectKindAllowed(const QString& kind);

class DevSession : public QObject {
    Q_OBJECT
public:
    explicit DevSession(QObject* parent = nullptr);
    ~DevSession() override;
    void start(const QString& port);
    void stop();
    void setOrigin(const QString& origin, const QString& version);
    void setProject(const QString& path);
    void createApp(const QString& appId);
    void build();
    void lint();
    void updateSdk();
    void installUfbt();
    void deploy(bool overwrite);
    void inspect(const QString& kind, const QString& arg);
    bool busy() const;
    QJsonObject snapshot() const;
signals:
    void ready();
    void changed();
    void failed(const QJsonObject& error);
    void stopped();
private:
    enum class Phase { Banner, StartRpc, Ping, Ready, Stopping, Done };
    enum class Op { None, Inspect, Mkdir, Write, Start };
    void readReady();
    void send(const QByteArray& bytes);
    void drainWrite();
    void sendRpc(const PB::Main& message);
    void handleRpc(const PB::Main& message);
    void fail(const QJsonObject& error);
    void opError(const QString& code, const QString& reason, const QString& suggestion);
    void closePort();
    void beginOp(Op op, quint32 timeoutMs = 8000);
    void finishOp();
    void appendLog(const QString& text);
    void refreshProject();
    void runUfbt(const QStringList& args, int timeoutMs);
    void fillWrite();
    void beginInspect(const QString& kind, const QString& arg);
    int fd_ = -1;
    bool saved_ = false;
    bool exclusive_ = false;
    termios previous_{};
    QString port_;
    Phase phase_ = Phase::Done;
    Op op_ = Op::None;
    QSocketNotifier* reader_ = nullptr;
    QSocketNotifier* writer_ = nullptr;
    QTimer timeout_, dtrTimer_;
    QByteArray input_, output_;
    quint32 commandId_ = 0;
    QString error_;
    QString errorCode_;
    QString origin_;
    QString version_;
    QString project_;
    QJsonObject fam_;
    QString fap_;
    QString log_;
    QString inspect_;
    QString inspectKind_;
    QString targetPath_;
    QFile file_;
    QProcess process_;
};
}
