# OpenPublisher format lab (docs/gfoffice/PUB-FORMAT.md). MIT, Copyright (c) 2026 AmigaChrome Team.
import os, sys, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tlv import walk, block_at
def fields(b, start, end):
    out = []; walk(b, start, end, 0, out, ())
    return {i: v for d, o, i, t, v, sub, p in out if d == 0}, out
def index(b):
    ok, out, l = block_at(b, 0x2c)
    hdr = {i: v for d, o, i, t, v, sub, p in out if d == 0}
    trailer = hdr.get(0x0a)
    ok, out, l = block_at(b, trailer)
    arr = [x for x in out if x[0] == 0 and x[2] == 3][0]
    s, e = arr[5]
    items = []
    o = s
    k = 0
    while o + 2 <= e:
        t = b[o+1]
        if t == 0x78:
            items.append((k, None)); o += 2
        else:
            ln = struct.unpack_from('<I', b, o+2)[0]
            f, _ = fields(b, o+6, o+2+ln)
            items.append((k, f)); o += 2 + ln
        k += 1
    return hdr, items
if __name__ == '__main__':
    b = open(sys.argv[1], 'rb').read()
    hdr, items = index(b)
    print('header', {hex(k): (hex(v) if isinstance(v,int) else v) for k, v in hdr.items()})
    for k, f in items:
        if f is None: continue
        off = f.get(4)
        okb = ''
        if off is not None and off + 4 <= len(b):
            try:
                ok, out, l = block_at(b, off); okb = 'len 0x%x %s' % (l, 'ok' if ok else 'BAD')
            except Exception as ex: okb = 'ERR %s' % ex
        print('%4d kind %-5s off %-7s parent %-6s f06 %-7s %s  %s' % (k, hex(f.get(2,-1)), hex(off) if off is not None else '-', hex(f.get(5,0)), hex(f.get(6,0)),
              ' '.join('%x=%s' % (i, hex(v) if isinstance(v,int) else v) for i, v in f.items() if i not in (2,4,5,6)), okb))
