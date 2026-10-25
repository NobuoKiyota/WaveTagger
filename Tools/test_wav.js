// node Tools/test_wav.js : checks the browser recorder's WAV writer against an independent parse
const assert = require('assert');
const { encodeWav, encodeWav24, peakOf, peaksOf } = require('../docs/wav.js');

const L = Float32Array.from([0, 0.5, -0.5, 1, -1, 2, -2]);
const R = Float32Array.from([0.25, -0.25, 0, 0.999999, -0.999999, 0.1, -0.1]);
const enc = encodeWav24([[L.slice(0, 3), R.slice(0, 3)], [L.slice(3), R.slice(3)]], 2, 48000);
const buf = Buffer.concat(enc.parts.map((p) => Buffer.from(p)));

assert.strictEqual(buf.toString('ascii', 0, 4), 'RIFF');
assert.strictEqual(buf.toString('ascii', 8, 12), 'WAVE');
assert.strictEqual(buf.readUInt32LE(4), buf.length - 8, 'RIFF size');
assert.strictEqual(buf.readUInt16LE(20), 1, 'PCM');
assert.strictEqual(buf.readUInt16LE(22), 2, 'channels');
assert.strictEqual(buf.readUInt32LE(24), 48000, 'rate');
assert.strictEqual(buf.readUInt32LE(28), 48000 * 6, 'byte rate');
assert.strictEqual(buf.readUInt16LE(32), 6, 'block align');
assert.strictEqual(buf.readUInt16LE(34), 24, 'bits');
assert.strictEqual(buf.toString('ascii', 36, 40), 'data');
assert.strictEqual(buf.readUInt32LE(40), 7 * 6, 'data size');
assert.strictEqual(enc.frames, 7);

const s = (o) => buf.readIntLE(44 + o, 3);
assert.strictEqual(s(0), 0);                       // L0
assert.strictEqual(s(3), Math.round(0.25 * 8388607)); // R0
assert.strictEqual(s(6), Math.round(0.5 * 8388607));  // L1
assert.strictEqual(s(12), Math.round(-0.5 * 8388607)); // L2
assert.strictEqual(s(18), 8388607);                // L3 = +1 -> full scale
assert.strictEqual(s(24), -8388607);               // L4 = -1
assert.strictEqual(s(30), 8388607);                // L5 = 2 is clipped, not wrapped
assert.strictEqual(s(36), -8388607);               // L6 = -2
assert.strictEqual(peakOf([L, R]), 2);

// 16 bit: header, size, scaling, clipping, and a dither that is small and does not drift
{
  const mono = Float32Array.from([0, 0.5, -0.5, 1, -1, 2, -2]);
  const e16 = encodeWav([[mono]], 1, 44100, 16);
  const b = Buffer.concat(e16.parts.map((p) => Buffer.from(p)));
  assert.strictEqual(b.readUInt16LE(22), 1, '16: channels');
  assert.strictEqual(b.readUInt32LE(24), 44100, '16: rate');
  assert.strictEqual(b.readUInt32LE(28), 44100 * 2, '16: byte rate');
  assert.strictEqual(b.readUInt16LE(32), 2, '16: block align');
  assert.strictEqual(b.readUInt16LE(34), 16, '16: bits');
  assert.strictEqual(b.readUInt32LE(40), 7 * 2, '16: data size');
  assert.strictEqual(b.readUInt32LE(4), b.length - 8, '16: RIFF size');
  assert.strictEqual(e16.bits, 16);
  const v = (o) => b.readInt16LE(44 + o);
  assert.ok(Math.abs(v(2) - 16384) <= 1, '16: 0.5 -> about half scale');
  assert.ok(Math.abs(v(4) + 16384) <= 1, '16: -0.5');
  assert.ok(v(6) >= 32766 && v(6) <= 32767, '16: +1 -> full scale');
  assert.ok(v(8) <= -32767, '16: -1');
  assert.ok(v(10) >= 32766 && v(12) <= -32767, '16: over-range is clipped, not wrapped');

  // quiet sine of 3 LSB: after dithering the average error is ~0 and the error stays within 1.5 LSB
  const n = 48000, q = new Float32Array(n);
  for (let i = 0; i < n; i++) q[i] = 3 * Math.sin(2 * Math.PI * 440 * i / 48000) / 32768;
  const eq = encodeWav([[q]], 1, 48000, 16);
  const bq = Buffer.concat(eq.parts.map((p) => Buffer.from(p)));
  let sum = 0, worst = 0;
  for (let i = 0; i < n; i++) { const err = bq.readInt16LE(44 + i * 2) - q[i] * 32768; sum += err; worst = Math.max(worst, Math.abs(err)); }
  assert.ok(Math.abs(sum / n) < 0.05, 'dither: mean error ' + sum / n);
  assert.ok(worst <= 1.5 + 1e-6, 'dither: worst error ' + worst);

  // the same recording gives the same file
  const again = Buffer.concat(encodeWav([[q]], 1, 48000, 16).parts.map((p) => Buffer.from(p)));
  assert.ok(again.equals(bq), 'deterministic');

  // 24 bit is unchanged by the generalisation
  const e24 = Buffer.concat(encodeWav([[mono]], 1, 48000, 24).parts.map((p) => Buffer.from(p)));
  assert.strictEqual(e24.readUInt16LE(34), 24);
  assert.strictEqual(e24.readIntLE(44 + 3, 3), Math.round(0.5 * 8388607));
  assert.deepStrictEqual(peaksOf([L, R]), [2, Math.fround(0.999999)]);
}

console.log('wav encoder: all checks passed');
