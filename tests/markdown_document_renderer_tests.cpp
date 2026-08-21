#include "gui/markdown_document_renderer.h"

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextDocument>
#include <QAbstractTextDocumentLayout>
#include <QTextFragment>
#include <QTextFrame>
#include <QTextImageFormat>
#include <QTextList>
#include <QTextListFormat>
#include <QTextTable>
#include <QUrl>
#include <QVariant>
#include <QPainter>

#include <cstdlib>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool hasFormattedText(const QTextDocument& document, const QString& needle, auto predicate)
{
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (fragment.isValid() && fragment.text().contains(needle) && predicate(fragment.charFormat())) {
                return true;
            }
        }
    }
    return false;
}

bool hasImageWithNamePrefix(const QTextDocument& document, const QString& prefix)
{
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid() || !fragment.charFormat().isImageFormat()) {
                continue;
            }
            if (fragment.charFormat().toImageFormat().name().startsWith(prefix)) {
                return true;
            }
        }
    }
    return false;
}

int imageCountWithNamePrefix(const QTextDocument& document, const QString& prefix)
{
    int count = 0;
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid() || !fragment.charFormat().isImageFormat()) {
                continue;
            }
            if (fragment.charFormat().toImageFormat().name().startsWith(prefix)) {
                ++count;
            }
        }
    }
    return count;
}

QTextImageFormat imageFormatWithNamePrefix(const QTextDocument& document, const QString& prefix)
{
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid() || !fragment.charFormat().isImageFormat()) {
                continue;
            }
            const QTextImageFormat format = fragment.charFormat().toImageFormat();
            if (format.name().startsWith(prefix)) {
                return format;
            }
        }
    }
    throw std::runtime_error("expected image resource was not rendered");
}

QVector<QTextImageFormat> imageFormatsWithNamePrefix(const QTextDocument& document, const QString& prefix)
{
    QVector<QTextImageFormat> formats;
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid() || !fragment.charFormat().isImageFormat()) {
                continue;
            }
            const QTextImageFormat format = fragment.charFormat().toImageFormat();
            if (format.name().startsWith(prefix)) {
                formats.append(format);
            }
        }
    }
    return formats;
}

QTextImageFormat imageFormatWithTooltipContaining(const QTextDocument& document, const QString& needle)
{
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid() || !fragment.charFormat().isImageFormat()) {
                continue;
            }
            const QTextImageFormat format = fragment.charFormat().toImageFormat();
            if (format.name().startsWith(QStringLiteral("mqt-math:")) && format.toolTip().contains(needle)) {
                return format;
            }
        }
    }
    throw std::runtime_error("expected math image tooltip was not rendered");
}

QImage imageResourceForFormat(const QTextDocument& document, const QTextImageFormat& format)
{
    const QVariant resource = document.resource(QTextDocument::ImageResource, QUrl(format.name()));
    if (resource.canConvert<QImage>()) {
        return resource.value<QImage>();
    }
    return {};
}

QImage imagePaintedAtDisplaySize(const QImage& source, const QTextImageFormat& format)
{
    QImage painted(
        QSize(format.width(), format.height()),
        QImage::Format_ARGB32_Premultiplied);
    painted.fill(Qt::transparent);
    QPainter painter(&painted);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(QRect(0, 0, format.width(), format.height()), source);
    painter.end();
    return painted;
}

QRect nonTransparentBounds(const QImage& image)
{
    QRect bounds;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(image.pixel(x, y)) == 0) {
                continue;
            }
            const QPoint point(x, y);
            bounds = bounds.isNull() ? QRect(point, QSize(1, 1)) : bounds.united(QRect(point, QSize(1, 1)));
        }
    }
    return bounds;
}

QTextBlock blockForText(const QTextDocument& document, const QString& needle)
{
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        if (block.text().contains(needle)) {
            return block;
        }
    }
    throw std::runtime_error("expected text was not rendered");
}

QColor firstFragmentColorForText(const QTextBlock& block, const QString& needle)
{
    for (auto it = block.begin(); !it.atEnd(); ++it) {
        const QTextFragment fragment = it.fragment();
        if (fragment.isValid() && fragment.text().contains(needle)) {
            return fragment.charFormat().foreground().color();
        }
    }
    throw std::runtime_error("expected formatted text was not rendered");
}

