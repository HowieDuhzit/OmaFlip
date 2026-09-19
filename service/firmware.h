#pragma once
#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
class QNetworkReply;
namespace omaflip {
constexpr auto kOfficialFirmwareIndex = "https://update.flipperzero.one/firmware/directory.json";
constexpr auto kMomentumFirmwareIndex = "https://up.momentum-fw.dev/firmware/directory.json";
constexpr auto kMomentumAssetIndex = "https://up.momentum-fw.dev/asset-packs/directory.json";
constexpr auto kBackupDir = "/ext/omaflip_backup";
constexpr auto kAssetPackDir = "/ext/asset_packs";
constexpr int kBackupMetaVersion = 1;

QString hardwareTargetName(const QString& raw);
QString originKind(const QString& fork);
QString providerLabel(const QString& provider);
QJsonObject backupMetadata(const QJsonObject& info, const QJsonObject& rpc, const QString& archiveName);
QString backupCompat(const QJsonObject& meta, const QJsonObject& info);
QJsonObject parseFirmwareIndex(const QByteArray& json, const QString& provider, const QString& channel,
    const QString& target, QString& error);
QJsonObject parseOfficialIndex(const QByteArray& json, const QString& channel, const QString& target, QString& error);
QJsonArray parseAssetIndex(const QByteArray& json, QString& error);
QString updateManifestPath(const QJsonObject& package);
bool verifySha256(const QByteArray& data, const QString& expected);
QString firmwareHostDir(QString& error);
QString assetHostDir(QString& error);
bool urlAllowed(const QString& url);
bool urlAllowedFor(const QString& url, const QString& provider);
bool channelAllowed(const QString& provider, const QString& channel);

class FirmwareClient : public QObject {
    Q_OBJECT
public:
    explicit FirmwareClient(QObject* parent = nullptr);
    void check(const QString& provider, const QString& channel, const QString& target);
    void download(const QJsonObject& package);
    void checkPacks();
    void downloadPack(const QJsonObject& pack);
signals:
    void checked(const QJsonObject& package);
    void downloaded(const QString& path, const QJsonObject& package);
    void packsChecked(const QJsonArray& catalog);
    void packDownloaded(const QString& path, const QJsonObject& pack);
    void failed(const QJsonObject& error);
    void progress(qint64 received, qint64 total);
private:
    void abortReply();
    QNetworkAccessManager network_;
    QNetworkReply* reply_ = nullptr;
    QJsonObject pending_;
};
}
