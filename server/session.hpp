#pragma once

#include <atomic>
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

    std::size_t infer_pcm_offset = 0;
    std::string committed_text;
    bool speech_seen = false;
    int trailing_silence_ms = 0;
    std::string last_partial_text;
    std::atomic<bool> infer_pending{false};

    bool append_pcm_int16(const int16_t* data, std::size_t count, float energy_threshold,
                          int chunk_ms);
    void clear();
    bool should_schedule_infer(int step_ms, int min_speech_ms) const;
    void mark_inferred();
    bool should_commit_on_pause(int silence_commit_ms) const;
    void commit_segment(const std::string& tail_text);
    std::vector<float> pcm_for_infer() const;
    std::string display_text(const std::string& tail) const;
    bool has_pending_tail() const;
};
