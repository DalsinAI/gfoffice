# OpenWrite: design

Version 0.1 | 4 October 2026 | First design | Dalsin Limited, MIT

> **Architecture update, 6 October 2026:** the WebCore-first editor in this
> original design is superseded as the baseline. OpenWrite now uses the small
> native C editor described in `docs/architecture/NATIVE_CORE.md`, with the UI
> in `docs/UX_DESIGN.md`. WebCore remains an optional future component. The
> product goals and format research below remain useful historical context.

We, 4 October 2026: "let's do the OpenWrite project", a word processor
"that can import any of the Amiga formats, along with DOCX and the
OpenDocument formats". It follows from the browser work: WebKit on AmigaOS
3.2 sets us up for a lot more app porting, and a word processor is the first
app to build on it.

**Decided** (We, 4 October 2026):
- **The name:** OpenWrite. **Repository:** `DalsinAI/openamigawrite`.
- **The engine (superseded 6 October):** the first design chose WebCore. The
  shipping baseline is now the native C editor; WebCore is optional, not required.
- **Formats:** it opens every Amiga word processor's documents we can get
  samples of, plus DOCX and the OpenDocument formats, and saves DOCX and ODT.

**Proposed here** (ours to change):
- ODT is the default save format (section 4.1).
- The format filters are a separate C library with a Shell converter, so they
  are useful on any Amiga long before the editor is (section 3.1).

## 1. What OpenWrite is

A word processor for AmigaOS 3.2 that:
- opens and saves the documents people exchange today (DOCX and ODT), with
  their text, styles, lists, tables and pictures;
- opens the documents people wrote on their Amigas (ProWrite, Final Writer,
  Wordworth, Final Copy, Excellence!, Kindwords, Textcraft and the rest), so
  thirty years of letters and stories come back as editable text;
- prints, and saves PDF;
- looks and behaves like an Amiga program: a GadTools window, Amiga keys,
  OS 3.2-style icons, an ARexx port and the clipboard.

