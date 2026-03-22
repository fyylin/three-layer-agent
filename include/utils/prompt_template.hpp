#pragma once
#include <string>
#include <map>
#include <regex>

namespace agent {

class PromptTemplate {
public:
    explicit PromptTemplate(std::string tmpl) : template_(std::move(tmpl)) {}

    std::string render(const std::map<std::string, std::string>& vars) const {
        std::string result = template_;
        for (auto& [key, val] : vars) {
            std::string placeholder = "{{" + key + "}}";
            size_t pos = 0;
            while ((pos = result.find(placeholder, pos)) != std::string::npos) {
                result.replace(pos, placeholder.length(), val);
                pos += val.length();
            }
        }
        return result;
    }

    static std::string inject_context(const std::string& base_prompt,
                                      const std::string& context_key,
                                      const std::string& context_value) {
        if (context_value.empty()) return base_prompt;
        std::string marker = "<!-- " + context_key + " -->";
        size_t pos = base_prompt.find(marker);
        if (pos == std::string::npos) return base_prompt + "\n\n" + context_value;
        return base_prompt.substr(0, pos) + context_value + base_prompt.substr(pos + marker.length());
    }

private:
    std::string template_;
};

} // namespace agent
