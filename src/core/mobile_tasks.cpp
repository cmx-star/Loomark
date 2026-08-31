#include "core/mobile_tasks.h"

#include "core/mobile.h"

namespace mqt::core {

std::uint64_t TaskRegistry::registerTask(const std::string& name)
{
    auto task = std::make_unique<Task>();
    task->name = name;
    tasks_[nextId_] = std::move(task);

    return nextId_++;
}

bool TaskRegistry::shouldRun(std::uint64_t taskId) const
{
    const auto it = tasks_.find(taskId);
    if (it == tasks_.end()) {
        return false;
    }
    const auto state = static_cast<State>(it->second->state.load());
    return state == State::Running;
}

void TaskRegistry::pause(std::uint64_t taskId)
{
    const auto it = tasks_.find(taskId);
    if (it != tasks_.end()) {
        it->second->state.store(static_cast<int>(State::Paused));
    }
}

void TaskRegistry::resume(std::uint64_t taskId)
{
    const auto it = tasks_.find(taskId);
    if (it != tasks_.end() &&
        static_cast<State>(it->second->state.load()) == State::Paused) {
        it->second->state.store(static_cast<int>(State::Running));
    }
}

void TaskRegistry::cancel(std::uint64_t taskId)
{
    const auto it = tasks_.find(taskId);
    if (it != tasks_.end()) {
        it->second->state.store(static_cast<int>(State::Cancelled));
    }
}

void TaskRegistry::complete(std::uint64_t taskId)
{
    const auto it = tasks_.find(taskId);
    if (it != tasks_.end()) {
        it->second->state.store(static_cast<int>(State::Completed));
    }
}

TaskRegistry::State TaskRegistry::state(std::uint64_t taskId) const
{
    const auto it = tasks_.find(taskId);
    return it != tasks_.end() ? static_cast<State>(it->second->state.load())
                              : State::Cancelled;
}

MobileExportPlan mobileExportPlan(bool isTablet)
{
    MobileExportPlan plan;
    plan.format = ExportFormat::Txt;
    plan.maxBytes = isTablet ? MobileBudgets::tabletMaxExportBytes
                             : MobileBudgets::phoneMaxExportBytes;
    return plan;
}

} // namespace mqt::core
