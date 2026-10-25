# OpenPublisher format lab (docs/gfoffice/PUB-FORMAT.md). MIT, Copyright (c) 2026 AmigaChrome Team.
# The tagged values of a .pub Contents stream, as worked out from the
# corpus's bytes (docs/gfoffice/PUB-FORMAT.md): (id, type) byte pairs; the
# type gives the size of what follows.
import sys, struct
FIXED = {0x00: 0, 0x08: 0, 0x78: 0, 0x10: 2, 0x18: 2, 0x20: 4, 0x68: 4, 0x70: 4, 0xB8: 4, 0x28: 8, 0x38: 16}
BLOCK = {0x88, 0x90, 0x98, 0xA0, 0xC0}
unknown = {}
def walk(b, start, end, depth, out, path):
    o = start
    while o + 2 <= end:
        i, t = b[o], b[o+1]
        if t in FIXED:
            n = FIXED[t]
            v = b[o+2:o+2+n]
            out.append((depth, o, i, t, int.from_bytes(v, 'little') if n else None, None, path))
            o += 2 + n
        elif t in BLOCK:
            if o + 6 > end: out.append((depth, o, i, t, 'CUT', None, path)); return False
            l = struct.unpack_from('<I', b, o+2)[0]
            if l < 4 or o + 2 + l > end:
                out.append((depth, o, i, t, 'BADLEN %d' % l, None, path)); return False
            out.append((depth, o, i, t, l, (o+6, o+2+l), path))
            if t != 0xC0:
                walk(b, o+6, o+2+l, depth+1, out, path + (i,))
            o += 2 + l
        else:
            unknown[t] = unknown.get(t, 0) + 1
            out.append((depth, o, i, t, 'UNKNOWN', None, path)); return False
    return o == end
def block_at(b, off):
    l = struct.unpack_from('<I', b, off)[0]
    out = []
    ok = walk(b, off+4, off+l, 0, out, ())
    return ok, out, l
if __name__ == '__main__':
    b = open(sys.argv[1], 'rb').read()
    off = int(sys.argv[2], 0)
    ok, out, l = block_at(b, off)
    print('block at 0x%x length 0x%x consumed=%s' % (off, l, ok))
    for d, o, i, t, v, sub, p in out[:int(sys.argv[3]) if len(sys.argv) > 3 else 400]:
        print('%s%06x id %02x type %02x %s' % ('  '*d, o, i, t, ('0x%x' % v) if isinstance(v, int) else v))

