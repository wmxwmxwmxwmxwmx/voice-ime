#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

struct Session {
    static constexpr int kSampleRate = 16000;
    static constexpr std::size_t kMaxSamples = kSampleRate * 30;  // 最长缓存 30 秒音频

    std::mutex mutex;
    std::vector<float> pcm;
    bool recording = false;
    std::string language = "auto";
    std::string last_partial;
    std::chrono::steady_clock::time_point last_infer_time{};
    std::chrono::steady_clock::time_point recording_start{};

    void append_pcm_int16(const int16_t* data, std::size_t count);
    void clear();
    bool should_infer(int step_ms) const;
    void mark_inferred();
};
