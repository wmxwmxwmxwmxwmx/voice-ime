#include "websocket_server.hpp"

#include "protocol.hpp"

#include <iostream>

namespace {

ConnectionHdl copy_hdl(ConnectionHdl hdl) {
    return hdl;
}

}  // namespace

VoiceWebSocketServer::VoiceWebSocketServer(std::string model_path, uint16_t port,
                                           std::size_t threads, int step_ms)
    : model_path_(std::move(model_path)),
      port_(port),
      step_ms_(step_ms),
      pool_(threads > 0 ? threads : 1),
      asr_(model_path_) {
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
    schedule_infer(hdl, true);
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    sessions_.erase(hdl);
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
        schedule_infer(hdl, true);
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

    {
        std::lock_guard<std::mutex> lock(session.mutex);
        if (!session.recording) {
            return;
        }
        const auto* samples = reinterpret_cast<const int16_t*>(payload.data());
        const std::size_t count = payload.size() / sizeof(int16_t);
        session.append_pcm_int16(samples, count);
        should_run = session.should_infer(step_ms_);
    }

    if (should_run) {
        schedule_infer(hdl, false);
    }
}

void VoiceWebSocketServer::schedule_infer(ConnectionHdl hdl, bool final_pass) {
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

    if (!pool_.enqueue([this, hdl_copy, session_ptr, final_pass]() {
            std::vector<float> pcm_copy;
            std::string language;
            {
                std::lock_guard<std::mutex> lock(session_ptr->mutex);
                if (!final_pass && !session_ptr->should_infer(step_ms_)) {
                    return;
                }
                pcm_copy = session_ptr->pcm;
                language = session_ptr->language;
                if (!final_pass) {
                    session_ptr->mark_inferred();
                }
            }

            if (pcm_copy.empty()) {
                if (final_pass) {
                    send_text(hdl_copy, protocol::make_final(""));
                }
                return;
            }

            const TranscribeResult tr = asr_.transcribe(pcm_copy, language);
            if (!tr.ok) {
                send_text(hdl_copy, protocol::make_error(tr.error));
                return;
            }

            {
                std::lock_guard<std::mutex> lock(session_ptr->mutex);
                session_ptr->last_partial = tr.text;
            }

            if (final_pass) {
                send_text(hdl_copy, protocol::make_final(tr.text));
            } else {
                send_text(hdl_copy, protocol::make_partial(tr.text));
            }
        })) {
        send_text(hdl, protocol::make_error("服务器繁忙，请稍后重试"));
    }
}

void VoiceWebSocketServer::send_text(ConnectionHdl hdl, const std::string& json) {
    try {
        server_.get_io_service().post([this, hdl, json]() {
            try {
                server_.send(hdl, json, websocketpp::frame::opcode::text);
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
    std::cout << "模型：" << model_path_ << " | 推理间隔：" << step_ms_ << " ms\n";
    server_.run();
}
