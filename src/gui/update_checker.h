#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVector>

namespace mqt::gui {

struct ReleaseAsset {
    QString name;
    QUrl downloadUrl;
};

struct ReleaseInfo {
    QString tagName;
    QString version;
    QUrl releaseUrl;
    QVector<ReleaseAsset> assets;
};

QString currentAppVersion();
QString githubLatestReleaseApiUrl();
QString githubReleasesUrl();
int compareReleaseVersions(QStringView left, QStringView right);
ReleaseInfo parseGitHubLatestRelease(const QByteArray& payload);
ReleaseAsset selectPlatformAsset(const ReleaseInfo& release);

class UpdateChecker final : public QObject {
    Q_OBJECT

public:
    explicit UpdateChecker(QObject* parent = nullptr);

    void checkNow();

signals:
    void updateAvailable(const ReleaseInfo& release, const ReleaseAsset& asset);
    void alreadyUpToDate(const QString& latestVersion);
    void checkFailed(const QString& message);

private:
    QNetworkAccessManager network_;
    bool checking_ = false;
};

} // namespace mqt::gui
