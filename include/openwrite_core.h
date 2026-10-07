#ifndef OPENWRITE_CORE_H
#define OPENWRITE_CORE_H

#include <stddef.h>
#include "owf.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OW_CORE_VERSION "1.0.1"

typedef struct ow_editor ow_editor;

typedef struct {
    int paragraph;
    int run;
    size_t byte_offset;
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
void ow_editor_mark_saved(ow_editor *editor);

int ow_editor_layout(ow_editor *editor);
int ow_editor_page_count(const ow_editor *editor);
int ow_editor_current_page(const ow_editor *editor);
void ow_editor_page_setup(const ow_editor *editor, owf_page *page);
int ow_editor_apply_page_setup(ow_editor *editor, const owf_page *page);
int ow_editor_insert_page_break(ow_editor *editor);

typedef enum { OW_STORY_HEADER = 1, OW_STORY_FOOTER = 2 } ow_story_kind;
/* Header/footer templates use {PAGE}, {PAGES}, {DATE} and {TIME}. Newlines
 * create separate paragraphs. The returned story text is malloc()'d. */
char *ow_editor_story_text(const ow_editor *editor, ow_story_kind story);
int ow_editor_set_story_text(ow_editor *editor, ow_story_kind story, const char *text);
int ow_editor_insert_field(ow_editor *editor, owf_field field);
int ow_editor_set_link(ow_editor *editor, const char *url);
int ow_editor_insert_link(ow_editor *editor, const char *text, const char *url);
int ow_editor_insert_table(ow_editor *editor, int rows, int cols);
int ow_editor_insert_image(ow_editor *editor, int image_index);
int ow_editor_in_table(const ow_editor *editor);
int ow_editor_table_move(ow_editor *editor, int delta);
int ow_editor_table_insert_row(ow_editor *editor);
int ow_editor_table_delete_row(ow_editor *editor);
int ow_editor_table_insert_column(ow_editor *editor);
int ow_editor_table_delete_column(ow_editor *editor);
int ow_editor_render_page(const ow_editor *editor, int page_index,
                          const ow_renderer *renderer);

#ifdef __cplusplus
}
#endif

#endif
