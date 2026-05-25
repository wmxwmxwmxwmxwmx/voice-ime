#pragma once

#include <cstddef>
#include <cstdint>

float chunk_rms(const int16_t* data, std::size_t count);

bool is_speech_chunk(float rms, float threshold);