QTextTable* tableContainingBlock(const QTextDocument& document, const QTextBlock& block)
{
    for (QTextFrame* frame : document.rootFrame()->childFrames()) {
        auto* table = dynamic_cast<QTextTable*>(frame);
        if (table && table->cellAt(block.position()).isValid()) {
            return table;
        }
    }
    return nullptr;
}

void testCommonMarkdown()
{
    const QString markdown = QStringLiteral(
        "# Native preview\n\n"
        "A **bold** and *italic* paragraph with ~~removed~~ text, `inline()` and [Qt](https://qt.io).\n\n"
        "> quoted text\n\n"
        "- [x] done\n"
        "- [ ] pending\n\n"
        "```cpp\nint answer = 42;\n```\n\n"
        "| Name | Value |\n"
        "| :--- | ----: |\n"
        "| one | 1 |\n\n"
        "![local image](image.png)\n\n"
        "---\n");

    QTextDocument document;
    const auto result = mqt::gui::renderMarkdownDocument(document, markdown);
    require(result.success, "common Markdown should render");
    require(document.toPlainText().contains(QStringLiteral("Native preview")), "heading text should remain visible");
    require(document.toPlainText().contains(QStringLiteral("int answer = 42;")), "code text should remain visible");
    require(hasFormattedText(document, QStringLiteral("Native preview"), [](const QTextCharFormat& format) {
        return format.fontPointSize() >= 24 && format.fontWeight() >= QFont::Bold;
    }), "heading text should carry heading formatting");
    require(hasFormattedText(document, QStringLiteral("bold"), [](const QTextCharFormat& format) {
        return format.fontWeight() >= QFont::Bold;
    }), "bold text should carry bold formatting");
    require(hasFormattedText(document, QStringLiteral("italic"), [](const QTextCharFormat& format) {
        return format.fontItalic();
    }), "italic text should carry italic formatting");
    require(hasFormattedText(document, QStringLiteral("removed"), [](const QTextCharFormat& format) {
        return format.fontStrikeOut();
    }), "strikethrough text should carry strikeout formatting");
    require(hasFormattedText(document, QStringLiteral("Qt"), [](const QTextCharFormat& format) {
        return format.isAnchor() && format.anchorHref() == QStringLiteral("https://qt.io");
    }), "links should carry an anchor URL");
    require(hasFormattedText(document, QStringLiteral("int answer = 42;"), [](const QTextCharFormat& format) {
        return format.font().styleHint() == QFont::Monospace;
    }), "code blocks should use a monospace format");

    bool hasLocalImage = false;
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (fragment.isValid() && fragment.charFormat().isImageFormat()) {
                hasLocalImage = fragment.charFormat().toImageFormat().name() == QStringLiteral("image.png");
            }
        }
    }
    require(hasLocalImage, "local images should use QTextImageFormat");

    require(document.toPlainText().contains(QStringLiteral("• ☑ done")),
        "task lists should render with a visible bullet and checkbox");

    bool hasTable = false;
    for (QTextFrame* frame : document.rootFrame()->childFrames()) {
        hasTable = hasTable || dynamic_cast<QTextTable*>(frame) != nullptr;
    }
    require(hasTable, "tables should use QTextTable");

    document.setTextWidth(640);
    require(document.size().height() > 0, "rendered tables should complete QTextDocument layout");
}

