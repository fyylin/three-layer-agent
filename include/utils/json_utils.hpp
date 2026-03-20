#pragma once
// =============================================================================
// include/utils/json_utils.hpp
// =============================================================================

#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace agent {

[[nodiscard]] inline std::string render_template(
        const std::string& text,
        const std::unordered_map<std::string, std::string>& vars) {
    std::string out = text;
    for (const auto& [key, value] : vars) {
        const std::string ph = "{{" + key + "}}";
        size_t pos = 0;
        while ((pos = out.find(ph, pos)) != std::string::npos) {
            out.replace(pos, ph.size(), value);
            pos += value.size();
        }
    }
    size_t lb = out.find("{{");
    if (lb != std::string::npos) {
        size_t rb = out.find("}}", lb);
        std::string missing = (rb != std::string::npos)
            ? out.substr(lb, rb - lb + 2) : out.substr(lb);
        throw std::runtime_error("render_template: unreplaced placeholder: " + missing);
    }
    return out;
}

[[nodiscard]] inline std::string load_prompt(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open())
        throw std::runtime_error("load_prompt: cannot open: " + path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// extract_json: find the first complete JSON value (object or array) in text.
// Handles ```json fences, plain ``` fences, and raw JSON.
[[nodiscard]] inline std::string extract_json(const std::string& text) {
    std::string body;

    // 1. Try ```json ... ``` fence
    {
        size_t fs = text.find("```json");
        if (fs != std::string::npos) {
            fs += 7;
            if (fs < text.size() && (text[fs] == '\n' || text[fs] == '\r')) ++fs;
            size_t fe = text.find("```", fs);
            if (fe != std::string::npos)
                body = text.substr(fs, fe - fs);
        }
    }
    // 2. Try plain ``` ... ``` fence
    if (body.empty()) {
        size_t fs = text.find("```");
        if (fs != std::string::npos) {
            fs += 3;
            if (fs < text.size() && (text[fs] == '\n' || text[fs] == '\r')) ++fs;
            size_t fe = text.find("```", fs);
            if (fe != std::string::npos)
                body = text.substr(fs, fe - fs);
        }
    }
    // 3. Raw text
    if (body.empty()) body = text;

    // Find first { or [
    size_t obj = body.find('{');
    size_t arr = body.find('[');
    size_t start;
    if      (obj == std::string::npos) start = arr;
    else if (arr == std::string::npos) start = obj;
    else                               start = std::min(obj, arr);

    if (start == std::string::npos)
        throw std::runtime_error(
            "extract_json: no JSON found in LLM response.\n"
            "Raw response (first 500 chars):\n" + text.substr(0, 500));

    // Matched bracket scan
    const char open  = body[start];
    const char close = (open == '{') ? '}' : ']';
    int  depth = 0;
    bool in_str = false, escaped = false;
    for (size_t i = start; i < body.size(); ++i) {
        char c = body[i];
        if (escaped)            { escaped = false; continue; }
        if (c == '\\' && in_str){ escaped = true;  continue; }
        if (c == '"')           { in_str = !in_str; continue; }
        if (in_str)               continue;
        if (c == open)  { ++depth; continue; }
        if (c == close) { if (--depth == 0) return body.substr(start, i - start + 1); }
    }
    throw std::runtime_error(
        "extract_json: unbalanced brackets in LLM response.\n"
        "Raw response (first 500 chars):\n" + text.substr(0, 500));
}

// parse_llm_json: extract + parse, with full raw response in error message
[[nodiscard]] inline nlohmann::json parse_llm_json(const std::string& llm_output) {
    std::string raw = extract_json(llm_output);
    try {
        return nlohmann::json::parse(raw);
    } catch (const std::exception& e) {
        throw std::runtime_error(
            std::string("parse_llm_json failed: ") + e.what() +
            "\n--- Extracted JSON ---\n" + raw.substr(0, 500) +
            "\n--- Full LLM response ---\n" + llm_output.substr(0, 1000));
    }
}

} // namespace agent
