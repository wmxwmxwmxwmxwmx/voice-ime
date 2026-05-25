#include "audio_vad.hpp"

#include <cmath>

float chunk_rms(const int16_t* data, std::size_t count) {
    if (!data || count == 0) {
        return 0.0f;
    }
    double sum = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        const double s = static_cast<double>(data[i]) / 32768.0;
        sum += s * s;
    }
    return static_cast<float>(std::sqrt(sum / static_cast<double>(count)));
}

bool is_speech_chunk(float rms, float threshold) {
    return rms >= threshold;
}
