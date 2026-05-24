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
- **Python 3**（可选，用于启动前端静态页面）

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
| 客户端 → 服务端 | JSON | `{"cmd":"start","language":"zh"}`、`{"cmd":"stop"}`、`{"cmd":"ping"}` |
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