It follows the Open app rules (OpenGadTools' design, section 0): obvious,
icon-driven toolbars with text by default, everything visible as a button or
a menu item.

## 2. Why WebCore, and what it does not give us

WebCore is the engine inside WebKit. Its editing code is what Apple Mail's
compose window and every `contenteditable` page uses. OpenBrowser's port
(`DalsinAI/openamigabrowser`) already draws real pages on AmigaOS 3.2.3 with
cairo, FreeType and HarfBuzz, and its editor client
(`src/webcore/AmigaEditorClient.cpp`) already turns typing and editing keys
into WebCore's commands.

**What it gives us, already written and tested:**
- text layout with proper shaping, kerning and Unicode, including right-to-left;
- the caret, selection by mouse and keys, and drag and drop of text;
- editing commands: bold, italic, underline, strike-through, fonts, sizes,
  colours, alignment, indents, numbered and bulleted lists, links, pictures,
  inserting HTML, and undo/redo steps for each;
- CSS for styles: a paragraph style is a CSS class;
- tables, pictures with text flowing round them (floats), several columns;
- printing with page breaks (WebCore's `PrintContext`), and a paginated view
  (WebCore's pagination modes) for a page layout view.

**What it does not give us, and our answer:**

| Gap | Our answer |
| --- | --- |
| Tab stops (left, right, centre, decimal) | CSS has only a fixed tab width. Phase W2 imports tabs at their nearest default stop and lists the loss in the import report; phase W4 adds real tab stops to WebCore's line layout (a small, contained patch) |
| Headers, footers and page numbers | Not in WebKit's CSS. We draw them ourselves: in the page view between pages, and on each printed page |
| Footnotes | Not in CSS. Kept as numbered notes at the end of the document first (W4); on the page later |
| Different page setup per section | One page setup per document at first; sections later |
| Tracked changes and comments | Not planned. Imported documents keep the accepted text; comments are listed in the import report |
| Fields (page count, dates, cross-references) | Page number, page count and date first (they are what Amiga documents use) |

## 3. Architecture

```
   OpenWrite (the window, C)                C:OWConvert (Shell, C)
   menus, toolbars, dialogs, status bar              |
          |                                          |
   ob_webview + editing calls                        |
   (C++, WebCore; shared with OpenBrowser)           |
          |                                          |
          +------------------+-----------------------+
                             |
               libowf: the OpenWrite filters (C99, zlib only)
         importers --> one document model --> exporters
```

### 3.1 libowf, the filters

The filters are plain C99, depend only on zlib, and build three ways: for
AmigaOS 3.x on any 68000 (GCC 6.5, libnix), for OS 3.2 with the GCC 16
stove, and for Linux, where the tests run. They never touch WebCore.

- **One document model** (`owf_doc`): styles (paragraph and character), page
  setup, headers and footers, and a list of blocks: paragraphs (runs of text
  with character formatting, tabs, line breaks, links, pictures, fields,
  notes), tables (rows of cells, each a list of blocks), page breaks. It is
  what every importer builds and every exporter walks, so N formats in and M
  out need N + M filters, not N × M.
- **Importers and exporters,** one file each, registered in one table with
  their file name extensions and a "does this look like me?" check on the
  first bytes (IFF FORM types, zip contents, RTF's `{\rtf`).
- **An import report** with every document: what was kept, what was
  approximated (a tab stop, a font) and what was dropped (a comment). The
  window shows it once, and the Shell converter prints it.
- **Small own parts:** a zip reader and writer on zlib, a streaming XML reader
  (namespaces, entities; well-formed input only, which is what ODT and DOCX
  are), an IFF reader, and character set tables (Amiga ISO-8859-1, ISO-8859-2,
  Windows-1252 for RTF and DOCX, UTF-8). We write our own XML reader rather
  than use libxml2 so the converter fits a 68000 with 1 MB.

### 3.2 The editing view

OpenWrite uses OpenBrowser's page layer, `ob_webview.h`: a C interface to one
WebCore page that paints into a 32-bit buffer and takes the mouse and keys.
It needs a few more calls, which also serve OpenMail's rich compose window
later:

| Call | What |
| --- | --- |
| `ob_webview_set_editable(view, on)` | The whole document is editable (`designMode`) |
| `ob_webview_command(view, name, value)` | Runs one of WebCore's editing commands (`Bold`, `FontName`, `InsertOrderedList`, `Undo`...) |
| `ob_webview_command_state(view, name, value, size)` | Whether a command applies here, is on, and its value (for the toolbar's buttons and the style and font gadgets) |
| `ob_webview_get_html(view)` / `ob_webview_set_html(...)` | The document out and in |
| `ob_webview_find(view, text, flags)` | Find and find again (WebCore's `Editor::findString`) |
| `ob_webview_print(view, page setup, callbacks)` | Lays the document out in pages and draws each one onto a cairo surface (PDF or bitmap) |
| `selection_changed`, `content_changed` callbacks | So the window updates its toolbar and knows the document is unsaved |

Beside those, the editing view needs three things that OpenBrowser needs too:
an **undo stack** in the editor client, the **Amiga clipboard** behind
WebCore's pasteboard (IFF FTXT on `clipboard.device`, with a UTF-8 chunk and
our own HTML chunk so formatting survives a copy from OpenWrite to OpenWrite
or to OpenMail), and **spelling** through WebCore's text checker (Hunspell,
phase W5).

**Who writes them:** the OpenBrowser work owns `src/webcore` and adds them
there (one shared page layer, not a second copy in OpenWrite), after its
current milestone, Microsoft's sign-in page. Every piece needs the WebKit
build to compile and test, and the undo stack, the clipboard and printing
touch its WebKit patches too. Appendix A is the shape we propose for it to
implement against.

### 3.3 The window

GadTools through OpenGadTools: a menu strip, a toolbar, a format bar, the
document, and a status bar. Section 6 has the detail.

### 3.4 C:OWConvert

The filters in a Shell command, so documents convert on any Amiga and in
scripts:

```
OWConvert FROM/A,TO/A,FORMAT/K,REPORT/S
1> OWConvert Work:Letters/Bank.pw RAM:Bank.odt
1> OWConvert Work:Story.fw Story.docx REPORT
```

It runs on a stock 68000 Amiga. On AmigaChrome, Cradle can use the same
filters' Linux build for its own document jobs.

### 3.5 ARexx

A port named `OPENWRITE`: `OPEN`, `SAVE`, `SAVEAS`, `EXPORT`, `PRINT`,
`INSERTTEXT`, `GETTEXT`, `FIND`, `COMMAND` (any editing command), `QUIT`.
Enough for mail merge and for other programs to hand OpenWrite a document.

## 4. Formats

The full list, with what is known about each format and where its knowledge
comes from, is in [docs/FORMATS.md](docs/FORMATS.md). In short:

| Formats | Open | Save |
| --- | --- | --- |
| ODT, and its template (OTT) and flat XML (FODT) forms | Yes | Yes (ODT is the default) |
| DOCX, and its template (DOTX) | Yes | Yes |
| RTF | Yes | Yes |
| HTML | Yes | Yes |
| Plain text (Amiga ISO-8859-1 or UTF-8), Markdown | Yes | Yes |
| PDF | No | Yes, and print |
| Amiga word processors' own formats | Yes | No |
| Amiga text formats: IFF FTXT, AmigaGuide, ANSI text | Yes | FTXT (the clipboard) |
| Word 97-2003 (.doc), WordPerfect | Later | No |

### 4.1 The default save format

**ODT** (OpenDocument Text, ISO/IEC 26300): an open standard that LibreOffice,
Word and Google Docs all open, so a document written on the Amiga goes
anywhere without a "Save as" step. Everything OpenWrite can make maps onto
ODT, and the tests check that ODT → OpenWrite → ODT keeps it. Until the ODT
filter passes those tests (phase W1), OpenWrite saves HTML.

### 4.2 The Amiga formats

"Any of the Amiga formats" is the goal, and the formats fall into three
groups:
1. **Documented:** IFF FTXT (EA IFF 85), ProWrite's IFF WORD (New Horizons'
   1987 specification, in the IFF documents), AmigaGuide, ANSI text. Built
   from their specifications.
2. **IFF, not documented:** Final Writer and Final Copy (SoftWood), Wordworth
   (Digita; its own IFF form, WTXT). The IFF reader gets us the chunks; their
   meaning comes from samples.
3. **Their own format, not documented:** Excellence!, Kindwords, Textcraft,
   Pen Pal, Protext, BeckerText, WordPerfect for the Amiga and others.

**The format lab:** for groups 2 and 3 we learn each format from documents we
make ourselves. An AmigaChrome instance runs the original programs; the same
test document (headings, the styles, fonts and sizes, tabs, a header and
footer, a page break, a picture) is typed into each one, then saved again
with one change at a time, and the files are compared. Where a program can
also save RTF or plain text, that copy is a second answer to check against.
The sample files are ours, so they go into the tests.

This is clean-room work: the specifications and our own samples. Other
people's converters are read for reference only, unless their licence lets us
reuse them with credit (fw2odf, an MIT Final Writer converter, is one).

**What "imports" means:** the text always comes back. Then, as far as each
format allows: paragraphs, fonts (mapped to modern ones, section 8), sizes,
bold, italic and underline, alignment, indents, tabs, headers and footers,
page breaks and pictures (ILBM, decoded to PNG). The import report names
anything that could not come across.

## 5. The document inside the editor

Inside the editor a document is an HTML page with a fixed shape, so the
filters can read it back exactly:

- `<head>` holds a `<style>` with the page setup (`@page`) and one CSS class
  per named style (`p.Body`, `h1.Heading1`, `span.Emphasis`...), and a
  `<meta name="generator" content="OpenWrite">`.
- Paragraphs are `<p>` and `<h1>`-`<h6>` with their style's class; direct
  formatting is an inline `style`.
- Lists are `<ol>`/`<ul>`, tables `<table>`, pictures `<img>` with the
  picture inside the document (a `data:` URL, PNG or JPEG), links `<a>`.
- Our own parts are elements with an `ow-` class: page breaks
  (`ow-page-break`), fields (`ow-field`, `data-field="page"`), notes
  (`ow-note`), tab stops on a paragraph (`data-ow-tabs`).

The window loads this into WebCore to edit, and on save takes the page back
and gives it to the filters.

## 6. The window

**First size:** 800 x 600, centred in the screen's free area (below the title
bar and beside OpenDock), and never larger than that area, so on a screen
smaller than 800 x 600 it is the whole free area (the rule for every Open app,
10 October 2026, as in OpenFiles 0.2.3). A size the user gives the window is
kept: OpenWindows remembers it. At 800 wide the inspector starts hidden (it
shows from 900); making the window wider brings it back.

**Menus** (Amiga keys in brackets):
- **Project:** New (A-N), Open... (A-O), Open Recent, Save (A-S), Save As...,
  Export PDF..., Page Setup..., Print... (A-P), Import Report, About, Quit (A-Q).
- **Edit:** Undo (A-Z), Redo, Cut (A-X), Copy (A-C), Paste (A-V), Paste as
  Text, Select All (A-A), Find... (A-F), Find Next (A-G), Replace... (A-R).
- **View:** Draft, Page Layout, Zoom (50-200 % and Page Width), Toolbar,
  Format Bar, Ruler, Status Bar.
- **Insert:** Picture..., Table..., Page Break, Link..., Special Character...,
  Date, Page Number.
- **Format:** Font..., Paragraph..., Styles..., Bullets and Numbering...,
  Columns..., Page...
- **Tools:** Word Count, Spelling..., Preferences...
- **Help:** OpenWrite Guide (AmigaGuide).

**Toolbar:** New, Open, Save, Print, PDF | Cut, Copy, Paste | Undo, Redo |
Find. Icons only by default (every Open app starts with icons, the Team's rule
of 10 October 2026); icons and text or text only from the View menu.

**Format bar:** style (cycle), font (a list from fontconfig), size, bold,
italic, underline, text colour, the four alignments, numbered list, bullets,
indent less and more. Each button shows its state at the cursor.

**Status bar:** page *n* of *m*, words, zoom, and "Changed" when unsaved.

**Views:** *Draft* is one long page as wide as the paper, for writing.
*Page Layout* shows the pages with their margins, headers and footers, as
they print.

**Dialogs:** Font, Paragraph (indents, spacing, alignment, tab stops), Page
Setup (paper, margins, orientation, header and footer), Insert Table, Find
and Replace, Styles, Word Count, Preferences (default font, paper size,
default save format, units: cm or inches), and the import report.

**From the Amiga:** pictures through the ASL file requester and datatypes,
any picture the system has a datatype for; documents dropped on the window
(AppWindow) or on its icon (AppIcon); the clipboard shared with every other
program; one running copy, with a new window per document.

## 7. Printing and PDF

WebCore lays the document out in pages (`PrintContext`), and we draw each
page, with its header and footer, onto a cairo surface:
- **Save as PDF:** a cairo PDF surface, written to the file.
- **Print to a modern printer:** the same PDF, handed to **OpenPrint**, which
  sends it over IPP.
- **Print to a printer on the parallel port:** each page drawn into a bitmap
  and sent through `printer.device`, so the printer drivers Amiga users have
  keep working.

Our cairo build (`DalsinAI/openamigacairo`) has image surfaces only today;
OpenWrite needs its PDF surface turned on (section 10).

## 8. Fonts

WebCore draws with TrueType and OpenType fonts through FreeType and
fontconfig, not with Amiga bitmap fonts. OpenWrite ships the fonts that make
exchanged documents look right:
- **Liberation** Serif, Sans and Mono: the same widths as Times New Roman,
  Arial and Courier New, so a DOCX keeps its line breaks;
- **Carlito** and **Caladea:** the same widths as Calibri and Cambria, Word's
  defaults since 2007;
- **DejaVu,** for the characters the others lack.

Any TrueType or OpenType font the user adds to `FONTS:` is found too. The ASL
font requester lists only Amiga fonts, so the font dialog is our own list
(OpenGadTools).

**Amiga font names** in imported documents are mapped: Times and CG Times to
Liberation Serif; Helvetica and CG Triumvirate to Liberation Sans; Courier
and topaz to Liberation Mono; other names to the nearest by family and kind,
noted in the import report.

## 9. Platform, size and speed

- **The editor** needs what OpenBrowser needs: AmigaOS 3.2, a 68020 or better
  with an FPU (a 68040 or 68060, an AC090 or a PiStorm in practice), and a
  lot of Fast RAM: 128 MB at the least, 256 MB recommended. A graphics card
  screen (RTG) looks best; on AGA the page is dithered to the screen's pens.
- **Size:** a WebCore program is about 115 MB today: 82 MB of code and
  33 MB of ICU's data, linked into each program. In order:
  1. ICU's data as one shared file (for example `LIBS:icudt78b.dat`) instead
     of a copy in every program: 33 MB less each.
  2. ICU trimmed to the languages and data we use.
  3. One shared WebCore for OpenBrowser, OpenWrite and OpenMail's rich
     compose: a `webcore.library` with data per opener (base-relative data
     across WebCore and JavaScriptCore, an experiment), or one WebCore task
     that the programs talk to through message ports. Worth doing once the
     browser works; until then each program carries its own copy.

  Turning off the network and media code saves only a few MB.
- **The filters and C:OWConvert** run on any Amiga: a 68000 and 1 MB.

## 10. What OpenWrite needs from the rest of the Open family

| From | What | State |
| --- | --- | --- |
| OpenBrowser (`openamigabrowser`) | WebCore in a window; the editing calls (Appendix A), undo stack, Amiga clipboard and print to cairo; ICU data as a shared file | WebCore draws pages; the window is being brought up. The editing calls come after its sign-in milestone |
| `openamigacairo` | The PDF surface | Image surfaces only; we turn the PDF surface on (agreed with the browser work) |
| OpenGadTools | Toolbar (0.1 has it), a font list, a ruler gadget, a colour picker | 0.1 in review |
| OpenPrint | PDF jobs in, IPP out | Takes PDF jobs |
| `openamigaimage` | libpng and libjpeg for pictures in documents | Built |
| Datatypes | Pictures in, in every format the user has | In the OS |
| OpenTypes | OpenWrite as the default app for documents | Specified |
| OpenFiles | Previews of documents through libowf | Specified |
| OpenUp | Ships OpenWrite | Specified |

## 11. Phases

**W0, set up (now).** This design, the formats list, the repository, and the
editing calls agreed with the OpenBrowser work.

**W1, the filters and C:OWConvert (starts now; needs no WebCore).**
1. The document model, the zip, XML and IFF readers, character sets.
2. The HTML filter (section 5's shape), plain text and Markdown.
3. ODT in and out; DOCX in and out; RTF in and out.
4. C:OWConvert for OS 3.x and Linux; tests on Linux for every filter, and
   round trips (ODT → ODT, DOCX → ODT → DOCX).

*Exit:* a DOCX written by Word and an ODT written by LibreOffice convert both
ways with their text, styles, lists, tables and pictures, on Linux and on an
Amiga.

**W2, the Amiga formats.**
1. IFF FTXT, AmigaGuide, ANSI text, ProWrite's IFF WORD (documented).
2. The format lab instance; then Final Writer and Final Copy, Wordworth.
3. Then Excellence!, Kindwords, Textcraft, Pen Pal, Protext, BeckerText,
   WordPerfect for the Amiga, and any others we find samples of.

*Exit:* every format in FORMATS.md marked "sample" opens with its text and
basic formatting, and its sample files are in the tests.

**W3, the editor (needs WebCore in OpenBrowser's window).**
1. The window, menus, toolbar, format bar, status bar.
2. The editing calls, open and save through libowf, undo and redo,
   clipboard, find and replace, the Amiga keys.

*Exit:* type a letter, format it, save it as ODT, and open it in LibreOffice
on the PC.

**W4, a word processor.**
1. Styles, lists, tables, pictures, page setup.
2. The page layout view, headers, footers and page numbers, notes, tab stops.
3. Save as PDF and print (OpenPrint, `printer.device`), word count, zoom.

**W5, release.**
1. Spelling (Hunspell and free dictionaries), templates, the ARexx port,
   preferences, the AmigaGuide help.
2. Icons (OS 3.2 style), an Installer script, the Aminet release, OpenUp.
3. Size and start-up time.

## 12. Risks

- **Size and speed.** WebCore on a 68k is large and its JavaScript is slow.
  OpenWrite runs no JavaScript, and editing exercises less of WebCore than
  browsing; we measure typing speed on a long document early in W3.
- **Undocumented formats.** Some Amiga formats may only give up their text.
  The format lab tells us early, and the import report says so honestly.
- **DOCX and ODT are large standards.** We aim at the documents people
  actually write (text, styles, lists, tables, pictures, headers and footers),
  test against real files, and report what was dropped.
- **Licences.** Our code is MIT. A program built with WebCore includes LGPL
  code, so it ships with the source and relinking notes, as OpenBrowser does.
  The fonts are free: Liberation and Carlito under the SIL Open Font
  Licence, Caladea under Apache 2.0, DejaVu under its own free licence.
  Hunspell is used under the MPL.

## 13. Questions for us

1. **The repository:** create `DalsinAI/openamigawrite` (public, like the
   other Open apps, or private until there is code?). This design is ready to
   go in.
2. **The format lab:** which Amiga word processors do we own (Wordworth,
   Final Writer, Final Copy, ProWrite, Excellence!...) to install in a lab
   instance? Old documents from our own disks are welcome too: real documents
   find what made-up ones miss.
3. **Test files from LibreOffice:** installing LibreOffice on this PC lets the
   tests make ODT, DOCX and RTF files in bulk. It is a download, so it waits
   for our yes.

## Appendix A: proposed editing calls for `ob_webview.h`

For the OpenBrowser work to implement in `src/webcore`, beside the calls
`ob_webview.h` has today. Names and shapes are a proposal; the browser work
may change them, and this appendix follows.

```c
/* Editing: the whole document takes typing (designMode). */
void ob_webview_set_editable(OBWebView *view, int editable);

/* Runs one of WebCore's editing commands by name (Editor::Command), at the
 * selection: "Bold", "Italic", "Underline", "StrikeThrough", "Superscript",
 * "Subscript", "FontName", "FontSize", "ForeColor", "BackColor",
 * "JustifyLeft", "JustifyCenter", "JustifyRight", "JustifyFull",
 * "InsertOrderedList", "InsertUnorderedList", "Indent", "Outdent",
 * "FormatBlock" (value "p", "h1"...), "CreateLink", "Unlink", "InsertImage",
 * "InsertHTML", "InsertText", "Delete", "SelectAll", "Undo", "Redo", "Cut",
 * "Copy", "Paste", "PasteAsPlainText". Values are UTF-8. 1 if it ran. */
int ob_webview_command(OBWebView *view, const char *name, const char *value);

/* The command's state at the selection: OB_COMMAND_ENABLED and OB_COMMAND_ON
 * bits (bold is on here), and its value (the font's name) copied into value,
 * at most valueSize bytes. */
enum { OB_COMMAND_ENABLED = 1 << 0, OB_COMMAND_ON = 1 << 1 };
int ob_webview_command_state(OBWebView *view, const char *name, char *value, int valueSize);

/* Paragraph styles: sets the class of every block in the selection
 * (OpenWrite's styles are CSS classes, DESIGN.md section 5), and reads the
 * class of the block at the caret. */
void ob_webview_set_block_class(OBWebView *view, const char *tagName, const char *className);
int ob_webview_block_class(OBWebView *view, char *className, int size);

/* The document out, as UTF-8 HTML with its doctype, and as plain text (for
 * word counts). The caller frees the result with ob_free(). Documents go in
 * with the existing ob_webview_load_html(). */
char *ob_webview_get_html(OBWebView *view);
char *ob_webview_get_text(OBWebView *view);
void ob_free(void *memory);

/* Find, from the selection. 1 when found (and selected). */
enum { OB_FIND_BACKWARDS = 1 << 0, OB_FIND_CASE = 1 << 1, OB_FIND_WRAP = 1 << 2, OB_FIND_WORDS = 1 << 3 };
int ob_webview_find(OBWebView *view, const char *text, int flags);

/* Pages. Sizes in points (1/72 inch). print_begin lays the document out in
 * pages (PrintContext) and returns how many; print_page draws one onto a
 * cairo context (a cairo_t *, so this header needs no cairo), scaled to
 * points, for a PDF surface or a printer bitmap. */
typedef struct {
    double width, height;
    double marginTop, marginRight, marginBottom, marginLeft;
} OBPageSetup;
int ob_webview_print_begin(OBWebView *view, const OBPageSetup *setup);
int ob_webview_print_page(OBWebView *view, int page, void *cairoContext);
void ob_webview_print_end(OBWebView *view);

/* The page layout view: WebCore's paginated mode, pages of this height with
 * a gap between them; 0 turns it off. */
void ob_webview_set_paginated(OBWebView *view, int pageHeight, int gap);
```

New members of `OBWebViewCallbacks` (any may be NULL):

```c
    /* The selection or caret moved: update the toolbar's states. */
    void (*selection_changed)(void *context);
    /* The document changed: it is now unsaved. */
    void (*content_changed)(void *context);
    /* Whether Undo and Redo can run now (for the menu and the toolbar). */
    void (*undo_changed)(void *context, int canUndo, int canRedo);
```
