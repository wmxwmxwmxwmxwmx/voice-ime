const OUTPUT_SAMPLE_RATE = 16000;
const CAPTURE_SAMPLE_RATE = 48000;
const CHUNK_MS = 200;
const SAMPLES_PER_CHUNK = (OUTPUT_SAMPLE_RATE * CHUNK_MS) / 1000;

const startBtn = document.getElementById("startBtn");
const stopBtn = document.getElementById("stopBtn");
const wsStatus = document.getElementById("wsStatus");
const recStatus = document.getElementById("recStatus");
const resultEl = document.getElementById("result");
const serverUrlInput = document.getElementById("serverUrl");
const languageSelect = document.getElementById("language");
const denoiseCheckbox = document.getElementById("denoiseEnabled");

let socket = null;
let audioContext = null;
let mediaStream = null;
let sourceNode = null;
let rnnoiseNode = null;
let pcmCaptureNode = null;
let silentGain = null;
let scriptProcessor = null;
let pcmBuffer = [];
let finalText = "";
let partialText = "";
let recording = false;
let stopping = false;
let stopFlushDone = null;
let useWorklet = false;

function denoiseEnabled() {
  return denoiseCheckbox ? denoiseCheckbox.checked : true;
}

function supportsAudioWorklet() {
  return (
    typeof window.AudioContext !== "undefined" &&
    typeof AudioWorkletNode !== "undefined"
  );
}

function vendorUrl(file) {
  return new URL(`./vendor/rnnoise/${file}`, window.location.href).href;
}

function audioUrl(file) {
  return new URL(`./audio/${file}`, window.location.href).href;
}

async function compileRnnoiseWasm() {
  const wasmUrl = vendorUrl("simple-rnnoise.wasm");
  const response = await fetch(wasmUrl);
  if (!response.ok) {
    throw new Error(`无法加载 RNNoise WASM：${wasmUrl}`);
  }
  const bytes = await response.arrayBuffer();
  return WebAssembly.compile(bytes);
}

async function setupWorkletChain(stream) {
  const denoise = denoiseEnabled();
  const captureRate = denoise ? CAPTURE_SAMPLE_RATE : OUTPUT_SAMPLE_RATE;

  audioContext = new AudioContext({ sampleRate: captureRate });
  await audioContext.resume();
  sourceNode = audioContext.createMediaStreamSource(stream);

  if (denoise) {
    const wasmModule = await compileRnnoiseWasm();
    await audioContext.audioWorklet.addModule(vendorUrl("rnnoise.worklet.js"));
    rnnoiseNode = new AudioWorkletNode(audioContext, "rnnoise", {
      channelCount: 1,
      channelCountMode: "explicit",
      numberOfInputs: 1,
      numberOfOutputs: 1,
      outputChannelCount: [1],
      processorOptions: { module: wasmModule },
    });
    rnnoiseNode.port.postMessage(true);
  }

  await audioContext.audioWorklet.addModule(audioUrl("pcm-capture-processor.js"));
  pcmCaptureNode = new AudioWorkletNode(audioContext, "pcm-capture", {
    channelCount: 1,
    channelCountMode: "explicit",
    numberOfInputs: 1,
    numberOfOutputs: 1,
    outputChannelCount: [1],
    processorOptions: {
      outputSampleRate: OUTPUT_SAMPLE_RATE,
      chunkSamples: SAMPLES_PER_CHUNK,
    },
  });

  pcmCaptureNode.port.onmessage = (event) => {
    const msg = event.data;
    if (!msg) return;
    if (msg.type === "flushed") {
      if (stopFlushDone) stopFlushDone();
      return;
    }
    if (!recording && !stopping) return;
    if (msg.type === "pcm" && socket && socket.readyState === WebSocket.OPEN) {
      socket.send(msg.buffer);
    }
  };

  silentGain = audioContext.createGain();
  silentGain.gain.value = 0;

  if (denoise && rnnoiseNode) {
    sourceNode.connect(rnnoiseNode);
    rnnoiseNode.connect(pcmCaptureNode);
  } else {
    sourceNode.connect(pcmCaptureNode);
  }
  pcmCaptureNode.connect(silentGain);
  silentGain.connect(audioContext.destination);
  useWorklet = true;
}

function setupScriptProcessorFallback(stream) {
  audioContext = new AudioContext({ sampleRate: OUTPUT_SAMPLE_RATE });
  sourceNode = audioContext.createMediaStreamSource(stream);
  pcmBuffer = [];

  scriptProcessor = audioContext.createScriptProcessor(4096, 1, 1);
  scriptProcessor.onaudioprocess = (e) => {
    if (!recording && !stopping) return;
    const input = e.inputBuffer.getChannelData(0);
    for (let i = 0; i < input.length; i++) {
      pcmBuffer.push(input[i]);
    }
    while (pcmBuffer.length >= SAMPLES_PER_CHUNK) {
      const chunk = pcmBuffer.splice(0, SAMPLES_PER_CHUNK);
      const merged = new Float32Array(chunk);
      const int16 = floatToInt16(merged);
      if (socket && socket.readyState === WebSocket.OPEN) {
        socket.send(int16.buffer);
      }
    }
  };

  silentGain = audioContext.createGain();
  silentGain.gain.value = 0;
  sourceNode.connect(scriptProcessor);
  scriptProcessor.connect(silentGain);
  silentGain.connect(audioContext.destination);
  useWorklet = false;
}

function setWsStatus(online) {
  wsStatus.textContent = online ? "已连接" : "未连接";
  wsStatus.className = online ? "badge badge-online" : "badge badge-offline";
}

function setRecStatus(active) {
  recStatus.textContent = active ? "录音中" : "空闲";
  recStatus.className = active ? "badge badge-recording" : "badge badge-idle";
}

