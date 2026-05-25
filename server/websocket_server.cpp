#include "websocket_server.hpp"

#include "protocol.hpp"
#include "text_quality.hpp"

#include <iostream>
#include <utility>

namespace {

ConnectionHdl copy_hdl(ConnectionHdl hdl) {
    return hdl;
}

struct InferPendingGuard {
    std::shared_ptr<Session> session;
    ~InferPendingGuard() {
        if (session) {
            session->infer_pending = false;
        }
    }
};

std::string build_context_prompt(const Session& session, bool use_context,
                                 float garbled_thresh) {
    if (!use_context) {
        return {};
    }
    if (!session.committed_text.empty()) {
        const std::string cleaned = clean_transcript_text(session.committed_text);
        if (is_acceptable_transcript(cleaned, garbled_thresh)) {
            return truncate_utf8_tail(cleaned, 120);
        }
    }
    if (!session.last_partial_text.empty()) {
        const std::string cleaned = clean_transcript_text(session.last_partial_text);
        if (is_acceptable_transcript(cleaned, garbled_thresh)) {
            return truncate_utf8_tail(cleaned, 80);
        }
    }
    return {};
}

}  // namespace

VoiceWebSocketServer::VoiceWebSocketServer(std::string model_path, uint16_t port,
                                           std::size_t threads, int step_ms,
                                           int min_speech_ms, float vad_energy,
                                           int silence_commit_ms, float no_speech_thold,
                                           bool use_zh_prompt, int partial_max_sec,
                                           bool use_context_prompt, bool repeat_filter,
                                           int max_utterance_sec, float garbled_ratio_thold)
    : model_path_(std::move(model_path)),
      port_(port),
      step_ms_(step_ms),
      min_speech_ms_(min_speech_ms),
      vad_energy_(vad_energy),
      silence_commit_ms_(silence_commit_ms),
      no_speech_thold_(no_speech_thold),
      partial_max_sec_(partial_max_sec > 0 ? partial_max_sec : 3),
      max_utterance_sec_(max_utterance_sec > 0 ? max_utterance_sec : partial_max_sec_),
      use_context_prompt_(use_context_prompt),
      repeat_filter_(repeat_filter),
      garbled_ratio_thold_(garbled_ratio_thold > 0.f ? garbled_ratio_thold : 0.15f),
      pool_(threads > 0 ? threads : 1),
      asr_(model_path_, no_speech_thold_, use_zh_prompt) {
    server_.init_asio();
    server_.set_reuse_addr(true);

    server_.set_open_handler([this](ConnectionHdl hdl) { on_open(hdl); });
    server_.set_close_handler([this](ConnectionHdl hdl) { on_close(hdl); });
    server_.set_message_handler(
        [this](ConnectionHdl hdl, MessagePtr msg) { on_message(hdl, msg); });
}

VoiceWebSocketServer::~VoiceWebSocketServer() {
    stop();
}

void VoiceWebSocketServer::stop() {
    running_ = false;
    server_.stop_listening();
    pool_.stop();
}

Session& VoiceWebSocketServer::session_for(ConnectionHdl hdl) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    return *sessions_.at(hdl);
}

void VoiceWebSocketServer::on_open(ConnectionHdl hdl) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    sessions_[hdl] = std::make_shared<Session>();
    std::cout << "[ws] 客户端已连接\n";
}

void VoiceWebSocketServer::on_close(ConnectionHdl hdl) {
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        auto it = sessions_.find(hdl);
        if (it != sessions_.end()) {
            {
                std::lock_guard<std::mutex> slock(it->second->mutex);
                it->second->recording = false;
            }
            it->second->closed.store(true);
            sessions_.erase(it);
        }
    }
    std::cout << "[ws] 客户端已断开\n";
}

void VoiceWebSocketServer::on_message(ConnectionHdl hdl, MessagePtr msg) {
    const auto opcode = msg->get_opcode();
    const auto& payload = msg->get_payload();

    if (opcode == websocketpp::frame::opcode::binary) {
        handle_binary(hdl, payload);
        return;
    }
    if (opcode == websocketpp::frame::opcode::text) {
        handle_control(hdl, payload);
    }
}

