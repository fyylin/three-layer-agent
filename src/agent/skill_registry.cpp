// =============================================================================
// src/agent/skill_registry.cpp
// =============================================================================
#include "agent/skill_registry.hpp"


#include <algorithm>
#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <dirent.h>
#endif
#include <cctype>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>
#include <stdexcept>

namespace {
static auto   mo_() { return nlohmann::json::object(); }
static void   js_(nlohmann::json& j,const char* k,const std::string& v){j[k]=nlohmann::json(v);}
static void   ji_(nlohmann::json& j,const char* k,int v){j[k]=nlohmann::json::from_int((int64_t)v);}
} // anon


namespace agent {

// ---------------------------------------------------------------------------
// CapabilityProfile
// ---------------------------------------------------------------------------
std::string CapabilityProfile::to_payload() const {
    std::ostringstream j;
    j << "{\"agent_id\":\"" << agent_id << "\","
      << "\"tasks_done\":" << tasks_done << ","
      << "\"tasks_failed\":" << tasks_failed << ","
      << "\"best_tools\":[";
    auto btools = best_tools();
    for (size_t i = 0; i < btools.size(); ++i) {
        if (i) j << ",";
        j << "\"" << btools[i] << "\"";
    }
    j << "],\"success_rates\":{";
    bool first = true;
    for (auto& [t,s] : tool_success) {
        if (!first) j << ","; first = false;
        j << "\"" << t << "\":" << tool_rate(t);
    }
    j << "}}";
    return j.str();
}

// ---------------------------------------------------------------------------
// SkillRegistry
// ---------------------------------------------------------------------------
SkillRegistry::SkillRegistry(const std::string& skills_dir)
    : skills_dir_(skills_dir) {
    if (!skills_dir_.empty()) {
        try { load_from_dir(skills_dir_); } catch (...) {}
    }
}

std::string SkillRegistry::render(const std::string& tpl,
                                   const std::map<std::string,std::string>& params) {
    std::string result = tpl;
    for (auto& [key, val] : params) {
        std::string ph = "{{" + key + "}}";
        size_t pos = 0;
        while ((pos = result.find(ph, pos)) != std::string::npos) {
            result.replace(pos, ph.size(), val);
            pos += val.size();
        }
    }
    return result;
}

float SkillRegistry::keyword_overlap(const std::string& a, const std::string& b) {
    auto tokenize = [](const std::string& s) {
        std::set<std::string> tokens;
        std::string cur;
        for (unsigned char c : s) {
            if (std::isalnum(c) || c > 127) {
                cur += (char)std::tolower(c);
            } else if (!cur.empty()) {
                if (cur.size() >= 3) tokens.insert(cur);
                cur.clear();
            }
        }
        if (cur.size() >= 3) tokens.insert(cur);
        return tokens;
    };
    auto ta = tokenize(a), tb = tokenize(b);
    if (ta.empty() || tb.empty()) return 0.0f;
    int overlap = 0;
    for (auto& t : ta) if (tb.count(t)) ++overlap;
    return (float)overlap / std::max(ta.size(), tb.size());
}

std::string SkillRegistry::invoke_skill(
        const std::string& name,
        const std::map<std::string, std::string>& params,
        ToolRegistry& tools) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = skills_.find(name);
    if (it == skills_.end())
        throw std::runtime_error("skill not found: " + name);

    const SkillDef& skill = it->second;
    std::string last_output;
    std::string last_error;

    for (size_t i = 0; i < skill.steps.size(); ++i) {
        const auto& step = skill.steps[i];
        std::string input = render(step.input_tpl, params);
        try {
            last_output = tools.invoke(step.tool, input, "skill:" + name);
            bool failed = (last_output.find("[Tool error:") != std::string::npos ||
                           last_output.find("[Tool '")      != std::string::npos);
            if (!failed) return last_output;

            last_error = last_output;
            if (step.on_fail == "next") continue;
            if (step.on_fail == "abort")
                throw std::runtime_error("skill " + name + " aborted: " + last_error);
            // "report" - fall through to return error
            return "[Skill " + name + " failed at step " + std::to_string(i+1) + "]: " + last_error;

        } catch (const std::exception& e) {
            last_error = e.what();
            if (step.on_fail == "next") continue;
            if (step.on_fail == "abort") throw;
            return "[Skill " + name + " error at step " + std::to_string(i+1) + "]: " + last_error;
        }
    }
    return "[Skill " + name + " all steps failed]: " + last_error;
}

