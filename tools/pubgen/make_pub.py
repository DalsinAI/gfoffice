#!/usr/bin/env python3
"""make_pub.py OUTDIR: writes GFOpenPublisher's test .pub files.

Every .pub the tests read is made here, from the clean-room notes in
docs/gfoffice/PUB-FORMAT.md, so the repository holds no outside documents.
Each file sets out to reach one part of the reader (libowf/src/in_pub.c):

  gen-2007-plain.pub     the 2007 form at its simplest: one text box, the
                         default style (Times New Roman 10 pt), a master
  gen-2007-runs.pub      character runs (bold, italic, sizes, typefaces), a
                         table, a PNG picture, two pages, a scratch frame,
                         and links: a web address, an e-mail, a page
  gen-2010-styles.pub    the 2010 mark; paragraph styles, centred, right and
                         left alignment over a style's
  gen-2013-look.pub      the 2013 mark; the colour scheme, fills, outlines,
                         line widths, text margins, groups, a white default fill
  gen-2003-columns.pub   the 2003 mark, A4 landscape; linked frames listed out
                         of their chain's order, WordArt, GIF, JPEG, WMF and
                         DIB pictures
  gen-2003-names.pub     a named master, a page that uses it, surrogate pairs,
                         tabs and line breaks
  gen-98.pub, gen-2000.pub  the 98/2000 form: text frames, a table, a shape
  gen-cut.pub            a 2010 file cut short: what is left is read
  gen-notpub.pub         a compound file that is not a publication
  gen-damaged.pub        Publisher's storages around a Contents that isn't one

The files are small: the reader's tests change their bytes thousands of
times (tests/pub/test_pub.py), so small files keep that fast.

MIT, Copyright (c) 2026 AmiCradle Team.
"""
import os
import struct
import sys
import zlib

EMU_MM = 36000
# ---------------------------------------------------------------- compound file ([MS-CFB])

FREE, END, FATSECT = 0xFFFFFFFF, 0xFFFFFFFE, 0xFFFFFFFD


def cfb(streams):
    """streams: {path: bytes}, path 'A/B/C'. 512-byte sectors, a mini
    stream for those under 4096 bytes. Children are kept as a chain of
    right siblings (a valid, if lopsided, tree)."""
    # the directory: the root, storages, streams
    entries = [{'name': 'Root Entry', 'type': 5, 'kids': [], 'data': b''}]
    index = {'': 0}
    for path in sorted(streams):
        parts = path.split('/')
        for depth in range(1, len(parts) + 1):
            p = '/'.join(parts[:depth])
            if p in index:
                continue
            e = {'name': parts[depth - 1], 'type': 2 if depth == len(parts) else 1, 'kids': [],
                 'data': streams[path] if depth == len(parts) else b''}
            index[p] = len(entries)
            entries.append(e)
            entries[index['/'.join(parts[:depth - 1])]]['kids'].append(index[p])
    # mini stream
    mini, minifat = bytearray(), []
    for e in entries[1:]:
        if e['type'] == 2 and len(e['data']) < 4096:
            n = (len(e['data']) + 63) // 64
            e['start'] = len(minifat) if n else END
            for k in range(n):
                minifat.append(len(minifat) + 1 if k < n - 1 else END)
            mini += e['data'] + b'\0' * (n * 64 - len(e['data']))
    # the sectors in order: the FAT, the directory, the mini FAT, the mini
    # stream, then the big streams; so a file cut short loses stream data
    # first, as a damaged file most often does
    mf = b''.join(struct.pack('<I', v) for v in minifat)
    big = [e for e in entries[1:] if e['type'] == 2 and len(e['data']) >= 4096]

    def count(n):
        return (n + 511) // 512
    ndir = count(128 * len(entries))
    total = ndir + count(len(mf)) + count(len(mini)) + sum(count(len(e['data'])) for e in big)
    nfat = 1
    while total + nfat > nfat * 128:
        nfat += 1
    at = nfat
    dir_start, at = at, at + ndir
    minifat_start, at = (at if mf else END), at + count(len(mf))
    entries[0]['start'], at = (at if mini else END), at + count(len(mini))
    entries[0]['size'] = len(mini)
    for e in big:
        e['start'], at = at, at + count(len(e['data']))
    dirbytes = bytearray()
    for e in entries:
        name = e['name'].encode('utf-16-le') + b'\0\0'
        kids = e['kids']
        rec = bytearray(128)
        rec[0:len(name)] = name
        struct.pack_into('<HBB', rec, 0x40, len(name), e['type'], 1)
        struct.pack_into('<III', rec, 0x44, FREE, FREE, kids[0] if kids else FREE)
        size = e.get('size', len(e['data']))
        struct.pack_into('<II', rec, 0x74, e.get('start', END) if e['type'] != 1 else 0, size)
        dirbytes += rec
    for e in entries:                        # siblings: each child's right is the next
        for a, b in zip(e['kids'], e['kids'][1:]):
            struct.pack_into('<I', dirbytes, 128 * a + 0x48, b)
    while len(dirbytes) % 512:
        dirbytes += bytes(128)
        struct.pack_into('<III', dirbytes, len(dirbytes) - 128 + 0x44, FREE, FREE, FREE)
    fat = [FATSECT] * nfat
    sectors = []

    def put(data):
        n = count(len(data))
        first = nfat + len(sectors)
        for k in range(n):
            sectors.append(bytes(data[k * 512:(k + 1) * 512]).ljust(512, b'\0'))
            fat.append(first + k + 1 if k < n - 1 else END)
        return first if n else END
    assert put(bytes(dirbytes)) == dir_start
    if mf:
        assert put(mf) == minifat_start
    if mini:
        assert put(bytes(mini)) == entries[0]['start']
    for e in big:
        assert put(e['data']) == e['start']
    fat += [FREE] * (nfat * 128 - len(fat))
    fatbytes = b''.join(struct.pack('<I', v) for v in fat)
    hdr = bytearray(512)
    hdr[0:8] = b'\xd0\xcf\x11\xe0\xa1\xb1\x1a\xe1'
    struct.pack_into('<HHHHH', hdr, 0x18, 0x3E, 3, 0xFFFE, 9, 6)
    struct.pack_into('<IIIIIIII', hdr, 0x2C, nfat, dir_start, 0, 4096,
                     minifat_start, count(len(mf)), END, 0)
    for k in range(109):
        struct.pack_into('<I', hdr, 0x4C + 4 * k, k if k < nfat else FREE)
    return bytes(hdr) + fatbytes + b''.join(sectors)


