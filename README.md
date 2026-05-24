# Voice-IME

基于流式 ASR（自动语音识别）的实时语音输入：浏览器采集 PCM 音频，经 WebSocket 发送至运行 [whisper.cpp](https://github.com/ggerganov/whisper.cpp) 的 C++ 服务端，并实时显示中间结果与最终结果。

## 架构

```
浏览器（Web Audio PCM） --WebSocket--> C++ 服务端（websocketpp + 线程池 + whisper.cpp）
```

## 环境要求

- **CMake** 3.16+
- **C++17** 编译器（Windows 使用 Visual Studio 2022，Linux 使用 GCC/Clang）
- **Git**（含子模块）
- **Python 3**（编译时需用于构建 OpenCC 字典；也可用于启动前端静态页面）

## 快速开始

### 1. 克隆代码并初始化子模块

```bash
git clone --recursive https://github.com/wmxwmxwmxwmxwmx/voice-ime.git
cd voice-ime
# 若已克隆：
git submodule update --init --recursive
cd third_party/asio && git checkout asio-1-18-2 && cd ../..
```

**asio** 子模块必须固定在标签 `asio-1-18-2`，以兼容 websocketpp（新版 Asio 已移除 `io_service`）。

### 2. 下载 Whisper 模型

**Windows（PowerShell）：**

```powershell
.\scripts\download_model.ps1 -Model base
```

**Linux / macOS：**

```bash
chmod +x scripts/download_model.sh
./scripts/download_model.sh base
```

模型保存至 `models/ggml-base.bin`。开发调试可使用 `tiny` 以加快速度。

### 3. 编译服务端

首次编译会通过 CMake 拉取 [OpenCC](https://github.com/BYVoid/OpenCC) 并构建繁简字典（需本机已安装 Python 3）。构建完成后，`t2s.json` 与字典文件会自动复制到 `voice_server` 同目录下的 `opencc/` 文件夹。

**Windows（VS 2022）：**

```powershell
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

可执行文件：`build\Release\voice_server.exe`

**Linux：**

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

可执行文件：`build/voice_server`

### 4. 启动服务端

```bash
./build/voice_server --model models/ggml-base.bin --port 9000 --threads 4 --step 400
```

| 参数 | 说明 |
|------|------|
| `--model` | GGML whisper 模型路径（必填） |
| `--port` | WebSocket 端口（默认 9000） |
| `--threads` | ASR 工作线程数（默认 4） |
| `--step` | 两次中间识别之间的最小间隔，单位毫秒（默认 400） |

### 5. 启动并打开前端

```bash
python -m http.server 8080 -d frontend
```

在浏览器打开 [http://localhost:8080](http://localhost:8080)，允许麦克风权限，点击 **开始** 后说话。

可选：通过查询参数指定 WebSocket 地址：`http://localhost:8080?ws=ws://localhost:9000`

## 通信协议（MVP）

| 方向 | 格式 | 内容 |
|------|------|------|
| 客户端 → 服务端 | 二进制 | PCM int16 小端，单声道，16 kHz（每帧约 200 ms） |
| 客户端 → 服务端 | JSON | `{"cmd":"start","language":"zh"}`、`{"cmd":"stop"}`、`{"cmd":"ping"}`（`language` 支持 `auto`、`zh`、`en`） |
| 服务端 → 客户端 | JSON | `{"type":"partial","text":"..."}`、`{"type":"final","text":"..."}`、`{"type":"error","message":"..."}` |

## 项目结构

```
voice-ime/
├── frontend/          # 浏览器界面
├── server/            # C++ WebSocket + ASR
├── third_party/       # whisper.cpp、websocketpp、asio（子模块）
├── scripts/           # 模型下载脚本
├── models/            # 已下载的 GGML 权重（已 gitignore）
└── CMakeLists.txt
```

## 简体中文输出

- 语言为 **中文**（`zh`）或 **自动**（`auto`）时，识别结果经 **OpenCC**（`t2s`）转为**简体中文**后再返回；**英语**（`en`）不转换。
- 中文模式会使用 Whisper `initial_prompt` 引导普通话简体转写。
- 若 `voice_server` 旁缺少 `opencc/t2s.json` 及 `.ocd2` 字典，服务仍可运行，但不会在日志外提示的情况下跳过繁简转换（stderr 会打印一次 `[opencc]` 警告）。
- 识别准确率主要取决于 Whisper 模型大小；建议使用 `ggml-base.bin` 或更大，并在界面选择 **中文**。

## 性能说明

- 传输分片：**200 ms**（浏览器侧）
- 中间结果刷新：通常 **400–800 ms**，受 whisper CPU 推理限制
- 开发阶段可使用 `ggml-tiny.bin` 或 `ggml-base.en.bin` 降低延迟

## 后续规划（MVP 之后）

- WebRTC VAD、RNNoise、热词
- Docker 部署
- TLS（WSS）
- AudioWorklet（替代 ScriptProcessorNode）

## 许可证

第三方组件许可证见 `third_party/`。