void testSafeFallbacks()
{
    QTextDocument document;
    const auto result = mqt::gui::renderMarkdownDocument(
        document,
        QStringLiteral(
            "<script>alert('x')</script>\n\n"
            "Math $x^2 + y^2$ stays readable.\n\n"
            "数学源码保持可读： $x^2 + y^2$。\n\n"
            "![remote](https://example.com/a.png)\n\n"
            "[unsafe](javascript:alert('x'))\n"));

    require(result.success, "fallback Markdown should render");
    const QString text = document.toPlainText();
    require(text.contains(QStringLiteral("<script>")), "raw HTML should remain visible as text");
    require(!text.contains(QStringLiteral("$x^2 + y^2$")), "math delimiters should not be shown when MathJax renders");
    require(hasImageWithNamePrefix(document, QStringLiteral("mqt-math:")),
        "math should render through MathJax as an embedded image resource");
    require(imageCountWithNamePrefix(document, QStringLiteral("mqt-math:")) >= 2,
        "inline math followed by CJK punctuation should render through MathJax too");
    require(text.contains(QStringLiteral("。")), "CJK punctuation after inline math should remain visible");
    const QTextImageFormat mathImage = imageFormatWithNamePrefix(document, QStringLiteral("mqt-math:"));
    require(mathImage.width() > mathImage.height() * 2,
        "MathJax inline formulas should render as a complete unbroken SVG image");
    require(mathImage.height() >= 16 && mathImage.height() <= 21,
        "MathJax inline formulas should stay close to the surrounding text height");
    const QImage mathResource = imageResourceForFormat(document, mathImage);
    require(!mathResource.isNull(), "MathJax should store a rasterized image resource");
    require(mathResource.width() >= mathImage.width() * 2 && mathResource.height() >= mathImage.height() * 2,
        "MathJax should rasterize above the logical display size for crisp preview output");
    const QRect paintedBounds = nonTransparentBounds(imagePaintedAtDisplaySize(mathResource, mathImage));
    require(paintedBounds.isValid(), "MathJax painted output should contain visible pixels");
    require(paintedBounds.width() > mathImage.width() / 2,
        "MathJax painted output should not be horizontally clipped to a partial formula");
    require(paintedBounds.height() > mathImage.height() / 3,
        "MathJax painted output should not be vertically clipped to a partial formula");
    require(text.contains(QStringLiteral("remote")), "remote image should fall back to visible alt text");
    require(hasFormattedText(document, QStringLiteral("<script>"), [](const QTextCharFormat& format) {
        return format.background().style() == Qt::NoBrush && format.font().styleHint() == QFont::Monospace;
    }), "raw HTML should render as visible source text without a code strip");
    int rawHtmlBackgroundBlocks = 0;
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        if (block.text().contains(QStringLiteral("<script>"))
            && block.blockFormat().background().style() != Qt::NoBrush) {
            ++rawHtmlBackgroundBlocks;
        }
    }
    require(rawHtmlBackgroundBlocks == 0, "raw HTML blocks should not use a code block background");
    require(!hasFormattedText(document, QStringLiteral("unsafe"), [](const QTextCharFormat& format) {
        return format.isAnchor();
    }), "unsafe link schemes should not become clickable anchors");
}

void testSoftLineBreaksRemainVisible()
{
    QTextDocument document;
    const auto result = mqt::gui::renderMarkdownDocument(
        document,
        QStringLiteral(
            "# vue3-js-admin 体积优化审计报告\n\n"
            "日期： 2026-07-10\n"
            "项目： `/Users/cmx/WebstormProjects/vue3-js-admin`\n\n"
            "## 结论\n\n"
            "最终 `dist` 体积：\n"));

    require(result.success, "soft line break sample should render");
    const QString rendered = document.toPlainText();
    require(rendered.contains(QStringLiteral("日期： 2026-07-10\n")
            + QStringLiteral("项目： /Users/cmx/WebstormProjects/vue3-js-admin")),
        "soft line breaks between metadata lines should remain visible");
    require(hasFormattedText(document, QStringLiteral("dist"), [](const QTextCharFormat& format) {
        return format.background().color() == QColor(QStringLiteral("#3a414a"))
            && format.font().styleHint() == QFont::Monospace;
    }), "inline code should remain inline-code formatted after soft line breaks");
}

