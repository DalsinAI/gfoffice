/*
 * pub_streams FILE [OUTDIR]: the storage tree of an OLE compound file
 * ([MS-CFB]) through libowf's own reader (owf_cfb.c), and with OUTDIR each
 * stream written out under it, for looking at a .pub's bytes. Part of the
 * format lab (docs/gfoffice/PUB-FORMAT.md, section 1).
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static owf_cfb cfb;
static const char *outdir;

struct walk { char path[600]; int depth; };

static int visit(const owf_cfb *c, unsigned long entry, void *user)
{
    struct walk *w = user, sub;
    const unsigned char *e = owf_cfb_entry(c, entry);
    char name[200], file[800];
    unsigned long size = e[0x78] | (e[0x79] << 8) | ((unsigned long)e[0x7A] << 16) | ((unsigned long)e[0x7B] << 24);
    int i;
    owf_cfb_entry_name(c, entry, name, sizeof name);
    printf("%*s%s %s", w->depth * 2, "", e[0x42] == 1 ? "[storage]" : e[0x42] == 2 ? "[stream]" : "[?]", name);
    if (e[0x42] == 2)
        printf(" %lu bytes", size);
    if (e[0x42] == 1) {
        printf(" clsid ");
        for (i = 0; i < 16; i++)
            printf("%02x", e[0x50 + i]);
    }
    printf(" (entry %lu)\n", entry);
    if (strlen(w->path) + strlen(name) + 2 > sizeof sub.path)
        return 0;
    snprintf(sub.path, sizeof sub.path, "%s/%s", w->path, name);
    sub.depth = w->depth + 1;
    if (outdir && strlen(outdir) + strlen(sub.path) + 1 < sizeof file) {
        snprintf(file, sizeof file, "%s%s", outdir, sub.path);
        if (e[0x42] == 1) {
            mkdir(file, 0777);
        } else if (e[0x42] == 2) {
            unsigned char *d;
            size_t n;
            FILE *f;
            if (owf_cfb_read_entry(&cfb, entry, &d, &n) == OWF_OK) {
                if ((f = fopen(file, "wb"))) {
                    fwrite(d, 1, n, f);
                    fclose(f);
                }
                owf_free(d);
            }
        }
    }
    if (e[0x42] == 1)
        owf_cfb_children(c, entry, visit, &sub);
    return 0;
}

int main(int argc, char **argv)
{
    unsigned char *data;
    size_t n;
    struct walk w;
    int i;
    if (argc < 2 || owf_sys_read_file(argv[1], &data, &n) != OWF_OK) {
        fprintf(stderr, "usage: pub_streams FILE [OUTDIR]\n");
        return 20;
    }
    outdir = argc > 2 ? argv[2] : NULL;
    if (outdir)
        mkdir(outdir, 0777);
    if ((i = owf_cfb_open(&cfb, data, n)) != OWF_OK) {
        printf("not a compound file: %s\n", owf_error_text(i));
        free(data);
        return 10;
    }
    printf("sector %u, %lu directory entries, root clsid ", cfb.sector_size, (unsigned long)cfb.ndir);
    for (i = 0; i < 16; i++)
        printf("%02x", cfb.dir[0x50 + i]);
    printf("\n");
    w.path[0] = 0;
    w.depth = 0;
    owf_cfb_children(&cfb, 0, visit, &w);
    if (cfb.cut_short)
        printf("cut short\n");
    owf_cfb_close(&cfb);
    free(data);
    return 0;
}
