#include "gui/update_checker.h"

#include <QByteArray>

#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void testVersionComparison()
{
    require(mqt::gui::compareReleaseVersions(QStringLiteral("v0.1.13"), QStringLiteral("v0.1.14")) < 0,
        "v0.1.13 should compare lower than v0.1.14");
    require(mqt::gui::compareReleaseVersions(QStringLiteral("0.1.14"), QStringLiteral("v0.1.14")) == 0,
        "leading v should not affect version comparison");
    require(mqt::gui::compareReleaseVersions(QStringLiteral("1.10.0"), QStringLiteral("1.2.9")) > 0,
        "numeric version comparison should not be lexicographic");
    require(mqt::gui::compareReleaseVersions(QStringLiteral("1.2"), QStringLiteral("1.2.0")) == 0,
        "missing version segments should compare as zero");
}

void testReleaseParsingAndAssetSelection()
{
    const QByteArray payload = R"json({
      "tag_name": "v0.1.14",
      "html_url": "https://github.com/cmx-star/Loomark/releases/tag/v0.1.14",
      "assets": [
        {
          "name": "loomark-macos.dmg",
          "browser_download_url": "https://github.com/cmx-star/Loomark/releases/download/v0.1.14/loomark-macos.dmg"
        },
        {
          "name": "loomark-linux.deb",
          "browser_download_url": "https://github.com/cmx-star/Loomark/releases/download/v0.1.14/loomark-linux.deb"
        },
        {
          "name": "loomark-windows-setup.exe",
          "browser_download_url": "https://github.com/cmx-star/Loomark/releases/download/v0.1.14/loomark-windows-setup.exe"
        }
      ]
    })json";

    const mqt::gui::ReleaseInfo release = mqt::gui::parseGitHubLatestRelease(payload);
    require(release.tagName == QStringLiteral("v0.1.14"), "release tag should be parsed");
    require(release.version == QStringLiteral("0.1.14"), "release version should be normalized");
    require(release.releaseUrl.isValid(), "release URL should be parsed");
    require(release.assets.size() == 3, "release assets should be parsed");

    const mqt::gui::ReleaseAsset asset = mqt::gui::selectPlatformAsset(release);
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN) || defined(Q_OS_LINUX)
    require(!asset.name.isEmpty(), "current platform should select a release asset");
    require(asset.downloadUrl.isValid(), "selected asset should have a download URL");
#else
    require(asset.name.isEmpty(), "unsupported platforms should not select an asset");
#endif
}

} // namespace

int main()
{
    try {
        testVersionComparison();
        testReleaseParsingAndAssetSelection();
    } catch (const std::exception& error) {
        std::cerr << "test failure: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
