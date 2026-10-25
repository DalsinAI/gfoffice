# OpenPublisher format lab (docs/gfoffice/PUB-FORMAT.md). MIT, Copyright (c) 2026 AmigaChrome Team.
import sys
def hexdump(b, base=0, width=16):
    for i in range(0, len(b), width):
        chunk = b[i:i+width]
        h = ' '.join('%02x' % c for c in chunk)
        a = ''.join(chr(c) if 32 <= c < 127 else '.' for c in chunk)
        print('%08x  %-*s  %s' % (base+i, width*3, h, a))
if __name__ == '__main__':
    p = sys.argv[1]; off = int(sys.argv[2], 0) if len(sys.argv) > 2 else 0
    n = int(sys.argv[3], 0) if len(sys.argv) > 3 else 256
    d = open(p, 'rb').read()
    hexdump(d[off:off+n], off)
