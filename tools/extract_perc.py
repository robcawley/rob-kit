"""Run on the Mac: decode each Logic percussion CAF once with ffmpeg, slice the planned zones."""
import json, os, subprocess, struct, sys, numpy as np
here = os.path.dirname(os.path.abspath(__file__))
plan = json.load(open(os.path.join(here, 'perc_plan.json')))
out = os.path.join(here, '..', 'Samples', 'PercRaw'); os.makedirs(out, exist_ok=True)
tmp = os.path.join(here, '..', 'work', 'decode.f32'); os.makedirs(os.path.dirname(tmp), exist_ok=True)
ff = '/opt/homebrew/bin/ffmpeg'
def write(path, x, sr):
    data = np.ascontiguousarray(x.astype('<f4')).tobytes()
    hdr = b'RIFF' + struct.pack('<I', 36 + len(data)) + b'WAVE' + b'fmt ' + struct.pack('<IHHIIHH', 16, 3, 2, sr, sr * 8, 8, 32)
    open(path, 'wb').write(hdr + b'data' + struct.pack('<I', len(data)) + data)
by = {}
for p in plan: by.setdefault(p['caf'], []).append(p)
for caf, zones in by.items():
    subprocess.run([ff, '-nostdin', '-loglevel', 'error', '-y', '-i', caf, '-f', 'f32le', '-ac', '2', '-ar', '44100', tmp], check=True)
    x = np.memmap(tmp, dtype='<f4', mode='r').reshape(-1, 2)
    for z in zones:
        seg = np.array(x[z['start']:z['end']])
        write(os.path.join(out, f"{z['label']}_v{z['layer']:02d}_{z['rr']}.wav"), seg, 44100)
    del x
    print(os.path.basename(caf), len(zones), flush=True)
os.remove(tmp)
print('DONE', len(os.listdir(out)))
