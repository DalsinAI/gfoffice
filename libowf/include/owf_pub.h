/*
 * libowf: Microsoft Publisher files (.pub), read for OpenPublisher.
 *
 * Phase 0's reader (docs/gfoffice/Design-OpenPublisher.md): it tells the
 * version, gives the page size, the pages and master pages, each page's
 * frames with their boxes and kinds, the text of text frames and tables as
 * paragraphs, the fonts the text names and the pictures. Formatting,
 * colours, wrap and the rest come in phase 1.
 *
 * There is no published specification of .pub. Everything this reader
 * knows was worked out from sample files' bytes (a clean room); the record
 * of what was found where is docs/gfoffice/PUB-FORMAT.md, and each fact
 * there is marked verified (on how many samples) or a hypothesis.
 *
 * The model is a small one of its own for now. It is shaped to move onto
 * OpenPresentation's deck model (owf_deck.h: owf_obj) once that is on
 * main: the same kinds, boxes in twips from the page's top left, text as an
 * owf_story, pictures in an owf_doc's images.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#ifndef OWF_PUB_H
#define OWF_PUB_H

#include "owf.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Which Publisher wrote the file's format. */
enum {
    OWF_PUB_UNKNOWN = 0,
    OWF_PUB_OLD,         /* the 98/2000-style Contents with no Quill text storage: Publisher 97 or older (hypothesis) */
    OWF_PUB_98,          /* the Publisher 98 file format */
    OWF_PUB_2000,        /* the Publisher 2000 file format */
    OWF_PUB_2003,        /* the 2002/2003 format, as Publisher 2003 writes it */
    OWF_PUB_2007,
    OWF_PUB_2010,
    OWF_PUB_2013,        /* Publisher 2013 or later */
    OWF_PUB_MODERN       /* the 2002-or-later format with a build mark not seen yet */
};
/* "Publisher 2007" and so on. */
const char *owf_pub_version_name(int version);

typedef enum {
    OWF_PUB_TEXT,        /* a text box */
    OWF_PUB_PICTURE,     /* a picture frame */
    OWF_PUB_TABLE,
    OWF_PUB_LINE,
    OWF_PUB_SHAPE,       /* any other drawn shape */
    OWF_PUB_GROUP,       /* its frames follow it, with parent set to it */
    OWF_PUB_OTHER        /* not known yet: drawn as its box */
} owf_pub_kind;

/* owf_pub.pages, masters: Publisher's lengths are EMU (914400 an inch,
 * 635 a twip); boxes are kept in both. */
typedef struct {
    owf_pub_kind kind;
    int parent;                  /* its group's index in the same array; -1: on the page */
    long left, top, right, bottom;   /* EMU from the page's top left, as the file has them */
    int x, y, width, height;     /* the same box in twips */
    long chunk;                  /* the file's own number for the frame (for the notes) */
    int shape_type;              /* the drawing's shape number; -1: none */
    long text_id;                /* the text's id; -1: none */
    int story;                   /* its text: into owf_pub.stories; -1: none */
    int image;                   /* a picture: into owf_pub.res->images; -1: none */
    int rows, cols;              /* a table's */
    int wordart;                 /* 1: the story is WordArt's text, drawn as a shape */
} owf_pub_frame;

typedef struct {
    char *name;                  /* a master page's ("Master Page A"); NULL for a page */
    int master;                  /* a page's master: into owf_pub.masters; -1: none */
    int nframes, capframes;
    owf_pub_frame *frames;       /* back to front as the file lists them */
} owf_pub_page;

typedef struct owf_pub {
    int version;                 /* OWF_PUB_... */
    int build_mark;              /* the byte that tells 2003, 2007, 2010 and 2013 apart; -1: none */
    int app_major, app_minor;    /* the writing program's version from the file's summary; 0: none */
    long width_emu, height_emu;  /* the page */
    int width, height;           /* the page in twips */
    int npages, cappages;
    owf_pub_page *pages;         /* in order */
    int nmasters, capmasters;
    owf_pub_page *masters;
    int nstories, capstories;
    owf_story *stories;          /* every text in the file, in the file's order; then WordArt's */
    owf_doc *res;                /* the fonts the text names, and the pictures */
    int scratch_frames;          /* frames on the scratch area, not on any page (not kept) */
} owf_pub;

/* 0 (not .pub) to 100. */
int owf_pub_detect(const unsigned char *data, size_t length);
/* OWF_OK with *pub set, or OWF_ERR_FORMAT (not .pub), OWF_ERR_CORRUPT or
 * OWF_ERR_MEMORY with *pub NULL. A damaged or cut-short file gives what can
 * be read, with notes in the report (which may be NULL). */
int owf_pub_read(const unsigned char *data, size_t length, owf_pub **pub, owf_report *report);
int owf_pub_read_file(const char *path, owf_pub **pub, owf_report *report);
void owf_pub_free(owf_pub *pub);

#ifdef __cplusplus
}
#endif

/* owf.library's calls, once every header with calls is complete (owf.h) */
#define OWF_PUB_H_DONE
#if defined(OWF_USE_LIBRARY) && !defined(OWF_LIBRARY_BUILD) && defined(OWF_H_DONE) && \
    defined(OWF_CALC_H_DONE) && defined(OWF_ZIP_H_DONE)
#include <proto/owf.h>
#endif

#endif
