#include "asr_worker.hpp"

#include <whisper.h>

#include <cctype>
#include <memory>
#include <sstream>

namespace {

whisper_context* get_thread_context(const std::string& model_path) {
    thread_local std::unique_ptr<whisper_context, void (*)(whisper_context*)> ctx{
        nullptr, whisper_free};

    if (!ctx) {
        whisper_context_params cparams = whisper_context_default_params();
        cparams.use_gpu = false;
        ctx.reset(whisper_init_from_file_with_params(model_path.c_str(), cparams));
        if (!ctx) {
            return nullptr;
        }
    }
    return ctx.get();
}

std::string collect_segments(whisper_context* ctx) {
    std::ostringstream oss;
    const int n = whisper_full_n_segments(ctx);
    for (int i = 0; i < n; ++i) {
        const char* seg = whisper_full_get_segment_text(ctx, i);
        if (seg) {
            oss << seg;
        }
    }
    return oss.str();
}

}  // namespace

AsrEngine::AsrEngine(std::string model_path) : model_path_(std::move(model_path)) {}

std::string AsrEngine::postprocess(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    bool prev_space = false;
    for (unsigned char c : text) {
        if (std::isspace(c)) {
            if (!prev_space && !out.empty()) {
                out += ' ';
                prev_space = true;
            }
        } else {
            out += static_cast<char>(c);
            prev_space = false;
        }
    }
    while (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }
    return out;
}

std::string AsrEngine::transcribe(const std::vector<float>& pcm,
                                  const std::string& language) const {
    if (pcm.empty()) {
        return {};
    }

    whisper_context* ctx = get_thread_context(model_path_);
    if (!ctx) {
        return {};
    }

    whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    wparams.print_realtime   = false;
    wparams.print_progress   = false;
    wparams.print_timestamps = false;
    wparams.print_special    = false;
    wparams.no_timestamps    = true;
    wparams.single_segment   = false;
    wparams.n_threads        = 1;

    if (language != "auto" && !language.empty()) {
        wparams.language = language.c_str();
        wparams.detect_language = false;
    } else {
        wparams.language = nullptr;
        wparams.detect_language = true;
    }

    if (whisper_full(ctx, wparams, pcm.data(), static_cast<int>(pcm.size())) != 0) {
        return {};
    }

    return postprocess(collect_segments(ctx));
}