# ---------------------------------------------------------------- tagged values (PUB-FORMAT.md 4)

def fv(fid, typ, value=None, wide=False):
    """One field. wide: the text storage's 11-bit ids (6.3)."""
    if wide:
        head = bytes([fid & 0xFF, typ | (fid >> 8)])
    else:
        head = bytes([fid, typ])
    if typ in (0x00, 0x08, 0x78):
        return head
    if typ in (0x10, 0x18):
        return head + struct.pack('<H', value & 0xFFFF)
    if typ in (0x20, 0x68, 0x70, 0xB8):
        return head + struct.pack('<I', value & 0xFFFFFFFF)
    if typ == 0x28:
        return head + struct.pack('<Q', value)
    return head + struct.pack('<I', len(value) + 4) + value      # a block


def block(*fields):
    body = b''.join(fields)
    return struct.pack('<I', len(body) + 4) + body


def u16text(s):
    return s.encode('utf-16-le') + b'\0\0'


# ---------------------------------------------------------------- the text storage (6)

def qf(fid, typ, value=None):
    return fv(fid, typ, value, wide=True)


def char_props(bold=None, italic=None, size=None, font=None, old=False):
    out = b''
    if bold is not None:
        out += qf(0x202, 0x08 if bold else 0x10, None if bold else 0)
    if italic is not None:
        out += qf(0x203, 0x08 if italic else 0x10, None if italic else 0)
    if size is not None:
        out += qf(0x20C, 0x20, int(size * 12700))
    if font is not None:
        if old:                              # 98/2000: the slots as plain fields
            out += qf(0x224, 0x88, qf(0, 0x18, font) + qf(8, 0x18, font))
        else:                                # 2002 and later: each slot a block
            out += qf(0x224, 0x88, qf(0, 0x88, qf(0x0B, 0x18, font)) + qf(1, 0x88, qf(0x0B, 0x18, font)))
    return out


ALIGN = {'left': 0, 'right': 1, 'centre': 2, 'odd': 6}


def para_props(style=None, align=None, high=0):
    out = b''
    if align is not None:
        out += qf(0x204, 0x18, (high << 8) | ALIGN[align])
    if style is not None:
        out += qf(0x219, 0x18, style)
    return out


def fdp_pages(text_at, runs):
    """runs: [(end in characters, props bytes or None)] as FDPC/FDPP's 512-byte pages."""
    pages = []
    k = 0
    while k < len(runs):
        part = runs[k:k + 12]
        k += len(part)
        n = len(part)
        pg = bytearray(512)
        struct.pack_into('<HHI', pg, 0, n, 0, 0)
        top = 512
        for i, (end, props) in enumerate(part):
            struct.pack_into('<I', pg, 8 + 4 * i, text_at + 2 * end)
            if props is None:
                struct.pack_into('<H', pg, 8 + 4 * n + 2 * i, 0)
                continue
            blk = struct.pack('<I', len(props) + 4) + props
            top -= len(blk)
            top &= ~1
            pg[top:top + len(blk)] = blk
            struct.pack_into('<H', pg, 8 + 4 * n + 2 * i, top)
        pages.append(bytes(pg))
    return b''.join(pages)


