# OpenPublisher's stress documents: where they come from

Team 5 (W142), 26 October 2026. What they test, and what the reader gets
out of them, is in [docs/gfoffice/STRESS-DOCS.md](../../../docs/gfoffice/STRESS-DOCS.md),
sections 2 and 3. Nothing here is from a private document, and nothing was
opened in another program that reads .pub (the clean room,
`docs/gfoffice/Design-OpenPublisher.md`, section 2).

## Microsoft Publisher: checked in

| File | Source | Licence | Version (written by) | Bytes | SHA-256 |
|---|---|---|---|---|---|
| `SampleNewsletter.pub` | apache/poi `test-data/publisher/SampleNewsletter.pub`, commit `4f3c25563919e7efa4a75feccf6cef1ac3d05a04`, fetched by `tests/pub/fetch_samples.sh` | Apache-2.0 (`licenses/Apache-2.0.txt`; POI's notice in `tests/pub/samples/NOTICE-Apache-POI.txt`) | 2002/2003 format (11.5606) | 291840 | `6a825ba26ba35d6e885acdc62e859591ed37cb0ff7480b554b9cb362b644dfcf` |

The same file `tests/pub/CORPUS.md` lists as fetched; it is checked in here
because it is the most complicated .pub with a licence that lets us: 4 A4
pages, 99 frames (63 text, 10 pictures, 13 shapes, 9 groups, 4 lines), 45
stories of which 10 run through linked frames (29 frames), 9 pictures (8
WMF, 1 JPEG) and a WordArt. The rest of the stress set is the corpus in
`tests/pub/samples` (STRESS-DOCS.md, section 2).

## Looked for, not found

- **More free .pub files:** apart from the corpus (`tests/pub/CORPUS.md`), none
  with a licence we can use. govdocs1 (digitalcorpora.org, a million files
  from US government web sites, freely redistributable): every one of its
  `.pub` files read so far is plain text (lists of publications), not a
  Publisher file; the zip threads' directories were read by HTTP ranges on
  daletop, and the files over 3 KB fetched and looked at.
- **Generated .pub:** not made. The format has no specification; a file we
  wrote would hold only what PUB-FORMAT.md already believes.

## PageStream: listed, not checked in

17 real PageStream documents from Aminet, kept out because their readmes
give no licence. `tests/amiga/fetch_stress_samples.sh --extract DIR` fetches
them with the other Amiga samples and checks each archive's SHA-256. Team 5
keeps a copy on daletop in
`/run/media/da1ek/WorkingSpace/AmigaChrome/team5/w142/samples/`. Fetched 25
October 2026 from `https://aminet.net/<path>`.

| Aminet path | Document(s) | Version | Bytes | Readme says |
|---|---|---|---|---|
| `mags/misc/FLAK9505.lha` | `FLAK-archive/PageStream3/GAC_NL_9505`, `GAC_NL_9505.cvr`: a club newsletter and its cover | 3 | 476872, 652882 | "Distribution: Global" |
| `mags/misc/FLAK9701.lha` | `GAC_NL/newsletters/GAC_NL_9701`: 9 pages, 17 articles (its pictures beside it as EPS and JPEG) | 3.2 | 107888 | nothing |
| `misc/sci/PeriodicTable.lha` | `PeriodicTable-FR.pgs`, `-NL.pgs`: 182 table cells (and a JPEG of how it looks) | 4.1 or later | 393462, 475042 | nothing |
| `text/dtp/CDCover.lha` | `Pagestream/CD Cover`, `CD Cover - AminetGames1` | 3 or later | 13814, 290804 | nothing |
| `game/role/CharacterSheet.lha` | `Character.p4`, `Character.p2`: the same sheet in two versions | 4, 2 | 66552, 28066 | nothing |
| `text/dtp/BusinessCards.lha` | `BusinessCards.pgs` | 3 or later | 3536 | nothing |
| `text/dtp/PgS3sampleDoc.lha` | `ParchmentMFSample.DOC`: a 2-page font sample | 3 | 22956 | nothing |
| `mags/misc/daug0193.lha`, `daug0293.lha`, `daug0393.lha` | `January93`, `February93`, `March93.pgs`: a club newsletter | 2 | 187036, 160612, 163414 | nothing |
| `mags/misc/ahug0193.lha` | `AmigaBreeze.Jan93`: a club newsletter | 2 | 104299 | nothing |
| `text/dtp/8mmlabel.lha` | `8mm Labels` | 2.2 or later | 114636 | nothing |
| `util/misc/OpticalMousepd.lha` | `GoldenImage_Mousepad` | 2 | 39904 | nothing |

PageStream's own demo, `text/dtp/pgs22demo.lha` (Soft-Logik, PageStream 2.2),
opens PageStream 2 documents but doesn't save, so it can show these but not
make new ones.
