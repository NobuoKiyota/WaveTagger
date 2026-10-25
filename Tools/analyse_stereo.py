# python Tools/analyse_stereo.py <wav>... : left/right analysis of 24-bit stereo WAVs from the phone (peaks, correlation, inter-channel time difference per 0.5 s, level difference and coherence per band)
import sys, wave
import numpy as np
from scipy import signal

RATE_BANDS = [(20, 200), (200, 1000), (1000, 4000), (4000, 12000), (12000, 20000)]


def load(path):
    w = wave.open(path, 'rb')
    ch, sw, rate, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
    raw = w.readframes(n)
    assert sw == 3 and ch == 2, (ch, sw)
    b = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3).astype(np.int32)
    v = b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16)
    v = np.where(v >= 0x800000, v - 0x1000000, v).astype(np.float64) / 8388608.0
    return v.reshape(-1, 2), rate


def db(x):
    return 20 * np.log10(np.maximum(x, 1e-12))


def lag_of(l, r, maxlag=40):
    # lag (samples) at which r best matches l: positive = r is delayed relative to l
    n = len(l)
    c = signal.correlate(r, l, mode='full', method='fft')
    mid = n - 1
    seg = c[mid - maxlag: mid + maxlag + 1]
    k = int(np.argmax(np.abs(seg))) - maxlag
    norm = np.sqrt(np.sum(l * l) * np.sum(r * r)) + 1e-18
    return k, seg[k + maxlag] / norm


def report(path):
    x, rate = load(path)
    l, r = x[:, 0], x[:, 1]
    n = len(l)
    print('=' * 70)
    print(path.split('/')[-1], f'{n / rate:.1f} s  {rate} Hz')
    print(f'  peak L {db(np.max(np.abs(l))):.1f} / R {db(np.max(np.abs(r))):.1f} dBFS   rms L {db(np.sqrt(np.mean(l*l))):.1f} / R {db(np.sqrt(np.mean(r*r))):.1f} dBFS')
    clipL = int(np.sum(np.abs(l) > 0.999)); clipR = int(np.sum(np.abs(r) > 0.999))
    print(f'  samples at full scale: L {clipL}  R {clipR}')
    corr = float(np.corrcoef(l, r)[0, 1])
    print(f'  correlation {corr:.3f}   max |L-R| {db(np.max(np.abs(l - r))):.1f} dBFS')

    k, c = lag_of(l, r)
    print(f'  best lag over the whole file: {k} samples ({k / rate * 1000:.3f} ms), normalised xcorr {c:.3f}')

    # per 0.5 s windows with a sound: lag, level difference, correlation
    win = rate // 2
    rows = []
    for s in range(0, n - win, win):
        a, b = l[s:s + win], r[s:s + win]
        ra, rb = np.sqrt(np.mean(a * a)), np.sqrt(np.mean(b * b))
        if max(ra, rb) < 10 ** (-50 / 20):
            continue
        kk, cc = lag_of(a, b)
        rows.append((s / rate, db(ra) - db(rb), kk, cc, float(np.corrcoef(a, b)[0, 1])))
    print(f'  windows with a sound: {len(rows)}')
    for t, d, kk, cc, rr in rows[:30]:
        print(f'    t={t:5.1f}s  L-R {d:+6.1f} dB   lag {kk:+3d} smp ({kk / rate * 1000:+.2f} ms)  xcorr {cc:+.2f}  corr {rr:+.2f}')
    if rows:
        lags = np.array([q[2] for q in rows])
        print(f'  lag: min {lags.min()}  max {lags.max()}  distinct {len(set(lags.tolist()))}')

    # spectra / coherence
    f, Pl = signal.welch(l, rate, nperseg=4096)
    _, Pr = signal.welch(r, rate, nperseg=4096)
    _, Coh = signal.coherence(l, r, rate, nperseg=4096)
    print('  band           L-R level    coherence')
    for lo, hi in RATE_BANDS:
        m = (f >= lo) & (f < hi)
        print(f'    {lo:5d}-{hi:5d} Hz   {10 * np.log10(np.sum(Pl[m]) / max(np.sum(Pr[m]), 1e-30)):+6.1f} dB    {np.mean(Coh[m]):.2f}')

    # is it a mid/side pair? (L+R and L-R would then be the natural channels)
    m_, s_ = (l + r) / 2, (l - r) / 2
    print(f'  rms (L+R)/2 {db(np.sqrt(np.mean(m_*m_))):.1f}  (L-R)/2 {db(np.sqrt(np.mean(s_*s_))):.1f} dBFS')


for p in sys.argv[1:]:
    report(p)