class Text:
    """Stories (each a list of paragraphs, each a list of (text, props)),
    paragraph props per paragraph, styles and fonts, as Quill/QuillSub/CONTENTS."""

    def __init__(self, fonts, styles, old=False):
        self.fonts = fonts                   # names
        self.styles = styles                 # [(char props, para props)]
        self.old = old
        self.stories = []                    # [(text id, [(para props, [(text, char props)])])]
        self.links = {}                      # story: [(from, length, kind, target)]

    def story(self, text_id, paras, links=None):
        self.stories.append((text_id, paras))
        if links:
            self.links[len(self.stories) - 1] = links
        return len(self.stories) - 1

    def tokn(self, k, length):
        """Story k's links as TOKN (PUB-FORMAT.md 6.7): places, each link's
        changed properties, each link's target, then the targets' table."""
        links = self.links[k]
        out = struct.pack('<III', len(links), 12, 0x1FFFF)
        out += b''.join(struct.pack('<I', f) for f, _, _, _ in links) + struct.pack('<I', length)
        last = (None, None)
        for i, (f, n, kind, _) in enumerate(links):
            fields = b''
            if i == 0:
                fields += qf(0x200, 0x20, 0x8C0)
            if n != last[0]:
                fields += qf(0x201, 0x20, n)
            if kind != last[1]:
                fields += qf(0x202, 0x20, kind)
            last = (n, kind)
            out += block(fields)
        for i in range(len(links)):
            out += block(qf(0x200, 0x20, i))
        strs = [struct.pack('<H', len(t.encode('utf-16-le')) // 2) + t.encode('utf-16-le') for _, _, _, t in links]
        offs, at = [], 4 * len(strs)
        for x in strs:
            offs.append(at)
            at += len(x)
        body = b''.join(struct.pack('<I', o) for o in offs) + b''.join(strs)
        return out + struct.pack('<II', len(body), len(strs)) + bytes(12) + body

    def build(self):
        text = ''
        cruns, pruns, lengths = [], [], []

        def units():
            return len(text.encode('utf-16-le')) // 2
        for tid, paras in self.stories:
            start = units()
            for pprops, runs in paras:
                for s, cprops in runs:
                    text += s
                    cruns.append((units(), cprops))
                text += '\r'
                if cruns and cruns[-1][0] == units() - 1:
                    cruns[-1] = (units(), cruns[-1][1])     # the paragraph's mark takes its last run's look
                else:
                    cruns.append((units(), None))
                pruns.append((units(), pprops))
            lengths.append(units() - start)
        tokns = [('TOKN', k, self.tokn(k, lengths[k])) for k in sorted(self.links)]
        tbytes = text.encode('utf-16-le')
        strs = struct.pack('<III', len(lengths), 8, 0xFF) + b''.join(struct.pack('<I', n) for n in lengths)
        syid = struct.pack('<II', 0, len(self.stories)) + b''.join(struct.pack('<I', t) for t, _ in self.stories)
        names = [struct.pack('<H', len(f)) + f.encode('utf-16-le') for f in self.fonts]
        offs, at = [], 4 * len(names)
        for nm in names:
            offs.append(at)
            at += len(nm)
        font = struct.pack('<IIIII', 0, len(names), 0, 0, 0) + b''.join(struct.pack('<I', o) for o in offs) + b''.join(names)
        ents = []
        for cp, pp in self.styles:
            ents.append(struct.pack('<H', 0) + block(cp) + struct.pack('<H', 0))
            ents.append(struct.pack('<H', 0) + block(pp) + struct.pack('<H', 0))
        offs, at = [], 4 * len(ents)
        for e in ents:
            offs.append(at)
            at += len(e)
        body = b''.join(struct.pack('<I', o) for o in offs) + b''.join(ents)
        stsh1 = struct.pack('<IIIII', 0x14 + len(body), len(ents), 0, 0, 0) + body
        stsh0 = struct.pack('<II', 0, 0)
        # chunks: name, index, data; TEXT at 0x200 as the files have it
        chunks = [('TEXT', 0, tbytes), ('STRS', 0, strs), ('SYID', 0, syid), ('FONT', 0, font),
                  ('STSH', 0, stsh0), ('STSH', 1, stsh1), ('FDPC', 0, None), ('FDPP', 0, None)] + tokns
        out = bytearray(0x200)
        out[0:8] = b'CHNKINK '
        struct.pack_into('<HH', out, 0x0C, len(chunks), 0)
        struct.pack_into('<HHI', out, 0x18, 0x1F8, len(chunks), 0xFFFFFFFF)
        places = []
        for name, idx, data in chunks:
            at = len(out)
            if data is None:                 # the run pages count from TEXT's offset
                data = fdp_pages(0x200, cruns if name == 'FDPC' else pruns)
            while len(out) % 2:
                out += b'\0'
            at = len(out)
            out += data
            places.append((name, idx, at, len(data)))
        for i, (name, idx, at, n) in enumerate(places):
            e = 0x20 + 24 * i
            struct.pack_into('<H4sHI4sII', out, e, 0, name.encode(), idx, 0, b'CHNK', at, n)
        return bytes(out)


# ---------------------------------------------------------------- Escher (7, [MS-ODRAW])

def rec(typ, body, inst=0, ver=0):
    return struct.pack('<HHI', (inst << 4) | ver, typ, len(body)) + body


def props(pairs, extra=b''):
    body = b''.join(struct.pack('<HI', p, v) for p, v in pairs)
    return rec(0xF00B, body + extra, inst=len(pairs), ver=3)


def client(*fields):
    return block(*fields)


class Shape:
    def __init__(self, chunk, box, stype=202, text_id=None, blip=None, look=(), wordart=None):
        self.chunk, self.box, self.stype = chunk, box, stype
        self.text_id, self.blip, self.look, self.wordart = text_id, blip, list(look), wordart

    def bytes(self, in_group=None):
        out = rec(0xF00A, struct.pack('<II', 1024, 0xA00), inst=self.stype, ver=2)
        pairs = list(self.look)
        extra = b''
        if self.blip:
            pairs.append((0x4104, self.blip))
        if self.wordart is not None:
            w = self.wordart.encode('utf-16-le') + b'\0\0'
            pairs.append((0xC0C0, len(w)))
            extra = w
        if pairs:
            out += props(pairs, extra)
        if in_group:
            out += rec(0xF00F, struct.pack('<4i', *self.box))
        else:
            out += rec(0xF010, client(*[fv(k + 1, 0x20, v & 0xFFFFFFFF) for k, v in enumerate(self.box)]))
        out += rec(0xF011, client(fv(1, 0x68, self.chunk)))
        if self.text_id is not None:
            out += rec(0xF00D, client(fv(1, 0x20, self.text_id)))
        return rec(0xF004, out, ver=15)


class Group:
    def __init__(self, chunk, box, grect, members):
        self.chunk, self.box, self.grect, self.members = chunk, box, grect, members

    def bytes(self, in_group=None):
        own = rec(0xF009, struct.pack('<4i', *self.grect), ver=1)
        own += rec(0xF00A, struct.pack('<II', 1025, 0x201), inst=0, ver=2)
        if in_group:
            own += rec(0xF00F, struct.pack('<4i', *self.box))
        else:
            own += rec(0xF010, client(*[fv(k + 1, 0x20, v & 0xFFFFFFFF) for k, v in enumerate(self.box)]))
        own += rec(0xF011, client(fv(1, 0x68, self.chunk)))
        body = rec(0xF004, own, ver=15) + b''.join(m.bytes(in_group=self) for m in self.members)
        return rec(0xF003, body, ver=15)


def escher(pages_shapes, pictures):
    """pages_shapes: [[Shape or Group]] one drawing each; pictures: [(record type, body)]."""
    delay = bytearray()
    store = b''
    for rtype, body in pictures:
        off = len(delay)
        delay += rec(rtype, body, inst=0x46A if rtype == 0xF01D else 0x6E0, ver=0)
        bse = bytearray(36)
        bse[0] = bse[1] = 2
        struct.pack_into('<I', bse, 20, len(body) + 8)
        struct.pack_into('<I', bse, 24, 1)
        struct.pack_into('<I', bse, 28, off)
        store += rec(0xF007, bytes(bse), inst=2, ver=2)
    dgg = rec(0xF006, struct.pack('<IIII', 1024, 2, 1, 1))
    stm = rec(0xF000, dgg + rec(0xF001, store, inst=len(pictures), ver=15), ver=15)
    for k, shapes in enumerate(pages_shapes):
        top = rec(0xF004, rec(0xF009, bytes(16), ver=1) + rec(0xF00A, struct.pack('<II', 1024 * (k + 1), 5), ver=2), ver=15)
        body = top + b''.join(s.bytes() for s in shapes)
        stm += struct.pack('<I', 0) + rec(0xF002, rec(0xF008, struct.pack('<II', len(shapes) + 1, 1024 * (k + 1)), inst=k + 1)
                                          + rec(0xF003, body, ver=15), ver=15)
    return bytes(stm), bytes(delay)


# ---------------------------------------------------------------- Contents, the 2002-or-later form (5)

class Contents:
    def __init__(self, mark, width, height):
        self.mark, self.width, self.height = mark, width, height
        self.chunks = {}                     # number: (kind, parent, body bytes)
        self.next = 0x101

    def add(self, kind, parent, *fields):
        n = self.next
        self.next += 1
        self.chunks[n] = (kind, parent, block(*fields))
        return n

    def build(self, doc_fields):
        out = bytearray(0x2C)
        hdr_at = len(out)
        hdr_len = 4 + 2 * 4 + 4 * 6          # 0x01 and 0x02 (u16), 0x03, 0x08, 0x0A, 0x0B (u32)
        out += bytes(hdr_len)
        self.chunks[0x100] = (0x44, None, block(*doc_fields))
        offsets = {}
        for n in sorted(self.chunks):
            offsets[n] = len(out)
            out += self.chunks[n][2]
        items = []
        for n in range(max(self.chunks) + 1):
            if n not in self.chunks:
                items.append(fv(0, 0x78))
                continue
            kind, parent, _ = self.chunks[n]
            f = fv(0x02, 0x10, kind) + fv(0x04, 0xB8, offsets[n])
            if parent is not None:
                f += fv(0x05, 0x68, parent)
            items.append(fv(0, 0x88, f))
        index_at = len(out)
        out += block(fv(0x01, 0x20, len(items)), fv(0x02, 0x20, len(items) - 1), fv(0x03, 0xA0, b''.join(items)))
        second_at = len(out)
        out += block(fv(0x01, 0x20, 0))
        hdr = block(fv(0x01, 0x18, 0xACE8), fv(0x02, 0x18, 0x100), fv(0x03, 0x20, (1000 << 16) | self.mark),
                    fv(0x08, 0x20, len(out)), fv(0x0A, 0x20, index_at), fv(0x0B, 0x20, second_at))
        assert len(hdr) == hdr_len
        out[hdr_at:hdr_at + hdr_len] = hdr
        struct.pack_into('<HHHHI', out, 0, 0xACE8, 0x2C, 0x3E8, 0, len(out))
        out[0x0C] = self.mark
        struct.pack_into('<II', out, 0x1A, index_at, second_at)
        return bytes(out)


def doc_summary(major, minor):
    """\\x05DocumentSummaryInformation ([MS-OLEPS]) with only the version (0x17)."""
    props_ = [(0x17, struct.pack('<HHI', 3, 0, (major << 16) | minor))]
    body = b''
    offs = []
    at = 8 + 8 * len(props_)
    for pid, v in props_:
        offs.append((pid, at))
        at += len(v)
        body += v
    section = struct.pack('<II', at, len(props_)) + b''.join(struct.pack('<II', p, o) for p, o in offs) + body
    head = struct.pack('<HHI16sI', 0xFFFE, 0, 0x00020006, bytes(16), 1)
    fmtid = bytes.fromhex('02d5cdd59c2e1b10939708002b2cf9ae')
    return head + fmtid + struct.pack('<I', 48) + section


# ---------------------------------------------------------------- pictures (our own, made here)

def png(w, h, rgb):
    raw = b''.join(b'\0' + bytes(rgb) * w for _ in range(h))

    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xFFFFFFFF)
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(raw, 0)) + chunk(b'IEND', b''))


