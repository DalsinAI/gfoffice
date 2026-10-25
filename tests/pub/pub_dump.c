/*
 * pub_dump FILE: reads a Publisher file with libowf's .pub reader and
 * prints what the model holds, one item a line, for test_pub.py and for the
 * format notes (docs/gfoffice/PUB-FORMAT.md). Exit 0 when the file was
 * read, 10 when it was refused (with the reason), never a crash, cut short
 * or not.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf_pub.h"

#include <stdio.h>
#include <string.h>

static const char *const kinds[] = { "text", "picture", "table", "line", "shape", "group", "other" };

static void put_string(const char *s)
{
    putchar('"');
    for (; *s; s++) {
        if (*s == '"' || *s == '\\')
            printf("\\%c", *s);
        else if ((unsigned char)*s < 0x20)
            printf("\\x%02x", (unsigned char)*s);
        else
            putchar(*s);
    }
    putchar('"');
}

static void put_story(const owf_story *story, const char *indent)
{
    int i, j;
    for (i = 0; i < story->nparas; i++) {
        const owf_para *p = &story->paras[i];
        printf("%spara ", indent);
        putchar('"');
        for (j = 0; j < p->nruns; j++) {
            const owf_run *run = &p->runs[j];
            if (run->kind == OWF_RUN_TEXT && run->text) {
                const char *s;
                for (s = run->text; *s; s++) {
                    if (*s == '"' || *s == '\\')
                        printf("\\%c", *s);
                    else
                        putchar(*s);
                }
            } else if (run->kind == OWF_RUN_TAB) {
                printf("\\t");
            } else if (run->kind == OWF_RUN_LINEBREAK) {
                printf("\\n");
            }
        }
        printf("\"\n");
    }
}

/* EMU as millimetres with one decimal, for checking against the page. */
static void put_mm(long emu)
{
    long tenths = (emu >= 0 ? emu + 1800 : emu - 1800) / 3600;
    printf("%s%ld.%ld", tenths < 0 ? "-" : "", (tenths < 0 ? -tenths : tenths) / 10, (tenths < 0 ? -tenths : tenths) % 10);
}

static void put_page(const owf_pub *pub, const owf_pub_page *page, int with_text)
{
    int i;
    for (i = 0; i < page->nframes; i++) {
        const owf_pub_frame *f = &page->frames[i];
        printf("  frame %d %s", i, kinds[f->kind]);
        if (f->parent >= 0)
            printf(" in %d", f->parent);
        printf(" box ");
        put_mm(f->left);
        printf(" ");
        put_mm(f->top);
        printf(" ");
        put_mm(f->right - f->left);
        printf(" ");
        put_mm(f->bottom - f->top);
        printf(" mm (emu %ld %ld %ld %ld)", f->left, f->top, f->right, f->bottom);
        if (f->shape_type >= 0)
            printf(" shape %d", f->shape_type);
        if (f->text_id >= 0)
            printf(" text-id %ld story %d", f->text_id, f->story);
        if (f->wordart)
            printf(" wordart story %d", f->story);
        if (f->image >= 0)
            printf(" picture %d", f->image);
        if (f->kind == OWF_PUB_TABLE)
            printf(" table %dx%d", f->rows, f->cols);
        printf("\n");
        if (with_text && f->story >= 0 && f->story < pub->nstories)
            put_story(&pub->stories[f->story], "    ");
    }
}

int main(int argc, char **argv)
{
    owf_report *report = owf_report_new();
    owf_pub *pub = NULL;
    int result, i;
    const char *path = argc > 1 ? argv[argc - 1] : NULL;
    int stories = argc > 2 && !strcmp(argv[1], "--stories");

    if (!path || !report) {
        fprintf(stderr, "usage: pub_dump [--stories] FILE\n");
        return 20;
    }
    result = owf_pub_read_file(path, &pub, report);
    for (i = 0; i < owf_report_count(report); i++) {
        owf_note_level level;
        int times;
        const char *note = owf_report_note(report, i, &level, &times);
        printf("note %d ", (int)level);
        put_string(note);
        printf(" x%d\n", times);
    }
    if (result != OWF_OK) {
        printf("error %s\n", owf_error_text(result));
        owf_report_free(report);
        return 10;
    }
    printf("version %s\n", owf_pub_version_name(pub->version));
    if (pub->build_mark >= 0)
        printf("build-mark 0x%02X\n", pub->build_mark);
    if (pub->app_major)
        printf("written-by %d.%d\n", pub->app_major, pub->app_minor);
    printf("page-size ");
    put_mm(pub->width_emu);
    printf(" x ");
    put_mm(pub->height_emu);
    printf(" mm (emu %ld x %ld, twips %d x %d)\n", pub->width_emu, pub->height_emu, pub->width, pub->height);
    printf("masters %d\n", pub->nmasters);
    for (i = 0; i < pub->nmasters; i++) {
        printf("master %d ", i + 1);
        put_string(pub->masters[i].name ? pub->masters[i].name : "");
        printf(" frames %d\n", pub->masters[i].nframes);
        put_page(pub, &pub->masters[i], 1);
    }
    printf("pages %d\n", pub->npages);
    for (i = 0; i < pub->npages; i++) {
        printf("page %d master %d frames %d\n", i + 1, pub->pages[i].master + 1, pub->pages[i].nframes);
        put_page(pub, &pub->pages[i], 1);
    }
    if (pub->scratch_frames)
        printf("scratch-frames %d\n", pub->scratch_frames);
    printf("stories %d\n", pub->nstories);
    if (stories)
        for (i = 0; i < pub->nstories; i++) {
            printf("story %d\n", i);
            put_story(&pub->stories[i], "  ");
        }
    printf("fonts %d\n", pub->res->nfonts);
    for (i = 0; i < pub->res->nfonts; i++) {
        printf("font ");
        put_string(pub->res->fonts[i].name);
        printf("\n");
    }
    printf("pictures %d\n", pub->res->nimages);
    for (i = 0; i < pub->res->nimages; i++)
        printf("picture %d %s %lu bytes\n", i, pub->res->images[i].mime, (unsigned long)pub->res->images[i].length);
    owf_pub_free(pub);
    owf_report_free(report);
    return 0;
}
