/* GFOpenPublisher's built-in publication templates. MIT, Copyright (c) 2026 AmiCradle Team. */
#include "openpublisher_template.h"
#include <string.h>
#include <strings.h>

#define MM(v) ((int)((v) * 1440L / 254L))            /* tenths of a millimetre to twips */
#define PT(v) ((v) * 20)                             /* points to twips */
#define INK 0x203040UL
#define ACCENT 0x2F6FB0UL
#define PALE 0xE4EEF8UL

/* A frame of text in a page: lines split at '\n' into paragraphs */
static owf_obj *frame(owf_slide *s, int x, int y, int w, int h, const char *text, int size, unsigned flags,
                      owf_align align, unsigned long colour)
{
    owf_obj *o = owf_slide_add(s, OWF_OBJ_TEXT);
    owf_parafmt pf;
    owf_charfmt cf;
    const char *p, *nl;
    if (!o) return NULL;
    o->x = x; o->y = y; o->width = w; o->height = h;
    owf_parafmt_init(&pf);
    pf.align = align;
    owf_charfmt_init(&cf);
    cf.size = size;
    cf.flags = flags;
    cf.colour = colour;
    for (p = text;; p = nl + 1) {
        owf_para *para = owf_story_add(&o->text, &pf);
        size_t n;
        if (!para) return NULL;
        nl = strchr(p, '\n');
        n = nl ? (size_t)(nl - p) : strlen(p);
        if (n && owf_para_add_text(para, &cf, p, n) != OWF_OK) return NULL;
        if (!nl) break;
    }
    return o;
}

static owf_obj *block(owf_slide *s, int x, int y, int w, int h, unsigned long fill)
{
    owf_obj *o = owf_slide_add(s, OWF_OBJ_SHAPE);
    if (!o) return NULL;
    o->x = x; o->y = y; o->width = w; o->height = h;
    o->shape = OWF_SHAPE_RECT;
    o->look.fill = OWF_SHADE(fill);
    return o;
}

#define TRY(e) do { if (!(e)) return 0; } while (0)

static int flyer(owf_deck *d)
{
    owf_slide *s = owf_deck_add_slide(d, -1, -1);
    TRY(s);
    TRY(block(s, 0, 0, d->width, MM(700), ACCENT));
    TRY(frame(s, MM(150), MM(150), d->width - MM(300), MM(450), "Event Title", PT(40), OWF_BOLD, OWF_ALIGN_CENTRE, 0xFFFFFFUL));
    TRY(frame(s, MM(200), MM(900), d->width - MM(400), MM(1200),
              "Say what is happening, when and where.\nAdd a picture above or beside this text.",
              PT(16), 0, OWF_ALIGN_CENTRE, INK));
    TRY(block(s, MM(200), d->height - MM(500), d->width - MM(400), MM(300), PALE));
    TRY(frame(s, MM(250), d->height - MM(470), d->width - MM(500), MM(240), "Date | Place | Contact", PT(14), OWF_BOLD,
              OWF_ALIGN_CENTRE, INK));
    return 1;
}

static int postcard(owf_deck *d)
{
    owf_slide *s = owf_deck_add_slide(d, -1, -1);
    TRY(s);
    TRY(block(s, 0, 0, d->width, d->height, PALE));
    TRY(frame(s, MM(100), MM(100), d->width - MM(200), MM(250), "Greetings from", PT(24), OWF_BOLD, OWF_ALIGN_LEFT, ACCENT));
    TRY((s = owf_deck_add_slide(d, -1, -1)) != NULL);    /* the back: message left, address right */
    TRY(block(s, d->width / 2, MM(100), MM(5), d->height - MM(200), INK));
    TRY(frame(s, MM(80), MM(100), d->width / 2 - MM(160), d->height - MM(200), "Your message", PT(11), 0, OWF_ALIGN_LEFT, INK));
    TRY(frame(s, d->width / 2 + MM(80), MM(400), d->width / 2 - MM(160), MM(400), "Name\nStreet\nTown\nPostcode", PT(11), 0,
              OWF_ALIGN_LEFT, INK));
    return 1;
}

static int business_card(owf_deck *d)
{
    owf_slide *s = owf_deck_add_slide(d, -1, -1);
    TRY(s);
    TRY(block(s, 0, 0, MM(60), d->height, ACCENT));
    TRY(frame(s, MM(120), MM(80), d->width - MM(180), MM(120), "Your Name", PT(12), OWF_BOLD, OWF_ALIGN_LEFT, INK));
    TRY(frame(s, MM(120), MM(190), d->width - MM(180), MM(80), "Job Title", PT(8), OWF_ITALIC, OWF_ALIGN_LEFT, ACCENT));
    TRY(frame(s, MM(120), MM(320), d->width - MM(180), MM(180), "Phone\nE-mail\nWeb", PT(7), 0, OWF_ALIGN_LEFT, INK));
    return 1;
}

static int folded_card(owf_deck *d)
{
    static const char *const text[4] = { "Front", "Inside left", "Inside right", "Back" };
    int i;
    for (i = 0; i < 4; ++i) {
        owf_slide *s = owf_deck_add_slide(d, -1, -1);
        TRY(s);
        if (i == 0) TRY(block(s, MM(100), MM(100), d->width - MM(200), d->height - MM(200), PALE));
        TRY(frame(s, MM(150), d->height / 2 - MM(150), d->width - MM(300), MM(300), text[i], i == 0 ? PT(28) : PT(14),
                  i == 0 ? OWF_BOLD : 0, OWF_ALIGN_CENTRE, i == 0 ? ACCENT : INK));
    }
    return 1;
}

