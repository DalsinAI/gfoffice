# OpenPublisher format lab (docs/gfoffice/PUB-FORMAT.md). MIT, Copyright (c) 2026 AmigaChrome Team.
# Property set stream reader after [MS-OLEPS] (Microsoft's published spec).
import sys, struct
NAMES = {1:'codepage',2:'title',3:'subject',4:'author',5:'keywords',6:'comments',7:'template',8:'lastauthor',
         9:'revnumber',10:'edittime',11:'lastprinted',12:'created',13:'lastsaved',14:'pagecount',15:'wordcount',
         16:'charcount',17:'thumbnail',18:'appname',19:'security'}
def props(b):
    out = []
    nsets = struct.unpack_from('<I', b, 24)[0]
    for s in range(nsets):
        fmtid = b[28+20*s:44+20*s].hex(); off = struct.unpack_from('<I', b, 44+20*s)[0]
        size, n = struct.unpack_from('<II', b, off)
        for i in range(n):
            pid, po = struct.unpack_from('<II', b, off+8+8*i)
            p = off + po
            t = struct.unpack_from('<H', b, p)[0]
            v = None
            if t == 0x1E:
                l = struct.unpack_from('<I', b, p+4)[0]; v = b[p+8:p+8+l].rstrip(b'\0').decode('cp1252','replace')
            elif t == 0x03: v = struct.unpack_from('<i', b, p+4)[0]
            elif t == 0x02: v = struct.unpack_from('<h', b, p+4)[0]
            elif t == 0x40: v = 'filetime %d' % struct.unpack_from('<Q', b, p+4)[0]
            elif t == 0x47:
                l, fmt = struct.unpack_from('<Ii', b, p+4)
                v = 'clipdata %d bytes, format %d, then %s (at stream offset %d)' % (l, fmt, b[p+12:p+12+16].hex(' '), p+12)
            out.append((fmtid, pid, NAMES.get(pid, pid), hex(t), v))
    return out
if __name__ == '__main__':
    for x in props(open(sys.argv[1],'rb').read()):
        print(*x)

def thumbnail(b):
    """The WMF of the thumbnail (CF_METAFILEPICT) and its METAFILEPICT extents."""
    nsets = struct.unpack_from('<I', b, 24)[0]
    off = struct.unpack_from('<I', b, 44)[0]
    size, n = struct.unpack_from('<II', b, off)
    for i in range(n):
        pid, po = struct.unpack_from('<II', b, off+8+8*i)
        if pid != 17: continue
        p = off + po
        l, fmt, cf = struct.unpack_from('<IiI', b, p+4)
        mm, xe, ye, hmf = struct.unpack_from('<hhhH', b, p+16)
        return b[p+24:p+8+l], (cf, mm, xe, ye)
    return None, None
