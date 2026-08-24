#include "gui/update_checker.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStringList>

#include <algorithm>

namespace mqt::gui {
namespace {

constexpr auto kLatestReleaseApiUrl = "https://api.github.com/repos/cmx-star/Loomark/releases/latest";
constexpr auto kReleasesUrl = "https://github.com/cmx-star/Loomark/releases";

QString normalizeVersion(QStringView version)
{
    QString normalized = version.trimmed().toString();
    if (normalized.startsWith(QLatin1Char('v'), Qt::CaseInsensitive)) {
        normalized.remove(0, 1);
    }
    const int metadataIndex = normalized.indexOf(QLatin1Char('+'));
    if (metadataIndex >= 0) {
        normalized.truncate(metadataIndex);
    }
    const int prereleaseIndex = normalized.indexOf(QLatin1Char('-'));
    if (prereleaseIndex >= 0) {
        normalized.truncate(prereleaseIndex);
    }
    return normalized;
}

QVector<int> numericVersionParts(QStringView version)
{
    const QString normalized = normalizeVersion(version);
    QVector<int> parts;
    for (const QString& part : normalized.split(QLatin1Char('.'), Qt::SkipEmptyParts)) {
        bool ok = false;
        const int value = part.toInt(&ok);
        if (!ok) {
            break;
        }
        parts.append(value);
    }
    return parts;
}

QString platformAssetSuffix()
{
#if defined(Q_OS_MACOS)
    return QStringLiteral(".dmg");
#elif defined(Q_OS_WIN)
    return QStringLiteral(".exe");
#elif defined(Q_OS_LINUX)
    return QStringLiteral(".deb");
#else
    return {};
#endif
}

} // namespace

QString currentAppVersion()
{
#ifdef MQT_APP_VERSION
    return QStringLiteral(MQT_APP_VERSION);
#else
    return QStringLiteral("0.1.0");
#endif
}

QString githubLatestReleaseApiUrl()
{
    return QString::fromLatin1(kLatestReleaseApiUrl);
}

QString githubReleasesUrl()
{
    return QString::fromLatin1(kReleasesUrl);
}

int compareReleaseVersions(QStringView left, QStringView right)
{
    const QVector<int> leftParts = numericVersionParts(left);
    const QVector<int> rightParts = numericVersionParts(right);
    const int count = std::max(leftParts.size(), rightParts.size());
    for (int index = 0; index < count; ++index) {
        const int leftValue = index < leftParts.size() ? leftParts.at(index) : 0;
        const int rightValue = index < rightParts.size() ? rightParts.at(index) : 0;
        if (leftValue < rightValue) {
            return -1;
        }
        if (leftValue > rightValue) {
            return 1;
        }
    }
    return 0;
}

ReleaseInfo parseGitHubLatestRelease(const QByteArray& payload)
{
    ReleaseInfo release;
    const QJsonDocument document = QJsonDocument::fromJson(payload);
    if (!document.isObject()) {
        return release;
    }

    const QJsonObject object = document.object();
    release.tagName = object.value(QStringLiteral("tag_name")).toString();
    release.version = normalizeVersion(release.tagName);
    release.releaseUrl = QUrl(object.value(QStringLiteral("html_url")).toString());

    const QJsonArray assets = object.value(QStringLiteral("assets")).toArray();
    release.assets.reserve(assets.size());
    for (const QJsonValue& value : assets) {
        const QJsonObject assetObject = value.toObject();
        ReleaseAsset asset;
        asset.name = assetObject.value(QStringLiteral("name")).toString();
        asset.downloadUrl = QUrl(assetObject.value(QStringLiteral("browser_download_url")).toString());
        if (!asset.name.isEmpty() && asset.downloadUrl.isValid()) {
            release.assets.append(asset);
        }
    }
    return release;
}

ReleaseAsset selectPlatformAsset(const ReleaseInfo& release)
{
    const QString suffix = platformAssetSuffix();
    if (suffix.isEmpty()) {
        return {};
    }
    for (const ReleaseAsset& asset : release.assets) {
        if (asset.name.endsWith(suffix, Qt::CaseInsensitive)) {
            return asset;
        }
    }
    return {};
}

UpdateChecker::UpdateChecker(QObject* parent)
    : QObject(parent)
{
}

void UpdateChecker::checkNow()
{
    if (checking_) {
        return;
    }
    checking_ = true;

    QNetworkRequest request{QUrl(githubLatestReleaseApiUrl())};
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Loomark/%1").arg(currentAppVersion()));
    request.setRawHeader("Accept", "application/vnd.github+json");

    QNetworkReply* reply = network_.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        checking_ = false;
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            emit checkFailed(reply->errorString());
            return;
        }

        const ReleaseInfo release = parseGitHubLatestRelease(reply->readAll());
        if (release.version.isEmpty() || !release.releaseUrl.isValid()) {
            emit checkFailed(QStringLiteral("GitHub Release 响应缺少版本或发布地址。"));
            return;
        }

        if (compareReleaseVersions(currentAppVersion(), release.version) >= 0) {
            emit alreadyUpToDate(release.version);
            return;
        }

        emit updateAvailable(release, selectPlatformAsset(release));
    });
}

} // namespace mqt::gui