void testNestedUnorderedListsUseColoredBullets()
{
    QTextDocument document;
    const auto result = mqt::gui::renderMarkdownDocument(
        document,
        QStringLiteral(
            "- `src/views/network/firewall/ViewFirewallBasic.vue`\n"
            "  - 移除页面内对 `@heroicons/vue/24/outline` 的直接导入。\n"
            "  - 改为统一使用全局 `CompIcon` 和 `src/utils/icons.js` 的集中映射。\n"
            "- `src/assets/images/svg/**`\n"
            "  - 仅记录 15 个当前源码未引用 SVG，保留不删除：\n"
            "    - `mobile/nav/LAN-1.svg`\n"));

    require(result.success, "nested unordered lists should render");

    const QTextBlock top = blockForText(document, QStringLiteral("src/views/network"));
    const QTextBlock second = blockForText(document, QStringLiteral("移除页面内"));
    const QTextBlock third = blockForText(document, QStringLiteral("mobile/nav/LAN-1.svg"));

    require(top.text().startsWith(QStringLiteral("• "))
            && second.text().startsWith(QStringLiteral("• "))
            && third.text().startsWith(QStringLiteral("• ")),
        "unordered lists should keep the same solid bullet shape at every depth");
    require(firstFragmentColorForText(top, QStringLiteral("•")) == QColor(QStringLiteral("#80cbc4")),
        "first-level unordered bullets should use the primary bullet color");
    require(firstFragmentColorForText(second, QStringLiteral("•")) == QColor(QStringLiteral("#82aaff")),
        "second-level unordered bullets should use a distinct bullet color");
    require(firstFragmentColorForText(third, QStringLiteral("•")) == QColor(QStringLiteral("#c792ea")),
        "third-level unordered bullets should use a distinct bullet color");
    require(top.blockFormat().leftMargin() < second.blockFormat().leftMargin()
            && second.blockFormat().leftMargin() < third.blockFormat().leftMargin(),
        "nested unordered list indents should increase with depth");
    require(top.blockFormat().topMargin() >= 10,
        "top-level lists should leave visible space above the first item");
}

void testFencedCodeUsesSingleBlock()
{
    QTextDocument document;
    const auto result = mqt::gui::renderMarkdownDocument(
        document,
        QStringLiteral("```cpp\r\nint answer = 42;\r\n```\r\n\n```\r\n```\r\n"));

    require(result.success, "fenced code should render");

    const QColor codeBackground(QStringLiteral("#15171a"));
    QTextBlock codeBlock;
    QTextBlock syntaxBlock;
    int codeBackgroundBlocks = 0;
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        if (block.blockFormat().background().color() == codeBackground) {
            ++codeBackgroundBlocks;
        }
        if (block.text().contains(QStringLiteral("int answer = 42;"))) {
            codeBlock = block;
        }
        if (block.text().contains(QStringLiteral("cpp"))) {
            syntaxBlock = block;
        }
    }

    require(codeBlock.isValid(), "fenced code content should remain visible");
    require(syntaxBlock.isValid(), "the fenced code language should remain visible");
    require(syntaxBlock.blockFormat().alignment() == Qt::AlignRight,
        "the fenced code language should render as a right-aligned tag");
    require(hasFormattedText(document, QStringLiteral("cpp"), [](const QTextCharFormat& format) {
        return format.background().color() == QColor(QStringLiteral("#263036"))
            && format.foreground().color() == QColor(QStringLiteral("#80cbc4"));
    }), "the fenced code language tag should use a distinct small background");
    QTextTable* codePanel = tableContainingBlock(document, codeBlock);
    require(codePanel != nullptr, "fenced code should render inside a padded panel");
    require(codePanel->format().cellPadding() >= 14,
        "fenced code panels should leave padding around the code");
    require(codePanel->format().topMargin() >= 18,
        "fenced code panels should leave visible space above the code background");
    require(codePanel->format().background().color() == codeBackground,
        "fenced code panels should use one continuous panel background");
    require(codeBackgroundBlocks == 0,
        "fenced code should not leave extra paragraph background strips");
    require(hasFormattedText(document, QStringLiteral("int answer = 42;"), [](const QTextCharFormat& format) {
        return format.background().style() == Qt::NoBrush;
    }), "fenced code should not add a second character-level background strip");
}

void testDisplayMathUsesCenteredSpacedBlock()
{
    QTextDocument document;
    const auto result = mqt::gui::renderMarkdownDocument(
        document,
        QStringLiteral(
            "Before formula.\n\n"
            "$$\n"
            "\\int_0^1 x^2 dx\n"
            "$$\n\n"
            "After formula.\n"));

    require(result.success, "display math Markdown should render");

    QTextBlock mathBlock;
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (fragment.isValid()
                && fragment.charFormat().isImageFormat()
                && fragment.charFormat().toImageFormat().name().startsWith(QStringLiteral("mqt-math:"))) {
                mathBlock = block;
                break;
            }
        }
        if (mathBlock.isValid()) {
            break;
        }
    }

    require(mathBlock.isValid(), "display math should render as an embedded MathJax image");
    const QTextImageFormat displayMathImage = imageFormatWithNamePrefix(document, QStringLiteral("mqt-math:"));
    require(displayMathImage.height() >= 45 && displayMathImage.height() <= 60,
        "single-line display math should keep a readable but restrained height");
    require(mathBlock.blockFormat().topMargin() >= 20,
        "display math blocks should leave visible space above the formula");
    require(mathBlock.blockFormat().alignment() == Qt::AlignCenter,
        "display math blocks should be centered in the preview");
}

