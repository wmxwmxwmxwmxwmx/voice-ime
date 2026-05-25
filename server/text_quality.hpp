#pragma once

#include <string>

bool is_repetitive_hallucination(const std::string& text);

std::string collapse_adjacent_repeats(const std::string& text);

bool should_reject_partial(const std::string& new_tail, const std::string& last_tail,
                           double max_garbled_ratio = 0.15);

std::string sanitize_utf8_text(const std::string& text);

std::string strip_unwanted_codepoints(const std::string& text);

std::string clean_transcript_text(const std::string& text);

double text_garbled_ratio(const std::string& text);

bool is_acceptable_transcript(const std::string& text, double max_garbled_ratio = 0.15);

// UTF-8 安全截断尾部，约 max_chars 个字符（用于 initial_prompt 上下文）
std::string truncate_utf8_tail(const std::string& text, std::size_t max_chars);
