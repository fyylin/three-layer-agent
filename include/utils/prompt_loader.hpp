#pragma once
// =============================================================================
// include/utils/prompt_loader.hpp  --  v1.0
// Loads agent prompts from a directory hierarchy of .md files.
// Each .md file has YAML frontmatter with: name, role, version, description.
// Assembles final system prompt as: base + SOUL + skills/* + AGENTS.md
//
// Fully backward-compatible: falls back to .txt files if .md not found.
// =============================================================================
#include <string>
#include <vector>
#include <map>
#include <filesystem>
#include <sstream>
#include <algorithm>
#include <mutex>
#include <optional>
#include <chrono>

#include "utils/utf8_fstream.hpp"

namespace agent {

namespace fs = std::filesystem;

inline fs::path prompt_fs_path(const std::string& utf8_path) {
    return fs::u8path(utf8_path);
}

// Frontmatter extracted from YAML header of a .md file
struct PromptMeta {
    std::string name;
    std::string role;
    std::string version  = "1.0.0";
    std::string description;
};

// One loaded prompt file (meta + body content)
struct PromptFile {
    PromptMeta  meta;
    std::string body;        // content after frontmatter
    std::string path;        // source file path
};

// ---------------------------------------------------------------------------
// PromptLoader  --  scans prompt_dir, loads and assembles prompts per agent
// ---------------------------------------------------------------------------
class PromptLoader {
public:
    explicit PromptLoader(std::string prompt_dir)
        : dir_(std::move(prompt_dir)) {}

    // Assemble a complete system prompt for a given agent role.
    // role examples: "director-decompose", "director-review", "manager-decompose",
    //                "worker-core", "supervisor-evaluate"
    //
    // Assembly order (prompt-cache friendly — stable parts first):
    //   1. base.md          (shared safety rules — most stable)
    //   2. <agent>/SOUL.md  (agent identity)
    //   3. <agent>/skills/*.md matching the role suffix
    //   4. AGENTS.md from cwd (project context — most dynamic)
    //
    // Falls back to legacy .txt files when .md files are missing.
    [[nodiscard]] std::string assemble(const std::string& role,
                                        const std::string& fallback_txt = "") const {
        std::ostringstream out;

        // 1. base.md (shared)
        auto base = load_file(dir_ + "/base.md");
        if (base) out << extract_body(*base) << "\n\n";

        // 2. Agent SOUL.md
        std::string agent_dir = agent_dir_for_role(role);
        if (!agent_dir.empty()) {
            auto soul = load_file(dir_ + "/" + agent_dir + "/SOUL.md");
            if (soul) out << extract_body(*soul) << "\n\n";
        }

        // 3. Agent-specific skill file matching role
        std::string skill_path = skill_path_for_role(role);
        if (!skill_path.empty()) {
            auto skill = load_file(dir_ + "/" + skill_path);
            if (skill) out << extract_body(*skill) << "\n\n";
        }

        // 3b. Default cross-agent skills for this specific role
        for (const auto& sp : default_skills_for_role(role)) {
            auto sk = load_file(dir_ + "/" + sp);
            if (sk) out << "---\n" << extract_body(*sk) << "\n\n";
        }

        // 4. AGENTS.md from cwd (project context)
        auto agents_cwd = load_file_from_cwd("AGENTS.md");
        if (agents_cwd) out << extract_body(*agents_cwd) << "\n\n";

        std::string result = out.str();

        // Trim trailing whitespace
        while (!result.empty() && (result.back() == '\n' || result.back() == ' '))
            result.pop_back();

        // Fallback to legacy .txt if nothing was loaded
        if (result.empty() && !fallback_txt.empty())
            return fallback_txt;

        return result;
    }

    // Load a named cross-agent skill explicitly (e.g. "code_exec", "analysis")
    // Returns empty string if not found. Agent can inject via extra context.
    [[nodiscard]] std::string load_skill(const std::string& skill_name) const {
        // Try prompts/skills/<name>.md
        for (const auto& candidate : {
            dir_ + "/skills/" + skill_name + ".md",
            dir_ + "/skills/" + skill_name,
        }) {
            auto content = load_file(candidate);
            if (content) return extract_body(*content);
        }
        return "";
    }

    // List all cross-agent skills available in prompts/skills/
    [[nodiscard]] std::vector<std::string> list_skills() const {
        std::vector<std::string> result;
        std::string skills_dir = dir_ + "/skills";
        fs::path skills_path = prompt_fs_path(skills_dir);
        if (!fs::exists(skills_path)) return result;
        for (auto& e : fs::directory_iterator(skills_path)) {
            if (e.path().extension() == ".md")
                result.push_back(e.path().stem().string());
        }
        std::sort(result.begin(), result.end());
        return result;
    }

    // List all available .md prompt files for diagnostics
    [[nodiscard]] std::vector<std::string> list_prompts() const {
        std::vector<std::string> result;
        fs::path root = prompt_fs_path(dir_);
        if (!fs::exists(root)) return result;
        for (auto& e : fs::recursive_directory_iterator(root)) {
            if (e.path().extension() == ".md")
                result.push_back(e.path().string());
        }
        std::sort(result.begin(), result.end());
        return result;
    }

