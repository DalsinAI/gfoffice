#ifndef OPENWRITE_CORE_H
#define OPENWRITE_CORE_H

#include <stddef.h>
#include "owf.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OW_CORE_VERSION "1.8.0"

typedef struct ow_editor ow_editor;

typedef struct {
    int paragraph;
    int run;
    size_t byte_offset;
    /* 2.0.15: the story the position is in: 0 the body, k the text box
     * doc->frames[k - 1]'s paragraphs. A selection's two ends are in one. */
    int box;
} ow_position;

typedef struct {
    ow_position anchor;
    ow_position focus;
} ow_selection;

typedef enum {
    OW_VIEW_DRAFT = 0,
    OW_VIEW_PAGE = 1
} ow_view_mode;

typedef enum {
    OW_MOVE_LEFT = 0,
    OW_MOVE_RIGHT,
    OW_MOVE_UP,
    OW_MOVE_DOWN,
    OW_MOVE_HOME,
    OW_MOVE_END
} ow_move;

typedef struct {
    int width_twips;
    int height_twips;
    int page_index;
} ow_page_info;

typedef struct {
    void *userdata;
    void (*begin_page)(void *userdata, const ow_page_info *page);
    void (*end_page)(void *userdata, const ow_page_info *page);
    /* Legacy/simple callback. */
    void (*text)(void *userdata, int x, int y, const char *utf8,
                 const owf_charfmt *fmt);
    /* Preferred callback: identifies the run so a view can draw selection,
     * the caret and hit-test without duplicating the document walk. */
    void (*text_run)(void *userdata, int paragraph, int run,
                     int x, int y, const char *utf8,
                     const owf_charfmt *fmt);
    void (*rule)(void *userdata, int x1, int y1, int x2, int y2,
                 unsigned long rgb);
    void (*image)(void *userdata, int image_index, int x, int y, int width, int height);
} ow_renderer;

ow_editor *ow_editor_new(owf_doc *doc);
void ow_editor_free(ow_editor *editor);

owf_doc *ow_editor_document(ow_editor *editor);
const owf_doc *ow_editor_document_const(const ow_editor *editor);

void ow_editor_set_view(ow_editor *editor, ow_view_mode mode);
ow_view_mode ow_editor_view(const ow_editor *editor);

void ow_editor_set_zoom(ow_editor *editor, int percent);
int ow_editor_zoom(const ow_editor *editor);

void ow_editor_set_selection(ow_editor *editor, const ow_selection *selection);
ow_selection ow_editor_selection(const ow_editor *editor);
int ow_editor_selection_empty(const ow_editor *editor);
void ow_editor_select_all(ow_editor *editor);

/* A malloc() string containing the selected plain text, including tabs and
 * paragraph separators. The caller frees it. NULL means no selection or OOM. */
char *ow_editor_selection_text(const ow_editor *editor, size_t *length);

/* Character formatting. A zero-length selection changes the typing format;
 * a non-empty selection reformats exactly the selected text and is undoable. */
enum {
    OW_CHARFMT_FLAGS  = 1 << 0,
    OW_CHARFMT_FONT   = 1 << 1,
    OW_CHARFMT_SIZE   = 1 << 2,
    OW_CHARFMT_COLOUR = 1 << 3
};
int ow_editor_current_charfmt(const ow_editor *editor, owf_charfmt *fmt);
int ow_editor_apply_charfmt(ow_editor *editor, const owf_charfmt *fmt,
                            unsigned mask);
int ow_editor_toggle_char_flags(ow_editor *editor, unsigned flags);

/* Paragraph formatting applies to every paragraph touched by the selection,
 * or the caret paragraph when the selection is empty. */
enum {
    OW_PARAFMT_HEADING = 1 << 0,
    OW_PARAFMT_ALIGN   = 1 << 1,
    OW_PARAFMT_INDENTS = 1 << 2,
    OW_PARAFMT_SPACING = 1 << 3
};
int ow_editor_current_parafmt(const ow_editor *editor, owf_parafmt *fmt);
int ow_editor_apply_parafmt(ow_editor *editor, const owf_parafmt *fmt,
                            unsigned mask);

/* Text-compatible lists: ordered=0 bullet, ordered=1 numbered. Calling the
 * same mode again removes list prefixes from the touched paragraphs. */
