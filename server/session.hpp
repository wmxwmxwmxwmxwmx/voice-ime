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
    std::atomic<bool> closed{false};
    std::atomic<bool> finalize_pending{false};

    // 返回空字符串表示拒绝本次 partial 更新
    static std::string stable_tail_for_display(const std::string& new_tail,
                                               const std::string& last_tail,
                                               double max_garbled_ratio = 0.15,
                                               const std::string& language = "");

    bool append_pcm_int16(const int16_t* data, std::size_t count, float energy_threshold,
                          int chunk_ms);
    void clear();
    bool should_schedule_infer(int step_ms, int min_speech_ms) const;
    void mark_inferred();
    bool should_commit_on_pause(int silence_commit_ms) const;
    bool should_commit_on_duration(int max_utterance_ms) const;
    void commit_segment(const std::string& tail_text);
    std::vector<float> pcm_for_partial_infer(int max_tail_ms) const;
    std::vector<float> pcm_for_final_infer(float energy_threshold, int chunk_ms) const;
    static std::vector<float> extract_speech_pcm(const std::vector<float>& pcm,
                                                 float energy_threshold, int chunk_ms);
    std::vector<float> pcm_all() const;
    std::string display_text(const std::string& tail) const;
    bool has_pending_tail() const;
};
