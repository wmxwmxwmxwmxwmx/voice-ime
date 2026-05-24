#pragma once

#include <string>
#include <vector>

class AsrEngine {
public:
    explicit AsrEngine(std::string model_path);

    std::string transcribe(const std::vector<float>& pcm, const std::string& language) const;
    static std::string postprocess(const std::string& text);

private:
    std::string model_path_;
};
