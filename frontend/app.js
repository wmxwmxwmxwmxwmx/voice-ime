const SAMPLE_RATE = 16000;
const CHUNK_MS = 200;
const SAMPLES_PER_CHUNK = (SAMPLE_RATE * CHUNK_MS) / 1000;

const startBtn = document.getElementById("startBtn");
const stopBtn = document.getElementById("stopBtn");
const wsStatus = document.getElementById("wsStatus");
const recStatus = document.getElementById("recStatus");
const resultEl = document.getElementById("result");
const serverUrlInput = document.getElementById("serverUrl");
const languageSelect = document.getElementById("language");

let socket = null;
let audioContext = null;
let mediaStream = null;
let processor = null;
let pcmBuffer = [];
let finalText = "";
let partialText = "";
let recording = false;

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
  const int16 = floatToInt16(merged);
  socket.send(int16.buffer);
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

    mediaStream = await navigator.mediaDevices.getUserMedia({
      audio: {
        channelCount: 1,
        sampleRate: SAMPLE_RATE,
        echoCancellation: true,
        noiseSuppression: true,
      },
    });

    audioContext = new AudioContext({ sampleRate: SAMPLE_RATE });
    const source = audioContext.createMediaStreamSource(mediaStream);

    // ScriptProcessor 已弃用，MVP 阶段暂用
    processor = audioContext.createScriptProcessor(4096, 1, 1);
    processor.onaudioprocess = (e) => {
      if (!recording) return;
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

    // 静音节点：保持处理链运行，避免麦克风回放
    const silent = audioContext.createGain();
    silent.gain.value = 0;
    source.connect(processor);
    processor.connect(silent);
    silent.connect(audioContext.destination);

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

function stopRecording() {
  recording = false;
  setRecStatus(false);
  startBtn.disabled = false;
  stopBtn.disabled = true;

  if (pcmBuffer.length > 0) {
    flushPcmChunk();
  }

  if (socket && socket.readyState === WebSocket.OPEN) {
    socket.send(JSON.stringify({ cmd: "stop" }));
  }

  if (processor) {
    processor.disconnect();
    processor.onaudioprocess = null;
    processor = null;
  }

  if (audioContext) {
    audioContext.close();
    audioContext = null;
  }

  if (mediaStream) {
    mediaStream.getTracks().forEach((t) => t.stop());
    mediaStream = null;
  }
}

startBtn.addEventListener("click", startRecording);
stopBtn.addEventListener("click", stopRecording);

const params = new URLSearchParams(window.location.search);
if (params.has("ws")) {
  serverUrlInput.value = params.get("ws");
}
