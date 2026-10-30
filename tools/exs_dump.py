"""Dump an EXS (Logic Sampler) instrument: groups, zones, samples. Read-only."""
import struct, sys, json

def chunks(data):
    pos = 0
    while pos + 84 <= len(data):
        sig, size, idx, flags = struct.unpack_from('<IIII', data, pos)
        magic = data[pos+16:pos+20]
        name = data[pos+20:pos+84].split(b'\0')[0].decode('latin-1')
        body = data[pos+84:pos+84+size]
        yield (sig >> 24) & 0x0F, idx, name, body, magic
        pos += 84 + size

def main(path):
    data = open(path, 'rb').read()
    groups, zones, samples = [], [], []
    for t, idx, name, b, magic in chunks(data):
        if t == 2:
            g = dict(i=len(groups), name=name)
            if len(b) >= 16:
                g['vol'], g['pan'], g['poly'], g['opts'] = struct.unpack_from('<bbBB', b, 0)[:4]
                g['exclusive'] = b[4]
                g['raw'] = b[:48].hex()
            groups.append(g)
        elif t == 1:
            z = dict(i=len(zones), name=name, size=len(b))
            z['root'] = b[1]; z['fine'] = struct.unpack_from('<b', b, 2)[0]
            z['pan'] = struct.unpack_from('<b', b, 3)[0]; z['vol'] = struct.unpack_from('<b', b, 4)[0]
            z['keyLo'], z['keyHi'] = b[6], b[7]; z['velLo'], z['velHi'] = b[9], b[10]
            z['start'], z['end'] = struct.unpack_from('<ii', b, 12)
            z['group'] = struct.unpack_from('<i', b, len(b) - 8)[0]
            z['sample'] = struct.unpack_from('<i', b, len(b) - 4)[0]
            zones.append(z)
        elif t == 3:
            s = dict(i=len(samples), name=name, size=len(b))
            s['dataStart'], s['length'], s['rate'], s['bits'] = struct.unpack_from('<iiii', b, 0)
            samples.append(s)
    out = dict(groups=groups, zones=zones, samples=samples)
    json.dump(out, open(sys.argv[2], 'w'), indent=1)
    print(len(groups), 'groups', len(zones), 'zones', len(samples), 'samples')

main(sys.argv[1])