int ow_editor_toggle_list(ow_editor *editor, int ordered);

/* Plain-text search. Returns 1 when selected, 0 when no match, -1 on OOM.
 * Search stays within paragraph text but wraps across paragraphs. */
int ow_editor_find(ow_editor *editor, const char *needle, int backwards,
                   int match_case, int wrap);

/* Native editing. All positions are UTF-8 byte offsets inside text runs.
 * Consecutive typing is coalesced into one undo step. */
int ow_editor_insert_utf8(ow_editor *editor, const char *utf8, size_t length);
/* Inserts/pastes a text block as one undo transaction; CR/LF create paragraphs. */
int ow_editor_insert_text_block(ow_editor *editor, const char *utf8, size_t length);
int ow_editor_backspace(ow_editor *editor);
int ow_editor_delete_forward(ow_editor *editor);
int ow_editor_newline(ow_editor *editor);
int ow_editor_move_caret(ow_editor *editor, ow_move move, int extend_selection);

/* Undo is intentionally bounded: enough for an Amiga editor without an
 * unbounded memory bill. */
int ow_editor_can_undo(const ow_editor *editor);
int ow_editor_can_redo(const ow_editor *editor);
int ow_editor_undo(ow_editor *editor);
int ow_editor_redo(ow_editor *editor);
void ow_editor_break_edit_group(ow_editor *editor);

int ow_editor_is_dirty(const ow_editor *editor);
/* Counts every change to the document (an edit, undo, redo, page setup):
 * a view that keeps its own layout of the document lays it out again when
 * this moves. */
unsigned long ow_editor_changes(const ow_editor *editor);
/* 2.0.12: about the bytes the document's paragraphs, runs and frames hold (its pictures not counted), and those
 * the undo and redo snapshots hold (*nundo, *nredo of them; either may be NULL). For MEMCHECK. */
unsigned long ow_editor_document_bytes(const ow_editor *editor);
unsigned long ow_editor_snapshot_bytes(const ow_editor *editor, int *nundo, int *nredo);
void ow_editor_mark_saved(ow_editor *editor);

int ow_editor_layout(ow_editor *editor);
int ow_editor_page_count(const ow_editor *editor);
int ow_editor_current_page(const ow_editor *editor);
void ow_editor_page_setup(const ow_editor *editor, owf_page *page);
int ow_editor_apply_page_setup(ow_editor *editor, const owf_page *page);
/* The effective body section at the caret, and an undoable section start at
 * the current paragraph. columns 1-16; gap is twips (0: default). */
int ow_editor_current_section(const ow_editor *editor, int *start_paragraph,
                              int *columns, int *gap, owf_section_break *break_kind);
int ow_editor_apply_section(ow_editor *editor, int columns, int gap,
                            owf_section_break break_kind);
/* Apply one uniform column geometry to the whole body, removing explicit
 * later section column markers. Undo restores the original sections. */
int ow_editor_apply_document_columns(ow_editor *editor, int columns, int gap);
int ow_editor_insert_page_break(ow_editor *editor);

/* Build an undoable table of contents from the document's heading paragraphs.
 * The resolver receives the document after the TOC placeholders have been
 * inserted and returns each heading's zero-based physical page. This keeps
 * pagination in GFOpenLayout (or another caller-owned layout service) while
 * the editor owns document semantics, bookmarks, links and undo. max_level is
 * 1-6. OWF_ERR_UNSUPPORTED means that the document has no matching headings. */
typedef int (*ow_toc_pages_fn)(void *userdata, const owf_doc *doc,
                               const int *heading_paragraphs, int count,
                               int *pages);
int ow_editor_insert_table_of_contents(ow_editor *editor, int max_level,
                                       ow_toc_pages_fn resolve_pages,
                                       void *userdata);
/* Core 1.7: an alphabetical index. An entry is a mark at the caret (after a selection, which stays) that shows
 * nothing: "Lamps", or "Lamps:LED" for a subentry. ow_editor_insert_index builds, at the caret as the contents
 * are, a title and a line for each entry and subentry in alphabetical order with its marks' pages ("Lamps, 2,
 * 5"), undone as one edit; the resolver gets each mark's paragraph. OWF_ERR_UNSUPPORTED: no entries. */
