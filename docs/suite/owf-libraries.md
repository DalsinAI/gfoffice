# owf.library split into four libraries

Version 0.1 | 25 October 2026 | AmigaChrome Team (Team 5), MIT

Dale decided, and confirmed at 14:2x, to split owf.library along the
programs' lines: one library each for Write, Calc, Pub and Present. Today
owf.library 2.4 (libowf 1.4.0) is 744,880 bytes. OpenWrite loads all of it,
although it uses only the document code. This page says what goes where and
what the split costs and saves. The sizes were measured, not estimated (see
section 7). It also covers names, versions and checks, what happens to
owf.library 2, and how OWConvert uses the four libraries.

## 1. The split

Each library is named after its program. Each holds that program's model and
formats, and the calls it needs to share the model's structures. The sources
stay where they are: no file moves (section 6).

| Library | For | Holds (libowf files) |
| --- | --- | --- |
| `owfwrite.library` | OpenWrite | The document model (`owf_doc.c`) and its formats (`owf_format.c`'s table): DOCX and ODT in and out, flat ODT, Final Writer, Wordworth, ProWrite, IFF FTXT, ANSI, text, HTML out, and PDF out (`out_pdf.c`). RTF has no reader in libowf yet; when it gets one, it goes here. |
| `owfcalc.library` | OpenCalc | The workbook model (`owf_book.c`, `owf_booknum.c`), XLSX, ODS, flat ODS, XLS and CSV (with `owf_xlformula.c` and `owf_xlfunc.c`), charts (`owf_chart.c`), the formula engine (`libowf/calc`), and the library's own doubles and maths (`owf_softdouble.c`, `owf_libm.c`). |
| `owfpub.library` | OpenPublisher | The `.pub` reader (`in_pub.c`) and its compound-file reader (`owf_cfb.c`), with the document model its stories use. |
| `owfpresent.library` | OpenPresentation | The deck model (`owf_deck.c`), ODP and flat ODP (`in_odp.c`, `out_odp.c`, and W138's coming `in_pptx.c`), and the page PDF writer (`owf_pdf.c`, `owf_pdffonts.c`), with the document model its text frames use. |

These lists are not kept anywhere. Each library is built from one small file
holding its call table. That file is linked against libowf compiled once into
an archive, so the linker takes only the files the calls reach (section 6). A
new libowf file joins whichever library uses it. W138's `in_pptx.c` joins
owfpresent.library as soon as `owf_deck.c`'s format table names it. Nobody
has to edit a list in `build-library.sh`.

## 2. The shared core: a copy in each library

The core is the zip and inflate code (`owf_zip.c`, `owf_unzip.c`,
`owf_inflate.c`, the z.library glue), XML (`owf_xml.c`), buffers
(`owf_buf.c`), styles (`owf_style.c`), compound files (`owf_cfb.c`), reports
and memory (`owf_doc.c`), `owf_kind.c` and the runtime (`owf_runtime.c`,
`owf_libc.c`, `owf_sys_dos.c`).

**Recommended: each library links its own copy.** We measured both options
from the link maps of the four libraries as built:

| | owfwrite | owfcalc | owfpub | owfpresent |
| --- | --- | --- | --- | --- |
| Core code in the library | 40.8 KB | 38.0 KB | 30.1 KB | 39.7 KB |
| Calls from the library's own code into the core | 1,286 | 2,199 | 126 | 1,297 |
| Distinct core functions called | 86 | 63 | 30 | 78 |

The union of the core is 40.8 KB.

A fifth library, `owfcore.library`, would save memory only when several
programs run at once. The estimate below starts from those measured sizes.
It adds about 4 KB of glue in the core (its vector table and 145 stubs). It
keeps about 5 KB in each library: printf, the string calls and the variadic
calls. And it adds about 4 KB per library for its calls into the core going
through a base:

| Programs running | Copies (recommended) | Fifth library | Fifth library against copies |
| --- | --- | --- | --- |
| OpenWrite alone | 186.6 KB | 199.6 KB | costs about 13 KB |
| OpenWrite and OpenCalc | 768.6 KB | 752.6 KB | saves about 16 KB |
| All four | 1,067.2 KB | 999.4 KB | saves about 68 KB |

Against those savings, a fifth library would cost:

- **145 internal functions would become a library ABI.** These are
  `owf_buf_*`, `owf_xml_*`, `owf_zip_*`, `owf_cfb_*`, `owf_styles_*`, the
  builder and the reports. W130, W131 and W138 change them freely today; as
  ABI they could only change together with a new core version.
- **Every hot inner call would cross a library.** For example, every
  character a writer puts goes through `owf_buf_put`. That is slow on a 68040.
- **The core would also have to own malloc.** A document grown by one
  library's code is freed by the core, so every allocation becomes a library
  call.
- **The variadic calls can't move.** `owf_report_add`, `owf_buf_printf` and
  `snprintf` would stay in every library anyway.

Revisit this if running several GFOffice programs at once becomes the usual
case, for example with OpenBase.

**Copies that aren't the core.** owfpresent.library also carries 57 KB of
OpenWrite's ODF text code: `in_odt.c`, `out_odt.c`, `out_html.c` for the
paragraphs of slides, and `out_pdf.c` for pictures in PDFs. Splitting those
helpers into files of their own would trim it. That is a later change in
OpenPresentation's files, not part of this split.

### What changes in libowf for this

These changes are small, and they don't change behaviour:

- `owf_error_text`, `owf_error_name`, `owf_same_word` and `owf_ext_in_list`
  move to a new `owf_common.c`. They used to be in the format tables'
  files, so every library would have pulled in the document and workbook
  formats.
- `owf_pub_detect` moves from `in_pub.c` to `owf_kind.c`, so every library
  knows a `.pub` without the reader.
- `owf_kind.c` is built once per library. `OWF_KIND_NO_DOCUMENTS` and
  `OWF_KIND_NO_WORKBOOKS` leave out the format tables that library doesn't
  have.
- The runtime's maths names move to `owf_runtime_math.c`, so a library
  without doubles links no maths. owfwrite.library has none.
- The runtime gains a small `sscanf` (integers only) for `in_odp.c`.
- `owf_report_text(report, level, text)` is added: `owf_report_add` without
  the `...`, which a library call can't take.

## 3. Names, versions and calls

| Library | Base | Version | Calls | Proto |
| --- | --- | --- | --- | --- |
| `owfwrite.library` | `OWFWriteBase` | 3.0 | 39 | `<proto/owfwrite.h>` |
| `owfcalc.library` | `OWFCalcBase` | 3.0 | 77 | `<proto/owfcalc.h>` |
| `owfpub.library` | `OWFPubBase` | 3.0 | 37 | `<proto/owfpub.h>` |
| `owfpresent.library` | `OWFPresentBase` | 3.0 | 79 | `<proto/owfpresent.h>` |

**Version 3 for all four.** The four libraries continue owf.library 2, so
their versions go on from it. OpenUp's GFOffice part takes its version from
the library, and a part's version only goes up: GFOffice 2.4 becomes 3.0.
After the split, each library's revision moves on its own. A change to the
workbook's structures bumps owfcalc.library alone.

**The LVO tables.** Each library has its own SFD in `libowf/library/`
(`owfwrite_lib.sfd` and the others). The same block of 17 calls comes first
in each, at the same offsets: the calls every program needs. They are:

- `owf_error_text` and `owf_error_name`;
- `owf_malloc`, `owf_calloc`, `owf_realloc` and `owf_free`;
- the five report calls, including the new `owf_report_text`;
- `owf_struct_size` and `owf_file_kind`;
- the three `owf_unzip` calls and `owf_inflate`.

After that block:

- owfwrite, owfpub and owfpresent share the document-model block, 15 calls:
  `owf_doc_*`, `owf_story_add`, `owf_para_add_*` and `owf_font_modern`.
  owf.library didn't export `owf_doc_add_frame` and `owf_para_add_frame`;
  this block does.
- Then each library's own calls follow.
- owfcalc.library exports every workbook call OpenCalc makes. owf.library 2
  lacked seven of them (comments, validation, charts, kept parts and sheet
  extras), so OpenCalc couldn't use the library.

The library side of each SFD, `<name>_calls.h`, is generated by
`scripts/gen-lib-calls.py`. `scripts/gen-sdk.sh` makes it together with the
SDK headers, and `tests/library/check_sfd.py` checks that both are current.
So adding a call means adding one SFD line and running `gen-sdk.sh`.

**Memory.** Each library has its own memory pool. Memory one library hands
out goes back through that library's `owf_free`.

**Checks when a program opens a library.** `owf_open()` (`owf_open.h`)
opens the library the file was built for. It then checks the sizes of that
library's structures only:

- owfwrite.library checks the document model's.
- owfcalc.library checks the workbook's and the charts'.
- owfpub.library checks the document model's and `owf_pub`'s.
- owfpresent.library checks the document model's and the deck's.

So a change to `owf_cell` doesn't stop OpenWrite. The deck's 11 structures
are added at the end of `owf.h`'s `OWF_STRUCT_*`, as new structures always
are.

**How a program says which library it uses.** It defines one of
`OWF_USE_WRITE_LIBRARY`, `OWF_USE_CALC_LIBRARY`, `OWF_USE_PUB_LIBRARY` or
`OWF_USE_PRESENT_LIBRARY`:

- The new `owf_proto.h` then includes that library's inline calls, once
  every header has declared its calls.
- A program on a library calls `owf_report_add` as before: `owf_proto.h`
  formats the note and passes it to `owf_report_text`.
- The four libraries share their first 17 calls' names, so **one file uses
  one library**. A program that needs several keeps each library's calls in
  a file of its own (section 5).

**`owf_file_kind` in each library.** Every library judges these the same
way:

- packages (ODF by `mimetype`, OOXML by their main part);
- compound files, including `.pub`;
- flat ODF;
- OpenBase databases.

Other files, such as plain text, IFF and raw BIFF, are judged by the
library's own readers:

- owfwrite.library says plain text is a document.
- owfcalc.library knows old Excel.
- owfpub.library and owfpresent.library answer "unknown" for plain text.

## 4. owf.library 2: dropped

**Recommended: drop it once OpenUp ships the four libraries.**

We checked whether anything outside GFOffice uses it:

- Nothing outside GFOffice opens it.
- openamigaup is the only other repository that names it: `build_pack.py`'s
  GFOffice part, its tests, `openup.readme` and `Install-OpenUp`.
- These 17 DalsinAI repositories have no code that names `owf.library`,
  `OWFBase`, `proto/owf.h` or libowf: amigachrome, amigachrome-guest,
  openamiga, openamigabase, openamigabench, openamigabrowser,
  openamigacompress, openamigaedit, openamigafilemanager, openamigaimage,
  openamigamail, openamigamui, openamigaprefs, openamigaprint,
  openamigaservice, openamigaxml and opendatatypes. (amigachrome's design
  documents mention libowf, but no code does.)

Keeping owf.library would cost 745 KB on every install, and a revision bump
of its own for every structure change, for no user.

The order of the changes:

1. **This split's first PR** builds the four libraries next to owf.library
   2.4. owf.library is unchanged, so OpenUp's `build_pack.py` keeps working.
2. **The programs move to their libraries**, each in a PR of its own.
3. **The openamigaup PR**, after OpenUp 0.8.10, which W135 is building with
   today's owf.library:
   - the GFOffice part ships `LIBS:owfwrite.library`, `owfcalc.library`,
     `owfpub.library` and `owfpresent.library`;
   - it retires `LIBS:owf.library` (OpenUp's `retire` keeps the file aside);
   - the part's version comes from owfwrite.library (3.0);
   - OpenCalc comes from the library build.
4. **A last gfoffice PR** removes owf.library's build, its SFD and
   OWFLibTest. `OWF_USE_LIBRARY` alone then stops meaning owf.library.

## 5. OWConvert

OWConvert converts documents, workbooks and presentations. On the libraries
it uses all four, and loads only the ones a conversion needs.

**One file per library:**

| File | Built with | Does |
| --- | --- | --- |
| `tools/owconvert.c` | no library | The arguments, choosing a conversion, printing |
| `tools/owc_doc.c` | `OWF_USE_WRITE_LIBRARY` | Documents, and `owf_file_kind` |
| `tools/owc_book.c` | `OWF_USE_CALC_LIBRARY` | Workbooks |
| `tools/owc_deck.c` | `OWF_USE_PRESENT_LIBRARY` | Presentations, and their PDFs through OpenLayout (`src/deck`, compiled for the same library) |
| `tools/owc_pub.c` | `OWF_USE_PUB_LIBRARY` | `.pub` files: their version and pages. OWConvert can't convert them until OpenPublisher saves something. |

**How a conversion is chosen:**

1. OWConvert opens owfwrite.library first. Documents are the usual case, and
   its `owf_file_kind` knows every package and compound file.
2. A deck goes to owfpresent.library, a `.pub` to owfpub.library.
3. A workbook goes to owfcalc.library. So does a file whose target (`TO` or
   `FORMAT`) owfwrite.library can't write, such as text to `.csv`.
4. `FORMATS` opens all four and lists every format.

With libowf linked in, as on the PC and in `OWF=linked`, the same files are
built together with no library at all.

## 6. Building

`build-library.sh` builds:

- libowf, once, into `obj/libowf.a`;
- each library from `libowf/library/<name>_library.c` (its calls and
  `owf_libbase.h`, the library code they share) plus `owf_kind.c` for that
  library;
- owf.library 2.4 as before, until section 4's last step;
- the tests:
  - **OWFLibsTest**, the successor of OWFLibTest, opens the four libraries.
    For each, it checks the 17 shared calls through that library. Then it
    checks the library's own calls:
    - documents saved and read back;
    - the workbook fixtures and the formula engine;
    - `Sample.pub`;
    - an ODP saved and read back, a new deck, and a PDF page.

    It runs everything again from a second task at the same time.
    `ONLY=write` and the others test one library alone.
  - OWFLibTest stays as long as owf.library.

`build-os3.sh`'s `OWF=library` builds each program on its own library:
OpenWrite on owfwrite.library, OpenCalc on owfcalc.library, and OWConvert on
all four.

**Why build-library.sh, owf.h and the tests change, not the programs.** W130
(OpenWrite), W131 (OpenCalc) and W138 (OpenPresentation) keep working in
`app/`, `src/` and the format files. This split changes those files only as
follows:

- the four moved functions and the `owf_kind.c` guards (section 2);
- `owf.h`'s tail;
- one line in each program, its "needs ... in LIBS:" message;
- OWConvert's split into files.

## 7. Sizes

Both columns were built with OpenAmigaGCC 16.2 (open3) and GFOffice's own
flags, from main `2238c05` and this design's branch. They are file sizes in
bytes. The implementation PRs give the os32-gcc16 stove's builds and the
lab's `Avail` before and after.

**Before:**

| File | Bytes | Notes |
| --- | --- | --- |
| owf.library 2.4 | 744,880 | |
| OpenWrite on owf.library | 251,660 | as OpenUp ships it |
| OpenCalc with libowf linked in | 1,046,476 | owf.library lacked its calls |
| OWConvert with libowf linked in | 496,036 | as OpenUp ships it, with deck PDFs |

**After:**

| File | Bytes |
| --- | --- |
| owfwrite.library 3.0 | 186,600 |
| owfcalc.library 3.0 | 581,984 |
| owfpub.library 3.0 | 67,384 |
| owfpresent.library 3.0 | 231,248 |
| OpenWrite on owfwrite.library | 253,564 |
| OpenCalc on owfcalc.library | 250,884 |

**What each program loads (the program and its libraries):**

| Program | Before | After | Change |
| --- | --- | --- | --- |
| OpenWrite | 996,540 | 440,164 | −556,376 (−56%) |
| OpenCalc | 1,046,476 | 832,868 | −213,608 (−20%) |
| OpenWrite and OpenCalc together | 2,043,016 | 1,273,032 | −769,984 (−38%) |

OWConvert's numbers come with its PR: a document conversion loads
owfwrite.library only.

## 8. The PRs

1. **This design.**
2. **The four libraries, their build and OWFLibsTest**, beside owf.library
   2.4. No program moves yet.
3. **OpenWrite on owfwrite.library.** One message line in `app/openwrite2.c`.
4. **OpenCalc on owfcalc.library.** `build-os3.sh` builds it in
   `OWF=library` too.
5. **OWConvert on all four** (section 5).
6. **openamigaup, after OpenUp 0.8.10:** the GFOffice part ships the four
   libraries and retires owf.library (section 4).
7. **gfoffice: owf.library 2's build removed.**

Team 5
