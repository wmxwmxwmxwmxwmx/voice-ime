#include "text_convert.hpp"

#include <opencc.h>

#include <filesystem>
#include <iostream>
#include <mutex>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {

std::filesystem::path executable_directory() {
#ifdef _WIN32
    std::wstring buf;
    buf.resize(MAX_PATH);
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, buf.data(),
                                           static_cast<DWORD>(buf.size()));
        if (n == 0) {
            return {};
        }
        if (n < buf.size()) {
            buf.resize(n);
            return std::filesystem::path(buf).parent_path();
        }
        buf.resize(buf.size() * 2);
    }
#else
    std::error_code ec;
    const auto path = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec) {
        return {};
    }
    return path.parent_path();
#endif
}

std::filesystem::path t2s_config_path() {
    const auto dir = executable_directory();
    if (dir.empty()) {
        return {};
    }
    return dir / "opencc" / "t2s.json";
}

struct ThreadOpencc {
    opencc_t handle = reinterpret_cast<opencc_t>(-1);
    bool initialized = false;
    bool available = false;
};

ThreadOpencc& thread_converter() {
    thread_local ThreadOpencc conv;
    return conv;
}

std::once_flag g_warn_once;

void warn_opencc_once(const std::string& msg) {
    std::call_once(g_warn_once, [&]() {
        std::cerr << "[opencc] " << msg << "，将跳过繁简转换\n";
    });
}

bool ensure_opencc(ThreadOpencc& conv) {
    if (conv.initialized) {
        return conv.available;
    }
    conv.initialized = true;

    const auto config = t2s_config_path();
    if (config.empty()) {
        warn_opencc_once("无法解析可执行文件目录");
        return false;
    }
    std::error_code ec;
    if (!std::filesystem::is_regular_file(config, ec)) {
        warn_opencc_once("找不到配置文件 " + config.string());
        return false;
    }

#ifdef _WIN32
    const std::wstring wconfig = config.wstring();
    conv.handle = opencc_open_w(wconfig.c_str());
#else
    conv.handle = opencc_open(config.string().c_str());
#endif

    if (conv.handle == reinterpret_cast<opencc_t>(-1) || conv.handle == nullptr) {
        const char* err = opencc_error();
        warn_opencc_once(std::string("加载失败：") + (err ? err : "unknown"));
        conv.handle = reinterpret_cast<opencc_t>(-1);
        return false;
    }

    conv.available = true;
    return true;
}

}  // namespace

std::string to_simplified_chinese(const std::string& utf8) {
    if (utf8.empty()) {
        return utf8;
    }

    auto& conv = thread_converter();
    if (!ensure_opencc(conv)) {
        return utf8;
    }

    char* out = opencc_convert_utf8(conv.handle, utf8.c_str(),
                                    static_cast<size_t>(-1));
    if (!out) {
        return utf8;
    }

    std::string result(out);
    opencc_convert_utf8_free(out);
    return result;
}
