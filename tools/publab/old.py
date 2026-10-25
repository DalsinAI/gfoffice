# OpenPublisher format lab (docs/gfoffice/PUB-FORMAT.md). MIT, Copyright (c) 2026 AmigaChrome Team.
# Publisher 98/2000-format Contents (header 22 00 2c 01): the directory at
# the header's u32 0x16, a u16 count and 10-byte entries.
import sys, struct
def directory(b):
    at = struct.unpack_from('<I', b, 0x16)[0]
    n = struct.unpack_from('<H', b, at)[0]
    out = []
    for k in range(n):
        o = at + 2 + 10 * k
        if o + 10 > len(b): break
        fl, ident, parent, off = struct.unpack_from('<HHHI', b, o)
        out.append((fl, ident, parent, off))
    return out
if __name__ == '__main__':
    b = open(sys.argv[1], 'rb').read()
    d = directory(b)
    offs = sorted(set(x[3] for x in d)) + [len(b)]
    for fl, ident, parent, off in d:
        nxt = min(o for o in offs if o > off)
        print('flags %04x id %04x parent %04x off %05x len %5d  %s' % (fl, ident, parent, off, nxt - off, b[off:off+24].hex(' ')))