void testLatexStressMathRendersThroughMathJax()
{
    const QString markdown = QStringLiteral(
        "# LaTeX 数学公式渲染全面测试集\n\n"
        "## 1. 基础算术与上下标\n\n"
        "- **行内公式：** 欧拉公式 $e^{i\\pi} + 1 = 0$ 极其经典。\n"
        "- **简单标量：** $x_{i,j}^2 + y_1^3 \\le z_{new}$\n"
        "- **标准分数：** $\\frac{a+b}{c+d}$ 以及嵌套分数 $\\frac{1}{1 + \\frac{1}{x}}$\n"
        "- **根号与指数：** $\\sqrt{x} + \\sqrt[n]{y^2 + 1}$\n\n"
        "## 2. 经典数学方程\n\n"
        "- **二次方程求根公式：**\n"
        "  $$x = \\frac{-b \\pm \\sqrt{b^2 - 4ac}}{2a}$$\n\n"
        "- **高斯正态分布：**\n"
        "  $$f(x) = \\frac{1}{\\sigma \\sqrt{2\\pi}} e^{-\\frac{1}{2}\\left(\\frac{x-\\mu}{\\sigma}\\right)^2}$$\n\n"
        "- **傅里叶变换：**\n"
        "  $$\\hat{f}(\\xi) = \\int_{-\\infty}^{\\infty} f(x) e^{-2\\pi i x \\xi} \\,dx$$\n\n"
        "## 3. 微积分与极限\n\n"
        "- **极限：**\n"
        "  $$\\lim_{x \\to 0} \\frac{\\sin x}{x} = 1$$\n\n"
        "- **求和与求积：**\n"
        "  $$\\sum_{i=1}^{n} i = \\frac{n(n+1)}{2} \\quad \\text{和} \\quad \\prod_{i=1}^{n} \\frac{i+1}{i} = n+1$$\n\n"
        "- **定积分与重积分：**\n"
        "  $$\\int_{a}^{b} f(x) \\,dx = F(b) - F(a)$$\n"
        "  $$\\iint_{D} f(x,y) \\,dx\\,dy$$\n\n"
        "## 4. 线性代数与矩阵\n\n"
        "- **圆括号矩阵 (2x2)：**\n"
        "  $$\\begin{pmatrix} a & b \\\\ c & d \\end{pmatrix}$$\n\n"
        "- **方括号矩阵与省略号 (3x3)：**\n"
        "  $$A = \\begin{bmatrix}\n"
        "  a_{11} & a_{12} & \\cdots & a_{1n} \\\\\n"
        "  a_{21} & a_{22} & \\cdots & a_{2n} \\\\\n"
        "  \\vdots & \\vdots & \\ddots & \\vdots \\\\\n"
        "  a_{m1} & a_{m2} & \\cdots & a_{mn}\n"
        "  \\end{bmatrix}$$\n\n"
        "- **行列式：**\n"
        "  $$\\det(A) = \\begin{vmatrix} 1 & 2 \\\\ 3 & 4 \\end{vmatrix} = -2$$\n\n"
        "## 5. 分段函数与方程组\n\n"
        "- **分段函数：**\n"
        "  $$f(x) = \\begin{cases}\n"
        "  x^2 & \\text{如果 } x \\ge 0 \\\\\n"
        "  -x & \\text{如果 } x < 0\n"
        "  \\end{cases}$$\n\n"
        "- **多行等号对齐：**\n"
        "  $$\\begin{aligned}\n"
        "  (a + b)^2 &= (a + b)(a + b) \\\\\n"
        "  &= a^2 + ab + ba + b^2 \\\\\n"
        "  &= a^2 + 2ab + b^2\n"
        "  \\end{aligned}$$\n\n"
        "## 6. 特殊符号与希腊字母\n\n"
        "- **希腊字母全家桶：** $\\alpha, \\beta, \\gamma, \\delta, \\theta, \\lambda, \\mu, \\pi, \\sigma, \\omega, \\Omega, \\Delta$\n"
        "- **集合与逻辑符号：** $\\forall x \\in \\mathbb{R}, \\exists y \\subset \\mathbb{Z} \\implies x \\neq y \\cap \\emptyset$\n"
        "- **特殊字体：** $\\mathcal{L}$ (拉普拉斯), $\\mathcal{F}$ (傅里叶), $\\mathbb{C}$ (复数集), $\\mathbb{N}$ (自然数集)\n"
        "- **向量与箭头：** $\\vec{a} \\cdot \\vec{b} = 0 \\quad \\iff \\quad \\mathbf{u} \\times \\mathbf{v} = \\vec{w}$\n\n"
        "## 7. 复杂大魔王测试\n\n"
        "- **薛定谔方程：**\n"
        "  $$i\\hbar\\frac{\\partial}{\\partial t}\\Psi(\\mathbf{r},t) = \\left[ -\\frac{\\hbar^2}{2m}\\nabla^2 + V(\\mathbf{r},t) \\right]\\Psi(\\mathbf{r},t)$$\n\n"
        "- **拉马努金恒等式：**\n"
        "  $$\\frac{1}{\\pi} = \\frac{2\\sqrt{2}}{9801} \\sum_{k=0}^{\\infty} \\frac{(4k)!(1103 + 26390k)}{(k!)^4 396^{4k}}$$\n\n"
        "- **带有大括号修饰的公式：**\n"
        "  $$f(n) = \\underbrace{1 + 2 + 3 + \\cdots + n}_{\\text{共 } n \\text{ 项}} = \\frac{n(n+1)}{2}$$\n");

    QTextDocument document;
    const auto result = mqt::gui::renderMarkdownDocument(document, markdown);
    require(result.success, "LaTeX stress Markdown should render");

    const QVector<QTextImageFormat> mathImages = imageFormatsWithNamePrefix(document, QStringLiteral("mqt-math:"));
    require(mathImages.size() == 27, "all 27 LaTeX stress formulas should render as MathJax images");
    for (const QTextImageFormat& format : mathImages) {
        require(format.width() > 0 && format.height() > 0, "MathJax stress images should have a display size");
        require(format.height() >= 10 && format.height() <= 120,
            "MathJax stress images should stay inside the adaptive display height range");
        const QImage resource = imageResourceForFormat(document, format);
        require(!resource.isNull(), "MathJax stress images should store rasterized resources");
        const QRect paintedBounds = nonTransparentBounds(imagePaintedAtDisplaySize(resource, format));
        require(paintedBounds.isValid(), "MathJax stress images should paint visible pixels");
        require(paintedBounds.width() > format.width() / 3,
            "MathJax stress images should not be horizontally clipped");
        require(paintedBounds.height() > format.height() / 3,
            "MathJax stress images should not be vertically clipped");
    }

    require(imageFormatWithTooltipContaining(document, QStringLiteral("\\begin{bmatrix}")).height() >= 100,
        "multi-row matrix display math should preserve enough height to remain readable");
    require(imageFormatWithTooltipContaining(document, QStringLiteral("\\begin{aligned}")).height() >= 85,
        "aligned display math should preserve enough height to remain readable");
    require(imageFormatWithTooltipContaining(document, QStringLiteral("\\underbrace")).height() >= 60,
        "underbrace display math should preserve the annotation area");
}

