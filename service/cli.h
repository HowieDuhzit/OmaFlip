#pragma once
#include "model.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <termios.h>
class QSocketNotifier;
namespace omaflip {
constexpr int kMaxCliOutput = 65536;
constexpr int kMaxCliHistory = 50;
constexpr int kMaxCliCommand = 512;

QJsonArray parseHelpCommands(const QByteArray& bytes);
QString sanitizeCliLine(const QString& line);
bool isStreamingCommand(const QString& line);
bool isBlockedCliCommand(const QString& line);

class CliSession : public QObject {
    Q_OBJECT
public:
    explicit CliSession(QObject* parent = nullptr);
    ~CliSession() override;
    void start(const QString& port);
    void stop();
    void sendLine(const QString& line);
    void interrupt();
    QString save(QString& error);
    bool busy() const;
    QJsonObject snapshot() const;
signals:
    void ready();
    void changed();
    void failed(const QJsonObject& error);
    void stopped();
private:
    enum class Phase { Banner, Help, Ready, Stopping, Done };
    void readReady();
    void send(const QByteArray& bytes);
    void drainWrite();
    void fail(const QJsonObject& error);
    void closePort();
    void ingest();
    void completeCommand();
    void appendOutput(const QString& text);
    void publishLive();
    int fd_ = -1;
    bool saved_ = false;
    bool exclusive_ = false;
    termios previous_{};
    QString port_;
    Phase phase_ = Phase::Done;
    QSocketNotifier* reader_ = nullptr;
    QSocketNotifier* writer_ = nullptr;
    QTimer timeout_, dtrTimer_, liveTimer_;
    QByteArray input_, outputBytes_;
    QString output_;
    QString error_;
    QJsonArray commands_;
    QStringList history_;
    QString savedPath_;
    bool waiting_ = false;
    bool streaming_ = false;
};
}
