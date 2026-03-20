#pragma once
// =============================================================================
// include/setup.hpp   --   Interactive configuration wizard
// =============================================================================
#include "agent/models.hpp"
#include <string>

namespace setup {

// Run the interactive wizard.
// If existing_config_path is non-empty and the file exists, its values are
// used as defaults (upgrade / re-configure flow).
// Returns the fully populated AgentConfig ready to save.
[[nodiscard]] agent::AgentConfig run_wizard(
    const std::string& existing_config_path = "");

} // namespace setup
