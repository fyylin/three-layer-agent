// =============================================================================
// src/agent/advisor_agent.cpp
// =============================================================================
#include "agent/advisor_agent.hpp"
#include "utils/logger.hpp"
#include "utils/json_utils.hpp"
#include <nlohmann/json.hpp>
#include <sstream>

namespace agent {

static const char* kLayer = "Advisor";

std::string AdvisorAgent::default_prompt() {
    return
        "You are a diagnostic advisor for a multi-layer AI agent system.\n"
        "When tasks fail repeatedly, you analyze the root cause and suggest concrete fixes.\n\n"
        "CRITICAL OUTPUT RULE:\n"
        "Respond with ONLY this JSON object:\n"
        "{\n"
        "  \"should_retry\": true,\n"
        "  \"root_cause\": \"<one sentence: what fundamentally went wrong>\",\n"
        "  \"strategies\": [\n"
        "    \"<strategy 1: specific and actionable>\",\n"
        "    \"<strategy 2: alternative approach>\"\n"
        "  ],\n"
        "  \"recommended\": \"<which strategy to use and why>\",\n"
        "  \"refined_goal\": \"<rewritten goal with explicit constraints that prevent the same failure>\"\n"
        "}\n\n"
        "Set should_retry=false ONLY if the task is fundamentally impossible "
        "(e.g. requires hardware access, the user explicitly asked for something unavailable).\n\n"
        "ANALYSIS PRINCIPLES:\n"
        "- If a tool call failed with a bad path: suggest using the correct alias or get_env first\n"
        "- If the LLM generated placeholder text like <HOME>: suggest hardcoding the known alias\n"
        "- If tasks were split when they should have been one step: suggest single-step approach\n"
        "- If the error is a network/API issue: suggest retry with same goal\n"
        "- Be specific: name the tool, the correct input, the exact fix\n";
}

AdvisorAgent::AdvisorAgent(std::string id, ApiClient& client, std::string system_prompt)
    : id_(std::move(id))
    , client_(client)
    , system_prompt_(system_prompt.empty() ? default_prompt() : std::move(system_prompt))
{}

AdviceResult AdvisorAgent::advise(
        const std::string&              goal,
        const std::vector<std::string>& error_history,
        const std::string&              context) const noexcept {
    AdviceResult result;
    result.should_retry  = true;
    result.refined_goal  = goal;  // default: unchanged

    try {
        std::ostringstream user_msg;
        user_msg << "## Original goal\n" << goal << "\n\n"
                 << "## Failure history (" << error_history.size() << " attempts)\n";
        for (size_t i = 0; i < error_history.size(); ++i)
            user_msg << "Attempt " << (i+1) << ": " << error_history[i] << "\n";

        if (!context.empty())
            user_msg << "\n## System context\n" << context << "\n";

        user_msg << "\nAnalyze the failures and provide a fix strategy.";

        LOG_INFO(kLayer, id_, "advice", "analyzing " +
            std::to_string(error_history.size()) + " failures for: " +
            goal.substr(0, 60));

        std::string llm_out = client_.complete(system_prompt_, user_msg.str(), "advice");
        nlohmann::json j    = parse_llm_json(llm_out);

        if (j.contains("should_retry"))
            j["should_retry"].get_to(result.should_retry);
        if (j.contains("root_cause"))
            j["root_cause"].get_to(result.root_cause);
        if (j.contains("strategies") && j["strategies"].is_array())
            for (auto& s : j["strategies"])
                result.strategies.push_back(s.get<std::string>());
        if (j.contains("recommended"))
            j["recommended"].get_to(result.recommended);
        if (j.contains("refined_goal") && !j["refined_goal"].get<std::string>().empty())
            j["refined_goal"].get_to(result.refined_goal);

        LOG_INFO(kLayer, id_, "advice",
            "root cause: " + result.root_cause.substr(0, 100));
        LOG_INFO(kLayer, id_, "advice",
            "recommended: " + result.recommended.substr(0, 100));

    } catch (const std::exception& e) {
        LOG_WARN(kLayer, id_, "advice",
            std::string("advisor LLM call failed: ") + e.what() +
            "  --  using original goal");
        result.root_cause = std::string("Advisor analysis failed: ") + e.what();
        result.strategies = {"Retry with original goal unchanged"};
        result.recommended= "Retry as-is";
    }

    return result;
}

} // namespace agent