void testInlineMathUsesConsistentLineBox()
{
    QTextDocument document;
    const auto result = mqt::gui::renderMarkdownDocument(
        document,
        QStringLiteral(
            "- simple $x_i^2$\n"
            "- fraction $\\frac{a+b}{c+d}$\n"
            "- root $\\sqrt[n]{y^2 + 1}$\n"
            "- greek $\\alpha, \\beta, \\Omega$\n"));

    require(result.success, "inline math line-height sample should render");
    const QVector<QTextImageFormat> mathImages = imageFormatsWithNamePrefix(document, QStringLiteral("mqt-math:"));
    require(mathImages.size() == 4, "all inline math samples should render as MathJax images");

    const int inlineHeight = mathImages.constFirst().height();
    require(inlineHeight == 20, "inline math should use a fixed line box height");
    for (const QTextImageFormat& format : mathImages) {
        require(format.height() == inlineHeight,
            "inline math formulas should not produce inconsistent line heights");
    }

    document.setTextWidth(720);
    document.documentLayout()->documentSize();
    QVector<qreal> listBlockHeights;
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        if (block.text().startsWith(QStringLiteral("• "))) {
            listBlockHeights.append(document.documentLayout()->blockBoundingRect(block).height());
        }
    }
    require(listBlockHeights.size() == 4, "the inline math sample should render four list rows");
    const qreal firstHeight = listBlockHeights.constFirst();
    for (const qreal height : listBlockHeights) {
        require(std::abs(height - firstHeight) <= 1.0,
            "inline math list rows should keep a consistent rendered row height");
    }
}

