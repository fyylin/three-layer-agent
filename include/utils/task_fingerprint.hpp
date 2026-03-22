#pragma once
#include <string>
#include <vector>
#include <map>

namespace agent {

class TaskFingerprint {
public:
    static std::string compute(const std::string& description,
                               const std::map<std::string, std::string>& context) {
        std::string combined = description;
        for (auto& [k, v] : context)
            combined += "|" + k + "=" + v;
        return std::to_string(std::hash<std::string>{}(combined));
    }

    static double similarity(const std::string& desc1, const std::string& desc2) {
        auto tokens1 = tokenize(desc1);
        auto tokens2 = tokenize(desc2);
        int common = 0;
        for (auto& t : tokens1)
            if (tokens2.count(t)) common++;
        int total = tokens1.size() + tokens2.size() - common;
        return total > 0 ? (double)common / total : 0.0;
    }

private:
    static std::map<std::string, int> tokenize(const std::string& s) {
        std::map<std::string, int> tokens;
        std::string word;
        for (char c : s) {
            if (std::isalnum(c)) word += std::tolower(c);
            else if (!word.empty()) { tokens[word]++; word.clear(); }
        }
        if (!word.empty()) tokens[word]++;
        return tokens;
    }
};

} // namespace agent
