// Stereo scope for the recorder: a polar sample display (dB radius, half disc) and a Lissajous display, plus a left/right correlation.
// The maths (polarPoint, lissajousPoint, createCorrelator) is plain and is checked in Node (Tools/test_scope.js); create() draws on a canvas.
(function (root) {
  'use strict';

  var RANGE_DB = 54;           // the polar display covers 0 dBFS (edge) down to -54 dBFS (centre)
  var S = Math.SQRT1_2;

  // (l, r) -> a point of the half disc, or null when it is too quiet to show.
  // Straight up is mono (L = R), +-45 degrees is one channel alone, +-90 degrees is out of phase. The radius is the level in dB
  // (full scale mono = the edge), so quiet material is not a dot in the middle. Samples of negative polarity fold onto the half disc.
  function polarPoint(l, r) {
    var m = (l + r) * S, s = (r - l) * S;
    if (m < 0) { m = -m; s = -s; }
    var rho = Math.sqrt(m * m + s * s) * S; // 1 = full-scale mono
    if (!(rho > 0)) return null;
    var rad = (20 * Math.log10(rho) + RANGE_DB) / RANGE_DB;
    if (!(rad > 0)) return null;
    if (rad > 1) rad = 1;
    var th = Math.atan2(s, m);              // 0 = up, +90 degrees = right
    return { x: rad * Math.sin(th), y: rad * Math.cos(th) };
  }

  // The classic goniometer: mid up, side to the right, scaled by gain (L alone leans to the upper left, R alone to the upper right).
  function lissajousPoint(l, r, gain) {
    var x = (r - l) * 0.5 * gain, y = (l + r) * 0.5 * gain;
    x = x > 1 ? 1 : x < -1 ? -1 : x;
    y = y > 1 ? 1 : y < -1 ? -1 : y;
    return { x: x, y: y };
  }

  // Correlation of left and right over roughly the last half second: +1 the same signal (mono), 0 unrelated, -1 opposite.
  function createCorrelator(seconds) {
    var tau = seconds || 0.5;
    var sll = 0, srr = 0, slr = 0;
    return {
      push: function (l, r, rate) {
        var n = Math.min(l.length, r.length);
        var a = 0, b = 0, c = 0;
        for (var i = 0; i < n; i++) { a += l[i] * l[i]; b += r[i] * r[i]; c += l[i] * r[i]; }
        var k = Math.exp(-n / (rate * tau));
        sll = sll * k + a; srr = srr * k + b; slr = slr * k + c;
      },
      value: function () {
        if (sll + srr < 1e-9) return null;           // silence: nothing to say
        var den = Math.sqrt(sll * srr);
        if (den < 1e-12) return 0;                   // one channel only
        var v = slr / den;
        return v > 1 ? 1 : v < -1 ? -1 : v;
      },
      reset: function () { sll = srr = slr = 0; }
    };
  }

  var BG = '#1d2026', FADE = 'rgba(29,32,38,0.34)', DOT = 'rgba(96,205,255,0.62)';
  var GRID = 'rgba(139,146,156,0.38)', GRID2 = 'rgba(139,146,156,0.18)', TEXT = 'rgba(139,146,156,0.95)';
  var NAMES = { polar: 'Polar Sample', lissajous: 'Lissajous' };

  function create(canvas) {
    var g = canvas.getContext('2d');
    var mode = 'polar', pending = [], corr = createCorrelator(), peakEnv = 0, gain = 1;
    var W = 0, H = 0, dpr = 1;

    function size() {
      var d = window.devicePixelRatio || 1;
      var w = Math.max(1, Math.round(canvas.clientWidth * d)), h = Math.max(1, Math.round(canvas.clientHeight * d));
      if (w === W && h === H && d === dpr) return false;
      W = w; H = h; dpr = d; canvas.width = w; canvas.height = h;
      return true;
    }

    function geometry() {
      if (mode === 'polar') {
        var R = Math.min(W / 2 - 10 * dpr, H - 30 * dpr);
        return { cx: W / 2, cy: H - 16 * dpr, R: R };
      }
      return { cx: W / 2, cy: H / 2 + 6 * dpr, R: Math.min(W, H - 12 * dpr) / 2 - 14 * dpr };
    }

    function label(t, x, y, align) {
      g.fillStyle = TEXT; g.textAlign = align || 'center'; g.fillText(t, x, y);
    }

    function grid(geo) {
      var cx = geo.cx, cy = geo.cy, R = geo.R;
      g.font = (11 * dpr) + 'px system-ui, sans-serif';
      g.lineWidth = Math.max(1, dpr);
      if (R < 12) return;
      if (mode === 'polar') {
        [1, 0.667, 0.333].forEach(function (k, i) {
          g.strokeStyle = i === 0 ? GRID : GRID2;
          g.beginPath(); g.arc(cx, cy, R * k, Math.PI, 2 * Math.PI); g.stroke();
        });
        g.strokeStyle = GRID2;
        g.beginPath();
        g.moveTo(cx - R, cy); g.lineTo(cx + R, cy);
        g.moveTo(cx, cy); g.lineTo(cx, cy - R);
        g.moveTo(cx, cy); g.lineTo(cx - R * S, cy - R * S);
        g.moveTo(cx, cy); g.lineTo(cx + R * S, cy - R * S);
        g.stroke();
        label('L', cx - R * S - 6 * dpr, cy - R * S - 2 * dpr);
        label('R', cx + R * S + 6 * dpr, cy - R * S - 2 * dpr);
        label('-18', cx + R * 0.667 + 2 * dpr, cy + 12 * dpr, 'left');
        label('-36', cx + R * 0.333 + 2 * dpr, cy + 12 * dpr, 'left');
        label('0 dB', cx + R, cy + 12 * dpr, 'right');
        label('M', cx, cy - R - 4 * dpr);
      } else {
        g.strokeStyle = GRID; g.beginPath(); g.arc(cx, cy, R, 0, 2 * Math.PI); g.stroke();
        g.strokeStyle = GRID2;
        g.beginPath();
        g.moveTo(cx - R, cy); g.lineTo(cx + R, cy);
        g.moveTo(cx, cy - R); g.lineTo(cx, cy + R);
        g.moveTo(cx - R * S, cy - R * S); g.lineTo(cx + R * S, cy + R * S);
        g.moveTo(cx + R * S, cy - R * S); g.lineTo(cx - R * S, cy + R * S);
        g.stroke();
        label('L', cx - R * S - 8 * dpr, cy - R * S);
        label('R', cx + R * S + 8 * dpr, cy - R * S);
        label('M', cx, cy - R - 4 * dpr);
        label('+' + Math.round(20 * Math.log10(gain)) + ' dB', W - 6 * dpr, 14 * dpr, 'right');
      }
      label(NAMES[mode], 6 * dpr, 14 * dpr, 'left');
    }

    function paintPoints(geo) {
      g.fillStyle = DOT;
      var dot = Math.max(1.5, 1.6 * dpr);
      for (var b = 0; b < pending.length; b++) {
        var l = pending[b][0], r = pending[b][1];
        var n = Math.min(l.length, r.length), step = Math.max(1, Math.floor(n / 320));
        for (var i = 0; i < n; i += step) {
          var p = mode === 'polar' ? polarPoint(l[i], r[i]) : lissajousPoint(l[i], r[i], gain);
          if (!p) continue;
          g.fillRect(geo.cx + p.x * geo.R - dot / 2, geo.cy - p.y * geo.R - dot / 2, dot, dot);
        }
      }
    }

    // Draws only when there is something new (or the size changed): the trail fades with every new block.
    function draw() {
      var resized = size();
      if (!pending.length && !resized) return;
      var geo = geometry();
      g.fillStyle = resized ? BG : FADE;
      g.fillRect(0, 0, W, H);
      if (pending.length && geo.R >= 12) paintPoints(geo);
      pending.length = 0;
      grid(geo);
    }

    function clear() {
      size();
      pending.length = 0; peakEnv = 0; gain = 1; corr.reset();
      g.fillStyle = BG; g.fillRect(0, 0, W, H);
      grid(geometry());
    }

    return {
      push: function (block, rate) {
        if (!block || block.length < 2) return;
        var l = block[0], r = block[1], pk = 0;
        for (var i = 0; i < l.length; i++) {
          var a = l[i] < 0 ? -l[i] : l[i], b = r[i] < 0 ? -r[i] : r[i];
          if (a > pk) pk = a; if (b > pk) pk = b;
        }
        peakEnv = Math.max(peakEnv * 0.995, pk);              // the Lissajous scale follows the loudest recent peak
        gain = Math.min(60, 0.9 / Math.max(peakEnv, 0.015));
        corr.push(l, r, rate);
        pending.push(block);
        if (pending.length > 4) pending.shift();
      },
      draw: draw,
      clear: clear,
      relayout: function () { if (size()) { pending.length = 0; var geo = geometry(); g.fillStyle = BG; g.fillRect(0, 0, W, H); grid(geo); } },
      mode: function () { return mode; },
      setMode: function (m) { mode = m === 'lissajous' ? 'lissajous' : 'polar'; clear(); },
      correlation: function () { return corr.value(); }
    };
  }

  var api = { create: create, polarPoint: polarPoint, lissajousPoint: lissajousPoint, createCorrelator: createCorrelator, RANGE_DB: RANGE_DB };
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else root.Scope = api;
})(typeof self !== 'undefined' ? self : this);
