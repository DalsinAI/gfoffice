# Where the Amiga stress documents come from

Team 5 (W142), 26 October 2026; Convert Notes added 27 October. What the
documents test, and what libowf reads of each, is in [docs/gfoffice/STRESS-DOCS.md](../../../docs/gfoffice/STRESS-DOCS.md).
Nothing here is from a private document.

## Checked in: made by our scripts

Each file is written by a script in `tests/amiga/` from the format's
published specification, not from libowf's readers. Run the script again to
make the same bytes.

| File | Made by | From | Bytes | SHA-256 |
|---|---|---|---|---|
| `ftxt-stress.ftxt` | `make_stress_ftxt.py` | "FTXT IFF Formatted Text" (Shaw, Morrison, Burns; EA IFF 85, 1985, updated 1988): one `FORM FTXT` | 5964 | `23c28ec791afa5581cd30a4cfc4c4ec00c98579a7230b0937c77a4a55f7d5177` |
| `ftxt-stress.txt` | the same | the text a reader following that specification shows | 5276 | `13fcbefa4399eb6f5bb48fb8742bfb4ffcf5f188ab08030285d6f95f4793c0bb` |
| `ftxt-stress-list.iff` | the same | a `LIST FTXT` with a `PROP FTXT` and three `FORM FTXT` | 352 | `cee87b4ee8ed0c1e5efd200bb908b6a31b26ddb34c18c3c3760d81e98415bcef` |
| `ftxt-stress-list.txt` | the same | its text | 217 | `7f52a342d0384458494b16fbe400cd0dbf74bff89f3c8a3b31283fa733e310f0` |
| `ftxt-stress-cat.iff` | the same | a `CAT FTXT` holding two `FORM FTXT` and a `FORM ILBM` | 286 | `205acbd7c2da65023a204bcffec5355869284a0bacdd9d2ccdb5730bb1ac0e0b` |
| `ftxt-stress-cat.txt` | the same | its text | 88 | `f143caee1a4e2f58dcd0d6d00dd2397a3d6836ec3dc1c8b44a2c1aa812c66da2` |
| `prowrite-stress.pw` | `make_stress_prowrite.py` | "WORD IFF Document Storage" (James Bayless, New Horizons Software, March 1987) | 10228 | `fa6f86953c16d9877f576a01900409e04b2627ad486ad844f575a15d728a82bf` |
| `prowrite-stress.txt` | the same | the document's text, a paragraph a line; then its headers and footers | 5585 | `1066ceb216de8fbb91145003a638684e5f1b2eed349ccdd992a02317b960e3fb` |

Licence: MIT, as the rest of this repository.

### How they were checked by other programs

- **ProWrite 3.2.3 (New Horizons' own demo, Aminet `text/dtp/ProWriteDEMO.lzh`)**
  opened `prowrite-stress.pw` in a lab (t5-w142, AmigaOS 3.2.3): the title in
  Helvetica 24 bold and blue, the picture with its transparent colour at
  five inches across, every font, style, colour, raised and lowered text,
  page numbers in capital Roman numerals from VII, the footer and a header.
- **MultiRead 1.0 (Arian Kulp, 1996, freeware, Aminet `text/fwrit/MultiRead.lha`)**,
  an independent ProWrite and Final Writer reader, read every paragraph of
  `prowrite-stress.pw`, headers and footers too.
- **An FTXT stripper written from the specification's section 8** (not
  committed) gives exactly `ftxt-stress.txt`, `ftxt-stress-list.txt` and
  `ftxt-stress-cat.txt` from the three FTXT files.
- **The lab's FTXT DataType** (AmigaOS 3.2.3, text.datatype 47.14, shown in
  the lab's viewer) opened `ftxt-stress.ftxt`: bold, italic, underline, inverse,
  colours and backgrounds shown, fonts not; controls the specification
  reserves show as boxes and the text of DCS strings shows.

## Checked in: a real document

| File | Source | Bytes | SHA-256 |
|---|---|---|---|
| `convert-notes.pw` | `Convert Notes`, New Horizons Software's own file, shipped with ProWrite's distribution: the notes for ProWrite File Convert 2.0.2, saved by ProWrite 3, from Aminet `text/misc/pwconv202.lha` (archive SHA-256 `6475cb1c2cb422730adbb13a80f4538c2a843785a6cf37700ba59bead547145d`), fetched 25 October 2026 | 4680 | `5ade458127e89efb1248f26985976c44caa95c7885dc10db0f42f05232acf281` |

