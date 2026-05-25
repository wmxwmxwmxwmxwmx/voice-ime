#include "session.hpp"

#include "audio_vad.hpp"

bool Session::append_pcm_int16(const int16_t* data, std::size_t count,
                               float energy_threshold, int chunk_ms) {
    const float rms = chunk_rms(data, count);
    if (is_speech_chunk(rms, energy_threshold)) {
        pcm.reserve(pcm.size() + count);
        for (std::size_t i = 0; i < count; ++i) {
            pcm.push_back(static_cast<float>(data[i]) / 32768.0f);
        }
        if (pcm.size() > kMaxSamples) {
            const std::size_t drop = pcm.size() - kMaxSamples;
            pcm.erase(pcm.begin(), pcm.begin() + static_cast<std::ptrdiff_t>(drop));
            if (infer_pcm_offset >= drop) {
                infer_pcm_offset -= drop;
            } else {
                infer_pcm_offset = 0;
            }
        }
        speech_seen = true;
        trailing_silence_ms = 0;
        return true;
    }

    trailing_silence_ms += chunk_ms;
    return false;
}

void Session::clear() {
    pcm.clear();
    last_partial.clear();
    recording = false;
    infer_pcm_offset = 0;
    committed_text.clear();
    speech_seen = false;
    trailing_silence_ms = 0;
    last_partial_text.clear();
    infer_pending = false;
    closed = false;
}

bool Session::should_schedule_infer(int step_ms, int min_speech_ms) const {
    if (!recording || !speech_seen) {
        return false;
    }
    const std::size_t tail_samples = pcm.size() > infer_pcm_offset
                                         ? pcm.size() - infer_pcm_offset
                                         : 0;
    const std::size_t min_samples =
        static_cast<std::size_t>(kSampleRate) * static_cast<std::size_t>(min_speech_ms) /
        1000;
    if (tail_samples < min_samples) {
        return false;
    }
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - last_infer_time);
    return elapsed.count() >= step_ms;
}

void Session::mark_inferred() {
    last_infer_time = std::chrono::steady_clock::now();
}

bool Session::should_commit_on_pause(int silence_commit_ms) const {
    if (!recording || !speech_seen) {
        return false;
    }
    if (trailing_silence_ms < silence_commit_ms) {
        return false;
    }
    return has_pending_tail();
}

void Session::commit_segment(const std::string& tail_text) {
    if (!tail_text.empty()) {
        committed_text += tail_text;
    }
    infer_pcm_offset = pcm.size();
    last_partial_text.clear();
    trailing_silence_ms = 0;
}

std::vector<float> Session::pcm_for_infer() const {
    if (pcm.size() <= infer_pcm_offset) {
        return {};
    }
    return std::vector<float>(pcm.begin() + static_cast<std::ptrdiff_t>(infer_pcm_offset),
                            pcm.end());
}

std::string Session::display_text(const std::string& tail) const {
    return committed_text + tail;
}

bool Session::has_pending_tail() const {
    return pcm.size() > infer_pcm_offset;
}
