#pragma once

#include "core/export.h"

#include <atomic>
#include <memory>
#include <cstdint>
#include <functional>
#include <map>
#include <string>

namespace mqt::core {

/// M85 前后台任务恢复：任务注册表（暂停/恢复/取消/状态）。
/// 迟到结果以代际（generation）丢弃。UI 切后台/回前台时调用相应接口。
class TaskRegistry {
public:
    enum class State { Running, Paused, Cancelled, Completed };

    std::uint64_t registerTask(const std::string& name);
    /// 查询任务是否应继续运行（暂停/取消时返回 false，任务体轮询检查）。
    [[nodiscard]] bool shouldRun(std::uint64_t taskId) const;
    void pause(std::uint64_t taskId);
    void resume(std::uint64_t taskId);
    void cancel(std::uint64_t taskId);
    void complete(std::uint64_t taskId);
    [[nodiscard]] State state(std::uint64_t taskId) const;

private:
    struct Task {
        std::string name;
        std::atomic_int state{static_cast<int>(State::Running)};
    };
    std::map<std::uint64_t, std::unique_ptr<Task>> tasks_;
    std::uint64_t nextId_ = 1;
};

/// M83/M84/M86 移动预算应用：AI 上下文与导出上限按 FormFactor 缩减
/// （预算常量在 mobile.h）。本结构把预算绑定到具体导出/AI 计划。
struct MobileExportPlan {
    std::uint64_t maxBytes = 0;
    ExportFormat format = ExportFormat::Txt;
};

[[nodiscard]] MobileExportPlan mobileExportPlan(bool isTablet);

/// M87 发布清单（商店元数据核对项）。
struct ReleaseChecklist {
    std::vector<std::string> pendingItems;
    bool privacyPolicyReady = false;
    bool permissionsDeclared = false;
    bool signedBuild = false;
    [[nodiscard]] bool allClear() const
    {
        return privacyPolicyReady && permissionsDeclared && signedBuild &&
            pendingItems.empty();
    }
};

} // namespace mqt::core
