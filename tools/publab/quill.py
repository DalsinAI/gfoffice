# OpenPublisher format lab (docs/gfoffice/PUB-FORMAT.md). MIT, Copyright (c) 2026 AmigaChrome Team.
import sys, struct
def chunks(b):
    if b[:8] != b'CHNKINK ': return None
    n = struct.unpack_from('<H', b, 0x0C)[0]
    out = []
    for k in range(n):
        o = 0x20 + 24*k
        if o + 24 > len(b): break
        u0, = struct.unpack_from('<H', b, o)
        name = b[o+2:o+6].decode('latin1'); idx, = struct.unpack_from('<H', b, o+6)
        one, = struct.unpack_from('<I', b, o+8); typ = b[o+12:o+16].decode('latin1')
        off, ln = struct.unpack_from('<II', b, o+16)
        out.append((name, idx, typ, off, ln, u0, one))
    return out
if __name__ == '__main__':
    b = open(sys.argv[1], 'rb').read()
    print('header', b[8:0x20].hex(' '))
    for c in chunks(b):
        print('%-4s %2d %-4s off 0x%05x len 0x%05x  u0 %x one %x' % c)
    want = sys.argv[2:] 
    for c in chunks(b):
        if c[0].strip() in want:
            print('==', c[0], c[1])
            d = b[c[3]:c[3]+c[4]]
            if c[0] == 'TEXT': print(repr(d.decode('utf-16-le', 'replace')))
            else: print(d.hex(' '))
