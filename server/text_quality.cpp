#include "text_quality.hpp"

#include <cstdint>
#include <unordered_set>
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

void utf8_append(std::string& out, std::uint32_t cp) {
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

std::string codepoints_to_utf8(const std::vector<std::uint32_t>& cps) {
    std::string out;
    out.reserve(cps.size() * 3);
    for (std::uint32_t cp : cps) {
        utf8_append(out, cp);
    }
    return out;
}

bool cps_equal(const std::vector<std::uint32_t>& cps, std::size_t a, std::size_t b,
               std::size_t len) {
    for (std::size_t k = 0; k < len; ++k) {
        if (cps[a + k] != cps[b + k]) {
            return false;
        }
    }
    return true;
}

bool is_garbled_codepoint(std::uint32_t cp) {
    if (cp == 0xFFFD) {
        return true;
    }
    if (cp >= 0xE000 && cp <= 0xF8FF) {
        return true;
    }
    if (cp >= 0xFFF0 && cp <= 0xFFFF) {
        return true;
    }
    if (cp >= 0xFDD0 && cp <= 0xFDEF) {
        return true;
    }
    if (cp >= 0xF0000 && cp <= 0xFFFFD) {
        return true;
    }
    if (cp >= 0x100000 && cp <= 0x10FFFD) {
        return true;
    }
    return false;
}

bool is_cjk_codepoint(std::uint32_t cp) {
    return (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0x3400 && cp <= 0x4DBF);
}

bool transcript_has_cjk(const std::string& text) {
    for (std::uint32_t cp : utf8_to_codepoints(text)) {
        if (is_cjk_codepoint(cp)) {
            return true;
        }
    }
    return false;
}

bool transcript_has_latin_letters(const std::string& text) {
    for (std::uint32_t cp : utf8_to_codepoints(text)) {
        if ((cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z')) {
            return true;
        }
    }
    return false;
}

std::string apply_language_output_filter(const std::string& text,
                                         const std::string& language) {
    if (text.empty() || language != "zh") {
        return text;
    }
    if (transcript_has_cjk(text)) {
        return text;
    }
    if (transcript_has_latin_letters(text)) {
        return {};
    }
    return text;
}

bool is_readable_codepoint(std::uint32_t cp) {
    if ((cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') ||
        (cp >= '0' && cp <= '9')) {
        return true;
    }
    if (cp >= 0x4E00 && cp <= 0x9FFF) {
        return true;
    }
    if (cp >= 0x3400 && cp <= 0x4DBF) {
        return true;
    }
    if (cp >= 0x3000 && cp <= 0x303F) {
        return true;
    }
    if (cp >= 0xFF00 && cp <= 0xFFEF) {
        return true;
    }
    return false;
}

bool should_keep_codepoint(std::uint32_t cp) {
    if (cp == '\n' || cp == '\t') {
        return true;
    }
    if (cp < 0x20) {
        return false;
    }
    if (cp == 0x7F) {
        return false;
    }
    if (cp >= 0x80 && cp <= 0x9F) {
        return false;
    }
    if (is_garbled_codepoint(cp)) {
        return false;
    }
    return true;
}

bool has_low_diversity(const std::vector<std::uint32_t>& cps) {
    const std::size_t n = cps.size();
    if (n < 16) {
        return false;
    }
    std::unordered_set<std::uint32_t> uniq(cps.begin(), cps.end());
    return static_cast<double>(uniq.size()) / static_cast<double>(n) < 0.35;
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

std::string collapse_adjacent_repeats(const std::string& text) {
    std::vector<std::uint32_t> cps = utf8_to_codepoints(text);
    if (cps.size() < 4) {
        return text;
    }

    bool changed = true;
    while (changed) {
        changed = false;
        const std::size_t max_len =
            cps.size() < 32 ? cps.size() / 2 : static_cast<std::size_t>(16);
        for (std::size_t len = max_len; len >= 2; --len) {
            for (std::size_t i = 0; i + 2 * len <= cps.size();) {
                if (cps_equal(cps, i, i + len, len)) {
                    cps.erase(cps.begin() + static_cast<std::ptrdiff_t>(i + len),
                              cps.begin() + static_cast<std::ptrdiff_t>(i + 2 * len));
                    changed = true;
                    continue;
                }
                ++i;
            }
        }
    }
    return codepoints_to_utf8(cps);
}

bool is_repetitive_hallucination(const std::string& text) {
    const auto cps = utf8_to_codepoints(text);
    const std::size_t n = cps.size();
    if (n < 6) {
        return false;
    }

    if (has_low_diversity(cps)) {
        return true;
    }

    const std::size_t max_len = n / 2 < 24 ? n / 2 : static_cast<std::size_t>(24);
    for (std::size_t len = 3; len <= max_len; ++len) {
        for (std::size_t start = 0; start + 2 * len <= n; ++start) {
            if (cps_equal(cps, start, start + len, len)) {
                return true;
            }
        }
    }

    if (n < 12) {
        return false;
    }
    const std::size_t triple_max = n / 3;
    for (std::size_t len = 4; len <= triple_max && len <= 24; ++len) {
        for (std::size_t start = 0; start + len * 3 <= n; ++start) {
            bool triple = true;
            for (std::size_t rep = 1; rep < 3; ++rep) {
                if (!cps_equal(cps, start, start + rep * len, len)) {
                    triple = false;
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

std::string sanitize_utf8_text(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    std::size_t i = 0;
    while (i < text.size()) {
        std::uint32_t cp = 0;
        if (!utf8_decode_advance(text, i, cp)) {
            continue;
        }
        utf8_append(out, cp);
    }
    return out;
}

std::string strip_unwanted_codepoints(const std::string& text) {
    std::vector<std::uint32_t> kept;
    kept.reserve(text.size() / 3);
    for (std::uint32_t cp : utf8_to_codepoints(text)) {
        if (should_keep_codepoint(cp)) {
            kept.push_back(cp);
        }
    }
    return codepoints_to_utf8(kept);
}

std::string clean_transcript_text(const std::string& text) {
    return strip_unwanted_codepoints(sanitize_utf8_text(text));
}

double text_garbled_ratio(const std::string& text) {
    const auto cps = utf8_to_codepoints(text);
    if (cps.empty()) {
        return 0.0;
    }
    std::size_t garbled = 0;
    for (std::uint32_t cp : cps) {
        if (is_garbled_codepoint(cp)) {
            ++garbled;
        }
    }
    return static_cast<double>(garbled) / static_cast<double>(cps.size());
}

bool is_acceptable_transcript(const std::string& text, double max_garbled_ratio) {
    if (text.empty()) {
        return false;
    }
    if (text_garbled_ratio(text) > max_garbled_ratio) {
        return false;
    }
    const auto cps = utf8_to_codepoints(text);
    std::size_t readable = 0;
    for (std::uint32_t cp : cps) {
        if (is_readable_codepoint(cp)) {
            ++readable;
        }
    }
    return readable >= 1;
}

std::string extract_readable_transcript(const std::string& text) {
    const std::string cleaned = clean_transcript_text(text);
    std::vector<std::uint32_t> kept;
    kept.reserve(cleaned.size() / 3);
    for (std::uint32_t cp : utf8_to_codepoints(cleaned)) {
        if (is_readable_codepoint(cp)) {
            kept.push_back(cp);
        }
    }
    return codepoints_to_utf8(kept);
}

std::string prepare_transcript_for_output(const std::string& text, double max_garbled_ratio,
                                          const std::string& language) {
    const std::string cleaned = clean_transcript_text(text);
    if (is_acceptable_transcript(cleaned, max_garbled_ratio)) {
        return apply_language_output_filter(cleaned, language);
    }
    std::string extracted = extract_readable_transcript(text);
    if (extracted.empty()) {
        return {};
    }
    if (!is_acceptable_transcript(extracted, max_garbled_ratio)) {
        return {};
    }
    return apply_language_output_filter(extracted, language);
}

bool should_reject_partial(const std::string& new_tail, const std::string& last_tail,
                           double max_garbled_ratio, const std::string& language) {
    if (new_tail.empty()) {
        return true;
    }
    const std::string prepared =
        prepare_transcript_for_output(new_tail, max_garbled_ratio, language);
    if (prepared.empty()) {
        return true;
    }
    if (is_repetitive_hallucination(prepared)) {
        return true;
    }
    if (last_tail.empty()) {
        return false;
    }
    if (prepared.find(last_tail + last_tail) != std::string::npos) {
        return true;
    }
    if (prepared.size() >= last_tail.size() &&
        prepared.compare(0, last_tail.size(), last_tail) == 0 &&
        prepared.size() > last_tail.size() + last_tail.size() / 2) {
        return true;
    }
    return false;
}
