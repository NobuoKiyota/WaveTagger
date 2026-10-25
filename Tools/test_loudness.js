// node Tools/test_loudness.js : checks the browser recorder's loudness meter (BS.1770 / EBU R128 values)
const assert = require('assert');
const { createMeter } = require('../docs/loudness.js');

const near = (a, b, tol, what) => assert.ok(Math.abs(a - b) <= tol, `${what}: ${a} is not within ${tol} of ${b}`);

function sine(seconds, rate, freq, amp, channels) {
  const n = Math.round(seconds * rate);
  const ch = [];
  for (let c = 0; c < channels; c++) {
    const a = new Float32Array(n);
    for (let i = 0; i < n; i++) a[i] = amp * Math.sin(2 * Math.PI * freq * i / rate);
    ch.push(a);
  }
  return ch;
}
const peak = (db) => Math.pow(10, db / 20);

function feed(meter, block, chunk) {
  const n = block[0].length;
  for (let i = 0; i < n; i += chunk) meter.push(block.map((a) => a.subarray(i, Math.min(n, i + chunk))));
}

// EBU Tech 3341: a 1 kHz sine of -23 dBFS peak in both channels reads -23.0 LUFS
for (const rate of [48000, 44100]) {
  const m = createMeter(rate, 2);
  feed(m, sine(5, rate, 1000, peak(-23), 2), 2048);
  near(m.integrated(), -23.0, 0.1, `stereo reference at ${rate}`);
  near(m.momentary(), -23.0, 0.1, `momentary at ${rate}`);
  near(m.shortTerm(), -23.0, 0.1, `short-term at ${rate}`);
}

// one channel reads 3 dB lower
{
  const m = createMeter(48000, 1);
  feed(m, sine(5, 48000, 1000, peak(-23), 1), 2048);
  near(m.integrated(), -26.0, 0.1, 'mono');
}

// nothing yet: under 400 ms there is no figure
{
  const m = createMeter(48000, 2);
  feed(m, sine(0.3, 48000, 1000, peak(-23), 2), 2048);
  assert.strictEqual(m.momentary(), -Infinity);
  assert.strictEqual(m.integrated(), -Infinity);
  feed(m, sine(0.3, 48000, 1000, peak(-23), 2), 2048);
  near(m.momentary(), -23.0, 0.3, 'after 600 ms');
}

// silence is silence
{
  const m = createMeter(48000, 2);
  feed(m, [new Float32Array(48000 * 3), new Float32Array(48000 * 3)], 4096);
  assert.strictEqual(m.integrated(), -Infinity);
  assert.strictEqual(m.momentary(), -Infinity);
}

// gating: 3 s of sound then 10 s of silence is measured over the sound (a bit under it: the blocks that straddle the edge count)
{
  const m = createMeter(48000, 2);
  feed(m, sine(3, 48000, 1000, peak(-20), 2), 2048);
  feed(m, [new Float32Array(48000 * 10), new Float32Array(48000 * 10)], 2048);
  const i = m.integrated();
  assert.ok(i > -20.8 && i < -19.9, `gated integrated ${i}`);
  assert.strictEqual(m.momentary(), -Infinity); // the last 400 ms are silent
}

// momentary follows a sudden loud sound faster than short-term
{
  const m = createMeter(48000, 2);
  feed(m, sine(4, 48000, 1000, peak(-40), 2), 2048);
  feed(m, sine(0.5, 48000, 1000, peak(-10), 2), 2048);
  const mo = m.momentary(), st = m.shortTerm();
  near(mo, -10.0, 0.3, 'momentary after a loud half second');
  assert.ok(st < mo - 5, `short-term (${st}) is slower than momentary (${mo})`);
}

// the chunk size does not matter, and reset starts over
{
  const block = sine(4, 48000, 440, peak(-18), 2);
  const a = createMeter(48000, 2), b = createMeter(48000, 2);
  feed(a, block, 128);
  feed(b, block, 7919);
  near(a.integrated(), b.integrated(), 1e-9, 'chunk size');
  a.reset();
  assert.strictEqual(a.integrated(), -Infinity);
  feed(a, block, 2048);
  near(a.integrated(), b.integrated(), 1e-9, 'after reset');
}

console.log('loudness meter: all checks passed');
