#pragma once
#include "rpc.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <termios.h>
class QSocketNotifier;
namespace omaflip {
constexpr int kMaxNameLength = 255;
constexpr int kMaxPathLength = 512;
constexpr qint64 kMaxTransferBytes = 32ll * 1024 * 1024;
constexpr int kMaxPreviewBytes = 32 * 1024;
constexpr int kMaxListingEntries = 2048;

QString normalizeDevicePath(const QString& path);
QString parentDevicePath(const QString& path);
QString joinDevicePath(const QString& dir, const QString& name);
QString deviceBasename(const QString& path);
QString sanitizeHostName(const QString& name);
bool looksLikeText(const QByteArray& data);
QString previewKindFor(const QString& name, const QByteArray& data);
QString hostDownloadDir(QString& error);

class FileSession : public QObject {
    Q_OBJECT
public:
    explicit FileSession(QObject* parent = nullptr);
    ~FileSession() override;
    void start(const QString& port);
    void stop();
    void list(const QString& path);
    void preview(const QString& path);
    void download(const QString& path, const QString& hostPath, bool overwrite);
    void upload(const QString& path, const QString& hostPath, bool overwrite);
    void mkdir(const QString& path);
    void rename(const QString& oldPath, const QString& newPath);
    void remove(const QString& path, bool recursive);
    bool busy() const;
    QJsonObject snapshot() const;
signals:
    void ready();
    void changed();
    void failed(const QJsonObject& error);
    void stopped();
private:
    enum class Phase { Banner, StartRpc, Ping, Ready, Stopping, Done };
    enum class Op { None, List, Stat, Read, Write, Mkdir, Rename, Delete };
    enum class AfterStat { None, Download, Upload };
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
    void beginList(const QString& path);
    void beginRead();
    void beginWrite();
    void fillWrite();
    void publishTransfer(bool force);
    int fd_ = -1;
    bool saved_ = false;
    bool exclusive_ = false;
    termios previous_{};
    QString port_;
    Phase phase_ = Phase::Done;
    Op op_ = Op::None;
    AfterStat afterStat_ = AfterStat::None;
    QSocketNotifier* reader_ = nullptr;
    QSocketNotifier* writer_ = nullptr;
    QTimer timeout_, dtrTimer_;
    QByteArray input_, output_;
    quint32 commandId_ = 0;
    QString path_ = "/ext";
    QString pendingList_ = "/ext";
    QString targetPath_;
    QString hostPath_;
    QString error_;
    QString errorCode_;
    QJsonArray entries_;
    QJsonObject preview_;
    QJsonObject transfer_;
    QByteArray readBuffer_;
    QFile file_;
    qint64 transferTotal_ = 0;
    qint64 transferBytes_ = 0;
    qint64 lastPublishedBytes_ = 0;
    bool overwrite_ = false;
    bool previewOnly_ = false;
    bool listedExt_ = false;
    QString lastOp_;
};
}
