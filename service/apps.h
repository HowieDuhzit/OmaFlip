#pragma once
#include "files.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <termios.h>
class QSocketNotifier;
namespace omaflip {
constexpr int kMaxAppEntries = 512;
QString appKindForName(const QString& name);
bool isAppStoragePath(const QString& path);
QString defaultInstallDir(const QString& name);

class AppSession : public QObject {
    Q_OBJECT
public:
    explicit AppSession(QObject* parent = nullptr);
    ~AppSession() override;
    void start(const QString& port);
    void stop();
    void refresh();
    void launch(const QString& path);
    void exitApp();
    void remove(const QString& path);
    void install(const QString& hostPath, const QString& destDir, bool overwrite);
    void readScript(const QString& path);
    void writeScript(const QString& path, const QString& text);
    bool busy() const;
    QJsonObject snapshot() const;
signals:
    void ready();
    void changed();
    void failed(const QJsonObject& error);
    void stopped();
private:
    enum class Phase { Banner, StartRpc, Ping, Ready, Stopping, Done };
    enum class Op { None, List, Read, Write, Delete, Start, Exit, Mkdir };
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
    void beginScan();
    void beginList(const QString& path);
    void beginWrite();
    void fillWrite();
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
    QJsonArray apps_;
    QJsonObject script_;
    QStringList pendingDirs_;
    QString listPath_;
    QString targetPath_;
    QString hostPath_;
    QString afterWrite_;
    QFile file_;
    QByteArray readBuffer_;
    qint64 transferTotal_ = 0;
    qint64 transferBytes_ = 0;
    bool overwrite_ = false;
    bool locked_ = false;
};
}
