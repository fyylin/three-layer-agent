// =============================================================================
// src/utils/workspace.cpp
// =============================================================================
#include "utils/workspace.hpp"
#include "utils/file_lock.hpp"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <nlohmann/json.hpp>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <dirent.h>
#  include <sys/stat.h>
#  include <unistd.h>
#endif

namespace agent {

// -- Path utils ----------------------------------------------------------------

std::string WorkspaceManager::join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    char last = a.back();
    if (last == '/' || last == '\\') return a + b;
    return a + "/" + b;
}

void WorkspaceManager::mkdirs(const std::string& path) {
#ifdef _WIN32
    std::string p = path;
    for (auto& c : p) if (c == '/') c = '\\';
    // Walk each component
    for (size_t i = 1; i <= p.size(); ++i) {
        bool at_sep  = (i < p.size() && (p[i]=='\\'));
        bool at_end  = (i == p.size());
        if (at_sep || at_end) {
            std::string sub = p.substr(0, i);
            // Skip drive root like "C:"
            if (sub.size() == 2 && sub[1] == ':') continue;
            CreateDirectoryA(sub.c_str(), nullptr);
        }
    }
    CreateDirectoryA(p.c_str(), nullptr);
#else
    for (size_t i = 1; i <= path.size(); ++i) {
        if (i == path.size() || path[i] == '/') {
            std::string sub = path.substr(0, i);
            if (!sub.empty()) ::mkdir(sub.c_str(), 0755);
        }
    }
    ::mkdir(path.c_str(), 0755);
#endif
}

// -- WorkspacePaths ------------------------------------------------------------

std::string WorkspacePaths::agent_dir(const std::string& agent_id) const {
    std::string sub;
    if      (agent_id.find("dir-") == 0) sub = "director";
    else if (agent_id.find("sup-") == 0) sub = "supervisor";
    else                                  sub = agent_id;
    return WorkspaceManager::join(run_root, sub);
}

std::string WorkspacePaths::artifact_dir(const std::string& agent_id) const {
    return WorkspaceManager::join(agent_dir(agent_id), "artifacts");
}

// -- Init ----------------------------------------------------------------------

WorkspacePaths WorkspaceManager::init(const std::string& root,
                                       const std::string& run_id) {
    WorkspacePaths wp;
    // Conversation directory (one per conversation, not per message)
    wp.conv_root       = join(root, "conversations/" + run_id);
    wp.conv_md         = join(wp.conv_root, "CONVERSATION.md");
    wp.conv_memory_md  = join(wp.conv_root, "MEMORY.md");
    wp.conv_runs_md    = join(wp.conv_root, "runs.md");
    wp.conv_exp_md     = join(wp.conv_root, "experience.md");
    // Persistent directories (shared across ALL conversations)
    wp.current_dir     = join(root, "current");
    wp.files_dir       = join(wp.current_dir, "files");
    wp.shared_dir      = join(wp.current_dir, "shared");
    wp.memory_dir      = join(wp.current_dir, "memory");
    wp.experience_md   = join(wp.memory_dir, "EXPERIENCE.md");
    wp.workspace_md    = join(wp.current_dir, "WORKSPACE.md");
    wp.env_knowledge_md= join(wp.current_dir, "env_knowledge.md");
    // Append logs (shared across all conversations)
    std::string logs_dir = join(root, "logs");
    wp.activity_md     = join(logs_dir, "activity.md");
    wp.structured_log  = join(logs_dir, "structured.ndjson");
    // Legacy compat aliases
    wp.run_root        = wp.conv_root;
    wp.global_log      = wp.activity_md;
    wp.state_json      = join(wp.conv_root, "state.md");
    wp.result_json     = join(wp.conv_root, "result.md");             // persistent across runs

    mkdirs(wp.conv_root);
    mkdirs(wp.current_dir);
    mkdirs(wp.files_dir);
    mkdirs(wp.shared_dir);
    mkdirs(wp.memory_dir);
    mkdirs(join(wp.memory_dir, "long_term"));
    mkdirs(join(wp.memory_dir, "skills_promoted"));
    mkdirs(join(root, "logs"));
    mkdirs(join(root, "conversations"));

    // Initialise empty state.json
    std::ofstream f(wp.state_json);
    if (f.is_open()) f << "{}\n";

    return wp;
}

