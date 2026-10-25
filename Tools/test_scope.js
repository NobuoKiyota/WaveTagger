// node Tools/test_scope.js : checks the stereo scope maths of the browser recorder (polar sample, Lissajous, correlation)
const assert = require('assert');
const { polarPoint, lissajousPoint, createCorrelator, RANGE_DB } = require('../docs/scope.js');

const near = (a, b, tol, what) => assert.ok(Math.abs(a - b) <= tol, `${what}: ${a} is not within ${tol} of ${b}`);
const angleDeg = (p) => Math.atan2(p.x, p.y) * 180 / Math.PI;       // 0 = up, +90 = right
const radius = (p) => Math.hypot(p.x, p.y);

// polar: full-scale mono is straight up at the edge
let p = polarPoint(1, 1);
near(p.x, 0, 1e-9, 'mono x'); near(p.y, 1, 1e-9, 'mono y');

// one channel alone leans 45 degrees to its side (L left, R right), 3 dB below the edge
p = polarPoint(1, 0);  near(angleDeg(p), -45, 1e-9, 'L alone angle'); near(radius(p), (-3.0103 + RANGE_DB) / RANGE_DB, 1e-4, 'L alone radius');
p = polarPoint(0, 1);  near(angleDeg(p), 45, 1e-9, 'R alone angle');

// opposite phase lies on the side (the horizon)
p = polarPoint(1, -1); near(angleDeg(p), -90, 1e-9, 'out of phase angle'); near(p.y, 0, 1e-9, 'out of phase y');

// negative polarity folds onto the upper half: -1,-1 is the same place as 1,1
p = polarPoint(-1, -1); near(p.x, 0, 1e-9, 'negative mono x'); near(p.y, 1, 1e-9, 'negative mono y');
for (const [l, r] of [[0.3, -0.7], [-0.2, 0.9], [-0.5, -0.1]]) assert.ok(polarPoint(l, r).y >= -1e-12, 'always the upper half');

// the radius is the level in dB: -20 dBFS mono is at (54-20)/54, and the angle stays up
p = polarPoint(0.1, 0.1); near(p.y, (RANGE_DB - 20) / RANGE_DB, 1e-9, '-20 dB mono radius'); near(p.x, 0, 1e-9, '-20 dB mono x');

// too quiet / silence: nothing to draw; louder than full scale stays on the edge
assert.strictEqual(polarPoint(0, 0), null);
assert.strictEqual(polarPoint(1e-5, 1e-5), null);  // -100 dBFS
near(radius(polarPoint(3, 3)), 1, 1e-12, 'clipped to the edge');

// Lissajous: mid is up, side to the right, scaled by the gain and kept inside the circle
p = lissajousPoint(0.5, 0.5, 1); near(p.x, 0, 1e-12, 'liss mono x'); near(p.y, 0.5, 1e-12, 'liss mono y');
p = lissajousPoint(1, 0, 1);     near(p.x, -0.5, 1e-12, 'liss L alone x'); near(p.y, 0.5, 1e-12, 'liss L alone y');
p = lissajousPoint(0.5, 0.5, 4); near(p.y, 1, 1e-12, 'liss gain clamps');
p = lissajousPoint(1, -1, 1);    near(p.x, -1, 1e-12, 'liss out of phase x'); near(p.y, 0, 1e-12, 'liss out of phase y');

// correlation
function noise(n, seed) {
  const a = new Float32Array(n); let s = seed >>> 0;
  for (let i = 0; i < n; i++) { s = (Math.imul(s, 1664525) + 1013904223) >>> 0; a[i] = s / 2147483648 - 1; }
  return a;
}
const rate = 48000, n = rate * 2;
const x = noise(n, 1), y = noise(n, 99);
const feed = (c, l, r) => { for (let i = 0; i < l.length; i += 2048) c.push(l.subarray(i, i + 2048), r.subarray(i, i + 2048), rate); };

let c = createCorrelator(); assert.strictEqual(c.value(), null);               // nothing heard yet
feed(c, x, x);                near(c.value(), 1, 1e-6, 'same signal');
c = createCorrelator(); feed(c, x, x.map((v) => -v)); near(c.value(), -1, 1e-6, 'opposite signal');
c = createCorrelator(); feed(c, x, y);                assert.ok(Math.abs(c.value()) < 0.1, `independent noise: ${c.value()}`);
c = createCorrelator(); feed(c, x, new Float32Array(n)); near(c.value(), 0, 1e-12, 'one channel only');
c = createCorrelator(); feed(c, new Float32Array(n), new Float32Array(n)); assert.strictEqual(c.value(), null); // silence
c = createCorrelator(); feed(c, x, x.map((v) => 0.5 * v)); near(c.value(), 1, 1e-6, 'level difference is not decorrelation');

// it follows the signal: after noise that was identical, a second of opposite signal reads close to -1
c = createCorrelator(); feed(c, x, x); feed(c, y, y.map((v) => -v)); assert.ok(c.value() < -0.95, `follows the signal: ${c.value()}`);
c.reset(); assert.strictEqual(c.value(), null);

console.log('scope: all checks passed');
