#pragma once

#include <string>

// 将 UTF-8 文本转为简体中文（OpenCC t2s）。OpenCC 未加载或转换失败时返回原文。
std::string to_simplified_chinese(const std::string& utf8);
