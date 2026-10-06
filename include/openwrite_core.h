#ifndef OPENWRITE_CORE_H
#define OPENWRITE_CORE_H

#include <stddef.h>
#include "owf.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OW_CORE_VERSION "0.2"

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

/* Native editing. All positions are UTF-8 byte offsets inside text runs.
 * Consecutive typing is coalesced into one undo step. */
int ow_editor_insert_utf8(ow_editor *editor, const char *utf8, size_t length);
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
int ow_editor_render_page(const ow_editor *editor, int page_index,
                          const ow_renderer *renderer);

#ifdef __cplusplus
}
#endif

#endif
