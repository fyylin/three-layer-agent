#pragma once
#include <ctime>
#include <list>
#include <optional>
#include <string>
#include <unordered_map>

namespace agent {

struct CacheKey {
    std::string tool_name;
    std::string input;
    std::time_t mtime{0};

    bool operator==(const CacheKey& other) const {
        return tool_name == other.tool_name &&
               input == other.input &&
               mtime == other.mtime;
    }
};

} // namespace agent

namespace std {
template<>
struct hash<agent::CacheKey> {
    size_t operator()(const agent::CacheKey& k) const {
        size_t h1 = hash<string>()(k.tool_name);
        size_t h2 = hash<string>()(k.input);
        size_t h3 = hash<long long>()(static_cast<long long>(k.mtime));
        return h1 ^ (h2 << 1) ^ (h3 << 2);
    }
};
} // namespace std

namespace agent {

class ToolCache {
public:
    explicit ToolCache(size_t max_size = 1000) : max_size_(max_size) {}

    std::optional<std::string> get(const CacheKey& key);
    void put(const CacheKey& key, const std::string& result);
    void invalidate(const std::string& file_path);
    void clear() { cache_.clear(); lru_.clear(); lru_map_.clear(); }

private:
    void evict_lru();

    size_t max_size_;
    std::unordered_map<CacheKey, std::string> cache_;
    std::list<CacheKey> lru_;
    std::unordered_map<CacheKey, std::list<CacheKey>::iterator> lru_map_;
};

} // namespace agent
