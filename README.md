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

配置时若出现来自 OpenCC、whisper.cpp 的 CMake 策略或弃用提示，一般可忽略（不影响编译）；根目录 [CMakeLists.txt](CMakeLists.txt) 已尽量抑制。仍想安静输出时可加：

```powershell
cmake -B build -DCMAKE_BUILD_TYPE=Release -Wno-dev
```

### 4. 启动服务端

```bash
./build/Release/voice_server --model models/ggml-base.bin --port 9000 --threads 4 --step 400
```

| 参数 | 说明 |
|------|------|
| `--model` | GGML whisper 模型路径（必填） |
| `--port` | WebSocket 端口（默认 9000） |
| `--threads` | ASR 工作线程数（默认 4） |
| `--step` | 两次中间识别的最小间隔，毫秒（默认 400） |
| `--min-speech-ms` | 未提交尾部至少多长才触发 partial，毫秒（默认 500） |
| `--vad-energy` | 能量 VAD 的 RMS 阈值（默认 0.015） |
| `--silence-commit-ms` | 静音多久将当前段落锁定为已确认，毫秒（默认 400） |
| `--partial-max-sec` | partial 推理的最大尾部音频窗口，秒（默认 3） |
| `--max-utterance-sec` | 连续说话多久自动 commit（默认与 partial-max-sec 相同） |
| `--no-speech-thold` | Whisper 非语音阈值（默认 0.65） |
| `--no-zh-prompt` | 禁用默认中文 `initial_prompt`（默认已开启，利于 `language=zh` 识别） |
| `--no-context-prompt` | 禁用已确认文本作为 `initial_prompt` |
| `--no-repeat-filter` | 关闭 partial 重复幻觉过滤 |
| `--garbled-ratio-thold` | 乱码码点占比上限，超过则拒发 partial（默认 0.15） |

### 5. 启动并打开前端

```bash
python -m http.server 8080 -d frontend
```

在浏览器打开 [http://localhost:8080](http://localhost:8080)，允许麦克风权限，点击 **开始** 后说话。

**首次使用或缺少 RNNoise 文件时**，在项目根目录执行：

```powershell
.\scripts\vendor_rnnoise.ps1
```

```bash
chmod +x scripts/vendor_rnnoise.sh && ./scripts/vendor_rnnoise.sh
```

可选：通过查询参数指定 WebSocket 地址：`http://localhost:8080?ws=ws://localhost:9000`

### 浏览器音频（AudioWorklet + RNNoise）

- 采集链：`麦克风 → RNNoise（48 kHz，可选）→ PCM 组帧（16 kHz，200 ms）→ WebSocket`。
- 须通过 `http://localhost` 或 `https` 访问页面（`file://` 无法使用 AudioWorklet）；不可用时会回退到 ScriptProcessor。
- 界面 **浏览器降噪（RNNoise）** 默认开启；开启时关闭浏览器内置 `noiseSuppression`，保留 `echoCancellation`。
- 静态资源来自 [simple-rnnoise-wasm](https://www.npmjs.com/package/simple-rnnoise-wasm)，由 `scripts/vendor_rnnoise.*` 下载到 `frontend/vendor/rnnoise/`。

## 通信协议（MVP）

| 方向 | 格式 | 内容 |
|------|------|------|
| 客户端 → 服务端 | 二进制 | PCM int16 小端，单声道，16 kHz（每帧约 200 ms） |
| 客户端 → 服务端 | JSON | `{"cmd":"start","language":"zh"}`、`{"cmd":"stop"}`、`{"cmd":"ping"}`（`language` 支持 `auto`、`zh`、`en`） |
| 服务端 → 客户端 | JSON | `{"type":"partial","text":"..."}`、`{"type":"final","text":"..."}`、`{"type":"error","message":"..."}` |

## 项目结构

```
voice-ime/
├── frontend/          # 浏览器界面（audio/ worklet、vendor/rnnoise/）
├── server/            # C++ WebSocket + ASR
├── third_party/       # whisper.cpp、websocketpp、asio（子模块）
├── scripts/           # 模型下载脚本
├── models/            # 已下载的 GGML 权重（已 gitignore）
└── CMakeLists.txt
```

## 简体中文输出

- 语言为 **中文**（`zh`）或 **自动**（`auto`）时，识别结果经 **OpenCC**（`t2s`）转为**简体中文**后再返回；**英语**（`en`）不转换。
- 默认在 `language=zh` 且无上下文时注入简短中文 `initial_prompt`，提高普通话识别率；后处理会剔除 prompt 残片。若需关闭可加 `--no-zh-prompt`。
- 若 `voice_server` 旁缺少 `opencc/t2s.json` 及 `.ocd2` 字典，服务仍可运行，但不会在日志外提示的情况下跳过繁简转换（stderr 会打印一次 `[opencc]` 警告）。
- 识别准确率主要取决于 Whisper 模型大小；**中文推荐 `ggml-small.bin` 或更大**（`base` 在流式场景易重复幻觉），并在界面选择 **中文**。
- 若出现 `󦱉󩦙` 类乱码：多为 Whisper 幻觉 + 脏文本进入上下文；服务端会过滤 PUA/非法 UTF-8，且不再用乱码作 `initial_prompt`。**`auto` 模式不再走 OpenCC**，仅 `zh` 转简体。

## VAD 与增量识别

- **能量 VAD**：低于 `--vad-energy` 的 PCM 块不写入缓冲，减少静音送入 Whisper 导致的幻觉（如 `(音)`）。
- **partial 尾部窗口**：仅对最近 `--partial-max-sec`（默认 3s）且自上次 commit 起的音频做中间识别。
- **自动分段 commit**：连续说话超过 `--max-utterance-sec`（默认与 partial 窗口相同）也会锁定上一段，避免无停顿时长句叠加。
- **增量显示**：`partial` 为 **已确认 + 稳定化后的当前句**；相邻重复短语会自动折叠；异常增长 partial 会被丢弃。
- **停顿 commit**：静音约 `--silence-commit-ms`（默认 400ms）后锁定上一段。
- **stop 整段 final**：停止后仅下发一条 `final`（beam search 整段重识别，全文替换）；停止后不再发送 partial。
- **上下文 prompt**：优先用已确认文本；无确认时用上一句 **可读** partial 尾部作 `initial_prompt`（乱码不会写入上下文）。
- **乱码过滤**：剔除 PUA/破碎 UTF-8；乱码占比超过 `--garbled-ratio-thold` 的 partial 不下发。
- **重复过滤**：检测双倍/三倍重复及低多样性文本；`--no-repeat-filter` 可关闭。
- **Whisper**：partial 用 greedy + `single_segment`；final 用 beam search（`beam_size=5`）。
- 推荐：`.\scripts\download_model.ps1 -Model small`，启动示例：  
  `voice_server --model models/ggml-small.bin --step 500 --partial-max-sec 3`

## 性能说明

- 传输分片：**200 ms**（浏览器侧）
- 中间结果刷新：通常 **400–800 ms**，受 whisper CPU 推理限制
- 开发阶段可使用 `ggml-tiny.bin` 或 `ggml-base.en.bin` 降低延迟

## 后续规划（MVP 之后）

- Whisper 内置 VAD 模型、热词
- Docker 部署
- TLS（WSS）

## 许可证

第三方组件许可证见 `third_party/`。
