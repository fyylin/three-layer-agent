#include "agent/tool_cache.hpp"

namespace agent {

std::optional<std::string> ToolCache::get(const CacheKey& key) {
    auto it = cache_.find(key);
    if (it == cache_.end()) return std::nullopt;

    auto lru_map_it = lru_map_.find(key);
    if (lru_map_it != lru_map_.end()) {
        lru_.erase(lru_map_it->second);
        lru_.push_front(key);
        lru_map_it->second = lru_.begin();
    }

    return it->second;
}

void ToolCache::put(const CacheKey& key, const std::string& result) {
    auto existing = lru_map_.find(key);
    if (existing != lru_map_.end()) {
        lru_.erase(existing->second);
        lru_map_.erase(existing);
    } else if (cache_.size() >= max_size_) {
        evict_lru();
    }

    cache_[key] = result;
    lru_.push_front(key);
    lru_map_[key] = lru_.begin();
}

void ToolCache::invalidate(const std::string& file_path) {
    for (auto it = cache_.begin(); it != cache_.end();) {
        if (it->first.input.find(file_path) != std::string::npos) {
            auto lru_map_it = lru_map_.find(it->first);
            if (lru_map_it != lru_map_.end()) {
                lru_.erase(lru_map_it->second);
                lru_map_.erase(lru_map_it);
            }
            it = cache_.erase(it);
        } else {
            ++it;
        }
    }
}

void ToolCache::evict_lru() {
    if (lru_.empty()) return;
    auto key = lru_.back();
    lru_.pop_back();
    cache_.erase(key);
    lru_map_.erase(key);
}

} // namespace agent
