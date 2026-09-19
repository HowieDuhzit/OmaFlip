#include "firmware.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QStandardPaths>
#include <QUrl>

namespace omaflip {
namespace {
constexpr auto kUserAgent = "OmaFlip/0.8";

QString indexHost(const QString& provider) {
    return provider == "momentum" ? QString("up.momentum-fw.dev") : QString("update.flipperzero.one");
}

QString indexUrl(const QString& provider) {
    return provider == "momentum" ? QString::fromUtf8(kMomentumFirmwareIndex)
                                  : QString::fromUtf8(kOfficialFirmwareIndex);
}

bool httpsHost(const QString& url, const QString& host) {
    const QUrl parsed(url);
    return parsed.scheme() == "https" && parsed.host().toLower() == host;
}

QString fileNameFromUrl(const QString& url) {
    return QFileInfo(QUrl(url).path()).fileName();
}
}

QString hardwareTargetName(const QString& raw) {
    const auto text = raw.trimmed().toLower();
    if(text == "7" || text == "f7") return "f7";
    if(text == "18" || text == "f18") return "f18";
    return text;
}

QString originKind(const QString& fork) {
    const auto text = fork.trimmed();
    if(text.isEmpty()) return "Unknown";
    if(text.compare("Official", Qt::CaseInsensitive) == 0) return "Official";
    if(text.compare("Momentum", Qt::CaseInsensitive) == 0) return "Momentum";
    return "Other";
}

QString providerLabel(const QString& provider) {
    return provider == "momentum" ? QString("Momentum") : QString("official");
}

QJsonObject backupMetadata(const QJsonObject& info, const QJsonObject& rpc, const QString& archiveName) {
    return {{"omaflip_backup", kBackupMetaVersion},
        {"archive", archiveName},
        {"firmware_version", info.value("firmware_version").toString()},
        {"firmware_origin", info.value("firmware_origin_fork").toString()},
        {"hardware_name", info.value("hardware_name").toString()},
        {"hardware_target", hardwareTargetName(info.value("hardware_target").toString())},
        {"protobuf", rpc.value("protobuf").toString()}};
}

QString backupCompat(const QJsonObject& meta, const QJsonObject& info) {
    if(meta.value("omaflip_backup").toInt() != kBackupMetaVersion)
        return "unknown_format";
    const auto origin = info.value("firmware_origin_fork").toString();
    const auto version = info.value("firmware_version").toString();
    const auto target = hardwareTargetName(info.value("hardware_target").toString());
    if(!meta.value("hardware_target").toString().isEmpty()
        && !target.isEmpty()
        && meta.value("hardware_target").toString() != target)
        return "target_mismatch";
    if(!meta.value("firmware_origin").toString().isEmpty()
        && !origin.isEmpty()
        && meta.value("firmware_origin").toString() != origin)
        return "origin_mismatch";
    if(!meta.value("firmware_version").toString().isEmpty()
        && !version.isEmpty()
        && meta.value("firmware_version").toString() != version)
        return "version_mismatch";
    return "ok";
}

bool urlAllowed(const QString& url) {
    return urlAllowedFor(url, "official");
}

bool urlAllowedFor(const QString& url, const QString& provider) {
    if(provider == "momentum" || provider == "pack")
        return httpsHost(url, "up.momentum-fw.dev");
    return httpsHost(url, "update.flipperzero.one");
}

bool channelAllowed(const QString& provider, const QString& channel) {
    if(provider == "momentum")
        return channel == "release" || channel == "development";
    return channel == "release" || channel == "release-candidate" || channel == "development";
}

QJsonObject parseFirmwareIndex(const QByteArray& json, const QString& provider, const QString& channel,
    const QString& target, QString& error) {
    const auto kind = provider == "momentum" ? QString("momentum") : QString("official");
    if(!channelAllowed(kind, channel)) {
        error = kind == "momentum"
            ? QString("OmaFlip only uses Momentum release and development channels.")
            : QString("Unsupported official firmware channel.");
        return {};
    }
    QJsonParseError parse;
    const auto doc = QJsonDocument::fromJson(json, &parse);
    if(parse.error != QJsonParseError::NoError || !doc.isObject()) {
        error = providerLabel(kind) + " firmware index was not valid JSON.";
        return {};
    }
    const auto channels = doc.object().value("channels").toArray();
    QJsonObject chosen;
    for(const auto& value : channels) {
        const auto object = value.toObject();
        if(object.value("id").toString() == channel) { chosen = object; break; }
    }
    if(chosen.isEmpty()) {
        error = "The " + providerLabel(kind) + " firmware index has no " + channel + " channel.";
        return {};
    }
    const auto versions = chosen.value("versions").toArray();
    if(versions.isEmpty()) {
        error = "The " + channel + " channel listed no firmware versions.";
        return {};
    }
    const auto latest = versions.first().toObject();
    const auto files = latest.value("files").toArray();
    QJsonObject package;
    for(const auto& value : files) {
        const auto file = value.toObject();
        if(file.value("target").toString() == target && file.value("type").toString() == "update_tgz") {
            package = file; break;
        }
    }
    if(package.isEmpty()) {
        error = "No " + providerLabel(kind) + " update package for target " + target + ".";
        return {};
    }
    const auto url = package.value("url").toString();
    if(!urlAllowedFor(url, kind)) {
        error = providerLabel(kind) + " firmware URL is not on " + indexHost(kind) + ".";
        return {};
    }
    return {{"provider", kind}, {"channel", channel}, {"title", chosen.value("title").toString()},
        {"version", latest.value("version").toString()},
        {"changelog", latest.value("changelog").toString().left(2048)},
        {"url", url}, {"sha256", package.value("sha256").toString()},
        {"target", target}, {"type", "update_tgz"}};
}

QJsonObject parseOfficialIndex(const QByteArray& json, const QString& channel, const QString& target, QString& error) {
    return parseFirmwareIndex(json, "official", channel, target, error);
}

QJsonArray parseAssetIndex(const QByteArray& json, QString& error) {
    QJsonParseError parse;
    const auto doc = QJsonDocument::fromJson(json, &parse);
    if(parse.error != QJsonParseError::NoError || !doc.isObject()) {
        error = "Momentum asset-pack index was not valid JSON.";
        return {};
    }
    QJsonArray catalog;
    for(const auto& value : doc.object().value("packs").toArray()) {
        if(catalog.size() >= 64) break;
        const auto pack = value.toObject();
        QJsonObject file;
        for(const auto& item : pack.value("files").toArray()) {
            const auto candidate = item.toObject();
            if(candidate.value("type").toString() == "pack_targz") { file = candidate; break; }
        }
        if(file.isEmpty() || !urlAllowedFor(file.value("url").toString(), "pack")) continue;
        QJsonArray folders;
        for(const auto& folder : pack.value("stats").toObject().value("folders").toArray())
            folders.append(folder.toString());
        const auto stats = pack.value("stats").toObject();
        catalog.append(QJsonObject{
            {"id", pack.value("id").toString()},
            {"name", pack.value("name").toString()},
            {"author", pack.value("author").toString()},
            {"description", pack.value("description").toString().left(240)},
            {"url", file.value("url").toString()},
            {"sha256", file.value("sha256").toString()},
            {"type", "pack_targz"},
            {"folders", folders},
            {"anims", stats.value("anims").toInt()},
            {"icons", stats.value("icons").toInt()}});
    }
    if(catalog.isEmpty()) {
        error = "The Momentum asset-pack index listed no installable tar.gz packs.";
        return {};
    }
    return catalog;
}

QString updateManifestPath(const QJsonObject& package) {
    const auto target = package.value("target").toString("f7");
    QString rest = package.value("version").toString();
    const auto name = QFileInfo(QUrl(package.value("url").toString()).path()).completeBaseName();
    const auto prefix = QString("flipper-z-%1-update-").arg(target);
    if(name.startsWith(prefix)) rest = name.mid(prefix.size());
    return QString("/ext/update/%1-update-%2/update.fuf").arg(target, rest);
}

bool verifySha256(const QByteArray& data, const QString& expected) {
    if(expected.size() != 64) return false;
    const auto hash = QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
    return QString::fromLatin1(hash).compare(expected, Qt::CaseInsensitive) == 0;
}

QString firmwareHostDir(QString& error) {
    const auto downloads = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if(downloads.isEmpty()) { error = "No Downloads directory is available."; return {}; }
    QDir dir(downloads + "/OmaFlip/firmware");
    if(!dir.exists() && !dir.mkpath(".")) { error = "Could not create the OmaFlip firmware folder."; return {}; }
    return dir.absolutePath();
}

QString assetHostDir(QString& error) {
    const auto downloads = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if(downloads.isEmpty()) { error = "No Downloads directory is available."; return {}; }
    QDir dir(downloads + "/OmaFlip/asset-packs");
    if(!dir.exists() && !dir.mkpath(".")) { error = "Could not create the OmaFlip asset-pack folder."; return {}; }
    return dir.absolutePath();
}

FirmwareClient::FirmwareClient(QObject* parent) : QObject(parent) {}

void FirmwareClient::abortReply() {
    if(!reply_) return;
    reply_->abort();
    reply_->deleteLater();
    reply_ = nullptr;
}

void FirmwareClient::check(const QString& provider, const QString& channel, const QString& target) {
    abortReply();
    const auto kind = provider == "momentum" ? QString("momentum") : QString("official");
    QNetworkRequest request{QUrl(indexUrl(kind))};
    request.setHeader(QNetworkRequest::UserAgentHeader, kUserAgent);
    reply_ = network_.get(request);
    connect(reply_, &QNetworkReply::finished, this, [this, kind, channel, target] {
        auto* reply = reply_;
        reply_ = nullptr;
        if(!reply) return;
        reply->deleteLater();
        if(reply->error() == QNetworkReply::OperationCanceledError) return;
        if(reply->error() != QNetworkReply::NoError) {
            emit failed({{"code", "firmware_index"},
                {"reason", "Could not fetch the " + providerLabel(kind) + " firmware index."},
                {"suggestion", "Check the network and retry Check " + providerLabel(kind) + " firmware."}});
            return;
        }
        QString error;
        const auto package = parseFirmwareIndex(reply->readAll(), kind, channel, target, error);
        if(package.isEmpty()) {
            emit failed({{"code", "firmware_index"}, {"reason", error},
                {"suggestion", "Retry Check " + providerLabel(kind) + " firmware."}});
            return;
        }
        emit checked(package);
    });
}

void FirmwareClient::download(const QJsonObject& package) {
    abortReply();
    const auto kind = package.value("provider").toString() == "momentum" ? QString("momentum") : QString("official");
    const auto url = package.value("url").toString();
    if(!urlAllowedFor(url, kind)) {
        emit failed({{"code", "firmware_url"},
            {"reason", providerLabel(kind) + " firmware URL is not on " + indexHost(kind) + "."},
            {"suggestion", "Retry Check " + providerLabel(kind) + " firmware."}});
        return;
    }
    pending_ = package;
    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::UserAgentHeader, kUserAgent);
    reply_ = network_.get(request);
    connect(reply_, &QNetworkReply::downloadProgress, this, &FirmwareClient::progress);
    connect(reply_, &QNetworkReply::finished, this, [this, kind] {
        auto* reply = reply_;
        reply_ = nullptr;
        if(!reply) return;
        reply->deleteLater();
        if(reply->error() == QNetworkReply::OperationCanceledError) return;
        if(reply->error() != QNetworkReply::NoError) {
            emit failed({{"code", "firmware_download"},
                {"reason", "Download of the " + providerLabel(kind) + " update package failed."},
                {"suggestion", "Retry Download " + providerLabel(kind) + " firmware."}});
            return;
        }
        const auto data = reply->readAll();
        if(!verifySha256(data, pending_.value("sha256").toString())) {
            emit failed({{"code", "firmware_hash"},
                {"reason", "SHA-256 of the downloaded package did not match the " + providerLabel(kind) + " index."},
                {"suggestion", "Retry the download. Do not install an unverified file."}});
            return;
        }
        QString dirError;
        const auto dir = firmwareHostDir(dirError);
        if(dir.isEmpty()) {
            emit failed({{"code", "firmware_save"}, {"reason", dirError},
                {"suggestion", "Check that Downloads/OmaFlip is writable."}});
            return;
        }
        auto name = fileNameFromUrl(pending_.value("url").toString());
        if(name.isEmpty()) {
            name = QString("flipper-z-%1-update-%2.tgz")
                .arg(pending_.value("target").toString(), pending_.value("version").toString());
        }
        const auto path = dir + "/" + name;
        QFile file(path);
        if(!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(data) != data.size()) {
            emit failed({{"code", "firmware_save"}, {"reason", "Could not write the verified update package."},
                {"suggestion", "Check free disk space."}});
            return;
        }
        emit downloaded(path, pending_);
    });
}