It is New Horizons' file, not ours, and is checked in byte for byte as it
came. Its text says: "Copyright © 1992, New Horizons Software, Inc. All
Rights Reserved. This program may be freely distributed so long as the
original copyright notice is unaltered, and these notes are included." It
is the only real ProWrite document in the repository: ProWrite 3's US
Letter page in decipoints (`PSET` 6120 x 7920), its colour map (`COLR`
30-37 in ProWrite's order), a `PREC` and a `SECT`, a header and a footer
for no pages (page type 0), five fonts (Helvetica 24 and 18, Times 12,
topaz and Pica 11), tab stops and bullets, and a page break. `tests/amiga/test_prowrite.py` reads it.

## Not checked in: real documents, listed

These are real documents from the programs themselves. Their Aminet
readmes don't say we may redistribute them (two say "only for a private
use", one says Digita allowed Aminet to distribute it), so they are not in
the repository. `tests/amiga/fetch_stress_samples.sh --extract DIR` fetches
every one from Aminet, checks its archive's SHA-256 and unpacks it. Team 5
keeps a copy on daletop in
`/run/media/da1ek/WorkingSpace/AmigaChrome/team5/w142/samples/` (`aminet/`
the archives and readmes, `extracted/` their contents).

Fetched 25 October 2026 from `https://aminet.net/<path>`. "Licence" is what
the archive's readme says.

### Final Writer (IFF `FORM SWRT`), 32 documents

| Aminet path | Document(s) in it | Bytes | Licence |
|---|---|---|---|
| `dev/c/FW_ACMdev.lha` | `ACE-Devices`: the Amiga C Encyclopedia, devices volume, 10 sections | 950500 | none stated ("shareware versions"); Anders Bjerin's text, Ben Straub's layout |
| `dev/c/FW_ACMgfx.lha` | `ACE-Graphics` | 611010 | as above |
| `dev/c/FW_ACMint.lha` | `ACE-Intuition` | 650954 | as above |
| `dev/c/FW_ACMsys.lha` | `ACE-System` | 333842 | as above |
| `biz/dbase/GenTreeFWDocs.lzh` | `GenTree/Docs/GenTree.Doc`: a manual with 32 pictures, contents, index and outline | 442670 | none stated |
| `text/fwrit/Vorlagen.FW.lha` | 23 templates (one the same file as FWEtikett3350's): envelopes, labels, CD and cassette covers, a postcard, a window letter, a dice game sheet (`Kniffel`: 62 text frames, 48 pictures), file spines | 16694-125854 | none stated |
| `text/fwrit/FinalTable.lha` | `FinalTable/ExampleDoc`: tables | 18408 | shareware (the program) |
| `text/fwrit/FWEtikett3350.lha` | `AdressEtiketten3350.fw`: address labels in table cells | 33828 | none stated |
| `text/dtp/CDCover.lha` | `FinalWriter/CD Cover`, `CD Cover -PartitionBackup`: text frames, tables, master pages | 35742, 61520 | none stated |

### Wordworth (IFF `FORM WOWO`), 10 documents

| Aminet path | Document(s) in it | Bytes | Licence |
|---|---|---|---|
| `dev/e/E_Ref.ww4-7.lha` | `E Reference.ww4-7`: the Amiga E reference, Wordworth 4-7 | 300136 | none stated; text by Wouter van Oortmerssen |
| `text/dtp/WW6_Anl1_00.lha` | `WW6_Anl1_00.dok`: Wordworth 6's German manual | 307226 | Digita allowed distribution "by the Aminet medium" |
| `docs/misc/ALEM_0019.lha` | `PolicesWordworth.ww7`: a font list with a drawing | 7354 | "Freeware ... Only for a private use" |
| `docs/misc/ALEM_0025.lha` | `ZIP_template.ww7`: labels in 37 linked text boxes, ellipses, rounded boxes | 30690 | "Freeware ... Only for a private use" |
| `docs/misc/TapeLabel.lha` | `LabelTape`: a label drawn with lines and text objects | 9540 | none stated |
| `docs/misc/Ww2008CalendarITA.lha` | `Calendario2008` | 23552 | none stated |
| `docs/misc/pwrdbamy.lha` | `PBALittle.ww`, `PoweredByAmiga.ww`: a logo, all text in drawing objects | 23396 each | "mailware" |
| `text/dtp/WW-Farben.lha` | `WW-Farben`: 24 coloured boxes with text in them | 9024 | none stated |
| `text/dtp/WordWorth2demo.lha` | `Wordworth2/AboutWordworth2`, from Digita's own demo | 866 | none stated (an Amiga Format cover disk) |

### Programs, for labs only

| Aminet path | What | Licence |
|---|---|---|
| `text/dtp/ProWriteDEMO.lzh` | ProWrite 3.2.3 demo (New Horizons): opens documents, doesn't save | a publisher's demo, from the "boing" archive |
| `text/fwrit/MultiRead.lha` | MultiRead 1.0: Final Writer and ProWrite to text | "Released to the public as FreeWare" |
| `text/dtp/pgs22demo.lha` | PageStream 2.2 demo (Soft-Logik): opens PageStream 2 documents, doesn't save | a publisher's demo |