void WorkspaceManager::init_agent_dir(const WorkspacePaths& wp,
                                       const std::string& agent_id) {
    mkdirs(wp.agent_dir(agent_id));
    if (agent_id.find("wkr-") == 0)
        mkdirs(wp.artifact_dir(agent_id));
}

// -- State update -------------------------------------------------------------
// Merge one agent's state fragment into state.json (JSON object keyed by agent_id)

void WorkspaceManager::write_state(const WorkspacePaths& wp,
                                    const std::string& agent_id,
                                    const std::string& state_json_fragment) {
    ScopedFileLock lock(wp.state_json + ".lock", 2000);
    if (!lock.acquired()) return;

    // Read current
    std::string current;
    {
        std::ifstream f(wp.state_json);
        if (f.is_open()) { std::ostringstream ss; ss << f.rdbuf(); current = ss.str(); }
    }

    // Parse, update, re-write using nlohmann
    try {
        nlohmann::json root;
        if (!current.empty()) {
            try { root = nlohmann::json::parse(current); } catch (...) {}
        }
        if (!root.is_object()) root = nlohmann::json::object();

        nlohmann::json fragment;
        try { fragment = nlohmann::json::parse(state_json_fragment); } catch (...) {
            fragment = nlohmann::json(state_json_fragment);
        }
        root[agent_id] = fragment;

        std::ofstream f(wp.state_json);
        if (f.is_open()) f << root.dump(2) << "\n";
    } catch (...) {}
}

// -- Global log ----------------------------------------------------------------

void WorkspaceManager::append_log(const WorkspacePaths& wp,
                                   const std::string& ndjson_line) {
    // Structured NDJSON to structured.ndjson (machine-readable)
    if (!wp.structured_log.empty()) {
        ScopedFileLock lock1(wp.structured_log + ".lock", 500);
        std::ofstream f1(wp.structured_log, std::ios::app);
        if (f1.is_open()) f1 << ndjson_line << "\n";
    }
    // Human-readable Markdown to activity.md (replaces agent.log)
    if (!wp.activity_md.empty()) {
        ScopedFileLock lock2(wp.activity_md + ".lock", 500);
        std::ofstream f2(wp.activity_md, std::ios::app);
        if (f2.is_open()) f2 << ndjson_line << "\n";
    }
    // Conv-scoped runs log
    if (!wp.conv_runs_md.empty()) {
        ScopedFileLock lock3(wp.conv_runs_md + ".lock", 500);
        std::ofstream f3(wp.conv_runs_md, std::ios::app);
        if (f3.is_open()) f3 << ndjson_line << "\n";
    }
}

// -- Sandbox check -------------------------------------------------------------

bool WorkspaceManager::is_sandboxed(const std::string& path,
                                     const WorkspacePaths& wp,
                                     const std::string& agent_id) {
    auto norm = [](std::string s) {
        for (auto& c : s) if (c == '\\') c = '/';
        return s;
    };
    std::string p    = norm(path);
    std::string adir = norm(wp.artifact_dir(agent_id));
    std::string sdir = norm(wp.shared_dir);
    auto starts = [](const std::string& s, const std::string& prefix) {
        return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
    };
    return starts(p, adir) || starts(p, sdir);
}

// -- Cleanup -------------------------------------------------------------------

