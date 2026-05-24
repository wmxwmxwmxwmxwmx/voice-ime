#include "websocket_server.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

void print_usage() {
    std::cerr
        << "用法：voice_server --model <模型路径.ggml> [选项]\n"
        << "  --model <路径>   whisper GGML 模型路径（必填）\n"
        << "  --port <端口>    WebSocket 端口（默认：9000）\n"
        << "  --threads <数量> ASR 工作线程数（默认：4）\n"
        << "  --step <毫秒>    两次中间识别的最小间隔（默认：400）\n";
}

struct Options {
    std::string model;
    uint16_t port = 9000;
    std::size_t threads = 4;
    int step_ms = 400;
};

bool parse_args(int argc, char** argv, Options& opts) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto need_value = [&](const char* flag) -> std::string {
            if (arg != flag || i + 1 >= argc) {
                return {};
            }
            return argv[++i];
        };

        if (arg == "--model") {
            opts.model = need_value("--model");
            if (opts.model.empty()) return false;
        } else if (arg == "--port") {
            const auto v = need_value("--port");
            if (v.empty()) return false;
            opts.port = static_cast<uint16_t>(std::stoi(v));
        } else if (arg == "--threads") {
            const auto v = need_value("--threads");
            if (v.empty()) return false;
            opts.threads = static_cast<std::size_t>(std::stoul(v));
        } else if (arg == "--step") {
            const auto v = need_value("--step");
            if (v.empty()) return false;
            opts.step_ms = std::stoi(v);
        } else if (arg == "--help" || arg == "-h") {
            print_usage();
            std::exit(0);
        } else {
            std::cerr << "未知参数：" << arg << "\n";
            return false;
        }
    }
    return !opts.model.empty();
}

}  // namespace

int main(int argc, char** argv) {
    Options opts;
    if (!parse_args(argc, argv, opts)) {
        print_usage();
        return 1;
    }

#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    if (!std::filesystem::exists(opts.model)) {
        std::cerr << "错误：找不到模型文件：" << opts.model << "\n";
        std::cerr << "请先运行 .\\scripts\\download_model.ps1 -Model tiny\n";
        std::cerr << "或 .\\scripts\\download_model.ps1 -Model base\n";
        std::cerr << "若已下载 tiny，请使用：--model models/ggml-tiny.bin\n";
        return 1;
    }

    try {
        VoiceWebSocketServer server(opts.model, opts.port, opts.threads, opts.step_ms);
        if (!server.validate_model()) {
            return 1;
        }
        server.run();
    } catch (const std::exception& e) {
        std::cerr << "致命错误：" << e.what() << "\n";
        return 1;
    }

    return 0;
}
