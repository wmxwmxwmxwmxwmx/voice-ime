#pragma once

#include <cstdint>
#include <string>

namespace protocol {

namespace detail {

inline bool utf8_decode(const std::string& s, std::size_t& i, std::uint32_t& cp) {
    if (i >= s.size()) {
        return false;
    }
    const unsigned char c0 = static_cast<unsigned char>(s[i]);
    if (c0 < 0x80) {
        cp = c0;
        ++i;
        return true;
    }
    std::size_t len = 0;
    if ((c0 & 0xE0) == 0xC0) {
        len = 2;
    } else if ((c0 & 0xF0) == 0xE0) {
        len = 3;
    } else if ((c0 & 0xF8) == 0xF0) {
        len = 4;
    } else {
        ++i;
        return false;
    }
    if (i + len > s.size()) {
        i = s.size();
        return false;
    }
    cp = c0;
    for (std::size_t j = 1; j < len; ++j) {
        const unsigned char c = static_cast<unsigned char>(s[i + j]);
        if ((c & 0xC0) != 0x80) {
            i += len;
            return false;
        }
        cp = (cp << 6) | (c & 0x3F);
    }
    i += len;
    return true;
}

inline void utf8_append(std::string& out, std::uint32_t cp) {
    if (cp <= 0x7F) {
        out += static_cast<char>(cp);
    } else if (cp <= 0x7FF) {
        out += static_cast<char>(0xC0 | ((cp >> 6) & 0x1F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp <= 0xFFFF) {
        out += static_cast<char>(0xE0 | ((cp >> 12) & 0x0F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | ((cp >> 18) & 0x07));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

}  // namespace detail

inline std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    std::size_t i = 0;
    while (i < s.size()) {
        std::uint32_t cp = 0;
        if (!detail::utf8_decode(s, i, cp)) {
            continue;
        }
        switch (cp) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (cp < 0x20) {
                    break;
                }
                detail::utf8_append(out, cp);
                break;
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