std::vector<std::string> WorkspaceManager::cleanup_artifacts(
        const WorkspacePaths& wp) {
    std::vector<std::string> deleted;
#ifndef _WIN32
    auto rm_dir = [&](const std::string& dir) {
        DIR* dp = opendir(dir.c_str());
        if (!dp) return;
        struct dirent* ep;
        while ((ep = readdir(dp))) {
            std::string name = ep->d_name;
            if (name == "." || name == "..") continue;
            std::string full = dir + "/" + name;
            if (::unlink(full.c_str()) == 0) deleted.push_back(full);
        }
        closedir(dp);
        ::rmdir(dir.c_str());
    };

    DIR* rdp = opendir(wp.run_root.c_str());
    if (rdp) {
        struct dirent* ep;
        while ((ep = readdir(rdp))) {
            std::string name = ep->d_name;
            if (name.find("wkr-") == 0)
                rm_dir(wp.run_root + "/" + name + "/artifacts");
        }
        closedir(rdp);
    }
#else
    (void)wp;
#endif
    return deleted;
}

} // namespace agent

// =============================================================================
// ResourceManager implementation
// =============================================================================

#include <optional>

namespace agent {

void ResourceManager::write_shared(const std::string& shared_path,
                                    const std::string& filename,
                                    const std::string& content,
                                    const std::string& writer_id) {
    std::string path = WorkspaceManager::join(shared_path, filename);
    {
        ScopedFileLock lock(path + ".lock", 3000);
        std::ofstream f(path);
        if (f.is_open()) f << content;
    }
    if (!writer_id.empty()) {
        std::ofstream m(path + ".meta");
        if (m.is_open())
            m << "{\"writer\":\"" << writer_id << "\","
              << "\"size\":" << content.size() << "}\n";
    }
}

std::optional<std::string> ResourceManager::read_shared(
        const std::string& shared_path,
        const std::string& filename) {
    std::string path = WorkspaceManager::join(shared_path, filename);
    std::ifstream f(path);
    if (!f.is_open()) return std::nullopt;
    std::ostringstream ss; ss << f.rdbuf();
    return ss.str();
}

std::vector<std::string> ResourceManager::list_shared(
        const std::string& shared_path) {
    std::vector<std::string> result;
#ifndef _WIN32
    DIR* dp = opendir(shared_path.c_str());
    if (!dp) return result;
    struct dirent* ep;
    while ((ep = readdir(dp)) != nullptr) {
        std::string name = ep->d_name;
        if (name == "." || name == ".." ||
            name.size() > 5 && name.substr(name.size()-5) == ".lock" ||
            name.size() > 5 && name.substr(name.size()-5) == ".meta")
            continue;
        result.push_back(name);
    }
    closedir(dp);
#else
    WIN32_FIND_DATAA ffd;
    std::string pattern = shared_path + "\\*";
    HANDLE h = FindFirstFileA(pattern.c_str(), &ffd);
    if (h == INVALID_HANDLE_VALUE) return result;
    do {
        std::string name = ffd.cFileName;
        if (name == "." || name == "..") continue;
        if (name.size() > 5 &&
            (name.substr(name.size()-5) == ".lock" ||
             name.substr(name.size()-5) == ".meta")) continue;
        result.push_back(name);
    } while (FindNextFileA(h, &ffd));
    FindClose(h);
#endif
    return result;
}

std::vector<std::string> ResourceManager::collect(
        const std::string& shared_path,
        const std::string& pattern) {
    auto files = list_shared(shared_path);
    std::vector<std::string> results;
    for (auto& name : files) {
        if (!pattern.empty() && name.find(pattern) == std::string::npos) continue;
        auto content = read_shared(shared_path, name);
        if (content) results.push_back(*content);
    }
    return results;
}

void ResourceManager::append_shared(const std::string& shared_path,
                                     const std::string& filename,
                                     const std::string& line,
                                     const std::string& /*writer_id*/) {
    std::string path = WorkspaceManager::join(shared_path, filename);
    ScopedFileLock lock(path + ".lock", 3000);
    std::ofstream f(path, std::ios::app);
    if (f.is_open()) f << line << "\n";
}

} // namespace agent