std::optional<SkillDef> SkillRegistry::find_matching_skill(
        const std::string& task_description,
        float min_confidence) const {
    std::lock_guard<std::mutex> lk(mu_);
    SkillDef* best = nullptr;
    float best_score = -1.0f;

    for (auto& [name, skill] : skills_) {
        float kw = keyword_overlap(task_description, skill.description);
        // Weight by success rate
        float score = kw * (0.5f + 0.5f * skill.success_rate());
        if (score > best_score) { best_score = score; best = const_cast<SkillDef*>(&skill); }
    }
    if (best && best_score >= min_confidence) return *best;
    return std::nullopt;
}

std::vector<SkillDef> SkillRegistry::all_skills() const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<SkillDef> result;
    for (auto& [n,s] : skills_) result.push_back(s);
    return result;
}

void SkillRegistry::register_skill(const SkillDef& skill, bool persist) {
    std::lock_guard<std::mutex> lk(mu_);
    skills_[skill.name] = skill;
    if (persist && !skills_dir_.empty()) {
        try { save_to_dir(skills_dir_); } catch (...) {}
    }
}

std::string SkillRegistry::maybe_extract_skill(
        const std::string& task_description,
        const std::vector<std::string>& tools_used,
        const std::vector<std::string>& inputs_used,
        const std::string& created_by,
        const std::string& run_id) {
    // Only extract if multiple different tools were used (single-tool = no skill needed)
    if (tools_used.size() < 2) return "";

    // Check if we already have a similar skill
    if (find_matching_skill(task_description, 0.7f)) return "";

    // Build skill name from task keywords
    std::string name;
    for (unsigned char c : task_description) {
        if (std::isalnum(c)) name += (char)std::tolower(c);
        else if (!name.empty() && name.back() != '_') name += '_';
    }
    if (name.size() > 40) name = name.substr(0, 40);
    name += "_skill";

    SkillDef skill;
    skill.name        = name;
    skill.description = task_description.substr(0, 200);
    skill.created_by  = created_by;
    skill.run_id      = run_id;
    skill.success_count = 1;

    for (size_t i = 0; i < tools_used.size(); ++i) {
        SkillStep step;
        step.tool      = tools_used[i];
        step.input_tpl = (i < inputs_used.size()) ? inputs_used[i] : "";
        step.on_fail   = (i + 1 < tools_used.size()) ? "next" : "report";
        skill.steps.push_back(step);
    }

    register_skill(skill, true);
    return name;
}

void SkillRegistry::record_success(const std::string& skill_name) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = skills_.find(skill_name);
    if (it != skills_.end()) ++it->second.success_count;
}

void SkillRegistry::record_failure(const std::string& skill_name) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = skills_.find(skill_name);
    if (it != skills_.end()) ++it->second.fail_count;
}

// Cross-platform directory scan using std::filesystem (C++17) or fallback glob
void SkillRegistry::load_from_dir(const std::string& dir) {
    // Helper: parse one skill JSON file
    auto parse_skill_file = [&](const std::string& path) {
        try {
            std::ifstream f(path);
            if (!f.is_open()) return;
            std::string content((std::istreambuf_iterator<char>(f)),
                                 std::istreambuf_iterator<char>());
            auto j = nlohmann::json::parse(content);
            SkillDef s;
            s.name          = j.contains("name")          ? j["name"].get<std::string>()          : "";
            s.description   = j.contains("description")   ? j["description"].get<std::string>()   : "";
            s.success_count = j.contains("success_count") ? j["success_count"].get<int>()         : 0;
            s.fail_count    = j.contains("fail_count")    ? j["fail_count"].get<int>()            : 0;
            s.created_by    = j.contains("created_by")    ? j["created_by"].get<std::string>()    : "";
            s.run_id        = j.contains("run_id")        ? j["run_id"].get<std::string>()        : "";
            if (j.contains("parameters"))
                for (auto& par : j["parameters"]) s.parameters.push_back(par.get<std::string>());
            if (j.contains("steps")) {
                for (auto& st : j["steps"]) {
                    SkillStep step;
                    step.tool      = st.contains("tool")    ? st["tool"].get<std::string>()    : "";
                    step.input_tpl = st.contains("input")   ? st["input"].get<std::string>()   : "";
                    step.on_fail   = st.contains("on_fail") ? st["on_fail"].get<std::string>() : "report";
                    s.steps.push_back(step);
                }
            }
            if (!s.name.empty() && !s.steps.empty())
                skills_[s.name] = s;
        } catch (...) {}
    };

#if defined(_WIN32)
    // Windows: use FindFirstFile / FindNextFile
    std::string pattern = dir + "\\*.json";
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(pattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            parse_skill_file(dir + "\\" + fd.cFileName);
        }
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
#else
    // POSIX: use opendir/readdir
    DIR* dp = opendir(dir.c_str());
    if (!dp) return;
    struct dirent* ep;
    while ((ep = readdir(dp)) != nullptr) {
        std::string name = ep->d_name;
        if (name.size() < 6 || name.substr(name.size()-5) != ".json") continue;
        parse_skill_file(dir + "/" + name);
    }
    closedir(dp);
#endif
}

