// Loudness (ITU-R BS.1770 / EBU R128): momentary (400 ms), short-term (3 s) and integrated (gated) in LUFS.
// Streaming: feed it blocks of any size. Works in the browser and in Node (Tools/test_loudness.js).
(function (root) {
  'use strict';

  var NONE = -Infinity;

  function toLufs(meanSquare) { return meanSquare > 0 ? -0.691 + 10 * Math.log10(meanSquare) : NONE; }

  // K-weighting: a high shelf (+4 dB above ~1.7 kHz), then the "RLB" high-pass at ~38 Hz. Coefficients for any sample rate.
  function shelf(fs) {
    var gainDb = 3.999843853973347, f0 = 1681.974450955533, q = 0.7071752369554196;
    var k = Math.tan(Math.PI * f0 / fs), vh = Math.pow(10, gainDb / 20), vb = Math.pow(vh, 0.4996667741545416);
    var a0 = 1 + k / q + k * k;
    return { b0: (vh + vb * k / q + k * k) / a0, b1: 2 * (k * k - vh) / a0, b2: (vh - vb * k / q + k * k) / a0,
             a1: 2 * (k * k - 1) / a0, a2: (1 - k / q + k * k) / a0, z1: 0, z2: 0 };
  }
  function highpass(fs) {
    var f0 = 38.13547087602444, q = 0.5003270373238773;
    var k = Math.tan(Math.PI * f0 / fs), a0 = 1 + k / q + k * k;
    return { b0: 1, b1: -2, b2: 1, a1: 2 * (k * k - 1) / a0, a2: (1 - k / q + k * k) / a0, z1: 0, z2: 0 };
  }
  function run(f, x) {
    var y = f.b0 * x + f.z1;
    f.z1 = f.b1 * x - f.a1 * y + f.z2;
    f.z2 = f.b2 * x - f.a2 * y;
    return y;
  }

  // Every channel counts with weight 1 (mono is one channel, stereo is two).
  function createMeter(sampleRate, channels) {
    var hop = Math.round(0.1 * sampleRate); // loudness is kept per 100 ms; a 400 ms block is four of them
    var filters = [];
    for (var c = 0; c < channels; c++) filters.push({ s: shelf(sampleRate), h: highpass(sampleRate) });

    var hops = [];       // sum of the K-weighted squares of every channel, per 100 ms
    var cur = 0, inHop = 0;

    function push(block) {
      var n = block[0].length;
      for (var i = 0; i < n; i++) {
        for (var ch = 0; ch < channels; ch++) {
          var y = run(filters[ch].h, run(filters[ch].s, block[ch][i]));
          cur += y * y;
        }
        if (++inHop === hop) { hops.push(cur); cur = 0; inHop = 0; }
      }
    }

    function windowLufs(count) {
      if (hops.length < 4) return NONE; // not a full 400 ms yet
      var use = Math.min(count, hops.length), sum = 0;
      for (var i = hops.length - use; i < hops.length; i++) sum += hops[i];
      return toLufs(sum / (use * hop));
    }

    function integrated() {
      if (hops.length < 4) return NONE;
      var blocks = [];
      var sum4 = hops[0] + hops[1] + hops[2];
      for (var j = 3; j < hops.length; j++) {
        sum4 += hops[j];
        blocks.push(sum4 / (4 * hop));
        sum4 -= hops[j - 3];
      }
      function gated(threshold) {
        var s = 0, n = 0;
        for (var b = 0; b < blocks.length; b++) if (toLufs(blocks[b]) > threshold) { s += blocks[b]; n++; }
        return { mean: n ? s / n : 0, n: n };
      }
      var abs = gated(-70);
      if (abs.n === 0) return NONE;
      var rel = gated(toLufs(abs.mean) - 10);
      return rel.n ? toLufs(rel.mean) : toLufs(abs.mean);
    }

    return {
      push: push,
      momentary: function () { return windowLufs(4); },
      shortTerm: function () { return windowLufs(30); }, // before 3 s: what there is (at least 400 ms)
      integrated: integrated,
      seconds: function () { return hops.length * 0.1; },
      reset: function () {
        hops.length = 0; cur = 0; inHop = 0;
        filters.forEach(function (f) { f.s.z1 = f.s.z2 = f.h.z1 = f.h.z2 = 0; });
      }
    };
  }

  var api = { createMeter: createMeter, toLufs: toLufs };
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else root.Loudness = api;
})(typeof self !== 'undefined' ? self : this);
