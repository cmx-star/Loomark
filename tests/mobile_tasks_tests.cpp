// 批次 19：任务恢复/移动预算/发布清单测试
#include "core/mobile_tasks.h"

#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void testTaskRegistry()
{
    mqt::core::TaskRegistry registry;
    const auto id = registry.registerTask("indexing");
    require(registry.shouldRun(id), "task runs by default");
    registry.pause(id);
    require(!registry.shouldRun(id), "paused task must not run");
    registry.resume(id);
    require(registry.shouldRun(id), "resumed task runs again");
    registry.cancel(id);
    require(!registry.shouldRun(id), "cancelled task must not run");
    require(registry.state(id) == mqt::core::TaskRegistry::State::Cancelled,
        "cancelled state reported");
    registry.complete(id);
    require(registry.state(id) == mqt::core::TaskRegistry::State::Completed,
        "completed state reported");
    require(!registry.shouldRun(999), "unknown task never runs");
}

void testMobilePlans()
{
    const auto phone = mqt::core::mobileExportPlan(false);
    const auto tablet = mqt::core::mobileExportPlan(true);
    require(phone.maxBytes < tablet.maxBytes, "phone export budget smaller");
    require(phone.format == mqt::core::ExportFormat::Txt, "mobile defaults to txt");
}

void testReleaseChecklist()
{
    mqt::core::ReleaseChecklist checklist;
    checklist.pendingItems = {"privacy: 采集说明"};
    require(!checklist.allClear(), "incomplete checklist blocks release");
    checklist.pendingItems.clear();
    checklist.privacyPolicyReady = true;
    checklist.permissionsDeclared = true;
    checklist.signedBuild = true;
    require(checklist.allClear(), "complete checklist allows release");
}

} // namespace

int main()
{
    try {
        testTaskRegistry();
        testMobilePlans();
        testReleaseChecklist();
        std::cout << "mobile tasks tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "test failure: " << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
