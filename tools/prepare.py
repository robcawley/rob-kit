"""Prepare ROB KIT samples and generate Source/DSP/KitMap.inc.

Rob's samples: 8 Hz DC high-pass, 30 ms cosine end fade, levels untouched (they are the kit balance).
SoCal fills: apply EXS zone volume, trim tails where they fall below -75 dB, then calibrate each
articulation's loudest layer against the nearest equivalent sound in Rob's kit."""
import json, os, sys, struct, numpy as np
sys.path.insert(0, os.path.dirname(__file__))
from analyse import load

ROOT = os.path.join(os.path.dirname(__file__), '..')
SRC_ROB = sys.argv[1]
OUT_ROB = os.path.join(ROOT, 'Samples', 'Rob')
SOCAL = os.path.join(ROOT, 'Samples', 'SoCal')
OUT_SOCAL = os.path.join(ROOT, 'Samples', 'Fill')
os.makedirs(OUT_ROB, exist_ok=True); os.makedirs(OUT_SOCAL, exist_ok=True)

def write(path, x, sr):
    x = np.ascontiguousarray(x.astype('<f4'))
    ch = x.shape[1]; data = x.tobytes()
    hdr = b'RIFF' + struct.pack('<I', 36 + len(data)) + b'WAVE'
    hdr += b'fmt ' + struct.pack('<IHHIIHH', 16, 3, ch, sr, sr * ch * 4, ch * 4, 32)
    hdr += b'data' + struct.pack('<I', len(data))
    open(path, 'wb').write(hdr + data)

def hp(x, sr, f=8.0):
    a = np.exp(-2 * np.pi * f / sr); y = np.zeros_like(x); px = np.zeros(x.shape[1]); py = np.zeros(x.shape[1])
    for n in range(len(x)):
        py = a * (py + x[n] - px); px = x[n]; y[n] = py
    return y

def fade_end(x, sr, ms=30):
    n = min(len(x), int(sr * ms / 1000)); w = 0.5 + 0.5 * np.cos(np.linspace(0, np.pi, n))
    x[-n:] *= w[:, None]; return x

def trim_tail(x, sr, floor_db=-75):
    env = np.sqrt(np.convolve((x ** 2).mean(1), np.ones(480) / 480, 'same'))
    pk = env.max(); idx = np.where(env > pk * 10 ** (floor_db / 20))[0]
    end = min(len(x), idx[-1] + int(0.05 * sr)) if len(idx) else len(x)
    return fade_end(x[:end].copy(), sr, 60)

def rms_head(x, sr, ms=150):
    return float(np.sqrt(np.mean(x[: int(sr * ms / 1000)] ** 2)))

rob = {}
for f in sorted(os.listdir(SRC_ROB)):
    if not f.lower().endswith('.wav'): continue
    x, sr = load(os.path.join(SRC_ROB, f))
    x = fade_end(hp(x, sr) if abs(x.mean()) > 1e-4 else x, sr)
    write(os.path.join(OUT_ROB, f), x, sr)
    rob[f[:-4]] = (x, sr)
print('Rob samples', len(rob))

plan = json.load(open(os.path.join(os.path.dirname(__file__), 'socal_plan.json')))
# articulation -> (reference Rob sample, offset dB relative to it at the top layer)
calib = { 'SideStick': ('SnareCenter', -9), 'RimShot': ('SnareCenter', +1), 'RimShotEdge': ('SnareCenter', -1),
          'HatFootClose': ('HiHatClosed', -4), 'HatFootSplash': ('HiHatOpen', -3),
          'RideBell': ('Ride1', +1), 'RideEdge': ('Ride1', 0), 'CrashLStop': ('CrashL', -2), 'CrashRStop': ('CrashR', -2) }
fill = {}
for z in plan:
    x, sr = load(os.path.join(SOCAL, f"{z['name']}_v{z['layer']:02d}.wav"))
    x = trim_tail(x * 10 ** (z['vol'] / 20), sr)
    fill.setdefault(z['name'], []).append((z, x, sr))
for name, layers in fill.items():
    ref, off = calib[name]
    top = layers[-1][1]
    g = rms_head(rob[ref][0], rob[ref][1]) / max(rms_head(top, layers[-1][2]), 1e-9) * 10 ** (off / 20)
    for z, x, sr in layers:
        write(os.path.join(OUT_SOCAL, f"{name}_v{z['layer']:02d}.wav"), x * g, sr)
    print(f"{name:14s} {len(layers)} layers, calibrated {20*np.log10(g):+.1f} dB vs {ref}, top peak {20*np.log10(np.abs(top*g).max()):.1f} dBFS")

# ---- hand percussion from Logic's Studio/Latin Percussion: level-set against Rob's clap
perc_offsets = { 'Tambourine': -6, 'Cowbell': -7, 'Snaps': -6, 'BongoHi': -4, 'BongoLo': -4, 'CongaMute': -4,
    'CongaOpen': -4, 'CongaLow': -4, 'TimbaleHi': -4, 'TimbaleLo': -4, 'AgogoHi': -8, 'AgogoLo': -8, 'Cabasa': -10,
    'Maracas': -10, 'Shekere': -10, 'Chimes': -12, 'Castanets': -8, 'Claves': -8, 'WoodBlockHi': -8, 'WoodBlockLo': -8,
    'CajonBass': -2, 'CajonSlap': -3, 'TriangleMute': -12, 'TriangleOpen': -12, 'Shaker': -10, 'SleighBells': -10,
    'JamBlockHi': -8, 'JamBlockLo': -8, 'Stomp': -2 }
pplan = json.load(open(os.path.join(os.path.dirname(__file__), 'perc_plan.json')))
PRAW = os.path.join(ROOT, 'Samples', 'PercRaw')
perc = {}
for z in sorted(pplan, key=lambda z: (z['label'], z['layer'], z['rr'])):
    x, sr = load(os.path.join(PRAW, f"{z['label']}_v{z['layer']:02d}_{z['rr']}.wav"))
    perc.setdefault(z['label'], []).append((z, trim_tail(x * 10 ** (z['vol'] / 20), sr), sr))
clap = rms_head(rob['Clap'][0], rob['Clap'][1])
for name, layers in perc.items():
    topHi = max(z['velHi'] for z, _, _ in layers)
    top = max((x for z, x, _ in layers if z['velHi'] == topHi), key=lambda x: rms_head(x, 44100))
    g = clap / max(rms_head(top, 44100), 1e-9) * 10 ** (perc_offsets[name] / 20)
    for k, (z, x, sr) in enumerate(layers):
        write(os.path.join(OUT_SOCAL, f"{name}_v{k + 1:02d}.wav"), x * g, sr)
    fill[name] = layers
    print(f"{name:13s} {len(layers):2d} files, {20*np.log10(g):+.1f} dB")

# KitMap.inc: one entry per layered sound; each file's upper velocity (equal values = round robins)
with open(os.path.join(ROOT, 'Source', 'DSP', 'KitMap.inc'), 'w') as f:
    f.write('// Generated by tools/prepare.py: layered sounds (file stem, file count, upper velocity per file).\n')
    for name, layers in fill.items():
        his = ', '.join(str(z['velHi']) for z, _, _ in layers)
        f.write(f'{{ "{name}", {len(layers)}, {{ {his} }} }},\n')
print('wrote KitMap.inc')
