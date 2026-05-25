#include "text_quality.hpp"

#include <cstdint>
#include <vector>

namespace {

bool utf8_decode_advance(const std::string& s, std::size_t& i, std::uint32_t& cp) {
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

std::vector<std::uint32_t> utf8_to_codepoints(const std::string& text) {
    std::vector<std::uint32_t> cps;
    cps.reserve(text.size() / 3);
    std::size_t i = 0;
    while (i < text.size()) {
        std::uint32_t cp = 0;
        if (!utf8_decode_advance(text, i, cp)) {
            continue;
        }
        cps.push_back(cp);
    }
    return cps;
}

}  // namespace

std::string truncate_utf8_tail(const std::string& text, std::size_t max_chars) {
    if (text.empty() || max_chars == 0) {
        return {};
    }
    std::size_t i = 0;
    std::vector<std::size_t> char_starts;
    while (i < text.size()) {
        char_starts.push_back(i);
        std::uint32_t cp = 0;
        if (!utf8_decode_advance(text, i, cp)) {
            break;
        }
    }
    if (char_starts.size() <= max_chars) {
        return text;
    }
    return text.substr(char_starts[char_starts.size() - max_chars]);
}

bool is_repetitive_hallucination(const std::string& text) {
    const auto cps = utf8_to_codepoints(text);
    const std::size_t n = cps.size();
    if (n < 12) {
        return false;
    }
    const std::size_t max_len = n / 3;
    for (std::size_t len = 4; len <= max_len && len <= 24; ++len) {
        for (std::size_t start = 0; start + len * 3 <= n; ++start) {
            bool triple = true;
            for (std::size_t rep = 1; rep < 3; ++rep) {
                for (std::size_t k = 0; k < len; ++k) {
                    if (cps[start + k] != cps[start + rep * len + k]) {
                        triple = false;
                        break;
                    }
                }
                if (!triple) {
                    break;
                }
            }
            if (triple) {
                return true;
            }
        }
    }
    return false;
}
