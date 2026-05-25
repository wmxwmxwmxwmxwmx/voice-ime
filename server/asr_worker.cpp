#include "asr_worker.hpp"

#include "text_convert.hpp"

#include <whisper.h>

#include <cctype>
#include <memory>
#include <sstream>
#include <string>

namespace {

whisper_context* get_thread_context(const std::string& model_path, std::string& error_out) {
    thread_local std::unique_ptr<whisper_context, void (*)(whisper_context*)> ctx{
        nullptr, whisper_free};
    thread_local bool load_failed = false;
    thread_local std::string load_error;

    if (load_failed) {
        error_out = load_error;
        return nullptr;
    }

    if (!ctx) {
        whisper_context_params cparams = whisper_context_default_params();
        cparams.use_gpu = false;
        ctx.reset(whisper_init_from_file_with_params(model_path.c_str(), cparams));
        if (!ctx) {
            load_failed = true;
            load_error = "无法加载 Whisper 模型：" + model_path;
            error_out = load_error;
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

constexpr const char* kZhInitialPrompt = "以下是普通话简体中文。";

std::string filter_prompt_artifacts(const std::string& text) {
    static const char* kPhrases[] = {
        u8"以下是普通话简体中文。",
        u8"以下是普通话简体中文",
        u8"以下是普通话",
        u8"简体中文",
        u8"普通话",
        u8"以下是",
    };
    std::string out = text;
    for (const char* phrase : kPhrases) {
        const std::string p(phrase);
        std::size_t pos = 0;
        while ((pos = out.find(p, pos)) != std::string::npos) {
            out.erase(pos, p.size());
        }
    }
    return out;
}

// 去掉 ASCII 括号片段，如 (音)、(转写)
std::string strip_bracket_hallucinations(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '(') {
            const std::size_t end = text.find(')', i + 1);
            if (end != std::string::npos) {
                i = end;
                continue;
            }
        }
        out += text[i];
    }
    return out;
}

}  // namespace

AsrEngine::AsrEngine(std::string model_path, float no_speech_thold, bool use_zh_prompt)
    : model_path_(std::move(model_path)),
      no_speech_thold_(no_speech_thold),
      use_zh_prompt_(use_zh_prompt) {}

bool AsrEngine::model_available() const {
    std::string err;
    return get_thread_context(model_path_, err) != nullptr;
}

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
    return sanitize_transcript(out);
}

std::string AsrEngine::sanitize_transcript(const std::string& text) {
    return filter_prompt_artifacts(strip_bracket_hallucinations(text));
}

TranscribeResult AsrEngine::transcribe(const std::vector<float>& pcm,
                                       const std::string& language) const {
    TranscribeResult result;
    if (pcm.empty()) {
        result.ok = true;
        return result;
    }

    std::string load_err;
    whisper_context* ctx = get_thread_context(model_path_, load_err);
    if (!ctx) {
        result.error = load_err.empty() ? "Whisper 模型未加载" : load_err;
        return result;
    }

    whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    wparams.print_realtime   = false;
    wparams.print_progress   = false;
    wparams.print_timestamps = false;
    wparams.print_special    = false;
    wparams.no_timestamps    = true;
    wparams.single_segment   = false;
    wparams.n_threads        = 1;
    wparams.suppress_blank   = true;
    wparams.suppress_nst     = true;
    wparams.no_speech_thold  = no_speech_thold_;

    if (language != "auto" && !language.empty()) {
        wparams.language = language.c_str();
        wparams.detect_language = false;
    } else {
        wparams.language = nullptr;
        wparams.detect_language = true;
    }

    if (use_zh_prompt_ && language == "zh") {
        wparams.initial_prompt = kZhInitialPrompt;
        wparams.carry_initial_prompt = false;
    }

    if (whisper_full(ctx, wparams, pcm.data(), static_cast<int>(pcm.size())) != 0) {
        result.error = "Whisper 推理失败";
        return result;
    }

    result.ok = true;
    std::string text = postprocess(collect_segments(ctx));
    if (language == "zh" || language == "auto") {
        text = to_simplified_chinese(text);
    }
    result.text = std::move(text);
    return result;
}