int ow_editor_insert_index_entry(ow_editor *editor, const char *entry);
int ow_editor_index_entry_count(const ow_editor *editor);
int ow_editor_insert_index(ow_editor *editor, ow_toc_pages_fn resolve_pages, void *userdata);

/* 2.7.0 (core 1.8): AutoCorrect. The program keeps one ow_autocorrect (a list of replacements and the options) and
 * passes it with what is typed: ow_editor_type corrects the word before the caret when a space or a tab is typed,
 * and makes typed quotes curly; ow_editor_autocorrect_word does the word before Return. Undo gives back the word
 * as typed. ow_autocorrect_save/load: "options n", then "from<TAB>to" a line each (UTF-8). */
typedef struct ow_autocorrect ow_autocorrect;
#define OW_AC_REPLACE   1u      /* from the list ("teh" -> "the") */
#define OW_AC_SENTENCE  2u      /* a sentence's first letter a capital */
#define OW_AC_TWO_CAPS  4u      /* "THe" -> "The" */
#define OW_AC_QUOTES    8u      /* curly quotes */
#define OW_AC_DASHES   16u      /* "a - b" an en dash, "a--b" an em dash */
#define OW_AC_ALL      31u
ow_autocorrect *ow_autocorrect_new(int with_defaults);
void ow_autocorrect_free(ow_autocorrect *ac);
unsigned ow_autocorrect_options(const ow_autocorrect *ac);
void ow_autocorrect_set_options(ow_autocorrect *ac, unsigned options);
int ow_autocorrect_add(ow_autocorrect *ac, const char *from, const char *to);
int ow_autocorrect_remove(ow_autocorrect *ac, const char *from);
int ow_autocorrect_count(const ow_autocorrect *ac);
const char *ow_autocorrect_from(const ow_autocorrect *ac, int i);
const char *ow_autocorrect_to(const ow_autocorrect *ac, int i);
const char *ow_autocorrect_lookup(const ow_autocorrect *ac, const char *from);
int ow_autocorrect_load(ow_autocorrect *ac, const char *text, size_t length);
char *ow_autocorrect_save(const ow_autocorrect *ac, size_t *length);   /* owf_free it */
int ow_editor_type(ow_editor *editor, const ow_autocorrect *ac, const char *utf8, size_t length);
int ow_editor_autocorrect_word(ow_editor *editor, const ow_autocorrect *ac);

/* 2.0.23: the left pages' (the even-numbered) own, doc->header_left and footer_left */
typedef enum { OW_STORY_HEADER = 1, OW_STORY_FOOTER = 2, OW_STORY_HEADER_LEFT = 3, OW_STORY_FOOTER_LEFT = 4 } ow_story_kind;
/* Header/footer templates use {PAGE}, {PAGES}, {DATE} and {TIME}. Newlines
 * create separate paragraphs. The returned story text is malloc()'d. Setting
 * a left pages' one to "" removes it: left pages have the header or footer. */
char *ow_editor_story_text(const ow_editor *editor, ow_story_kind story);
int ow_editor_set_story_text(ow_editor *editor, ow_story_kind story, const char *text);
int ow_editor_insert_field(ow_editor *editor, owf_field field);
int ow_editor_set_link(ow_editor *editor, const char *url);
int ow_editor_insert_link(ow_editor *editor, const char *text, const char *url);
int ow_editor_insert_table(ow_editor *editor, int rows, int cols);
int ow_editor_insert_image(ow_editor *editor, int image_index);
/* Anchored objects use the document's native frames and GFOpenLayout's page
 * anchoring, wrapping and text-box hit testing. Text boxes are inserted at the
 * current body paragraph and leave the caret in their editable story. Floating
 * images stay anchored to the current paragraph and leave the body caret where
 * it was. Nested frames inside a text box are deliberately not created. */
int ow_editor_insert_text_box(ow_editor *editor, const char *utf8,
                              int width, int height, owf_frame_wrap wrap);
int ow_editor_insert_floating_image(ow_editor *editor, int image_index,
                                    owf_frame_wrap wrap);
/* Front ends use the same handle geometry for live previews and committed
 * edits. Coordinates may be pixels, twips or layout units as long as every
 * argument uses the same unit. */
