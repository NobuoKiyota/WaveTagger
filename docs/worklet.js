// Captures raw input blocks and hands them to the page. One message per ~2048 frames (batched) keeps the main thread calm.
class RecorderProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.buf = null;
    this.fill = 0;
    this.size = 2048;
    this.port.onmessage = (e) => {
      if (e.data === 'flush') {
        if (this.buf && this.fill > 0) {
          const out = this.buf.map((b) => b.slice(0, this.fill));
          this.port.postMessage(out, out.map((b) => b.buffer));
          this.fill = 0;
        }
        this.port.postMessage('flushed');
      }
    };
  }

  process(inputs) {
    const input = inputs[0];
    if (!input || input.length === 0) return true;

    if (!this.buf || this.buf.length !== input.length) {
      this.buf = input.map(() => new Float32Array(this.size));
      this.fill = 0;
    }

    const n = input[0].length;
    for (let c = 0; c < input.length; c++) this.buf[c].set(input[c], this.fill);
    this.fill += n;

    if (this.fill + 128 > this.size) {
      const out = this.buf.map((b) => b.slice(0, this.fill));
      this.port.postMessage(out, out.map((b) => b.buffer));
      this.fill = 0;
    }
    return true;
  }
}

registerProcessor('recorder', RecorderProcessor);