    // List all prompts with parsed metadata (for --list-prompts output)
    struct PromptInfo { std::string path; PromptMeta meta; };
    [[nodiscard]] std::vector<PromptInfo> list_all_meta() const {
        std::vector<PromptInfo> result;
        for (auto& path : list_prompts()) {
            auto content = load_file(path);
            PromptInfo pi;
            pi.path = path;
            if (content) pi.meta = parse_meta(*content);
            result.push_back(std::move(pi));
        }
        return result;
    }

private:
    std::string dir_;
    mutable std::map<std::string, std::string> file_cache_; // path → content
    mutable std::mutex cache_mu_;

    // Map role → agent subdirectory
    static std::string agent_dir_for_role(const std::string& role) {
        if (role.rfind("director", 0) == 0) return "director";
        if (role.rfind("manager",  0) == 0) return "manager";
        if (role.rfind("worker",   0) == 0) return "worker";
        if (role.rfind("supervisor",0)== 0) return "supervisor";
        return "";
    }

    // Map role → agent-specific skill .md file
    static std::string skill_path_for_role(const std::string& role) {
        static const std::map<std::string, std::string> kSkillMap = {
            {"director-decompose",      "director/skills/decompose.md"},
            {"director-review",         "director/skills/review.md"},
            {"director-synthesise",     "director/skills/synthesise.md"},
            {"director-classify",       "director/skills/classify.md"},
            {"director-conversational", "director/skills/conversational.md"},
            {"manager-decompose",       "manager/skills/decompose.md"},
            {"manager-validate",        "manager/skills/validate.md"},
            {"worker-core",             "worker/skills/tool_execution.md"},
            {"worker-file",             "worker/skills/file_operations.md"},
            {"worker-system",           "worker/skills/system_query.md"},
            {"supervisor-evaluate",     "supervisor/skills/evaluate.md"},
        };
        auto it = kSkillMap.find(role);
        return (it != kSkillMap.end()) ? it->second : "";
    }

    // Default cross-agent skills injected per ROLE (not per agent type)
    // Fine-grained: worker-core gets both, worker-file gets only file_ops, etc.
    static std::vector<std::string> default_skills_for_role(const std::string& role) {
        if (role == "worker-core")
            return {"skills/file_ops.md", "skills/system_ops.md"};
        if (role == "worker-file")
            return {"skills/file_ops.md"};
        if (role == "worker-system")
            return {"skills/system_ops.md"};
        if (role == "director-synthesise")
            return {"skills/analysis.md"};
        if (role == "director-decompose")
            return {};  // keep decompose prompt tight
        return {};
    }

    // Load a file, return nullopt if missing
    std::optional<std::string> load_file(const std::string& path) const {
        // Check cache first
        {
            std::lock_guard<std::mutex> lk(cache_mu_);
            auto it = file_cache_.find(path);
            if (it != file_cache_.end()) return it->second;
        }
        agent::utf8_ifstream f(path);
        if (!f.is_open()) return std::nullopt;
        std::ostringstream ss;
        ss << f.rdbuf();
        std::string content = ss.str();
        {
            std::lock_guard<std::mutex> lk(cache_mu_);
            file_cache_[path] = content;
        }
        return content;
    }

    // Try to load AGENTS.md from cwd
    std::optional<std::string> load_file_from_cwd(const std::string& name) const {
        // Check cwd first, then up one level
        for (auto& candidate : {"./" + name, "../" + name}) {
            agent::utf8_ifstream f(candidate);
            if (f.is_open()) {
                std::ostringstream ss; ss << f.rdbuf();
                return ss.str();
            }
        }
        return std::nullopt;
    }

    // Parse YAML frontmatter and return meta (used for --list-prompts)
    static PromptMeta parse_meta(const std::string& content) {
        PromptMeta m;
        if (content.size() < 3 || content.substr(0, 3) != "---") return m;
        auto end = content.find("\n---", 3);
        if (end == std::string::npos) return m;
        std::string fm = content.substr(4, end - 4);
        for (auto& line : [&]() {
            std::vector<std::string> v; std::istringstream ss(fm); std::string l;
            while (std::getline(ss, l)) v.push_back(l); return v; }()) {
            auto colon = line.find(": ");
            if (colon == std::string::npos) continue;
            std::string key = line.substr(0, colon);
            std::string val = line.substr(colon + 2);
            if (!val.empty() && val.front() == '"') { val = val.substr(1); }
            if (!val.empty() && val.back()  == '"') { val.pop_back(); }
            if (key == "name")        m.name        = val;
            else if (key == "role")   m.role        = val;
            else if (key == "version")m.version     = val;
            else if (key == "description") m.description = val;
        }
        return m;
    }

    // Strip YAML frontmatter (--- ... ---) and return body
    std::string extract_body(const std::string& content) const {
        if (content.size() >= 3 && content.substr(0, 3) == "---") {
            auto end = content.find("\n---", 3);
            if (end != std::string::npos) {
                size_t body_start = end + 4;
                while (body_start < content.size() &&
                       (content[body_start] == '\n' || content[body_start] == '\r'))
                    ++body_start;
                return content.substr(body_start);
            }
        }
        return content;
    }
};

// Convenience: build prompts for each agent role used in main.cpp
// Returns assembled prompt; falls back to legacy string if .md files missing.
inline std::string load_agent_prompt(const PromptLoader& pl,
                                      const std::string& role,
                                      const std::string& fallback = "") {
    return pl.assemble(role, fallback);
}

} // namespace agent
