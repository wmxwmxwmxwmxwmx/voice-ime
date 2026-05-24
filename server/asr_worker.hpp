#pragma once

#include <string>
#include <vector>

struct TranscribeResult {
    bool ok = false;
    std::string text;
    std::string error;
};

class AsrEngine {
public:
    explicit AsrEngine(std::string model_path);

    bool model_available() const;
    TranscribeResult transcribe(const std::vector<float>& pcm,
                                const std::string& language) const;
    static std::string postprocess(const std::string& text);

private:
    std::string model_path_;
};