def gif():
    """A 1x1 GIF, one colour."""
    return (b'GIF89a' + struct.pack('<HHBBB', 1, 1, 0x80, 0, 0) + bytes([0x33, 0x66, 0x99, 0, 0, 0]) +
            b',' + struct.pack('<HHHHB', 0, 0, 1, 1, 0) + bytes([2, 2, 0x44, 0x01, 0]) + b';')


def jpeg():
    """An 8x8 mid-grey baseline JPEG: one DC-only block, one-code Huffman tables."""
    def seg(m, d):
        return bytes([0xFF, m]) + struct.pack('>H', len(d) + 2) + d
    dqt = seg(0xDB, bytes([0]) + bytes([1] * 64))
    sof = seg(0xC0, bytes([8]) + struct.pack('>HH', 8, 8) + bytes([1, 1, 0x11, 0]))
    dht_dc = seg(0xC4, bytes([0x00, 1] + [0] * 15 + [0]))
    dht_ac = seg(0xC4, bytes([0x10, 1] + [0] * 15 + [0]))
    sos = seg(0xDA, bytes([1, 1, 0x00, 0, 63, 0]))
    return b'\xff\xd8' + seg(0xE0, b'JFIF\0\x01\x01\0\0\x01\0\x01\0\0') + dqt + sof + dht_dc + dht_ac + sos + b'\x3f\xff\xd9'