static int newsletter(owf_deck *d)
{
    int p, col = (d->width - MM(400) - MM(60)) / 2;
    for (p = 0; p < 2; ++p) {
        owf_slide *s = owf_deck_add_slide(d, -1, -1);
        int top = p == 0 ? MM(600) : MM(200);
        owf_obj *o;
        TRY(s);
        if (p == 0) {
            TRY(block(s, 0, 0, d->width, MM(450), ACCENT));
            TRY(frame(s, MM(200), MM(100), d->width - MM(400), MM(250), "Newsletter Name", PT(32), OWF_BOLD, OWF_ALIGN_LEFT,
                      0xFFFFFFUL));
        }
        TRY((o = frame(s, MM(200), top, col, d->height - top - MM(200), p == 0 ? "Lead story headline" : "", PT(11), 0,
                       OWF_ALIGN_LEFT, INK)));
        o->flow_story = 0;                           /* one story flows down the columns and on to page 2 */
        TRY((o = frame(s, MM(200) + col + MM(60), top, col, d->height - top - MM(200), "", PT(11), 0, OWF_ALIGN_LEFT, INK)));
        o->flow_story = 0;
    }
    return 1;
}

static int letterhead(owf_deck *d)
{
    owf_slide *s = owf_deck_add_slide(d, -1, -1);
    TRY(s);
    TRY(frame(s, MM(200), MM(150), d->width - MM(400), MM(250), "Organisation Name", PT(22), OWF_BOLD, OWF_ALIGN_RIGHT, ACCENT));
    TRY(block(s, MM(200), MM(420), d->width - MM(400), MM(10), ACCENT));
    TRY(frame(s, MM(200), MM(550), d->width - MM(400), d->height - MM(950), "Dear ...", PT(11), 0, OWF_ALIGN_LEFT, INK));
    TRY(frame(s, MM(200), d->height - MM(300), d->width - MM(400), MM(150), "Address | Phone | E-mail", PT(8), 0,
              OWF_ALIGN_CENTRE, INK));
    return 1;
}

static int catalog_page(owf_deck *d)
{
    owf_slide *s = owf_deck_add_slide(d, -1, -1);
    owf_obj *g;
    int x = MM(200), y = MM(500), w = (d->width - MM(400)) / 2, h = MM(600);
    TRY(s);
    TRY(frame(s, MM(200), MM(150), d->width - MM(400), MM(250), "Catalog Title", PT(26), OWF_BOLD, OWF_ALIGN_LEFT, ACCENT));
    TRY((g = owf_slide_add(s, OWF_OBJ_GROUP)) != NULL);  /* the first item: Catalog Merge repeats it */
    g->x = x; g->y = y; g->width = w; g->height = h;
    TRY((g = block(s, x + MM(50), y + MM(50), w - MM(100), MM(300), PALE)) != NULL);
    g->parent = 1;
    TRY((g = frame(s, x + MM(50), y + MM(380), w - MM(100), MM(100), "Item name", PT(12), OWF_BOLD, OWF_ALIGN_LEFT, INK)));
    g->parent = 1;
    TRY((g = frame(s, x + MM(50), y + MM(480), w - MM(100), MM(80), "Price", PT(10), 0, OWF_ALIGN_LEFT, ACCENT)));
    g->parent = 1;
    return 1;
}

typedef struct {
    opp_template_info info;
    int (*make)(owf_deck *);
} entry;

static const entry templates[] = {
    { { "Blank A4", "An empty A4 page", MM(2100), MM(2970), 1 }, NULL },
    { { "Flyer", "An A4 flyer: a title band, a message and the details", MM(2100), MM(2970), 1 }, flyer },
    { { "Postcard", "A6 landscape: a picture front and a message and address back", MM(1480), MM(1050), 2 }, postcard },
    { { "Business Card", "85 x 55 mm: name, title and contact lines", MM(850), MM(550), 1 }, business_card },
    { { "Folded Card", "An A6 card folded from A5: front, inside left and right, back", MM(1050), MM(1480), 4 },
      folded_card },
    { { "Newsletter", "Two A4 pages, one story flowing through two columns", MM(2100), MM(2970), 2 }, newsletter },
    { { "Letterhead", "An A4 letter with the organisation's name and details", MM(2100), MM(2970), 1 }, letterhead },
    { { "Catalog Page", "An A4 page whose first item Catalog Merge repeats", MM(2100), MM(2970), 1 }, catalog_page }
};

#define NTEMPLATES ((int)(sizeof templates / sizeof templates[0]))

int opp_template_count(void)
{
    return NTEMPLATES;
}

const opp_template_info *opp_template_info_at(int i)
{
    return i >= 0 && i < NTEMPLATES ? &templates[i].info : NULL;
}

int opp_template_find(const char *name)
{
    int i;
    if (!name) return -1;
    for (i = 0; i < NTEMPLATES; ++i)
        if (!strcasecmp(templates[i].info.name, name)) return i;
    return -1;
}

int opp_template_new(int i, owf_deck **deck)
{
    owf_deck *d;
    if (!deck) return OWF_ERR_UNSUPPORTED;
    *deck = NULL;
    if (i < 0 || i >= NTEMPLATES) return OWF_ERR_UNSUPPORTED;
    if (!(d = owf_deck_new(templates[i].info.width, templates[i].info.height))) return OWF_ERR_MEMORY;
    if (templates[i].make ? !templates[i].make(d) : !owf_deck_add_slide(d, -1, -1)) {
        owf_deck_free(d);
        return OWF_ERR_MEMORY;
    }
    *deck = d;
    return OWF_OK;
}