typedef enum {
    OW_FRAME_MOVE = 1, OW_FRAME_RESIZE_NW, OW_FRAME_RESIZE_NE,
    OW_FRAME_RESIZE_SW, OW_FRAME_RESIZE_SE
} ow_frame_handle;
void ow_frame_adjust_rect(int *x, int *y, int *width, int *height,
                          ow_frame_handle handle, int dx, int dy,
                          int min_width, int min_height);
/* Move/resize an existing frame to a rectangle measured from the page in
 * twips. It remains attached to its anchor paragraph's page and is undoable. */
int ow_editor_apply_frame_geometry(ow_editor *editor, int frame_index,
                                   int x, int y, int width, int height,
                                   owf_frame_wrap wrap);
/* Remove a frame and its anchor run, renumbering later frame references. */
int ow_editor_delete_frame(ow_editor *editor, int frame_index);
int ow_editor_in_table(const ow_editor *editor);
typedef enum { OW_TABLE_CELL, OW_TABLE_ROW, OW_TABLE_ALL } ow_table_scope;
/* Cell fill plus paragraph borders for the current cell, row or table.
 * Colours use OWF_SHADE; zero clears the fill or selects automatic black. */
int ow_editor_table_apply_style(ow_editor *editor, ow_table_scope scope,
                                unsigned long shading, int borders,
                                unsigned long border_colour);
int ow_editor_table_move(ow_editor *editor, int delta);
int ow_editor_table_insert_row(ow_editor *editor);
int ow_editor_table_delete_row(ow_editor *editor);
int ow_editor_table_insert_column(ow_editor *editor);
int ow_editor_table_delete_column(ow_editor *editor);
int ow_editor_render_page(const ow_editor *editor, int page_index,
                          const ow_renderer *renderer);

/* ---- Tracked changes (core 1.2) ----
 * Recording on, typed and pasted text and new paragraphs are the review author's insertions, and Backspace,
 * Delete, Cut and typing over a selection mark text deleted instead of removing it: it stays, struck through,
 * until the change is accepted or rejected. Deleting the author's own insertion removes it. The document
 * keeps the setting (owf_doc.track_changes). Accepting or rejecting is undoable.
 *
 * A change, as these calls count them, is a row of text and paragraph ends in the caret's story that name
 * the same owf_revision. */
void ow_editor_set_track_changes(ow_editor *editor, int on);
int ow_editor_track_changes(const ow_editor *editor);
/* The name new changes are made under; NULL or "" for none (an unknown author). */
int ow_editor_set_review_author(ow_editor *editor, const char *utf8);
const char *ow_editor_review_author(const ow_editor *editor);

typedef struct {
    int index;               /* 0, 1... in the story */
    int kind;                /* OWF_REV_INSERT or OWF_REV_DELETE */
    const char *author;      /* the document's; NULL: unknown */
    const char *date;        /* ISO 8601; NULL: unknown */
    ow_position start, end;  /* where it is, as a selection */
} ow_change_info;
int ow_editor_change_count(const ow_editor *editor);
/* 1 and *info for the index-th change, 0 when there is none. The strings are valid until the next edit. */
int ow_editor_change_info(ow_editor *editor, int index, ow_change_info *info);
/* The change at the caret: 1 and *info, or 0. */
int ow_editor_current_change(ow_editor *editor, ow_change_info *info);
/* Selects the next change after the selection (or the one before it): 1, 2 when it went round the story's
 * end to find it, 0 when there are none. */
int ow_editor_next_change(ow_editor *editor, int backwards);
/* Accepts (accept 1) or rejects (0) the changes the selection touches, or the one at the caret. Accepting an
 * insertion keeps its text, a deletion removes it; rejecting does the opposite. OWF_ERR_UNSUPPORTED when
 * there is none there. */
int ow_editor_accept_change(ow_editor *editor, int accept);
/* Every change in the document (body, headers, footers, text boxes). OWF_ERR_UNSUPPORTED when it has none. */
int ow_editor_accept_all_changes(ow_editor *editor, int accept);

/* ---- Comments (core 1.3) ----
 * Notes in the margin, the document's (owf.h's owf_annotation): on a point, or on the text selected when it
 * was made. A reply answers a comment and is where it is. Comments are listed in the order of their places in
 * the caret's story; index counts them so. All of these are undoable. */
