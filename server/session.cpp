#include "session.hpp"

#include <algorithm>
#include <cmath>

void Session::append_pcm_int16(const int16_t* data, std::size_t count) {
    pcm.reserve(pcm.size() + count);
    for (std::size_t i = 0; i < count; ++i) {
        pcm.push_back(static_cast<float>(data[i]) / 32768.0f);
    }
    if (pcm.size() > kMaxSamples) {
        pcm.erase(pcm.begin(), pcm.begin() + (pcm.size() - kMaxSamples));
    }
}

void Session::clear() {
    pcm.clear();
    last_partial.clear();
    recording = false;
}

bool Session::should_infer(int step_ms) const {
    if (!recording || pcm.empty()) {
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
