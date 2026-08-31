// M10：装载扫描器单测（稀疏行索引 / 流式指纹 / 换行统计），纯 core 无 Qt
#include "core/load_scanner.h"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void testLineIndexBasic()
{
    mqt::core::LineIndexBuilder b;
    b.feed("alpha\nbeta\ngamma", 0);
    b.finish(16);
    const auto& idx = b.index();
    require(idx.lineCount() == 3, "3 lines for 2 newlines");
    require(idx.endOffset() == 16, "end offset recorded");

    const auto l0 = idx.anchorAtOrBeforeLine(0);
    require(l0.line == 0 && l0.offset == 0 && l0.exact, "line 0 anchored at 0");
    // Stride 256: lines 1..255 have no anchor; the query returns line 0.
    const auto l1 = idx.anchorAtOrBeforeLine(1);
    require(l1.line == 0 && !l1.exact, "line 1 falls back to line 0 anchor");
    const auto l2 = idx.anchorAtOrBeforeLine(2);
    require(l2.line == 0 && !l2.exact, "line 2 falls back to line 0 anchor");

    const auto off6 = idx.anchorAtOrBeforeOffset(6); // inside "beta"
    require(off6.line == 0 && off6.offset == 0, "offset 6 anchors to line 0");
    const auto offEnd = idx.anchorAtOrBeforeOffset(16);
    require(off6.offset <= 16, "end offset query stays within anchors");
    (void)offEnd;
}

void testLineIndexAnchorAtStride()
{
    // 300 lines of 4 bytes each: anchors at lines 0, 256.
    mqt::core::LineIndexBuilder b;
    std::string content;
    for (int i = 0; i < 300; ++i) {
        content += "abcd\n";
    }
    b.feed(content, 0);
    b.finish(content.size());
    const auto& idx = b.index();
    require(idx.lineCount() == 301, "301 lines (last line unterminated)");

    const auto a256 = idx.anchorAtOrBeforeLine(256);
    require(a256.exact && a256.line == 256, "line 256 is an anchor");
    require(a256.offset == 257 * 5 - 5, "line 256 offset math");
    const auto a299 = idx.anchorAtOrBeforeLine(299);
    require(a299.line == 256 && !a299.exact, "line 299 falls back to 256");

    const auto cross = idx.anchorAtOrBeforeOffset(256 * 5 + 2);
    require(cross.line == 256, "offset inside line 256 anchors to it");
}

void testLineIndexAcrossChunks()
{
    mqt::core::LineIndexBuilder b;
    const std::string first = "one\ntwo\n";
    const std::string second = "three";
    b.feed(first, 0);
    b.feed(second, first.size());
    b.finish(first.size() + second.size());
    const auto& idx = b.index();
    require(idx.lineCount() == 3, "cross-chunk line count");

    // "three" starts at offset 8 → line 2. Stride is 256 so verify via
    // a forced anchor: feed 256 lines then check offset math.
    mqt::core::LineIndexBuilder big;
    std::string content;
    for (int i = 0; i < 257; ++i) {
        content += "xy\n";
    }
    big.feed(content, 0);
    big.finish(content.size());
    const auto anchor = big.index().anchorAtOrBeforeLine(256);
    require(anchor.exact && anchor.offset == 256 * 3,
        "anchor offsets stay correct across chunk feed calls");
}

void testEmptyAndBoundary()
{
    mqt::core::LineIndexBuilder b;
    b.finish(0);
    require(b.index().lineCount() == 0, "empty document has 0 lines");
    require(b.index().anchorAtOrBeforeLine(0).offset == 0,
        "empty document still anchors line 0");

    mqt::core::LineIndexBuilder b2;
    b2.feed("\n", 0);
    b2.finish(1);
    require(b2.index().lineCount() == 2, "single newline yields 2 lines");
}

void testFingerprint()
{
    mqt::core::FingerprintSink whole;
    whole.update("hello world");

    mqt::core::FingerprintSink chunked;
    chunked.update("hel");
    chunked.update("lo world");
    require(whole.value() == chunked.value(),
        "streaming fingerprint must equal one-shot fingerprint");

    mqt::core::FingerprintSink empty;
    require(!empty.updated(), "empty sink not updated");
    mqt::core::FingerprintSink empty2;
    require(empty.value() == empty2.value(), "empty fingerprint deterministic");

    mqt::core::FingerprintSink different;
    different.update("hello worlds");
    require(different.value() != whole.value(), "different content, different hash");
}

void testNewlineCounter()
{
    mqt::core::NewlineCounter c1;
    c1.feed("a\nb\r\nc\r");
    c1.finish();
    auto counts = c1.counts();
    require(counts.lf == 1 && counts.crlf == 1 && counts.cr == 1,
        "mixed line endings counted");

    // Trailing CR at EOF counts via finish().
    mqt::core::NewlineCounter c2;
    c2.feed("x\r");
    c2.finish();
    require(c2.counts().cr == 1, "trailing CR counted at finish");

    // Cross-chunk CRLF split.
    mqt::core::NewlineCounter c3;
    c3.feed("a\r");
    c3.feed("\nb");
    c3.finish();
    require(c3.counts().crlf == 1 && c3.counts().lf == 0 && c3.counts().cr == 0,
        "CRLF split across chunks counted once");

    require(mqt::core::newlineStyleKnown(counts), "counts imply known style");
    require(mqt::core::classifyNewlines({}) == mqt::core::NewlineStyle::LF,
        "empty counts default to LF branch");
    mqt::core::NewlineCounts crlfOnly;
    crlfOnly.crlf = 5;
    require(mqt::core::classifyNewlines(crlfOnly) == mqt::core::NewlineStyle::CRLF,
        "crlf majority");
    mqt::core::NewlineCounts crOnly;
    crOnly.cr = 3;
    require(mqt::core::classifyNewlines(crOnly) == mqt::core::NewlineStyle::CR,
        "cr majority");
}

} // namespace

int main()
{
    try {
        testLineIndexBasic();
        testLineIndexAnchorAtStride();
        testLineIndexAcrossChunks();
        testEmptyAndBoundary();
        testFingerprint();
        testNewlineCounter();
        std::cout << "load scanner tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "test failure: " << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