void SkillRegistry::save_to_dir(const std::string& dir) const {
    // Create dir if not exists
#ifdef _WIN32
    (void)std::system(("mkdir \"" + dir + "\" 2>nul").c_str());
#else
    (void)std::system(("mkdir -p \"" + dir + "\" 2>/dev/null").c_str());
#endif
    for (auto& [name, skill] : skills_) {
        // Build JSON using object literal to avoid int/const-char* ambiguity
        nlohmann::json steps_arr = nlohmann::json::array();
        for (auto& st : skill.steps) {
            nlohmann::json sj = mo_();
            js_(sj,"tool",    st.tool);
            js_(sj,"input",   st.input_tpl);
            js_(sj,"on_fail", st.on_fail);
            steps_arr.push_back(sj);
        }
        nlohmann::json params_arr = nlohmann::json::array();
        for (auto& par : skill.parameters) params_arr.push_back(par);

        nlohmann::json j = mo_();
        js_(j,"name",          skill.name);
        js_(j,"description",   skill.description);
        ji_(j,"success_count", skill.success_count);
        ji_(j,"fail_count",    skill.fail_count);
        js_(j,"created_by",    skill.created_by);
        js_(j,"run_id",        skill.run_id);
        j["parameters"]    = params_arr;
        j["steps"]         = steps_arr;
        std::ofstream f(dir + "/" + name + ".json");
        if (f.is_open()) f << j.dump(2) << "\n";
    }
}

void SkillRegistry::update_capability(const std::string& agent_id,
                                       const std::string& tool,
                                       bool success) {
    std::lock_guard<std::mutex> lk(mu_);
    capabilities_[agent_id].agent_id = agent_id;
    capabilities_[agent_id].record(tool, success);
}

std::optional<CapabilityProfile> SkillRegistry::get_capability(
        const std::string& agent_id) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = capabilities_.find(agent_id);
    if (it == capabilities_.end()) return std::nullopt;
    return it->second;
}

std::string SkillRegistry::best_agent_for_tool(
        const std::string& tool,
        const std::vector<std::string>& candidate_ids) const {
    std::lock_guard<std::mutex> lk(mu_);
    std::string best_id;
    float best_rate = -1.0f;
    for (auto& id : candidate_ids) {
        auto it = capabilities_.find(id);
        if (it == capabilities_.end()) continue;
        float r = it->second.tool_rate(tool);
        if (r > best_rate) { best_rate = r; best_id = id; }
    }
    return best_id;
}


// ---------------------------------------------------------------------------
// record_tool_outcome  --  EMA update of tool success rate
// ---------------------------------------------------------------------------
void SkillRegistry::record_tool_outcome(const std::string& agent_id,
                                         const std::string& tool,
                                         bool               success) noexcept {
    if (agent_id.empty() || tool.empty()) return;
    std::lock_guard<std::mutex> lk(mu_);
    auto it = capabilities_.find(agent_id);
    if (it == capabilities_.end()) return;
    it->second.record(tool, success);  // uses existing CapabilityProfile::record()
}

void SkillRegistry::record_tool_outcome(const std::string& tool,
                                         bool               success) noexcept {
    if (tool.empty()) return;
    std::lock_guard<std::mutex> lk(mu_);
    // Update all agents that have used this tool
    for (auto& [aid, cap] : capabilities_) {
        if (cap.tool_success.count(tool) || cap.tool_fail.count(tool))
            cap.record(tool, success);
    }
}

} // namespace agent
