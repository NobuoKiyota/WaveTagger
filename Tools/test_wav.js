// node Tools/test_wav.js : checks the browser recorder's WAV writer against an independent parse
const assert = require('assert');
const { encodeWav24, peakOf } = require('../docs/wav.js');

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
console.log('wav encoder: all checks passed');
