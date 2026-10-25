# OpenPublisher format lab (docs/gfoffice/PUB-FORMAT.md). MIT, Copyright (c) 2026 AmigaChrome Team.
import os, sys, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tlv import walk, block_at
from idx import index
def show(b, off, maxblob=48):
    ok, out, l = block_at(b, off)
    lines = []
    for d, o, i, t, v, sub, p in out:
        s = '%s%02x/%02x ' % ('  '*d, i, t)
        if sub and t == 0xC0:
            blob = b[sub[0]:sub[1]]
            txt = ''
            if len(blob) >= 2 and len(blob) % 2 == 0:
                try:
                    u = blob.decode('utf-16-le')
                    if all(32 <= ord(c) < 0xFFFE or c in '\0\r\n\t' for c in u): txt = ' u16=' + repr(u)
                except Exception: pass
            s += 'blob[%d] %s%s' % (len(blob), blob[:maxblob].hex(' '), txt)
        elif sub:
            s += 'block[%d]' % (v - 4)
        else:
            s += '' if v is None else ('0x%x (%d)' % (v, v if v < 0x80000000 else v - (1 << 32)))
        lines.append(s)
    return ok, lines
if __name__ == '__main__':
    b = open(sys.argv[1], 'rb').read()
    hdr, items = index(b)
    kinds = set(int(k, 0) for k in sys.argv[2].split(',')) if len(sys.argv) > 2 else None
    for k, f in items:
        if f is None or (kinds and f.get(2) not in kinds): continue
        print('#%d (0x%x) kind 0x%x parent 0x%x at 0x%x' % (k, k, f.get(2), f.get(5, 0), f.get(4)))
        ok, lines = show(b, f.get(4))
        for s in lines: print('   ', s)
