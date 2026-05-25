#pragma once

#include "asr_worker.hpp"
#include "session.hpp"
#include "thread_pool.hpp"

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <map>

using WsServer = websocketpp::server<websocketpp::config::asio>;
using ConnectionHdl = websocketpp::connection_hdl;

class VoiceWebSocketServer {
public:
    VoiceWebSocketServer(std::string model_path, uint16_t port, std::size_t threads,
                         int step_ms, int min_speech_ms, float vad_energy,
                         int silence_commit_ms, float no_speech_thold);
    ~VoiceWebSocketServer();

    bool validate_model();
    void run();
    void stop();

private:
    using MessagePtr = WsServer::message_ptr;

    void on_open(ConnectionHdl hdl);
    void on_close(ConnectionHdl hdl);
    void on_message(ConnectionHdl hdl, MessagePtr msg);

    void handle_control(ConnectionHdl hdl, const std::string& payload);
    void handle_binary(ConnectionHdl hdl, const std::string& payload);
    void schedule_infer(ConnectionHdl hdl, bool final_pass, bool pause_commit = false);
    void send_text(ConnectionHdl hdl, const std::string& json);
    Session& session_for(ConnectionHdl hdl);

    std::string model_path_;
    uint16_t port_;
    int step_ms_;
    int min_speech_ms_;
    float vad_energy_;
    int silence_commit_ms_;
    float no_speech_thold_;
    static constexpr int kChunkMs = 200;

    WsServer server_;
    ThreadPool pool_;
    AsrEngine asr_;
    std::mutex sessions_mutex_;
    std::map<ConnectionHdl, std::shared_ptr<Session>, std::owner_less<ConnectionHdl>> sessions_;
    std::atomic<bool> running_{true};
};
