// PCM WAV writer (24 or 16 bit). Works in the browser and in Node (for the tests in Tools/test_wav.js).
(function (root) {
  'use strict';

  function clamp(v) { return v > 1 ? 1 : v < -1 ? -1 : v; }

  // chunks: array of arrays (one Float32Array per channel) in recording order.
  // Returns an array of Uint8Array parts (header first), so a Blob can be built without one giant copy.
  // bits: 24 (default) or 16. 16 bit gets TPDF dither (+-1 LSB) so quiet passages do not turn into quantisation distortion.
  function encodeWav(chunks, channels, sampleRate, bits) {
    bits = bits === 16 ? 16 : 24;
    var bytes = bits / 8;
    var frames = 0;
    for (var i = 0; i < chunks.length; i++) frames += chunks[i][0].length;
    var bytesPerFrame = channels * bytes;
    var dataBytes = frames * bytesPerFrame;
    if (dataBytes > 0xFFFFFFFF - 64) throw new Error('recording too long for a WAV file');

    var head = new DataView(new ArrayBuffer(44));
    function str(o, s) { for (var k = 0; k < s.length; k++) head.setUint8(o + k, s.charCodeAt(k)); }
    str(0, 'RIFF'); head.setUint32(4, 36 + dataBytes, true); str(8, 'WAVE');
    str(12, 'fmt '); head.setUint32(16, 16, true); head.setUint16(20, 1, true); head.setUint16(22, channels, true);
    head.setUint32(24, sampleRate, true); head.setUint32(28, sampleRate * bytesPerFrame, true);
    head.setUint16(32, bytesPerFrame, true); head.setUint16(34, bits, true);
    str(36, 'data'); head.setUint32(40, dataBytes, true);

    var seed = 0x9E3779B9; // a small deterministic generator: the same recording always gives the same file
    function rnd() { seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0; return seed / 4294967296; }

    var parts = [new Uint8Array(head.buffer)];
    for (var c = 0; c < chunks.length; c++) {
      var block = chunks[c];
      var n = block[0].length;
      var out = new Uint8Array(n * bytesPerFrame);
      var p = 0;
      for (var f = 0; f < n; f++) {
        for (var ch = 0; ch < channels; ch++) {
          if (bits === 24) {
            var v = Math.round(clamp(block[ch][f]) * 8388607);
            out[p++] = v & 0xFF;
            out[p++] = (v >> 8) & 0xFF;
            out[p++] = (v >> 16) & 0xFF;
          } else {
            var w = Math.round(clamp(block[ch][f]) * 32767 + (rnd() - rnd()));
            w = w > 32767 ? 32767 : w < -32768 ? -32768 : w;
            out[p++] = w & 0xFF;
            out[p++] = (w >> 8) & 0xFF;
          }
        }
      }
      parts.push(out);
    }
    return { parts: parts, frames: frames, dataBytes: dataBytes, bits: bits };
  }

  function encodeWav24(chunks, channels, sampleRate) { return encodeWav(chunks, channels, sampleRate, 24); }

  // The largest absolute sample of a block (all channels), and of each channel.
  function peakOf(block) {
    var m = 0;
    for (var ch = 0; ch < block.length; ch++) {
      var a = block[ch];
      for (var i = 0; i < a.length; i++) { var v = a[i] < 0 ? -a[i] : a[i]; if (v > m) m = v; }
    }
    return m;
  }
  function peaksOf(block) {
    var out = [];
    for (var ch = 0; ch < block.length; ch++) {
      var a = block[ch], m = 0;
      for (var i = 0; i < a.length; i++) { var v = a[i] < 0 ? -a[i] : a[i]; if (v > m) m = v; }
      out.push(m);
    }
    return out;
  }

  var api = { encodeWav: encodeWav, encodeWav24: encodeWav24, peakOf: peakOf, peaksOf: peaksOf };
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else root.WavEnc = api;
})(typeof self !== 'undefined' ? self : this);
