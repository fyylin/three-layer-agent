#pragma once
#include <string>
#include <map>
#include <mutex>
#include <chrono>
#include <list>

namespace agent {

struct CachedResult {
    std::string result;
    std::chrono::steady_clock::time_point timestamp;
    int hit_count = 0;
};

class ResultCache {
public:
    explicit ResultCache(size_t max_size = 100) : max_size_(max_size) {}

    void put(const std::string& key, const std::string& result) {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = cache_.find(key);
        if (it != cache_.end()) {
            lru_.erase(it->second.lru_it);
        } else if (cache_.size() >= max_size_) {
            auto victim = lru_.back();
            cache_.erase(victim);
            lru_.pop_back();
        }
        lru_.push_front(key);
        cache_[key] = {result, std::chrono::steady_clock::now(), 0, lru_.begin()};
    }

    bool get(const std::string& key, std::string& result) {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = cache_.find(key);
        if (it == cache_.end()) return false;
        result = it->second.result;
        it->second.hit_count++;
        lru_.erase(it->second.lru_it);
        lru_.push_front(key);
        it->second.lru_it = lru_.begin();
        return true;
    }

    void clear() {
        std::lock_guard<std::mutex> lk(mu_);
        cache_.clear();
        lru_.clear();
    }

    size_t size() const {
        std::lock_guard<std::mutex> lk(mu_);
        return cache_.size();
    }

private:
    struct Entry {
        std::string result;
        std::chrono::steady_clock::time_point timestamp;
        int hit_count;
        std::list<std::string>::iterator lru_it;
    };

    mutable std::mutex mu_;
    size_t max_size_;
    std::map<std::string, Entry> cache_;
    std::list<std::string> lru_;
};

} // namespace agent