void FirmwareClient::checkPacks() {
    abortReply();
    QNetworkRequest request{QUrl(QString::fromUtf8(kMomentumAssetIndex))};
    request.setHeader(QNetworkRequest::UserAgentHeader, kUserAgent);
    reply_ = network_.get(request);
    connect(reply_, &QNetworkReply::finished, this, [this] {
        auto* reply = reply_;
        reply_ = nullptr;
        if(!reply) return;
        reply->deleteLater();
        if(reply->error() == QNetworkReply::OperationCanceledError) return;
        if(reply->error() != QNetworkReply::NoError) {
            emit failed({{"code", "pack_index"}, {"reason", "Could not fetch the Momentum asset-pack index."},
                {"suggestion", "Check the network and retry Check packs."}});
            return;
        }
        QString error;
        const auto catalog = parseAssetIndex(reply->readAll(), error);
        if(catalog.isEmpty()) {
            emit failed({{"code", "pack_index"}, {"reason", error},
                {"suggestion", "Retry Check packs."}});
            return;
        }
        emit packsChecked(catalog);
    });
}

void FirmwareClient::downloadPack(const QJsonObject& pack) {
    abortReply();
    const auto url = pack.value("url").toString();
    if(!urlAllowedFor(url, "pack")) {
        emit failed({{"code", "pack_url"}, {"reason", "Asset-pack URL is not on up.momentum-fw.dev."},
            {"suggestion", "Retry Check packs."}});
        return;
    }
    pending_ = pack;
    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::UserAgentHeader, kUserAgent);
    reply_ = network_.get(request);
    connect(reply_, &QNetworkReply::downloadProgress, this, &FirmwareClient::progress);
    connect(reply_, &QNetworkReply::finished, this, [this] {
        auto* reply = reply_;
        reply_ = nullptr;
        if(!reply) return;
        reply->deleteLater();
        if(reply->error() == QNetworkReply::OperationCanceledError) return;
        if(reply->error() != QNetworkReply::NoError) {
            emit failed({{"code", "pack_download"}, {"reason", "Download of the asset pack failed."},
                {"suggestion", "Retry Download pack."}});
            return;
        }
        const auto data = reply->readAll();
        if(!verifySha256(data, pending_.value("sha256").toString())) {
            emit failed({{"code", "pack_hash"},
                {"reason", "SHA-256 of the downloaded pack did not match the Momentum index."},
                {"suggestion", "Retry the download. Do not install an unverified file."}});
            return;
        }
        QString dirError;
        const auto dir = assetHostDir(dirError);
        if(dir.isEmpty()) {
            emit failed({{"code", "pack_save"}, {"reason", dirError},
                {"suggestion", "Check that Downloads/OmaFlip is writable."}});
            return;
        }
        auto name = pending_.value("id").toString();
        if(name.isEmpty()) name = "pack";
        const auto path = dir + "/" + name + ".tar.gz";
        QFile file(path);
        if(!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(data) != data.size()) {
            emit failed({{"code", "pack_save"}, {"reason", "Could not write the verified asset pack."},
                {"suggestion", "Check free disk space."}});
            return;
        }
        emit packDownloaded(path, pending_);
    });
}
}
