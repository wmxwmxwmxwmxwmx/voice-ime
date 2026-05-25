#pragma once

#include <string>

bool is_repetitive_hallucination(const std::string& text);

// UTF-8 安全截断尾部，约 max_chars 个字符（用于 initial_prompt 上下文）
std::string truncate_utf8_tail(const std::string& text, std::size_t max_chars);
