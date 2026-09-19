#pragma once
#include "files.h"
#include "firmware.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <termios.h>
class QSocketNotifier;
namespace omaflip {
class ManageSession : public QObject {
    Q_OBJECT
public:
    explicit ManageSession(QObject* parent = nullptr);
    ~ManageSession() override;
    void start(const QString& port);
    void stop();
    void setDevice(const QJsonObject& info, const QJsonObject& rpc);
    void refresh();
    void createBackup();
    void restore(const QString& archive, bool confirmOrigin, bool confirmVersion);
    void applyFirmware(const QString& hostPath, const QJsonObject& package, bool replaceOrigin);
    void installPack(const QString& hostPath, const QJsonObject& pack);
    void removePack(const QString& name);
    bool busy() const;
    QJsonObject snapshot() const;
signals:
    void ready();
    void changed();
    void failed(const QJsonObject& error);
    void stopped();
private:
    enum class Phase { Banner, StartRpc, Ping, Ready, Stopping, Done };
    enum class Op { None, Mkdir, List, Read, Write, Backup, Restore, Extract, Update, Reboot, Delete };
    void readReady();
    void send(const QByteArray& bytes);
    void drainWrite();
    void sendRpc(const PB::Main& message);
    void handleRpc(const PB::Main& message);
    void fail(const QJsonObject& error);
    void opError(const QString& code, const QString& reason, const QString& suggestion);
    void closePort();
    void beginOp(Op op, quint32 timeoutMs = 15000);
    void finishOp();
    void beginList();
    void beginPackList();
    void beginWriteJson();
    void fillWrite();
    void nextRead();
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
    QByteArray input_, output_, readBuffer_, writeBuffer_;
    quint32 commandId_ = 0;
    QString error_;
    QString errorCode_;
    QJsonObject info_;
    QJsonObject rpc_;
    QJsonArray backups_;
    QJsonArray packs_;
    QJsonArray pendingJson_;
    QJsonObject currentMeta_;
    QString stamp_;
    QString tarPath_;
    QString jsonPath_;
    QString hostPath_;
    QString extractDir_;
    QString manifestPath_;
    QString after_;
};
}
