#pragma once

#include <string>

namespace protocol {

inline std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:   out += c; break;
        }
    }
    return out;
}

inline std::string make_partial(const std::string& text) {
    return "{\"type\":\"partial\",\"text\":\"" + json_escape(text) + "\"}";
}

inline std::string make_final(const std::string& text) {
    return "{\"type\":\"final\",\"text\":\"" + json_escape(text) + "\"}";
}

inline std::string make_error(const std::string& message) {
    return "{\"type\":\"error\",\"message\":\"" + json_escape(message) + "\"}";
}

// 简易 JSON 字段提取，例如 {"cmd":"start","language":"zh"}
inline std::string extract_string_field(const std::string& json, const std::string& key) {
    const std::string pattern = "\"" + key + "\"";
    auto pos = json.find(pattern);
    if (pos == std::string::npos) return {};
    pos = json.find(':', pos);
    if (pos == std::string::npos) return {};
    pos = json.find('"', pos + 1);
    if (pos == std::string::npos) return {};
    auto end = json.find('"', pos + 1);
    if (end == std::string::npos) return {};
    return json.substr(pos + 1, end - pos - 1);
}

inline std::string extract_cmd(const std::string& json) {
    return extract_string_field(json, "cmd");
}

// 仅允许 auto / zh / en；其它值（含 ja）回退为 auto
inline std::string normalize_language(const std::string& lang) {
    if (lang == "zh" || lang == "en" || lang == "auto") {
        return lang;
    }
    return "auto";
}

}  // namespace protocol