function renderTranscript() {
  resultEl.innerHTML = "";
  if (finalText) {
    const finalSpan = document.createElement("span");
    finalSpan.className = "result-final";
    finalSpan.textContent = finalText;
    resultEl.appendChild(finalSpan);
  }
  if (partialText) {
    const partialSpan = document.createElement("span");
    partialSpan.className = "result-partial";
    partialSpan.textContent = partialText;
    resultEl.appendChild(partialSpan);
  }
}

function floatToInt16(float32Array) {
  const int16 = new Int16Array(float32Array.length);
  for (let i = 0; i < float32Array.length; i++) {
    const s = Math.max(-1, Math.min(1, float32Array[i]));
    int16[i] = s < 0 ? s * 0x8000 : s * 0x7fff;
  }
  return int16;
}

function flushPcmChunk() {
  if (!socket || socket.readyState !== WebSocket.OPEN || pcmBuffer.length === 0) {
    pcmBuffer = [];
    return;
  }
  const merged = new Float32Array(pcmBuffer.length);
  for (let i = 0; i < pcmBuffer.length; i++) {
    merged[i] = pcmBuffer[i];
  }
  pcmBuffer = [];
  socket.send(floatToInt16(merged).buffer);
}

function connectWebSocket() {
  return new Promise((resolve, reject) => {
    const url = serverUrlInput.value.trim();
    socket = new WebSocket(url);
    socket.binaryType = "arraybuffer";

    socket.onopen = () => {
      setWsStatus(true);
      resolve();
    };

    socket.onclose = () => {
      setWsStatus(false);
      if (recording) {
        stopRecording();
      }
    };

    socket.onerror = () => {
      reject(new Error("WebSocket 连接失败"));
    };

    socket.onmessage = (event) => {
      try {
        const msg = JSON.parse(event.data);
        if (msg.type === "partial") {
          partialText = msg.text || "";
          renderTranscript();
        } else if (msg.type === "final") {
          finalText = msg.text || "";
          partialText = "";
          renderTranscript();
        } else if (msg.type === "error") {
          console.error(msg.message);
          alert(msg.message);
        }
      } catch (e) {
        console.warn("非 JSON 消息:", event.data);
      }
    };
  });
}

function teardownAudioNodes() {
  if (pcmCaptureNode) {
    pcmCaptureNode.port.onmessage = null;
    pcmCaptureNode.disconnect();
    pcmCaptureNode = null;
  }
  if (rnnoiseNode) {
    rnnoiseNode.port.postMessage(false);
    rnnoiseNode.disconnect();
    rnnoiseNode = null;
  }
  if (scriptProcessor) {
    scriptProcessor.disconnect();
    scriptProcessor.onaudioprocess = null;
    scriptProcessor = null;
  }
  if (sourceNode) {
    sourceNode.disconnect();
    sourceNode = null;
  }
  if (silentGain) {
    silentGain.disconnect();
    silentGain = null;
  }
  if (audioContext) {
    audioContext.close();
    audioContext = null;
  }
}

async function startRecording() {
  startBtn.disabled = true;
  stopBtn.disabled = false;
  finalText = "";
  partialText = "";
  renderTranscript();

  try {
    if (!socket || socket.readyState !== WebSocket.OPEN) {
      await connectWebSocket();
    }

    const useDenoise = denoiseEnabled();
    mediaStream = await navigator.mediaDevices.getUserMedia({
      audio: {
        channelCount: 1,
        echoCancellation: true,
        noiseSuppression: !useDenoise,
      },
    });

    if (supportsAudioWorklet() && window.isSecureContext) {
      try {
        await setupWorkletChain(mediaStream);
      } catch (workletErr) {
        console.warn("AudioWorklet 初始化失败，回退 ScriptProcessor：", workletErr);
        teardownAudioNodes();
        setupScriptProcessorFallback(mediaStream);
      }
    } else {
      if (!window.isSecureContext) {
        console.warn("非安全上下文，无法使用 AudioWorklet，已回退 ScriptProcessor");
      }
      setupScriptProcessorFallback(mediaStream);
    }

    const lang = languageSelect.value;
    socket.send(JSON.stringify({ cmd: "start", language: lang }));

    recording = true;
    setRecStatus(true);
  } catch (err) {
    console.error(err);
    alert(err.message || "无法开始录音");
    stopRecording();
  }
}

function waitWorkletFlush(timeoutMs = 300) {
  if (!pcmCaptureNode) {
    return Promise.resolve();
  }
  return new Promise((resolve) => {
    const timer = setTimeout(resolve, timeoutMs);
    stopFlushDone = () => {
      clearTimeout(timer);
      stopFlushDone = null;
      resolve();
    };
    pcmCaptureNode.port.postMessage({ flush: true });
  });
}

async function stopRecording() {
  setRecStatus(false);
  startBtn.disabled = false;
  stopBtn.disabled = true;

  stopping = true;

  if (useWorklet) {
    await waitWorkletFlush();
  } else if (pcmBuffer.length > 0) {
    flushPcmChunk();
  }

  if (socket && socket.readyState === WebSocket.OPEN) {
    socket.send(JSON.stringify({ cmd: "stop" }));
  }

  stopping = false;
  recording = false;
  teardownAudioNodes();

  if (mediaStream) {
    mediaStream.getTracks().forEach((t) => t.stop());
    mediaStream = null;
  }
}

startBtn.addEventListener("click", startRecording);
stopBtn.addEventListener("click", stopRecording);

window.addEventListener("pagehide", () => {
  if (recording) {
    stopRecording();
  }
});

const params = new URLSearchParams(window.location.search);
if (params.has("ws")) {
  serverUrlInput.value = params.get("ws");
}