typedef struct {
    int index;               /* in the story's order, from 0 */
    const char *author;      /* NULL: unknown */
    const char *initials;    /* NULL: none */
    const char *date;        /* ISO 8601; NULL: unknown */
    const char *text;        /* UTF-8, its paragraphs between '\n' */
    int reply_to;            /* the index of the comment it answers; -1: none */
    int done;                /* marked done */
    ow_position start, end;  /* its text (start == end's place: a point) */
} ow_comment_info;
int ow_editor_comment_count(const ow_editor *editor);
/* 1 and *info for the index-th comment, 0 when there is none. The strings are valid until the next edit. */
int ow_editor_comment_info(ow_editor *editor, int index, ow_comment_info *info);
/* The comment at the caret (its text holds the caret, or it is where the caret is): 1 and *info, or 0. */
int ow_editor_current_comment(ow_editor *editor, ow_comment_info *info);
/* Selects the next comment's text (or the one before): 1, 2 when it went round the story's end, 0: none.
 * Replies are passed over (they are where their comment is). */
int ow_editor_next_comment(ow_editor *editor, int backwards);
/* A comment of the review author's on the selection (or at the caret), made now; the caret after it. */
int ow_editor_insert_comment(ow_editor *editor, const char *utf8);
/* The review author's reply to the comment at the caret. OWF_ERR_UNSUPPORTED: no comment there. */
int ow_editor_reply_comment(ow_editor *editor, const char *utf8);
/* The comment at the caret: its text changed, marked done or not (with its replies), or deleted (with its
 * replies). OWF_ERR_UNSUPPORTED: no comment there. */
int ow_editor_set_comment_text(ow_editor *editor, const char *utf8);
int ow_editor_resolve_comment(ow_editor *editor, int done);
int ow_editor_delete_comment(ow_editor *editor);

/* ---- Footnotes and endnotes (core 1.5) ----
 * Notes the document's (owf.h's owf_note): a mark in the text, numbered (footnotes 1, 2; endnotes i, ii, in
 * the order of the marks), and its text, at the foot of the mark's page or after the text. Notes are listed in
 * the order of their marks in the caret's story; index counts them so. All of these are undoable. */
typedef struct {
    int index;               /* in the story's order, from 0 */
    int endnote;             /* 0: a footnote, 1: an endnote */
    const char *text;        /* UTF-8, its paragraphs between '\n' */
    const char *label;       /* its own mark; NULL: numbered */
    char mark[16];           /* what its mark shows */
    ow_position place;       /* its mark */
} ow_note_info;
int ow_editor_note_count(const ow_editor *editor);
/* 1 and *info for the index-th note, 0 when there is none. The strings are valid until the next edit. */
int ow_editor_note_info(ow_editor *editor, int index, ow_note_info *info);
/* The note whose mark is at the caret (just before or after it): 1 and *info, or 0. */
int ow_editor_current_note(ow_editor *editor, ow_note_info *info);
/* The caret to the next note's mark (or the one before): 1, 2 when it went round the story's end, 0: none. */
int ow_editor_next_note(ow_editor *editor, int backwards);
/* A footnote (endnote 0) or endnote with this text, its mark at the caret (the selection's end). */
int ow_editor_insert_note(ow_editor *editor, int endnote, const char *utf8);
/* The note at the caret: its text changed, made a footnote from an endnote or the other way round, or
 * deleted (its mark and its text). OWF_ERR_UNSUPPORTED: no note there. */
int ow_editor_set_note_text(ow_editor *editor, const char *utf8);
int ow_editor_convert_note(ow_editor *editor);
int ow_editor_delete_note(ow_editor *editor);

/* ---- Fields with a value (core 1.4) ----
 * The document's title, author and subject, the file's name, its words, the chapter (the level-1 heading a
 * paragraph is under), a user field, and a reference to a body paragraph (its text or its page) through a
 * bookmark. A field shows the value worked out when it was inserted or last updated: on inserting one, when
 * document information or a user field changes, and on ow_editor_update_fields (after opening, before saving
 * and printing). ow_editor_insert_field takes the kinds without a name; USER, REF and REF_PAGE need one. */
