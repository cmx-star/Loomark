#include "gui/markdown_document_renderer.h"

#include <doc.h>
#include <parser.h>

#include <QBrush>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHash>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSvgRenderer>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextFrameFormat>
#include <QTextImageFormat>
#include <QTextList>
#include <QTextListFormat>
#include <QTextLength>
#include <QTextStream>
#include <QTextTable>
#include <QTextTableCell>
#include <QTextTableFormat>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <exception>
#include <optional>

namespace mqt::gui {
namespace {

constexpr int kBasePointSize = 14;
constexpr int kMathJaxTimeoutMs = 4000;
constexpr qreal kInlineMathExPixels = 8.4;
constexpr qreal kDisplayMathExPixels = 10.0;
constexpr qreal kInlineMathMaxContentHeight = 17.0;
constexpr qreal kInlineMathBoxHeight = 20.0;
constexpr qreal kDisplayMathMaxHeight = 120.0;
constexpr qreal kMathRasterScale = 2.5;

struct MathRenderedImage {
    QImage image;
    QSize displaySize;
};

const QTextCharFormat& bodyFormat()
{
    static const QTextCharFormat format = [] {
        QTextCharFormat value;
        auto font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
        font.setPointSize(kBasePointSize);
        value.setFont(font);
        value.setForeground(QColor(QStringLiteral("#e8eaed")));
        return value;
    }();
    return format;
}

const QTextCharFormat& codeFormat()
{
    static const QTextCharFormat format = [] {
        QTextCharFormat value = bodyFormat();
        auto font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        font.setPointSize(kBasePointSize - 1);
        font.setStyleHint(QFont::Monospace);
        value.setFont(font);
        value.setForeground(QColor(QStringLiteral("#f1f3f4")));
        value.setBackground(QColor(QStringLiteral("#3a414a")));
        return value;
    }();
    return format;
}

const QTextCharFormat& codeBlockCharFormat()
{
    static const QTextCharFormat format = [] {
        QTextCharFormat value = codeFormat();
        value.clearBackground();
        return value;
    }();
    return format;
}

const QTextCharFormat& codeLanguageTagFormat()
{
    static const QTextCharFormat format = [] {
        QTextCharFormat value = codeBlockCharFormat();
        value.setForeground(QColor(QStringLiteral("#80cbc4")));
        value.setBackground(QColor(QStringLiteral("#263036")));
        value.setFontPointSize(kBasePointSize - 2);
        return value;
    }();
    return format;
}

const QTextCharFormat& sourceTextFormat()
{
    static const QTextCharFormat format = [] {
        QTextCharFormat value = bodyFormat();
        auto font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        font.setPointSize(kBasePointSize - 1);
        font.setStyleHint(QFont::Monospace);
        value.setFont(font);
        value.setForeground(QColor(QStringLiteral("#c6cbd3")));
        return value;
    }();
    return format;
}

const QTextCharFormat& mathSourceFormat()
{
    static const QTextCharFormat format = [] {
        QTextCharFormat value = bodyFormat();
        value.setForeground(QColor(QStringLiteral("#d8c4ff")));
        value.setFontItalic(true);
        return value;
    }();
    return format;
}

bool isSafeLinkUrl(const QString& rawUrl)
{
    const QUrl url(rawUrl);
    const QString scheme = url.scheme().toLower();
    return (scheme.isEmpty() && url.authority().isEmpty())
        || scheme == QStringLiteral("http")
        || scheme == QStringLiteral("https")
        || scheme == QStringLiteral("mailto")
        || scheme == QStringLiteral("file");
}

QColor unorderedBulletColorForDepth(int depth)
{
    switch ((std::max(depth, 1) - 1) % 3) {
    case 1:
        return QColor(QStringLiteral("#82aaff"));
    case 2:
        return QColor(QStringLiteral("#c792ea"));
    default:
        return QColor(QStringLiteral("#80cbc4"));
    }
}

QTextCharFormat unorderedBulletFormatForDepth(int depth)
{
    QTextCharFormat format = bodyFormat();
    format.setForeground(unorderedBulletColorForDepth(depth));
    format.setFontWeight(QFont::Bold);
    return format;
}

QHash<QString, MathRenderedImage>& mathImageCache()
{
    static QHash<QString, MathRenderedImage> cache;
    return cache;
}

QString mathJaxScriptPath()
{
    const QString configuredPath = qEnvironmentVariable("MQT_MATHJAX_SCRIPT");
    if (!configuredPath.isEmpty() && QFileInfo::exists(configuredPath)) {
        return configuredPath;
    }

    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates {
        QDir::current().absoluteFilePath(QStringLiteral("tools/mathjax_svg.mjs")),
        QDir(appDir).absoluteFilePath(QStringLiteral("../Resources/mathjax_svg.mjs")),
        QDir(appDir).absoluteFilePath(QStringLiteral("mathjax_svg.mjs")),
    };
    for (const QString& candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

QString nodeExecutablePath()
{
    const QString configuredPath = qEnvironmentVariable("MQT_NODE");
    if (!configuredPath.isEmpty() && QFileInfo::exists(configuredPath)) {
        return configuredPath;
    }

    const QString pathNode = QStandardPaths::findExecutable(QStringLiteral("node"));
    if (!pathNode.isEmpty()) {
        return pathNode;
    }

#ifdef MQT_CONFIGURED_NODE_EXECUTABLE
    const QString buildConfiguredNode = QString::fromUtf8(MQT_CONFIGURED_NODE_EXECUTABLE);
    if (!buildConfiguredNode.isEmpty() && QFileInfo::exists(buildConfiguredNode)) {
        return buildConfiguredNode;
    }
#endif

    const QStringList candidates {
        QStringLiteral("/opt/homebrew/bin/node"),
        QStringLiteral("/usr/local/bin/node"),
        QStringLiteral("/usr/bin/node"),
    };
    for (const QString& candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

bool isEscapedDollar(const QString& text, qsizetype index)
{
    qsizetype backslashes = 0;
    for (qsizetype i = index - 1; i >= 0 && text.at(i) == QLatin1Char('\\'); --i) {
        ++backslashes;
    }
    return backslashes % 2 == 1;
}

qsizetype findSingleDollar(const QString& text, qsizetype from)
{
    for (qsizetype index = from; index < text.size(); ++index) {
        if (text.at(index) != QLatin1Char('$') || isEscapedDollar(text, index)) {
            continue;
        }
        if ((index > 0 && text.at(index - 1) == QLatin1Char('$'))
            || (index + 1 < text.size() && text.at(index + 1) == QLatin1Char('$'))) {
            continue;
        }
        return index;
    }
    return -1;
}

bool looksLikeInlineMathSource(QStringView expression)
{
    const QStringView trimmed = expression.trimmed();
    if (trimmed.isEmpty()
        || trimmed.front().isSpace()
        || trimmed.back().isSpace()
        || trimmed.contains(QLatin1Char('\n'))) {
        return false;
    }

    static const QString mathSignals = QStringLiteral("\\^_{}=+-*/");
    for (const QChar ch : trimmed) {
        if (mathSignals.contains(ch)) {
            return true;
        }
    }
    return false;
}

std::optional<qreal> svgLengthToPixels(const QString& svg, const QString& attribute, qreal exPixels)
{
    const QRegularExpression pattern(attribute + QStringLiteral("\\s*=\\s*\"([0-9.]+)([a-z%]*)\""));
    const QRegularExpressionMatch match = pattern.match(svg);
    if (!match.hasMatch()) {
        return std::nullopt;
    }

    const qreal value = match.captured(1).toDouble();
    const QString unit = match.captured(2);
    if (unit == QStringLiteral("ex") || unit.isEmpty()) {
        return value * exPixels;
    }
    if (unit == QStringLiteral("em")) {
        return value * exPixels * 2.0;
    }
    if (unit == QStringLiteral("px")) {
        return value;
    }
    return std::nullopt;
}

QSizeF naturalMathImageSize(const QString& svg, const QRectF& viewBox, bool display)
{
    const qreal exPixels = display ? kDisplayMathExPixels : kInlineMathExPixels;
    const std::optional<qreal> width = svgLengthToPixels(svg, QStringLiteral("width"), exPixels);
    const std::optional<qreal> height = svgLengthToPixels(svg, QStringLiteral("height"), exPixels);
    if (width && height && *width > 0 && *height > 0) {
        return QSizeF(*width, *height);
    }
    if (viewBox.isValid() && viewBox.height() > 0) {
        const qreal fallbackHeight = display ? 42.0 : 24.0;
        const qreal widthFromRatio = fallbackHeight * viewBox.width() / viewBox.height();
        return QSizeF(widthFromRatio, fallbackHeight);
    }
    return QSizeF(display ? 84 : 42, display ? 42 : 24);
}

QSize mathDisplaySize(const QSizeF& naturalSize, bool display)
{
    QSizeF size = naturalSize;
    const qreal maxHeight = display ? kDisplayMathMaxHeight : kInlineMathMaxContentHeight;
    if (size.height() > maxHeight) {
        const qreal scale = maxHeight / size.height();
        size.setWidth(size.width() * scale);
        size.setHeight(maxHeight);
    }
    if (!display) {
        size.setHeight(kInlineMathBoxHeight);
    }
    return QSize(
        std::ceil(size.width()),
        std::ceil(size.height()));
}

QRectF mathContentRect(const QSizeF& naturalSize, const QSize& displaySize, bool display)
{
    QSizeF contentSize = naturalSize;
    const qreal maxHeight = display ? kDisplayMathMaxHeight : kInlineMathMaxContentHeight;
    if (contentSize.height() > maxHeight) {
        const qreal scale = maxHeight / contentSize.height();
        contentSize.setWidth(contentSize.width() * scale);
        contentSize.setHeight(maxHeight);
    }
    if (!display) {
        const qreal y = std::max<qreal>(0.0, (displaySize.height() - contentSize.height()) / 2.0);
        return QRectF(QPointF(0, y), contentSize);
    }
    return QRectF(QPointF(0, 0), QSizeF(displaySize));
}

std::optional<MathRenderedImage> renderMathImageWithMathJax(const QString& tex, bool display)
{
    const QString cacheKey = QStringLiteral("%1:%2").arg(display ? 1 : 0).arg(tex);
    QHash<QString, MathRenderedImage>& cache = mathImageCache();
    if (cache.contains(cacheKey)) {
        return cache.value(cacheKey);
    }

    const QString nodePath = nodeExecutablePath();
    const QString scriptPath = mathJaxScriptPath();
    if (nodePath.isEmpty() || scriptPath.isEmpty()) {
        return std::nullopt;
    }

    QJsonObject request;
    request.insert(QStringLiteral("tex"), tex);
    request.insert(QStringLiteral("display"), display);
    request.insert(QStringLiteral("color"), QStringLiteral("#d8c4ff"));

    QProcess process;
    process.setProgram(nodePath);
    process.setArguments({scriptPath});
    process.setWorkingDirectory(QFileInfo(scriptPath).absolutePath());
    process.start();
    if (!process.waitForStarted(kMathJaxTimeoutMs)) {
        return std::nullopt;
    }
    process.write(QJsonDocument(request).toJson(QJsonDocument::Compact));
    process.closeWriteChannel();
    if (!process.waitForFinished(kMathJaxTimeoutMs)) {
        process.kill();
        process.waitForFinished();
        return std::nullopt;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        return std::nullopt;
    }

    QJsonParseError parseError;
    const QJsonDocument response = QJsonDocument::fromJson(process.readAllStandardOutput(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !response.isObject()) {
        return std::nullopt;
    }
    const QString svg = response.object().value(QStringLiteral("svg")).toString();
    if (svg.isEmpty()) {
        return std::nullopt;
    }

    const QByteArray svgData = svg.toUtf8();
    QSvgRenderer renderer(svgData);
    if (!renderer.isValid()) {
        return std::nullopt;
    }

    const QSizeF naturalSize = naturalMathImageSize(svg, renderer.viewBoxF(), display);
    const QSize displaySize = mathDisplaySize(naturalSize, display);
    if (!displaySize.isValid() || displaySize.isEmpty()) {
        return std::nullopt;
    }

    const QSize rasterSize(
        std::ceil(displaySize.width() * kMathRasterScale),
        std::ceil(displaySize.height() * kMathRasterScale));
    QImage image(rasterSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.scale(kMathRasterScale, kMathRasterScale);
    renderer.render(&painter, mathContentRect(naturalSize, displaySize, display));
    painter.end();

    MathRenderedImage rendered { image, displaySize };
    cache.insert(cacheKey, rendered);
    return rendered;
}

QTextBlockFormat paragraphFormat(int quoteDepth = 0, int listDepth = 0)
{
    QTextBlockFormat format;
    format.setBottomMargin(12);
    format.setLineHeight(150, QTextBlockFormat::ProportionalHeight);
    format.setLeftMargin(quoteDepth * 18 + listDepth * 8);
    if (quoteDepth > 0) {
        format.setBackground(QColor(QStringLiteral("#202428")));
        format.setTextIndent(8);
    }
    return format;
}

QTextCharFormat applyOptions(const MD::ItemWithOpts* item, QTextCharFormat format)
{
    if (!item) {
        return format;
    }

    const int options = item->opts();
    if ((options & MD::BoldText) != 0) {
        format.setFontWeight(QFont::Bold);
    }
    if ((options & MD::ItalicText) != 0) {
        format.setFontItalic(true);
    }
    if ((options & MD::StrikethroughText) != 0) {
        format.setFontStrikeOut(true);
    }
    return format;
}

class NativeDocumentRenderer final {
public:
    NativeDocumentRenderer(QTextDocument& target, QSharedPointer<MD::Document> source)
        : target_(target)
        , cursor_(&target)
        , source_(std::move(source))
    {
    }

    void render()
    {
        target_.setDefaultFont(bodyFormat().font());
        cursor_ = QTextCursor(&target_);
        cursor_.beginEditBlock();

        if (source_) {
            renderBlocks(source_->items());
        }

        cursor_.endEditBlock();
    }

private:
    void startBlock(QTextBlockFormat blockFormat, const QTextCharFormat& charFormat = bodyFormat())
    {
        if (firstBlock_) {
            firstBlock_ = false;
        } else {
            cursor_.insertBlock();
        }
        cursor_.setBlockFormat(blockFormat);
        cursor_.setBlockCharFormat(charFormat);
        cursor_.setCharFormat(charFormat);
    }

    void renderBlocks(const MD::Block::Items& items)
    {
        for (const auto& item : items) {
            if (item) {
                renderBlock(item.get());
            }
        }
    }

    void renderBlock(MD::Item* item)
    {
        switch (item->type()) {
        case MD::ItemType::Heading:
            renderHeading(static_cast<MD::Heading*>(item));
            break;
        case MD::ItemType::Paragraph:
            renderParagraph(static_cast<MD::Paragraph*>(item));
            break;
        case MD::ItemType::Code:
            renderCode(static_cast<MD::Code*>(item));
            break;
        case MD::ItemType::Blockquote:
            renderBlockquote(static_cast<MD::Blockquote*>(item));
            break;
        case MD::ItemType::List:
            renderList(static_cast<MD::List*>(item));
            break;
        case MD::ItemType::Table:
            renderTable(static_cast<MD::Table*>(item));
            break;
        case MD::ItemType::RawHtml:
            renderRawHtml(static_cast<MD::RawHtml*>(item));
            break;
        case MD::ItemType::HorizontalLine:
            renderHorizontalLine();
            break;
        default:
            break;
        }
    }

    void renderHeading(MD::Heading* heading)
    {
        const int level = std::clamp(heading->level(), 1, 6);
        QTextBlockFormat block = paragraphFormat(quoteDepth_, listDepth_);
        block.setTopMargin(level == 1 ? 8 : 18);
        block.setBottomMargin(level == 1 ? 18 : 12);

        QTextCharFormat format = bodyFormat();
        format.setForeground(QColor(QStringLiteral("#ffffff")));
        format.setFontWeight(QFont::Bold);
        static constexpr int sizes[] = {30, 24, 19, 16, 15, 14};
        format.setFontPointSize(sizes[level - 1]);

        startBlock(block, format);
        if (heading->text()) {
            renderInlineItems(heading->text()->items(), cursor_, format);
        }
    }

    void renderParagraph(MD::Paragraph* paragraph)
    {
        if (isDisplayMathParagraph(paragraph)) {
            renderDisplayMathBlock(static_cast<MD::Math*>(paragraph->items().constFirst().get()));
            return;
        }

        const bool emptyParagraph = !paragraph || paragraph->items().isEmpty();

        QTextBlockFormat block = paragraphFormat(quoteDepth_, listDepth_);
        if (emptyParagraph && quoteDepth_ > 0) {
            block.clearBackground();
            block.setTopMargin(0);
            block.setBottomMargin(0);
            block.setLineHeight(100, QTextBlockFormat::ProportionalHeight);
        }

        startBlock(block);
        if (quoteDepth_ > 0 && !emptyParagraph) {
            QTextCharFormat quoteMarker = bodyFormat();
            quoteMarker.setForeground(QColor(QStringLiteral("#80cbc4")));
            cursor_.insertText(QStringLiteral("│ "), quoteMarker);
        }
        if (!emptyParagraph) {
            renderInlineItems(paragraph->items(), cursor_, bodyFormat());
        }
    }

    bool isDisplayMathParagraph(const MD::Paragraph* paragraph) const
    {
        if (!paragraph || paragraph->items().size() != 1 || !paragraph->items().constFirst()) {
            return false;
        }
        const MD::Item* item = paragraph->items().constFirst().get();
        return item->type() == MD::ItemType::Math && !static_cast<const MD::Math*>(item)->isInline();
    }

    void renderDisplayMathBlock(MD::Math* math)
    {
        QTextBlockFormat block = paragraphFormat(quoteDepth_, listDepth_);
        block.setTopMargin(20);
        block.setBottomMargin(18);
        block.setAlignment(Qt::AlignCenter);
        startBlock(block, bodyFormat());
        if (!insertMathImage(math->expr(), true, cursor_)) {
            QTextCharFormat format = mathSourceFormat();
            cursor_.insertText(QStringLiteral("$$") + math->expr() + QStringLiteral("$$"), format);
        }
    }

    void renderInlineItems(
        const MD::Block::Items& items,
        QTextCursor& cursor,
        const QTextCharFormat& baseFormat)
    {
        qsizetype previousLine = -1;
        for (const auto& item : items) {
            if (!item) {
                continue;
            }
            if (previousLine >= 0 && item->startLine() > previousLine && item->type() != MD::ItemType::LineBreak) {
                cursor.insertText(QString(QChar::LineSeparator), baseFormat);
            }
            renderInline(item.get(), cursor, baseFormat);
            previousLine = item->endLine();
        }
    }

    void renderInline(MD::Item* item, QTextCursor& cursor, const QTextCharFormat& baseFormat)
    {
        switch (item->type()) {
        case MD::ItemType::Text: {
            const auto* text = static_cast<MD::Text*>(item);
            renderTextWithInlineMathFallback(text->text(), cursor, applyOptions(text, baseFormat));
            break;
        }
        case MD::ItemType::Code: {
            const auto* code = static_cast<MD::Code*>(item);
            cursor.insertText(code->text(), applyOptions(code, codeFormat()));
            break;
        }
        case MD::ItemType::Math: {
            const auto* math = static_cast<MD::Math*>(item);
            if (insertMathImage(math->expr(), !math->isInline(), cursor)) {
                break;
            }
            QTextCharFormat format = applyOptions(math, mathSourceFormat());
            const QString delimiter = math->isInline() ? QStringLiteral("$") : QStringLiteral("$$");
            cursor.insertText(delimiter + math->expr() + delimiter, format);
            break;
        }
        case MD::ItemType::LineBreak:
            cursor.insertText(QString(QChar::LineSeparator), baseFormat);
            break;
        case MD::ItemType::Link:
            renderLink(static_cast<MD::Link*>(item), cursor, baseFormat);
            break;
        case MD::ItemType::Image:
            renderImage(static_cast<MD::Image*>(item), cursor, baseFormat);
            break;
        case MD::ItemType::FootnoteRef: {
            const auto* ref = static_cast<MD::FootnoteRef*>(item);
            QTextCharFormat format = applyOptions(ref, baseFormat);
            format.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
            format.setForeground(QColor(QStringLiteral("#80cbc4")));
            cursor.insertText(QStringLiteral("[%1]").arg(ref->id()), format);
            break;
        }
        case MD::ItemType::RawHtml: {
            const auto* html = static_cast<MD::RawHtml*>(item);
            QTextCharFormat format = applyOptions(html, sourceTextFormat());
            cursor.insertText(html->text(), format);
            break;
        }
        default:
            break;
        }
    }

    void renderTextWithInlineMathFallback(
        const QString& text,
        QTextCursor& cursor,
        const QTextCharFormat& format)
    {
        qsizetype position = 0;
        while (position < text.size()) {
            const qsizetype start = findSingleDollar(text, position);
            if (start < 0) {
                cursor.insertText(text.sliced(position), format);
                return;
            }

            const qsizetype end = findSingleDollar(text, start + 1);
            if (end < 0) {
                cursor.insertText(text.sliced(position), format);
                return;
            }

            const QString expression = text.sliced(start + 1, end - start - 1);
            if (!looksLikeInlineMathSource(QStringView(expression))) {
                cursor.insertText(text.sliced(position, end - position + 1), format);
                position = end + 1;
                continue;
            }

            if (start > position) {
                cursor.insertText(text.sliced(position, start - position), format);
            }
            if (!insertMathImage(expression.trimmed(), false, cursor)) {
                cursor.insertText(
                    QStringLiteral("$") + expression + QStringLiteral("$"),
                    mathSourceFormat());
            }
            position = end + 1;
        }
    }

    void renderLink(MD::Link* link, QTextCursor& cursor, const QTextCharFormat& baseFormat)
    {
        QString url = link->url();
        if (source_) {
            const auto labeled = source_->labeledLinks().find(url);
            if (labeled != source_->labeledLinks().cend()) {
                url = (*labeled)->url();
            }
        }

        QTextCharFormat format = applyOptions(link, baseFormat);
        format.setForeground(QColor(QStringLiteral("#80cbc4")));
        if (isSafeLinkUrl(url)) {
            format.setAnchor(true);
            format.setAnchorHref(url);
            format.setFontUnderline(true);
        }

        if (link->p() && !link->p()->isEmpty()) {
            renderInlineItems(link->p()->items(), cursor, format);
        } else if (link->img() && !link->img()->isEmpty()) {
            renderImage(link->img().get(), cursor, format);
        } else if (!link->text().isEmpty()) {
            cursor.insertText(link->text(), format);
        } else {
            cursor.insertText(url, format);
        }
    }

    void renderImage(MD::Image* image, QTextCursor& cursor, const QTextCharFormat& baseFormat)
    {
        const QUrl url(image->url());
        const QString scheme = url.scheme().toLower();
        const bool localResource = (scheme.isEmpty() && url.authority().isEmpty())
            || scheme == QStringLiteral("file")
            || scheme == QStringLiteral("qrc");
        if (!localResource || image->url().isEmpty()) {
            QTextCharFormat format = applyOptions(image, baseFormat);
            format.setForeground(QColor(QStringLiteral("#80cbc4")));
            const QString label = image->text().isEmpty() ? image->url() : image->text();
            cursor.insertText(QStringLiteral("[图片: %1]").arg(label), format);
            return;
        }

        QTextImageFormat format;
        format.setName(image->url());
        format.setToolTip(image->text());
        cursor.insertImage(format);
    }

    bool insertMathImage(const QString& tex, bool display, QTextCursor& cursor)
    {
        const std::optional<MathRenderedImage> rendered = renderMathImageWithMathJax(tex, display);
        if (!rendered || rendered->image.isNull() || !rendered->displaySize.isValid()) {
            return false;
        }

        const QByteArray keyBytes = (QStringLiteral("%1:%2").arg(display ? 1 : 0).arg(tex)).toUtf8();
        const QString resourceId = QString::fromLatin1(QCryptographicHash::hash(keyBytes, QCryptographicHash::Sha1).toHex());
        const QUrl resourceUrl(QStringLiteral("mqt-math:%1").arg(resourceId));
        target_.addResource(QTextDocument::ImageResource, resourceUrl, rendered->image);

        QTextImageFormat format;
        format.setName(resourceUrl.toString());
        format.setToolTip(tex);
        format.setWidth(rendered->displaySize.width());
        format.setHeight(rendered->displaySize.height());
        format.setVerticalAlignment(QTextCharFormat::AlignMiddle);
        cursor.insertImage(format);
        return true;
    }

    void renderCode(MD::Code* code)
    {
        QString text = code->text();
        text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
        while (text.endsWith(QLatin1Char('\n'))) {
            text.chop(1);
        }

        const QString syntax = code->syntax().trimmed();
        if (syntax.isEmpty() && text.isEmpty()) {
            return;
        }

        if (!firstBlock_) {
            cursor_.insertBlock(QTextBlockFormat());
        } else {
            firstBlock_ = false;
        }

        QTextTableFormat tableFormat;
        tableFormat.setBorder(0);
        tableFormat.setCellPadding(14);
        tableFormat.setCellSpacing(0);
        tableFormat.setTopMargin(18);
        tableFormat.setBottomMargin(16);
        tableFormat.setLeftMargin(quoteDepth_ * 18 + listDepth_ * 8);
        tableFormat.setRightMargin(0);
        tableFormat.setWidth(QTextLength(QTextLength::PercentageLength, 100));
        tableFormat.setBackground(QColor(QStringLiteral("#15171a")));

        QTextTable* codePanel = cursor_.insertTable(1, 1, tableFormat);
        QTextTableCell cell = codePanel->cellAt(0, 0);
        QTextCharFormat cellFormat = cell.format();
        cellFormat.setBackground(QColor(QStringLiteral("#15171a")));
        cell.setFormat(cellFormat);

        QTextCursor cellCursor = cell.firstCursorPosition();
        if (!syntax.isEmpty()) {
            QTextBlockFormat labelBlock;
            labelBlock.setAlignment(Qt::AlignRight);
            labelBlock.setBottomMargin(text.isEmpty() ? 0 : 8);
            cellCursor.setBlockFormat(labelBlock);
            cellCursor.insertText(QStringLiteral(" ") + syntax + QStringLiteral(" "), codeLanguageTagFormat());
            if (!text.isEmpty()) {
                cellCursor.insertBlock();
            }
        }

        if (!text.isEmpty()) {
            QTextBlockFormat codeBlock;
            codeBlock.setLineHeight(150, QTextBlockFormat::ProportionalHeight);
            cellCursor.setBlockFormat(codeBlock);
            text.replace(QLatin1Char('\n'), QChar::LineSeparator);
            cellCursor.insertText(text, codeBlockCharFormat());
        }

        cursor_.setPosition(std::min(codePanel->lastPosition() + 1, target_.characterCount() - 1));
    }

    void renderBlockquote(MD::Blockquote* blockquote)
    {
        ++quoteDepth_;
        renderBlocks(blockquote->items());
        --quoteDepth_;
    }

    void renderList(MD::List* list)
    {
        MD::ListItem* firstItem = nullptr;
        for (const auto& item : list->items()) {
            if (item && item->type() == MD::ItemType::ListItem) {
                firstItem = static_cast<MD::ListItem*>(item.get());
                break;
            }
        }
        if (!firstItem) {
            return;
        }

        ++listDepth_;
        const bool ordered = firstItem->listType() == MD::ListItem::Ordered;
        QTextListFormat listFormat;
        if (ordered) {
            listFormat.setIndent(listDepth_);
            listFormat.setStyle(QTextListFormat::ListDecimal);
            listFormat.setStart(firstItem->startNumber());
        }

        QTextList* textList = nullptr;
        bool firstListItem = true;
        for (const auto& child : list->items()) {
            if (!child || child->type() != MD::ItemType::ListItem) {
                continue;
            }
            auto* item = static_cast<MD::ListItem*>(child.get());
            QTextBlockFormat listBlock = paragraphFormat(quoteDepth_, 0);
            if (!ordered) {
                listBlock.setLeftMargin(quoteDepth_ * 18 + listDepth_ * 20);
                listBlock.setTextIndent(-14);
            }
            listBlock.setBottomMargin(8);
            if (firstListItem) {
                listBlock.setTopMargin(listDepth_ == 1 ? 10 : 4);
            }
            startBlock(listBlock);
            firstListItem = false;
            if (!ordered) {
                cursor_.insertText(QStringLiteral("• "), unorderedBulletFormatForDepth(listDepth_));
            } else if (!textList) {
                textList = cursor_.createList(listFormat);
            } else {
                textList->add(cursor_.block());
            }

            if (item->isTaskList()) {
                QTextCharFormat taskFormat = bodyFormat();
                taskFormat.setForeground(QColor(QStringLiteral("#80cbc4")));
                cursor_.insertText(item->isChecked() ? QStringLiteral("☑ ") : QStringLiteral("☐ "), taskFormat);
            }

            bool firstChild = true;
            for (const auto& content : item->items()) {
                if (!content) {
                    continue;
                }
                if (firstChild && content->type() == MD::ItemType::Paragraph) {
                    renderInlineItems(static_cast<MD::Paragraph*>(content.get())->items(), cursor_, bodyFormat());
                } else {
                    renderBlock(content.get());
                }
                firstChild = false;
            }
        }
        --listDepth_;
    }

    void renderTable(MD::Table* table)
    {
        if (table->isEmpty() || table->columnsCount() <= 0) {
            return;
        }

        if (!firstBlock_) {
            cursor_.insertBlock(QTextBlockFormat());
        } else {
            firstBlock_ = false;
        }

        QTextTableFormat tableFormat;
        tableFormat.setBorder(1);
        tableFormat.setBorderBrush(QColor(QStringLiteral("#464d56")));
        tableFormat.setCellPadding(12);
        tableFormat.setCellSpacing(0);
        tableFormat.setWidth(QTextLength(QTextLength::PercentageLength, 100));
        QVector<QTextLength> columnWidths;
        columnWidths.reserve(table->columnsCount());
        const qreal columnWidth = 100.0 / table->columnsCount();
        for (int column = 0; column < table->columnsCount(); ++column) {
            columnWidths.append(QTextLength(QTextLength::PercentageLength, columnWidth));
        }
        tableFormat.setColumnWidthConstraints(columnWidths);
        tableFormat.setTopMargin(12);
        tableFormat.setBottomMargin(16);

        QTextTable* textTable = cursor_.insertTable(table->rows().size(), table->columnsCount(), tableFormat);
        for (int row = 0; row < table->rows().size(); ++row) {
            const auto& sourceRow = table->rows().at(row);
            if (!sourceRow) {
                continue;
            }
            const int cellCount = std::min(table->columnsCount(), static_cast<int>(sourceRow->cells().size()));
            for (int column = 0; column < cellCount; ++column) {
                const auto& sourceCell = sourceRow->cells().at(column);
                QTextTableCell cell = textTable->cellAt(row, column);
                if (row == 0) {
                    QTextCharFormat cellFormat = cell.format();
                    cellFormat.setBackground(QColor(QStringLiteral("#303640")));
                    cell.setFormat(cellFormat);
                } else {
                    QTextCharFormat cellFormat = cell.format();
                    cellFormat.setBackground(QColor(QStringLiteral("#2a3037")));
                    cell.setFormat(cellFormat);
                }

                QTextCursor cellCursor = cell.firstCursorPosition();
                QTextBlockFormat block;
                switch (table->columnAlignment(column)) {
                case MD::Table::AlignCenter:
                    block.setAlignment(Qt::AlignCenter);
                    break;
                case MD::Table::AlignRight:
                    block.setAlignment(Qt::AlignRight);
                    break;
                case MD::Table::AlignLeft:
                    block.setAlignment(Qt::AlignLeft);
                    break;
                }
                cellCursor.setBlockFormat(block);

                QTextCharFormat format = bodyFormat();
                if (row == 0) {
                    format.setFontWeight(QFont::Bold);
                    format.setForeground(QColor(QStringLiteral("#ffffff")));
                }
                if (sourceCell) {
                    for (const auto& item : sourceCell->items()) {
                        if (!item) {
                            continue;
                        }
                        if (item->type() == MD::ItemType::Paragraph) {
                            renderInlineItems(static_cast<MD::Paragraph*>(item.get())->items(), cellCursor, format);
                        } else {
                            renderInline(item.get(), cellCursor, format);
                        }
                    }
                }
            }
        }

        cursor_.setPosition(std::min(textTable->lastPosition() + 1, target_.characterCount() - 1));
    }

    void renderRawHtml(MD::RawHtml* html)
    {
        QTextBlockFormat block = paragraphFormat(quoteDepth_, listDepth_);
        startBlock(block, sourceTextFormat());
        cursor_.insertText(html->text(), sourceTextFormat());
    }

    void renderHorizontalLine()
    {
        QTextBlockFormat block = paragraphFormat(quoteDepth_, listDepth_);
        block.setTopMargin(8);
        block.setBottomMargin(12);
        QTextCharFormat format = bodyFormat();
        format.setForeground(QColor(QStringLiteral("#565c65")));
        startBlock(block, format);
        cursor_.insertText(QStringLiteral("────────────────────────────────"), format);
    }

    QTextDocument& target_;
    QTextCursor cursor_;
    QSharedPointer<MD::Document> source_;
    bool firstBlock_ = true;
    int quoteDepth_ = 0;
    int listDepth_ = 0;
};

} // namespace

MarkdownRenderResult renderMarkdownDocument(
    QTextDocument& target,
    const QString& markdown,
    const QUrl& baseUrl)
{
    try {
        target.clear();
        target.setBaseUrl(baseUrl);
        QString source = markdown;
        QTextStream stream(&source, QIODeviceBase::ReadOnly);
        MD::Parser parser;
        const QString path = baseUrl.isLocalFile() ? baseUrl.toLocalFile() : QString();
        auto document = parser.parse(stream, path, QStringLiteral("preview.md"));
        NativeDocumentRenderer renderer(target, std::move(document));
        renderer.render();
        return {true, {}};
    } catch (const std::exception& error) {
        target.clear();
        target.setPlainText(QStringLiteral("Markdown 预览失败：%1").arg(QString::fromUtf8(error.what())));
        return {false, QString::fromUtf8(error.what())};
    } catch (...) {
        target.clear();
        target.setPlainText(QStringLiteral("Markdown 预览失败：未知错误"));
        return {false, QStringLiteral("unknown rendering error")};
    }
}

} // namespace mqt::gui
