# OpenPublisher format lab (docs/gfoffice/PUB-FORMAT.md). MIT, Copyright (c) 2026 AmigaChrome Team.
# Escher (OfficeArt-style) record tree: u16 (version low 4 bits, instance
# high 12), u16 type, u32 length; version 0xF holds child records.
import sys, struct
def recs(b, s, e, d=0, out=None):
    out = [] if out is None else out
    o = s
    while o + 8 <= e:
        vi, t, l = struct.unpack_from('<HHI', b, o)
        ver, inst = vi & 15, vi >> 4
        if o + 8 + l > e:
            out.append((d, o, t, inst, ver, l, 'CUT')); break
        out.append((d, o, t, inst, ver, l, None))
        if ver == 15:
            recs(b, o+8, o+8+l, d+1, out)
        o += 8 + l
    return out
if __name__ == '__main__':
    b = open(sys.argv[1], 'rb').read()
    for d, o, t, inst, ver, l, cut in recs(b, 0, len(b)):
        body = b[o+8:o+8+min(l, 24)]
        print('%s%06x %04x inst %3d ver %x len %5d %s %s' % ('  '*d, o, t, inst, ver, l, cut or '', body.hex(' ') if ver != 15 else ''))