int ow_editor_insert_field_arg(ow_editor *editor, owf_field field, const char *name);
/* Fields worked out again and laid out: how many changed (0: none), or an OWF_ERR_ */
int ow_editor_update_fields(ow_editor *editor);
/* The document's file (for FILENAME; NULL: untitled) */
void ow_editor_set_file_name(ow_editor *editor, const char *path);
/* Where a body paragraph is: its page from 0, for page references (NULL: the editor's own pagination) */
void ow_editor_set_page_lookup(ow_editor *editor, int (*page_of)(void *userdata, int paragraph), void *userdata);
typedef enum { OW_INFO_TITLE, OW_INFO_AUTHOR, OW_INFO_SUBJECT } ow_doc_info;
/* Document information, UTF-8 (NULL: none). Setting it is not undone (it is the file's, not the text's). */
const char *ow_editor_doc_info(const ow_editor *editor, ow_doc_info which);
int ow_editor_set_doc_info(ow_editor *editor, ow_doc_info which, const char *utf8);
/* User fields: name and value; set_user_field with value NULL removes it (OWF_ERR_UNSUPPORTED: none) */
int ow_editor_user_field_count(const ow_editor *editor);
int ow_editor_user_field_at(const ow_editor *editor, int index, const char **name, const char **value);
int ow_editor_set_user_field(ow_editor *editor, const char *name, const char *value);
/* A reference to body paragraph paragraph at the caret: its text, or (page 1) its page; the paragraph gets a
 * bookmark ("_Ref1" ...) when it has none */
int ow_editor_insert_cross_reference(ow_editor *editor, int paragraph, int page);

/* ---- Mail merge (core 1.6) ----
 * A data source is a CSV file (comma, semicolon or tab separated; quoted cells; UTF-8 or Latin-1) whose first
 * row names the columns. A merge field shows its column's name in guillemets, or a record's value while that
 * record is shown (ow_editor_merge_show). The document keeps the source's path (owf_doc.merge_source) and reads
 * it again when it is asked for. A merge makes each record's document, with the fields' values as plain text,
 * tracked changes accepted and comments left out. */
/* Read a data source (path NULL or "": none, and the document forgets it): OWF_OK, OWF_ERR_IO (no such file),
 * OWF_ERR_FORMAT (no columns), OWF_ERR_UNSUPPORTED (over 4 MB) or OWF_ERR_MEMORY */
int ow_editor_merge_load(ow_editor *editor, const char *path);
/* The same from bytes in memory; name (when given) becomes the document's data source */
int ow_editor_merge_load_text(ow_editor *editor, const char *name, const char *data, size_t length);
/* Forget the table read (the document keeps its data source) */
void ow_editor_merge_close(ow_editor *editor);
/* The document's data source (NULL: none) */
const char *ow_editor_merge_source(const ow_editor *editor);
/* The columns and records (0: no data source, or it can't be read) */
int ow_editor_merge_columns(ow_editor *editor);
const char *ow_editor_merge_column(ow_editor *editor, int index);
int ow_editor_merge_records(ow_editor *editor);
/* A record's value in a column (record from 0; column by name, then without minding case), NULL: none */
const char *ow_editor_merge_value(const ow_editor *editor, int record, const char *column);
/* The record the fields show (-1: the columns' names); show one (-1: the names) and lay out again */
int ow_editor_merge_shown(const ow_editor *editor);
int ow_editor_merge_show(ow_editor *editor, int record);
/* A merge field for column at the caret (undoable) */
int ow_editor_insert_merge_field(ow_editor *editor, const char *column);
/* Records first to last (from 0; last -1: to the end) merged into one new document, each from a new page
 * (*out is the caller's: owf_doc_free); OWF_ERR_UNSUPPORTED: no records */
int ow_editor_merge_document(ow_editor *editor, int first, int last, owf_doc **out);
/* Records first to last each saved as its own file, path with the record's number (from 1) before its
 * extension (RAM:letter.odt: RAM:letter1.odt ...), in the format the extension names; *written: how many */
int ow_editor_merge_to_files(ow_editor *editor, const char *path, int first, int last, int *written);

#ifdef __cplusplus
}
#endif

#endif
