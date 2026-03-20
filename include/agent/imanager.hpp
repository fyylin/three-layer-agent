#pragma once
// =============================================================================
// include/agent/imanager.hpp
// Interface for Manager agents (extracted to break circular dependency)
// =============================================================================
#include "agent/models.hpp"
#include <functional>
#include <memory>
#include <string>

namespace agent {

struct IManager {
    virtual ~IManager() = default;
    virtual SubTaskReport process(const SubTask& task) = 0;
    [[nodiscard]] virtual const std::string& id() const noexcept = 0;
};

using ManagerFactory = std::function<std::unique_ptr<IManager>(const SubTask&)>;

} // namespace agent
