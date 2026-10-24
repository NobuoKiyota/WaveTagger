// 24-bit PCM WAV writer. Works in the browser and in Node (for the tests in Tools/test_wav.js).
(function (root) {
  'use strict';

  function clamp(v) { return v > 1 ? 1 : v < -1 ? -1 : v; }

  // chunks: array of arrays (one Float32Array per channel) in recording order.
  // Returns an array of Uint8Array parts (header first), so a Blob can be built without one giant copy.
  function encodeWav24(chunks, channels, sampleRate) {
    var frames = 0;
    for (var i = 0; i < chunks.length; i++) frames += chunks[i][0].length;
    var bytesPerFrame = channels * 3;
    var dataBytes = frames * bytesPerFrame;
    if (dataBytes > 0xFFFFFFFF - 64) throw new Error('recording too long for a WAV file');

    var head = new DataView(new ArrayBuffer(44));
    function str(o, s) { for (var k = 0; k < s.length; k++) head.setUint8(o + k, s.charCodeAt(k)); }
    str(0, 'RIFF'); head.setUint32(4, 36 + dataBytes, true); str(8, 'WAVE');
    str(12, 'fmt '); head.setUint32(16, 16, true); head.setUint16(20, 1, true); head.setUint16(22, channels, true);
    head.setUint32(24, sampleRate, true); head.setUint32(28, sampleRate * bytesPerFrame, true);
    head.setUint16(32, bytesPerFrame, true); head.setUint16(34, 24, true);
    str(36, 'data'); head.setUint32(40, dataBytes, true);

    var parts = [new Uint8Array(head.buffer)];
    for (var c = 0; c < chunks.length; c++) {
      var block = chunks[c];
      var n = block[0].length;
      var out = new Uint8Array(n * bytesPerFrame);
      var p = 0;
      for (var f = 0; f < n; f++) {
        for (var ch = 0; ch < channels; ch++) {
          var v = Math.round(clamp(block[ch][f]) * 8388607);
          out[p++] = v & 0xFF;
          out[p++] = (v >> 8) & 0xFF;
          out[p++] = (v >> 16) & 0xFF;
        }
      }
      parts.push(out);
    }
    return { parts: parts, frames: frames, dataBytes: dataBytes };
  }

  function peakOf(block) {
    var m = 0;
    for (var ch = 0; ch < block.length; ch++) {
      var a = block[ch];
      for (var i = 0; i < a.length; i++) { var v = a[i] < 0 ? -a[i] : a[i]; if (v > m) m = v; }
    }
    return m;
  }

  var api = { encodeWav24: encodeWav24, peakOf: peakOf };
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else root.WavEnc = api;
})(typeof self !== 'undefined' ? self : this);
