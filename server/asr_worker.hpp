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
    explicit AsrEngine(std::string model_path, float no_speech_thold = 0.6f,
                       bool use_zh_prompt = false);

    bool model_available() const;
    TranscribeResult transcribe(const std::vector<float>& pcm,
                                const std::string& language,
                                const std::string* context_prompt = nullptr,
                                bool short_audio = false) const;
    static std::string postprocess(const std::string& text);
    static std::string sanitize_transcript(const std::string& text);

private:
    std::string model_path_;
    float no_speech_thold_ = 0.6f;
    bool use_zh_prompt_ = false;
};