def wmf():
    """A placeable-less WMF ([MS-WMF] 2.3): a 1000 x 1000 window, a blue brush, a box, the end record."""
    def rec(func, *fields):
        return struct.pack('<IH', 3 + len(fields), func) + struct.pack('<%dh' % len(fields), *fields)
    body = (rec(0x020B, 0, 0) + rec(0x020C, 1000, 1000) +        # SetWindowOrg, SetWindowExt (y, x)
            rec(0x02FC, 0, 0, 0xFF, 0) +                          # CreateBrushIndirect: solid, blue
            rec(0x012D, 0) + rec(0x041B, 900, 900, 100, 100) +    # SelectObject; Rectangle (b, r, t, l)
            rec(0x0000))
    words = 9 + len(body) // 2
    return struct.pack('<HHHIHIH', 1, 9, 0x300, words, 1, 7, 0) + body


def dib():
    pixels = bytes([0x99, 0x66, 0x33, 0]) * 4
    return struct.pack('<IiiHHIIiiII', 40, 2, 2, 1, 32, 0, len(pixels), 2835, 2835, 0, 0) + pixels


def blip_bitmap(data):
    return bytes(16) + b'\xff' + data


def blip_metafile(data):
    z = zlib.compress(data, 0)
    return bytes(16) + struct.pack('<I4iiiIBB', len(data), 0, 0, 1000, 1000, 1000, 1000, len(z), 0, 0xFE) + z


# ---------------------------------------------------------------- the publications

def box_mm(pw, ph, x, y, w, h):
    """A box in mm from the page's top left, as EMU about the page's centre."""
    l, t = x * EMU_MM - pw // 2, y * EMU_MM - ph // 2
    return [l, t, l + w * EMU_MM, t + h * EMU_MM]


def frame_chunk(c, page, kind=0x01, text_id=None, chain=None, prev=None, nxt=None, w=0, h=0, extra=()):
    f = []
    if text_id is not None:
        f.append(fv(0x27, 0x20, text_id))
    if chain:
        f.append(fv(0x28, 0x20, chain))
    if prev is not None:
        f.append(fv(0x36, 0x68, prev))
    if nxt is not None:
        f.append(fv(0x37, 0x68, nxt))
    f += list(extra)
    if w:
        f += [fv(0xAA, 0x20, w), fv(0xAB, 0x20, h)]
    return c.add(kind, page, *f)


def page_chunk(c, master=None, name=None, letter=None, y=0, scratch=False):
    f = [fv(0x10, 0x18, 3 if master is None and name else 5)]
    if name:
        f += [fv(0x0E, 0xC0, u16text(letter or 'A')), fv(0x0F, 0xC0, u16text(name))]
    else:
        f.append(fv(0x06, 0x28, 0 if scratch else 1))
        if master is not None:
            f.append(fv(0x0D, 0x68, master))
    f.append(fv(0x05, 0x88, fv(0x01, 0x20, 0) + fv(0x02, 0x20, y)))
    return c.add(0x43, 0x100, *f)


def doc_fields(c, width, height, pages):
    return [fv(0x12, 0x88, fv(0x01, 0x20, width) + fv(0x02, 0x20, height)),
            fv(0x02, 0xA0, b''.join(fv(0, 0x68, p) for p in pages))]


def publication(contents, text, shapes, pictures, version, extra=None):
    stm, delay = escher(shapes, pictures)
    streams = {'Contents': contents, 'Quill/QuillSub/CONTENTS': text.build(), 'Escher/EscherStm': stm,
               '\x05DocumentSummaryInformation': doc_summary(*version)}
    if delay:
        streams['Escher/EscherDelayStm'] = delay
    streams.update(extra or {})
    return cfb(streams)


TIMES = char_props(size=10, font=0)
DEFAULT_STYLE = (TIMES, para_props())
INSET = [(0x81, 91440), (0x82, 45720), (0x83, 91440), (0x84, 45720)]
NO_FILL_LINE = [(0x1BF, 0x100000), (0x1FF, 0x80000)]


def plain():
    pw, ph = 210 * EMU_MM, 297 * EMU_MM
    c = Contents(0x15, pw, ph)
    m = page_chunk(c, name='Master Page A')
    p = page_chunk(c, master=m)
    t = Text(['Times New Roman'], [DEFAULT_STYLE])
    t.story(1, [(None, [('A plain line of text, 0123456789', None)])])
    fc = frame_chunk(c, p, text_id=1)
    shapes = [Shape(fc, box_mm(pw, ph, 20, 22, 166, 20), text_id=1, look=NO_FILL_LINE + INSET)]
    return publication(c.build(doc_fields(c, pw, ph, [m, p])), t, [[], shapes], [], (12, 0))


