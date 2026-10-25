# OpenPublisher format lab (docs/gfoffice/PUB-FORMAT.md). MIT, Copyright (c) 2026 AmigaChrome Team.
# The bitmap inside a .pub thumbnail WMF (META_DIBBITBLT / META_STRETCHDIB, [MS-WMF]) as a PNG.
import sys, struct, io
from PIL import Image
def dib(w):
    o = 18
    while o + 6 <= len(w):
        size, fn = struct.unpack_from('<IH', w, o)
        if size < 3: break
        p = w[o+6:o+2*size]
        if fn == 0x0940:   # DIBBITBLT with bitmap: rop(4) ysrc xsrc height width ydest xdest, then DIB
            return p[4+12:] if size*2 - 6 > 4+16 else None
        if fn == 0x0B41:   # DIBSTRETCHBLT: rop(4) srcH srcW ysrc xsrc destH destW ydst xdst, DIB
            return p[4+16:]
        if fn == 0x0F43:   # STRETCHDIB: rop(4) usage(2) srcH srcW ysrc xsrc destH destW ydst xdst, DIB
            return p[4+2+16:]
        o += 2 * size
    return None
if __name__ == '__main__':
    w = open(sys.argv[1], 'rb').read()
    d = dib(w)
    hdr = struct.unpack_from('<I', d, 0)[0]
    bits = struct.unpack_from('<H', d, 14)[0]
    ncol = struct.unpack_from('<I', d, 32)[0] or (1 << bits if bits <= 8 else 0)
    off = 14 + hdr + 4 * ncol
    bmp = b'BM' + struct.pack('<IHHI', 14 + len(d), 0, 0, off) + d
    im = Image.open(io.BytesIO(bmp))
    scale = int(sys.argv[3]) if len(sys.argv) > 3 else 4
    im.resize((im.width*scale, im.height*scale), Image.NEAREST).save(sys.argv[2])
    print(sys.argv[1].split('/')[-1], im.size, bits)
