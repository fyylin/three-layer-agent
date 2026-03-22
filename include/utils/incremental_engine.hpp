#pragma once
#include <string>
#include <vector>
#include <map>
#include <set>
#include <filesystem>
#include <chrono>

namespace agent {

struct FileChange {
    std::string path;
    std::chrono::system_clock::time_point mtime;
    bool exists;
};

class IncrementalEngine {
public:
    void track_file(const std::string& path) {
        namespace fs = std::filesystem;
        if (fs::exists(path)) {
            auto mtime = fs::last_write_time(path);
            auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                mtime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
            tracked_[path] = {path, sctp, true};
        } else {
            tracked_[path] = {path, {}, false};
        }
    }

    std::vector<std::string> get_changed_files() {
        namespace fs = std::filesystem;
        std::vector<std::string> changed;
        for (auto& [path, old_state] : tracked_) {
            if (!fs::exists(path)) {
                if (old_state.exists) changed.push_back(path);
            } else {
                auto mtime = fs::last_write_time(path);
                auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                    mtime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
                if (sctp != old_state.mtime) changed.push_back(path);
            }
        }
        return changed;
    }

    void add_dependency(const std::string& task, const std::string& file) {
        deps_[task].insert(file);
    }

    std::vector<std::string> get_affected_tasks(const std::vector<std::string>& changed_files) {
        std::set<std::string> affected;
        for (auto& file : changed_files) {
            for (auto& [task, files] : deps_) {
                if (files.count(file)) affected.insert(task);
            }
        }
        return {affected.begin(), affected.end()};
    }

private:
    std::map<std::string, FileChange> tracked_;
    std::map<std::string, std::set<std::string>> deps_;
};

} // namespace agent