def runs():
    pw, ph = 210 * EMU_MM, 297 * EMU_MM
    c = Contents(0x15, pw, ph)
    m = page_chunk(c, name='Master Page A')
    p1 = page_chunk(c, master=m)
    p2 = page_chunk(c, master=m)
    scratch = page_chunk(c, master=m, y=-ph // 2, scratch=True)
    t = Text(['Times New Roman', 'Arial', 'Courier New'], [DEFAULT_STYLE])
    t.story(1, [(None, [('Plain text in the default style.', None)])])
    t.story(2, [(None, [('Bold and italic in Arial, 20 point. ', char_props(True, True, 20, 1)),
                        ('Then Courier at 12, ', char_props(False, False, 12, 2)),
                        ('bold alone', char_props(bold=True))]),
                (None, [('A second paragraph, italic', char_props(italic=True))])])
    t.story(3, [(None, [('Cell one', None)]), (None, [('Cell two', None)]), (None, [('Cell three', None)]),
                (None, [('Cell four', None)]), (None, [('Cell five', None)]), (None, [('Cell six', None)])])
    t.story(4, [(None, [('On the second page', None)])])
    t.story(5, [(None, [('Left on the scratch area', None)])])
    t.story(6, [(None, [('See our site, write to us, or turn to page 1.', None)])],
            links=[(4, 8, 1, 'https://example.org/gfoffice/'), (14, 11, 4, 'mailto:team@example.org?subject=Hello'),
                   (30, 14, 3, '\u010a')])
    f1 = frame_chunk(c, p1, text_id=1)
    f2 = frame_chunk(c, p1, text_id=2)
    tb = frame_chunk(c, p1, kind=0x10, text_id=3, extra=[fv(0x66, 0x20, 3), fv(0x67, 0x20, 2)])
    pic = frame_chunk(c, p1)
    f4 = frame_chunk(c, p2, text_id=4)
    f5 = frame_chunk(c, scratch, text_id=5)
    f6 = frame_chunk(c, p2, text_id=6)
    page1 = [Shape(f1, box_mm(pw, ph, 20, 20, 170, 20), text_id=1),
             Shape(f2, box_mm(pw, ph, 20, 50, 170, 40), text_id=2),
             Shape(tb, box_mm(pw, ph, 20, 100, 120, 40), stype=1),
             Shape(pic, box_mm(pw, ph, 150, 100, 30, 30), stype=75, blip=1)]
    page2 = [Shape(f4, box_mm(pw, ph, 20, 20, 170, 20), text_id=4),
             Shape(f6, box_mm(pw, ph, 20, 50, 170, 20), text_id=6)]
    page3 = [Shape(f5, box_mm(pw, ph, 20, -60, 80, 20), text_id=5)]
    return publication(c.build(doc_fields(c, pw, ph, [m, p1, p2, scratch])), t, [[], page1, page2, page3],
                       [(0xF01E, blip_bitmap(png(4, 3, (0x20, 0x80, 0x40))))], (12, 0))


def styles():
    pw, ph = 8.5 * 25.4 * EMU_MM, 11 * 25.4 * EMU_MM
    pw, ph = int(pw), int(ph)
    c = Contents(0x18, pw, ph)
    m = page_chunk(c, name='Master Page A')
    p = page_chunk(c, master=m)
    sheet = [DEFAULT_STYLE,
             (char_props(italic=True, size=14, font=1), para_props(align='centre')),   # 1: heading
             (char_props(size=11, font=1), para_props()),                              # 2: body
             (char_props(bold=True, font=2), para_props(align='right')),               # 3: right
             (b'', para_props(align='odd'))]                                           # 4: not known
    t = Text(['Times New Roman', 'Garamond', 'Arial'], sheet)
    t.story(1, [(para_props(style=1), [('A centred heading', None)]),
                (para_props(style=2), [('Body text in the body style', None)]),
                (para_props(style=2, align='centre'), [('A body paragraph centred by hand', None)]),
                (para_props(style=3), [('Page 2', None)]),
                (para_props(style=3, align='left'), [('Left over a right style', None)]),
                (para_props(style=1, align='left', high=0xFF), [('Left, with a high byte', None)]),
                (para_props(style=4), [('An alignment not known yet', None)]),
                (para_props(style=9), [('A style not in the sheet', None)])])
    f1 = frame_chunk(c, p, text_id=1)
    shapes = [Shape(f1, box_mm(pw, ph, 20, 20, 170, 120), text_id=1)]
    return publication(c.build(doc_fields(c, pw, ph, [m, p])), t, [[], shapes], [], (14, 0))


def look():
    pw, ph = 210 * EMU_MM, 297 * EMU_MM
    c = Contents(0x1A, pw, ph)
    scheme = [0x000000, 0xCCCCEB, 0x000099, 0x993300, 0xFFFFFF]
    items = b''.join(fv(0, 0x78) if v == 0 else
                     fv(0, 0x88, fv(0x01, 0x20, ((v & 0xFF) << 16) | (v & 0xFF00) | (v >> 16)))
                     for v in scheme)
    c.add(0x5C, 0x100, fv(0x01, 0x18, 8), fv(0x02, 0xA0, items), fv(0x06, 0xC0, u16text('Ours')))
    m = page_chunk(c, name='Master Page A')
    p = page_chunk(c, master=m)
    t = Text(['Times New Roman'], [DEFAULT_STYLE])
    t.story(1, [(None, [('A headline over a bar', None)])])
    bar = frame_chunk(c, p, text_id=1)
    corner = frame_chunk(c, p)
    border = frame_chunk(c, p)
    grp = frame_chunk(c, p)
    back = frame_chunk(c, grp)
    front = frame_chunk(c, grp)
    odd = frame_chunk(c, p)
    S = 0x08000000
    shapes = [Shape(bar, box_mm(pw, ph, 20, 20, 170, 25), text_id=1,
                    look=[(0x181, S | 1), (0x1BF, 0x100010), (0x1C0, S | 0), (0x1CB, 12700), (0x1FF, 0x80008)] + INSET),
              Shape(corner, box_mm(pw, ph, 10, 10, 10, 10), stype=1,
                    look=[(0x181, S | 2), (0x1BF, 0x100010), (0x1FF, 0x80000)]),
              Shape(border, box_mm(pw, ph, 5, 5, 200, 287), stype=1,
                    look=[(0x1BF, 0x100000), (0x1C0, 0x00000000), (0x1CB, 38100), (0x1FF, 0x80008)]),
              Group(grp, box_mm(pw, ph, 150, 250, 40, 40), [0, 0, 1000, 1000],
                    [Shape(back, [0, 0, 1000, 1000], stype=1, look=[(0x1BF, 0x100010), (0x1FF, 0x80000)]),
                     Shape(front, [250, 250, 750, 750], stype=1,
                           look=[(0x181, 0x00336699), (0x1BF, 0x100010), (0x1C0, S | 3), (0x1CB, 6350), (0x1FF, 0x80008)])]),
              Shape(odd, box_mm(pw, ph, 20, 260, 20, 20), stype=1,
                    look=[(0x181, 0x10000004), (0x1BF, 0x100010), (0x1C0, 0x08000063), (0x1FF, 0x80008)])]
    return publication(c.build(doc_fields(c, pw, ph, [m, p])), t, [[], shapes], [], (15, 0))


def columns():
    pw, ph = 297 * EMU_MM, 210 * EMU_MM
    c = Contents(0x13, pw, ph)
    m = page_chunk(c, name='Master Page A')
    p1 = page_chunk(c, master=m)
    p2 = page_chunk(c, master=m)
    t = Text(['Times New Roman', 'Franklin Gothic Book'], [DEFAULT_STYLE, (char_props(size=8.5, font=1), para_props())])
    words = ' '.join('word%d' % i for i in range(40))
    t.story(1, [(para_props(style=1), [('Column story, part %s: %s' % (n, words), None)]) for n in ('one', 'two', 'three')])
    t.story(2, [(para_props(style=1), [('The second story runs on to page two. ' + words, None)])])
    # story 1: three columns, listed middle, left, right
    mid = frame_chunk(c, p1, text_id=1, chain=1)
    left = frame_chunk(c, p1, text_id=1, nxt=mid)
    right = frame_chunk(c, p1, text_id=1, chain=2, prev=mid)
    c.chunks[mid] = (0x01, p1, block(fv(0x27, 0x20, 1), fv(0x28, 0x20, 1), fv(0x36, 0x68, left), fv(0x37, 0x68, right)))
    a = frame_chunk(c, p1, text_id=2)
    b = frame_chunk(c, p2, text_id=2, chain=1, prev=a)
    wa = frame_chunk(c, p1)
    g, j, w, d = frame_chunk(c, p2), frame_chunk(c, p2), frame_chunk(c, p2), frame_chunk(c, p2)
    page1 = [Shape(mid, box_mm(pw, ph, 110, 30, 80, 40), text_id=1),
             Shape(left, box_mm(pw, ph, 20, 30, 80, 40), text_id=1),
             Shape(right, box_mm(pw, ph, 200, 30, 80, 40), text_id=1),
             Shape(a, box_mm(pw, ph, 20, 185, 260, 20), text_id=2),
             Shape(wa, box_mm(pw, ph, 100, 5, 100, 20), stype=136, wordart='Our WordArt')]
    page2 = [Shape(b, box_mm(pw, ph, 20, 20, 120, 100), text_id=2),
             Shape(g, box_mm(pw, ph, 150, 20, 20, 20), stype=75, blip=1),
             Shape(j, box_mm(pw, ph, 180, 20, 20, 20), stype=75, blip=2),
             Shape(w, box_mm(pw, ph, 210, 20, 20, 20), stype=75, blip=3),
             Shape(d, box_mm(pw, ph, 240, 20, 20, 20), stype=75, blip=4)]
    pics = [(0xF01E, blip_bitmap(gif())), (0xF01D, blip_bitmap(jpeg())), (0xF01B, blip_metafile(wmf())),
            (0xF01F, blip_bitmap(dib()))]
    return publication(c.build(doc_fields(c, pw, ph, [m, p1, p2])), t, [[], page1, page2], pics, (11, 0x5606))


def names():
    pw, ph = 148 * EMU_MM, 210 * EMU_MM
    c = Contents(0x13, pw, ph)
    m = page_chunk(c, name='Front Matter', letter='F')
    p = page_chunk(c, master=m)
    t = Text(['Times New Roman'], [DEFAULT_STYLE])
    t.story(7, [(None, [('Tab\there, a break\x0bthen more; a clef \U0001D11E and é', None)])])
    f = frame_chunk(c, p, text_id=7)
    lost = frame_chunk(c, p, text_id=99)
    line = frame_chunk(c, p)
    shapes = [Shape(f, box_mm(pw, ph, 10, 10, 128, 30), text_id=7),
              Shape(lost, box_mm(pw, ph, 10, 50, 128, 30), text_id=99),
              Shape(line, box_mm(pw, ph, 10, 90, 128, 0), stype=20)]
    return publication(c.build(doc_fields(c, pw, ph, [m, p])), t, [[], shapes], [], (11, 9999))


# ---------------------------------------------------------------- Contents, the 98/2000 form (9)

def old(mark, owner_bytes):
    """mark 0xB2D2 (98) or 0xB2E8 (2000)."""
    pw, ph = 210 * EMU_MM, 297 * EMU_MM
    t = Text(['Times New Roman', 'Arial'], [(char_props(size=10, font=0, old=True), para_props())], old=True)
    t.story(1, [(None, [('Old form text, ', None), ('bold Arial', char_props(True, None, 12, 1, old=True))])])
    t.story(2, [(None, [('One', None)]), (None, [('Two', None)]), (None, [('Three', None)]), (None, [('Four', None)])])
    chunks = []                              # (id, parent, bytes)

    def frame(typ, box, text_id=None, rows=0, cols=0):
        d = bytearray(0x70)
        struct.pack_into('<HH', d, 0, typ, 0)
        struct.pack_into('<4i', d, 6, *box)
        if typ == 0x08:
            struct.pack_into('<H', d, 0x58, text_id)
        if typ == 0x0A:
            struct.pack_into('<H', d, 0x50, cols)
            struct.pack_into('<H', d, 0x56, rows)
            struct.pack_into('<H', d, 0x66, text_id)
        return bytes(d)
    ids = [0x200, 0x201, 0x202]
    chunks.append((0x200, 0x101, frame(0x08, box_mm(pw, ph, 20, 20, 170, 30), 1)))
    chunks.append((0x201, 0x101, frame(0x0A, box_mm(pw, ph, 20, 60, 100, 40), 2, rows=2, cols=2)))
    chunks.append((0x202, 0x101, frame(0x05, box_mm(pw, ph, 140, 60, 40, 40))))
    root = bytearray(0x20)
    struct.pack_into('<HHII', root, 0, 0x01, mark, 0, 0)
    struct.pack_into('<II', root, 0x14, pw, ph)
    page = bytearray(0x1C + owner_bytes + 10 + 2 * len(ids))
    struct.pack_into('<HH', page, 0, 0x14, mark)
    struct.pack_into('<I', page, 0x14, 1)
    struct.pack_into('<HHHI', page, 0x1C + owner_bytes, len(ids), 0, 0, 0)
    for k, i in enumerate(ids):
        struct.pack_into('<H', page, 0x1C + owner_bytes + 10 + 2 * k, i)
    chunks = [(0x100, 0, bytes(root)), (0x101, 0x100, bytes(page))] + chunks
    out = bytearray(0x40)
    struct.pack_into('<HHHH', out, 0, 0xACE8, 0x22, 0x12C, 0)
    offs = []
    for cid, parent, data in chunks:
        offs.append(len(out))
        out += data
    dir_at = len(out)
    out += struct.pack('<H', len(chunks))
    for (cid, parent, data), o in zip(chunks, offs):
        out += struct.pack('<HHHI', 0, cid, parent, o)
    struct.pack_into('<I', out, 0x08, len(out))
    struct.pack_into('<I', out, 0x16, dir_at)
    streams = {'Contents': bytes(out), 'Quill/QuillSub/CONTENTS': t.build(),
               '\x05DocumentSummaryInformation': doc_summary(12, 0)}
    return cfb(streams)


# ---------------------------------------------------------------- the damaged ones

def cut():
    """A 2010 file cut short in its text: the long story's end is gone, and
    with it the text's directory chunks after TEXT (the reader says so and
    reads what is left)."""
    pw, ph = 210 * EMU_MM, 297 * EMU_MM
    c = Contents(0x18, pw, ph)
    m = page_chunk(c, name='Master Page A')
    p = page_chunk(c, master=m)
    t = Text(['Times New Roman'], [DEFAULT_STYLE])
    t.story(1, [(None, [('The start of a long story. ' + 'More of it. ' * 400, None)])])
    f = frame_chunk(c, p, text_id=1)
    shapes = [Shape(f, box_mm(pw, ph, 20, 20, 170, 250), text_id=1)]
    data = publication(c.build(doc_fields(c, pw, ph, [m, p])), t, [[], shapes], [], (14, 0))
    return data[:len(data) - 4000]


def notpub():
    return cfb({'WordDocument': b'\0' * 600, '\x01CompObj': b'\1\0\xfe\xff' + bytes(60)})


def damaged():
    t = Text(['Times New Roman'], [DEFAULT_STYLE])
    t.story(1, [(None, [('text without a publication', None)])])
    return cfb({'Contents': b'\0' * 100, 'Quill/QuillSub/CONTENTS': t.build()})


FILES = {
    'gen-2007-plain.pub': plain, 'gen-2007-runs.pub': runs, 'gen-2010-styles.pub': styles,
    'gen-2013-look.pub': look, 'gen-2003-columns.pub': columns, 'gen-2003-names.pub': names,
    'gen-98.pub': lambda: old(0xB2D2, 2), 'gen-2000.pub': lambda: old(0xB2E8, 4),
    'gen-cut.pub': cut, 'gen-notpub.pub': notpub, 'gen-damaged.pub': damaged,
}


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    os.makedirs(sys.argv[1], exist_ok=True)
    for name, make in FILES.items():
        with open(os.path.join(sys.argv[1], name), 'wb') as f:
            f.write(make())
    return 0


if __name__ == '__main__':
    sys.exit(main())