void testBlockquoteDoesNotPaintBlankQuotedLine()
{
    QTextDocument document;
    const auto result = mqt::gui::renderMarkdownDocument(
        document,
        QStringLiteral(
            "before\n\n"
            "> A block quote provides another common rendering path.\n\n"
            "| Metric | Expected |\n"
            "| --- | --- |\n"
            "| Source | Markdown |\n"));

    require(result.success, "blockquote sample should render");

    int quotedBackgroundBlocks = 0;
    int emptyQuotedBlocks = 0;
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        if (block.blockFormat().background().color() == QColor(QStringLiteral("#202428"))) {
            ++quotedBackgroundBlocks;
            if (block.text().isEmpty()) {
                ++emptyQuotedBlocks;
            }
        }
    }

    require(quotedBackgroundBlocks >= 1, "blockquote should still render with a themed background");
    require(emptyQuotedBlocks == 0, "blockquote should not paint an empty quoted line");
}

void testWidgetLayout(QApplication& app)
{
    QTextBrowser preview;
    preview.resize(720, 520);
    const auto result = mqt::gui::renderMarkdownDocument(
        *preview.document(),
        QStringLiteral(
            "# Layout\n\n"
            "| Name | Value |\n"
            "| :--- | ----: |\n"
            "| one | 1 |\n\n"
            "After table.\n"));
    require(result.success, "widget layout Markdown should render");
    preview.document()->setTextWidth(680);
    preview.show();
    app.processEvents();
    require(!preview.grab().isNull(), "QTextBrowser should complete native table layout");

    QTextTable* table = nullptr;
    for (QTextFrame* frame : preview.document()->rootFrame()->childFrames()) {
        if (auto* candidate = dynamic_cast<QTextTable*>(frame)) {
            table = candidate;
            break;
        }
    }
    require(table != nullptr, "widget layout should contain a table");
    const QTextTableFormat tableFormat = table->format();
    require(tableFormat.width().type() == QTextLength::PercentageLength
            && tableFormat.width().rawValue() == 100,
        "tables should fill the preview text width");
    require(tableFormat.cellPadding() >= 12, "tables should use readable cell padding");
    require(table->cellAt(0, 0).format().background().color() == QColor(QStringLiteral("#303640")),
        "table headers should use the themed header background");
    QTextBlock trailingBlock = preview.document()->findBlockByLineNumber(preview.document()->lineCount() - 1);
    while (trailingBlock.isValid() && !trailingBlock.text().contains(QStringLiteral("After table."))) {
        trailingBlock = trailingBlock.previous();
    }
    require(trailingBlock.isValid(), "content after a table should remain visible");
    require(!table->cellAt(trailingBlock.position()).isValid(), "content after a table must not remain inside the final cell");
    preview.hide();
}

} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    try {
        testCommonMarkdown();
        testSafeFallbacks();
        testSoftLineBreaksRemainVisible();
        testNestedUnorderedListsUseColoredBullets();
        testFencedCodeUsesSingleBlock();
        testDisplayMathUsesCenteredSpacedBlock();
        testLatexStressMathRendersThroughMathJax();
        testInlineMathUsesConsistentLineBox();
        testBlockquoteDoesNotPaintBlankQuotedLine();
        testWidgetLayout(app);
        std::cout << "markdown renderer tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "test failure: " << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
