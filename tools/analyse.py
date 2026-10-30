import sys, wave, numpy as np, os, glob
def load(p):
    """Read 16/24/32-bit PCM or 32-bit float WAV, including WAVE_FORMAT_EXTENSIBLE."""
    import struct
    data = open(p, 'rb').read()
    pos, fmt, raw = 12, None, None
    while pos + 8 <= len(data):
        cid, size = data[pos:pos+4], struct.unpack_from('<I', data, pos + 4)[0]
        body = data[pos+8:pos+8+size]
        if cid == b'fmt ':
            tag, ch, sr, _, _, bits = struct.unpack_from('<HHIIHH', body, 0)
            if tag == 0xFFFE: tag = struct.unpack_from('<H', body, 24)[0]
            fmt = (tag, ch, sr, bits)
        elif cid == b'data':
            raw = body
        pos += 8 + size + (size & 1)
    tag, ch, sr, bits = fmt
    if tag == 3:
        x = np.frombuffer(raw, dtype='<f4').astype(np.float64)
    elif bits == 24:
        b = np.frombuffer(raw[: len(raw) // 3 * 3], dtype=np.uint8).reshape(-1, 3)
        x = (b[:, 0].astype(np.int32) | (b[:, 1].astype(np.int32) << 8) | (b[:, 2].astype(np.int32) << 16))
        x = np.where(x >= 1 << 23, x - (1 << 24), x).astype(np.float64) / (1 << 23)
    elif bits == 16:
        x = np.frombuffer(raw, dtype='<i2').astype(np.float64) / 32768.0
    else:
        x = np.frombuffer(raw, dtype='<i4').astype(np.float64) / 2147483648.0
    return x[: len(x) // ch * ch].reshape(-1, ch), sr
def db(v): return 20 * np.log10(max(v, 1e-12))
src = sys.argv[1]
print(f"{'file':20s} {'peak':>6s} {'lead ms':>7s} {'-60dB at':>8s} {'end lvl':>8s} {'last50ms':>8s} {'decay dB/s':>10s} {'centroid':>8s} {'corrLR':>6s} {'dc':>7s}")
for p in sorted(glob.glob(os.path.join(src, '*.wav'))):
    x, sr = load(p)
    m = x.mean(1); a = np.abs(x).max(1)
    pk = a.max()
    lead = np.argmax(a > pk * 0.05) / sr * 1000
    env = np.sqrt(np.convolve(m * m, np.ones(480) / 480, 'same'))
    ip = np.argmax(env)
    thr = env[ip] * 10 ** (-60 / 20)
    below = np.where(env[ip:] < thr)[0]
    t60 = (ip + below[0]) / sr if len(below) else float('nan')
    endlvl = db(np.abs(x[-480:]).max() / pk)
    last50 = db(np.sqrt(np.mean(x[-2400:] ** 2)) / pk)
    # decay rate over the final 30 %
    s0 = int(len(env) * 0.7); e0 = len(env) - 480
    slope = (db(env[e0]) - db(env[s0])) / ((e0 - s0) / sr) if e0 > s0 else 0
    seg = m[int(lead * sr / 1000): int(lead * sr / 1000) + 4800]
    sp = np.abs(np.fft.rfft(seg * np.hanning(len(seg)))); f = np.fft.rfftfreq(len(seg), 1 / sr)
    cen = (sp * f).sum() / sp.sum()
    c = np.corrcoef(x[:, 0], x[:, 1])[0, 1]
    print(f"{os.path.basename(p):20s} {db(pk):6.1f} {lead:7.1f} {t60:8.2f} {endlvl:8.1f} {last50:8.1f} {slope:10.1f} {cen:8.0f} {c:6.2f} {x.mean():+7.4f}")