void VoiceWebSocketServer::handle_control(ConnectionHdl hdl, const std::string& payload) {
    const std::string cmd = protocol::extract_cmd(payload);
    auto& session = session_for(hdl);

    if (cmd == "ping") {
        send_text(hdl, "{\"type\":\"pong\"}");
        return;
    }

    if (cmd == "start") {
        std::lock_guard<std::mutex> lock(session.mutex);
        session.clear();
        session.recording = true;
        session.recording_start = std::chrono::steady_clock::now();
        session.last_infer_time = session.recording_start;
        const std::string lang = protocol::extract_string_field(payload, "language");
        if (!lang.empty()) {
            session.language = protocol::normalize_language(lang);
        }
        std::cout << "[ws] 开始录音（语言=" << session.language << "）\n";
        return;
    }

    if (cmd == "stop") {
        {
            std::lock_guard<std::mutex> lock(session.mutex);
            session.recording = false;
        }
        session.finalize_pending.store(true);
        schedule_infer(hdl, true, false);
        std::cout << "[ws] 停止录音\n";
        return;
    }

    send_text(hdl, protocol::make_error("未知命令：" + cmd));
}

void VoiceWebSocketServer::handle_binary(ConnectionHdl hdl, const std::string& payload) {
    if (payload.size() % sizeof(int16_t) != 0) {
        send_text(hdl, protocol::make_error("PCM 帧大小无效"));
        return;
    }

    auto& session = session_for(hdl);
    bool should_run = false;
    bool pause_commit = false;

    {
        std::lock_guard<std::mutex> lock(session.mutex);
        if (!session.recording) {
            return;
        }
        const auto* samples = reinterpret_cast<const int16_t*>(payload.data());
        const std::size_t count = payload.size() / sizeof(int16_t);
        session.append_pcm_int16(samples, count, vad_energy_, kChunkMs);

        const int max_utterance_ms = max_utterance_sec_ * 1000;
        if (session.should_commit_on_pause(silence_commit_ms_)) {
            pause_commit = true;
            should_run = true;
        } else if (session.should_commit_on_duration(max_utterance_ms)) {
            pause_commit = true;
            should_run = true;
        } else if (session.should_schedule_infer(step_ms_, min_speech_ms_)) {
            should_run = true;
        }
    }

    if (should_run) {
        schedule_infer(hdl, false, pause_commit);
    }
}

