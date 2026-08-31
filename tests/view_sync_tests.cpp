// 批次 8：模式状态机 / 分屏同步 / Diff 测试
#include "core/view_sync.h"

#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void testModeStateMachine()
{
    mqt::core::ModeStateMachine sm;
    require(sm.mode() == mqt::core::ViewMode::Code, "initial mode is Code");
    require(sm.transitionTo(mqt::core::ViewMode::Split), "Code → Split allowed");
    require(sm.mode() == mqt::core::ViewMode::Split, "mode updated");
    require(!sm.transitionTo(mqt::core::ViewMode::Split), "same-mode transition rejected");
    require(sm.transitionTo(mqt::core::ViewMode::Diff), "Split → Diff allowed");
    require(sm.transitionTo(mqt::core::ViewMode::ParallelPreview), "Diff → Parallel allowed");
    require(sm.transitionTo(mqt::core::ViewMode::Code), "back to Code allowed");
}

void testScrollSync()
{
    mqt::core::ScrollSync sync;
    const auto anchor = mqt::core::ScrollSync::anchorFor(500, 1000, "# A");
    require(anchor.sourceOffset == 500 && anchor.ratio > 0.49 && anchor.ratio < 0.51,
        "anchor computed");
    require(sync.applyAnchor(anchor), "anchor accepted when not syncing");
    sync.setSyncing(true);
    require(!sync.applyAnchor(anchor), "anchor rejected while syncing (loop suppression)");
    sync.setSyncing(false);
    require(sync.applyAnchor(anchor), "anchor accepted after sync ends");
    require(sync.lastAppliedOffset() == 500, "last applied offset recorded");
}

void testDiff()
{
    const std::string baseline = "line1\nline2\nline3\nline4\n";
    const std::string current = "line1\nCHANGED\nline3\nline4\nline5\n";
    mqt::core::DiffModel diff;
    diff.compute(baseline, current);
    require(diff.hunks().size() == 2, "change + append produce two hunks");
    require(diff.hunks()[0].oldStart == 1 && diff.hunks()[0].oldLines == 1,
        "first hunk covers old line 2");
    require(diff.hunks()[0].newLines == 1 && diff.hunks()[1].oldLines == 0,
        "second hunk is a pure insertion");
    require(diff.valid(), "diff valid after compute");

    diff.invalidate(99);
    require(diff.hunks().empty(), "invalidate clears hunks");
    require(diff.baselineVersion() == 99, "new baseline version recorded");

    mqt::core::DiffModel same;
    same.compute(baseline, baseline);
    require(same.hunks().empty(), "identical content has no hunks");
}

} // namespace

int main()
{
    try {
        testModeStateMachine();
        testScrollSync();
        testDiff();
        std::cout << "view sync tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "test failure: " << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
