/* global sampleRate */

const DEFAULT_OUTPUT_RATE = 16000;
const DEFAULT_CHUNK_SAMPLES = 3200;

class PcmCaptureProcessor extends AudioWorkletProcessor {
  constructor(options) {
    super({
      numberOfInputs: 1,
      numberOfOutputs: 1,
      outputChannelCount: [1],
    });
    const opts = options.processorOptions || {};
    this.outputRate = opts.outputSampleRate || DEFAULT_OUTPUT_RATE;
    this.chunkSamples = opts.chunkSamples || DEFAULT_CHUNK_SAMPLES;
    this.inputRate = sampleRate;
    this.step = this.outputRate / this.inputRate;
    this.phase = 0;
    this.pending = [];
    this.enabled = true;

    this.port.onmessage = (event) => {
      const data = event.data;
      if (!data) return;
      if (typeof data.enabled === "boolean") {
        this.enabled = data.enabled;
      }
      if (data.flush) {
        this.flushPending(true);
      }
    };
  }

  downsampleInput(input) {
    for (let i = 0; i < input.length; i++) {
      this.phase += this.step;
      while (this.phase >= 1) {
        this.pending.push(input[i]);
        this.phase -= 1;
      }
    }
  }

  emitInt16(slice) {
    if (slice.length === 0) return;
    const int16 = new Int16Array(slice.length);
    for (let i = 0; i < slice.length; i++) {
      const s = Math.max(-1, Math.min(1, slice[i]));
      int16[i] = s < 0 ? s * 0x8000 : s * 0x7fff;
    }
    this.port.postMessage({ type: "pcm", buffer: int16.buffer }, [int16.buffer]);
  }

  flushChunks() {
    while (this.pending.length >= this.chunkSamples) {
      const slice = this.pending.splice(0, this.chunkSamples);
      this.emitInt16(slice);
    }
  }

  flushPending(forceAll) {
    if (forceAll) {
      if (this.pending.length > 0) {
        const slice = this.pending.splice(0, this.pending.length);
        this.emitInt16(slice);
      }
      this.port.postMessage({ type: "flushed" });
      return;
    }
    this.flushChunks();
  }

  process(inputs, outputs) {
    const channel = inputs[0] && inputs[0][0];
    const out = outputs[0] && outputs[0][0];
    if (out && channel) {
      out.set(channel);
    }
    if (!this.enabled || !channel || channel.length === 0) {
      return true;
    }
    this.downsampleInput(channel);
    this.flushChunks();
    return true;
  }
}

registerProcessor("pcm-capture", PcmCaptureProcessor);
