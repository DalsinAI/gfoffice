/*
 * libowf: reading IFF files (EA IFF 85), the container of the Amiga's
 * documents: FTXT, ProWrite's WORD, Final Writer's and Wordworth's own forms.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf_internal.h"

int owf_iff_is_form(const unsigned char *data, size_t length, unsigned long type)
{
    return length >= 12 && OWF_BE32(data) == OWF_ID('F', 'O', 'R', 'M') && OWF_BE32(data + 8) == type;
}

int owf_iff_open(owf_iff_reader *reader, const unsigned char *data, size_t length, unsigned long type)
{
    unsigned long size;

    if (!owf_iff_is_form(data, length, type))
        return 0;
    size = OWF_BE32(data + 4);
    reader->pos = data + 12;
    reader->truncated = 0;
    if (size < 4) {
        reader->end = reader->pos;
    } else if (size > length - 8) {
        reader->end = data + length;
        reader->truncated = 1;
    } else {
        reader->end = data + 8 + size;
    }
    return 1;
}

int owf_iff_next(owf_iff_reader *reader, owf_iff_chunk *chunk)
{
    unsigned long left;

    if (reader->end - reader->pos < 8)
        return 0;
    chunk->id = OWF_BE32(reader->pos);
    chunk->size = OWF_BE32(reader->pos + 4);
    chunk->data = reader->pos + 8;
    left = (unsigned long)(reader->end - chunk->data);
    if (chunk->size > left) {
        chunk->size = left;
        reader->truncated = 1;
        reader->pos = reader->end;
        return 1;
    }
    /* Chunks start on even bytes: an odd size is followed by a pad byte. */
    reader->pos = chunk->data + chunk->size + (chunk->size & 1);
    if (reader->pos > reader->end)
        reader->pos = reader->end;
    return 1;
}