void VoiceWebSocketServer::schedule_infer(ConnectionHdl hdl, bool final_pass,
                                          bool pause_commit) {
    ConnectionHdl hdl_copy = copy_hdl(hdl);

    std::shared_ptr<Session> session_ptr;
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        auto it = sessions_.find(hdl);
        if (it == sessions_.end()) {
            return;
        }
        session_ptr = it->second;
    }

    {
        std::lock_guard<std::mutex> lock(session_ptr->mutex);
        if (!final_pass && !pause_commit) {
            if (session_ptr->infer_pending.load()) {
                return;
            }
        }
        session_ptr->infer_pending = true;
    }

    const int partial_max_ms = partial_max_sec_ * 1000;

    if (!pool_.enqueue([this, hdl_copy, session_ptr, final_pass, pause_commit,
                        partial_max_ms]() {
            InferPendingGuard guard{session_ptr};

            if (session_ptr->closed.load()) {
                return;
            }

            std::vector<float> pcm_copy;
            std::string language;
            std::string context_storage;
            std::string last_partial_snapshot;
            const bool do_commit = pause_commit;

            {
                std::lock_guard<std::mutex> lock(session_ptr->mutex);
                if (!final_pass && !pause_commit &&
                    !session_ptr->should_schedule_infer(step_ms_, min_speech_ms_)) {
                    return;
                }
                if (final_pass) {
                    pcm_copy = session_ptr->pcm_all();
                } else {
                    pcm_copy =
                        session_ptr->pcm_for_partial_infer(partial_max_ms);
                }
                language = session_ptr->language;
                last_partial_snapshot = session_ptr->last_partial_text;
                context_storage =
                    build_context_prompt(*session_ptr, use_context_prompt_,
                                         garbled_ratio_thold_);
                if (!final_pass && !pause_commit) {
                    session_ptr->mark_inferred();
                }
            }

            const bool short_audio =
                !final_pass ||
                pcm_copy.size() <
                    static_cast<std::size_t>(Session::kSampleRate) * 6;
            const std::string* ctx =
                (use_context_prompt_ && !context_storage.empty()) ? &context_storage
                                                                  : nullptr;

            if (pcm_copy.empty()) {
                if (final_pass && !session_ptr->closed.load()) {
                    session_ptr->finalize_pending.store(false);
                    send_text(hdl_copy, protocol::make_final(""));
                }
                return;
            }

            const TranscribeResult tr = asr_.transcribe(
                pcm_copy, language, ctx, short_audio, final_pass);
            if (session_ptr->closed.load()) {
                return;
            }
            if (!tr.ok) {
                if (final_pass) {
                    session_ptr->finalize_pending.store(false);
                }
                send_text(hdl_copy, protocol::make_error(tr.error));
                return;
            }

            if (final_pass) {
                if (repeat_filter_ && is_repetitive_hallucination(tr.text)) {
                    std::cerr << "[ws] final 识别结果疑似重复幻觉\n";
                }
                std::string final_text = tr.text;
                if (!is_acceptable_transcript(final_text, garbled_ratio_thold_)) {
                    std::cerr << "[ws] final 乱码已丢弃\n";
                    final_text.clear();
                }
                session_ptr->finalize_pending.store(false);
                send_text(hdl_copy, protocol::make_final(final_text));
                return;
            }

            if (session_ptr->finalize_pending.load()) {
                return;
            }

            if (!is_acceptable_transcript(tr.text, garbled_ratio_thold_)) {
                return;
            }

            const std::string stable_tail = Session::stable_tail_for_display(
                tr.text, last_partial_snapshot, garbled_ratio_thold_);
            if (stable_tail.empty()) {
                return;
            }

            if (repeat_filter_ && is_repetitive_hallucination(stable_tail)) {
                return;
            }

            std::string outbound;
            {
                std::lock_guard<std::mutex> lock(session_ptr->mutex);
                if (session_ptr->finalize_pending.load()) {
                    return;
                }
                outbound = session_ptr->display_text(stable_tail);
                session_ptr->last_partial = stable_tail;
                session_ptr->last_partial_text = stable_tail;
                if (do_commit) {
                    session_ptr->commit_segment(stable_tail);
                    outbound = session_ptr->display_text("");
                }
            }

            if (session_ptr->closed.load() ||
                session_ptr->finalize_pending.load()) {
                return;
            }
            send_text(hdl_copy, protocol::make_partial(outbound));
        })) {
        session_ptr->infer_pending = false;
        send_text(hdl, protocol::make_error("服务器繁忙，请稍后重试"));
    }
}

void VoiceWebSocketServer::send_text(ConnectionHdl hdl, const std::string& json) {
    try {
        server_.get_io_service().post([this, hdl, json]() {
            try {
                server_.send(hdl, json, websocketpp::frame::opcode::text);
            } catch (const websocketpp::exception& e) {
                const std::string msg = e.what();
                if (msg.find("Bad Connection") == std::string::npos &&
                    msg.find("End of File") == std::string::npos) {
                    std::cerr << "[ws] 发送失败：" << msg << "\n";
                }
            } catch (const std::exception& e) {
                std::cerr << "[ws] 发送失败：" << e.what() << "\n";
            }
        });
    } catch (const std::exception& e) {
        std::cerr << "[ws] 投递失败：" << e.what() << "\n";
    }
}

bool VoiceWebSocketServer::validate_model() {
    if (asr_.model_available()) {
        return true;
    }
    std::cerr << "错误：无法加载模型 " << model_path_ << "\n";
    std::cerr << "请确认文件存在。若仅有 ggml-tiny.bin，请使用：\n";
    std::cerr << "  --model models/ggml-tiny.bin\n";
    return false;
}

void VoiceWebSocketServer::run() {
    server_.listen(port_);
    server_.start_accept();
    std::cout << "Voice-IME 服务监听 ws://0.0.0.0:" << port_ << "\n";
    std::cout << "模型：" << model_path_ << " | 推理间隔：" << step_ms_ << " ms"
              << " | VAD 能量阈值：" << vad_energy_
              << " | 停顿提交：" << silence_commit_ms_ << " ms"
              << " | partial 尾部窗口：" << partial_max_sec_ << " s"
              << " | 最长未提交句段：" << max_utterance_sec_ << " s\n";
    server_.run();
}
