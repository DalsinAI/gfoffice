/* GFOpenPressmaster: Insert > Page Number, a page number field in a text box (include/openpressmaster_pagenum.h). */
#include "openpressmaster_pagenum.h"
#include "openpressmaster_merge.h"

#include <string.h>

int opp_page_number_box(opr_undo *u, owf_deck *deck, int slide, owf_field which, int *selected)
{
    opr_textbox_spec spec;
    int obj = -1, rc;
    if (!u || !deck || slide < 0 || slide >= deck->nslides || (which != OWF_FIELD_PAGE && which != OWF_FIELD_PAGES))
        return -1;
    memset(&spec, 0, sizeof spec);
    spec.width = deck->width / 6 > 1440 ? deck->width / 6 : 1440;    /* a sixth of the page, an inch at least */
    spec.height = 480;                                               /* a third of an inch */
    if (spec.width > deck->width || spec.height * 2 > deck->height)
        return -2;
    spec.x = (deck->width - spec.width) / 2;
    spec.y = deck->height - spec.height - (deck->height / 20 > 240 ? deck->height / 20 : 240);
    if (spec.y < 0) spec.y = 0;
    spec.inset[0] = spec.inset[2] = 72;
    spec.inset[1] = spec.inset[3] = 36;
    spec.valign = OWF_VALIGN_CENTRE;
    if (opr_undo_group_begin(u, 3) < 0)                             /* the box, its field, its alignment */
        return -1;
    rc = opr_obj_insert_textbox(u, deck, slide, &spec, &obj);
    if (rc == 0 && opr_undo_push(u, deck, slide, obj) < 0) rc = -1;
    if (rc == 0 && opp_insert_page_field_at(&deck->slides[slide].objs[obj], 0, 0, which) != OWF_OK) rc = -1;
    if (rc == 0 && opr_text_set_alignment(u, deck, slide, obj, 0, 0, OWF_ALIGN_CENTRE) < 0) rc = -1;
    if (rc < 0) {
        opr_undo_group_cancel(u, deck);
        return rc;
    }
    opr_undo_group_end(u);
    if (selected) *selected = obj;
    return 0;
}
