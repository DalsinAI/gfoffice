# GFOpenLayout capabilities in GFOpenWrite

GFOpenWrite keeps document semantics in `libowf`, editing transactions in
`openwrite_core`, and all geometry in GFOpenLayout. Writer must not grow a
second pagination or object-placement engine.

## Used now

GFOpenWrite currently builds one GFOpenLayout tree from an `owf_doc` and uses
its display list for rendering, paging, caret placement and pointer hit tests.
The integration uses:

- block and inline flow, word wrapping and explicit page breaks;
- page cutting, keep-with-next and headers/footers;
- grid tables;
- inline pictures;
- anchored pictures and text boxes, including page/margin/paragraph anchors,
  front/behind placement, square or top-and-bottom wrapping and frame borders;
- editing and hit testing inside text-box stories;
- aligned tab stops and leaders;
- uniform newspaper-column pagination, physical-page display lists, caret
  placement and pointer hit testing.

GFOpenWrite 2.0.25 exposes capabilities that previously worked only for
imported documents: `ow_editor_insert_text_box()` and
`ow_editor_insert_floating_image()`. They create native `owf_frame` objects;
GFOpenLayout places them and reflows body text around them. The Amiga window's
Insert menu is only a caller of those portable operations. The same release
adds `ol_layout_pages_columns()` to GFOpenLayout and an undoable
`ow_editor_apply_document_columns()` command; the Layout menu remains a thin
caller of the reusable engine and editor APIs.

## Extensions worth making in GFOpenLayout

These are layout services shared by Writer, Presentation, Publisher and the
browser. They belong in GFOpenLayout rather than in an application.

### 1. Object hit testing and manipulation geometry

Add an object-level hit result beside text hit testing:

- stable frame/object ID, page and z-order;
- body rectangle plus eight resize handles and a move handle;
- anchor marker and the anchor paragraph's stable ID;
- conversion from a dragged page rectangle back to anchor-relative geometry.

This lets Writer, Presentation and Publisher share selection, move, resize and
anchor feedback while each keeps its own document transaction and undo model.

### 2. Node-to-page and generated-reference results

`ol_node_page_box()` now exposes the physical page and first rectangle for a
stable node ID, and `ow_layout_paragraph_box()` maps that service to Writer's
body paragraphs. It deliberately ignores separately anchored descendants when
locating their paragraph. The next layer can build TOCs, indexes, captions and
cross-references from headings/bookmarks without inspecting the display list or
duplicating pagination; page-number style belongs in that generated-reference
layer.

### 3. Section regions and newspaper columns

Uniform newspaper columns are now implemented in GFOpenLayout, including
physical-page display ranges, frames, caret placement and pointer hit testing,
and consumed by Writer. Its pagination result now records each flow region's
physical page, x-position, width, galley span and vertical shift independently;
uniform columns no longer bake a fixed count into caret, hit-test or frame
placement. `ol_layout_pages_sections()` now switches column count and gap at
top-level flow boundaries, maps new/odd/even-page sections, and places a
continuous section in the rectangular band below its predecessor. It balances
the predecessor's partially occupied final page when that section has multiple
columns, while respecting explicit breaks and keep spans. Writer uses that path
for native and imported sections, including the two-to-three-column DOCX
fixture. Page size, margins and header/footer changes are the next extension of
the same section contract into Writer page styles. The service is also directly
useful to Publisher.

### 4. Footnote and endnote flow

Add deferred page-bottom regions whose references are inline but whose stories
are laid out at the foot of the page. The engine should move a reference and
its note together when possible and continue long notes safely. Endnotes can
reuse the same deferred-story interface with a document- or section-end target.

### 5. Richer tables

GFOpenLayout already supports column spans, vertical alignment and repeated
header rows. Extend its page contract with row splitting, row spans and border
conflict resolution. GFOpenWrite then only needs to add the corresponding
semantics and editor commands to `libowf`/`openwrite_core`.

### 6. Text services hooks

Keep platform text libraries outside the engine but add deterministic hooks
for hyphenation opportunities, bidirectional runs and glyph shaping. Layout
continues to own line breaking and caret geometry; Amiga bitmap fonts and
future outline-font backends supply measurements and shaped clusters.

### 7. Incremental reflow

Accept a dirty semantic node/range and reuse geometry before the earliest
affected block. This matters after the structural features above: a changed
footnote, wrapped frame or section can invalidate later pages, while ordinary
typing should not reconstruct an entire long document.

## Integration order

1. Object manipulation geometry, then Writer move/resize/wrap controls.
2. Node-to-page results (landed), then TOC/caption/cross-reference generation.
3. Section regions and columns, then Writer page styles.
4. Footnote regions, then native notes in `libowf` and the editor.
5. Table and text-service improvements.
6. Incremental reflow once the result contracts above are stable.

