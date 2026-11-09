/* Shared GFOffice Amiga FTXT clipboard adapter. */
#include "ow_clipboard.h"

#include <devices/clipboard.h>
#include <exec/io.h>
#include <exec/ports.h>
#include <proto/exec.h>

#include <stdlib.h>
#include <string.h>

#define OW_CLIP_ID(a,b,c,d) ((ULONG)(a) << 24 | (ULONG)(b) << 16 | (ULONG)(c) << 8 | (ULONG)(d))
#define OW_CLIP_FORM OW_CLIP_ID('F','O','R','M')
#define OW_CLIP_FTXT OW_CLIP_ID('F','T','X','T')
#define OW_CLIP_CHRS OW_CLIP_ID('C','H','R','S')
#define OW_CLIP_LIMIT (1UL << 20)
#ifdef OW_CLIPBOARD_OBJECTS
#define OW_CLIP_GFDO OW_CLIP_ID('G','F','D','O')
#define OW_CLIP_INFO OW_CLIP_ID('I','N','F','O')
#define OW_CLIP_ODP  OW_CLIP_ID('O','D','P',' ')
#define OW_CLIP_OBJECT_LIMIT (16UL << 20)
#endif

typedef struct {
    struct MsgPort *port;
    struct IOClipReq *io;
} ow_clip;

static int clip_open(ow_clip *clip)
{
    memset(clip, 0, sizeof *clip);
    clip->port = CreateMsgPort();
    if (!clip->port)
        return 0;
    clip->io = (struct IOClipReq *)CreateIORequest(clip->port, sizeof *clip->io);
    if (!clip->io)
        return 0;
    if (OpenDevice((STRPTR)"clipboard.device", PRIMARY_CLIP,
                   (struct IORequest *)clip->io, 0)) {
        clip->io->io_Device = NULL;
        return 0;
    }
    return 1;
}

static void clip_close(ow_clip *clip)
{
    if (!clip)
        return;
    if (clip->io) {
        if (clip->io->io_Device)
            CloseDevice((struct IORequest *)clip->io);
        DeleteIORequest((struct IORequest *)clip->io);
    }
    if (clip->port)
        DeleteMsgPort(clip->port);
    memset(clip, 0, sizeof *clip);
}

static LONG clip_read(ow_clip *clip, void *buffer, LONG length)
{
    clip->io->io_Command = CMD_READ;
    clip->io->io_Data = (STRPTR)buffer;
    clip->io->io_Length = length;
    DoIO((struct IORequest *)clip->io);
    return clip->io->io_Error ? -1 : (LONG)clip->io->io_Actual;
}

static void clip_finish_read(ow_clip *clip)
{
    UBYTE discard[64];
    while (clip_read(clip, discard, sizeof discard) > 0)
        ;
}

static char *latin1_to_utf8(const char *text, size_t length, size_t *out_length)
{
    const unsigned char *source = (const unsigned char *)text;
    char *out = malloc(length * 2 + 1);
    size_t i, n = 0;
    if (!out)
        return NULL;
    for (i = 0; i < length; ++i) {
        if (source[i] < 0x80)
            out[n++] = (char)source[i];
        else {
            out[n++] = (char)(0xC0 | (source[i] >> 6));
            out[n++] = (char)(0x80 | (source[i] & 0x3F));
        }
    }
    out[n] = 0;
    if (out_length)
        *out_length = n;
    return out;
}

static char *utf8_to_latin1(const char *text, size_t length, size_t *out_length)
{
    const unsigned char *source = (const unsigned char *)text;
    char *out = malloc(length + 1);
    size_t i = 0, n = 0;
    if (!out)
        return NULL;
    while (i < length) {
        unsigned c = source[i], cp, need, k;
        if (c < 0x80) {
            out[n++] = (char)c;
            ++i;
            continue;
        }
        need = c >= 0xF0 && c < 0xF8 ? 3 : c >= 0xE0 ? 2 : c >= 0xC2 ? 1 : 0;
        if (c >= 0xF8)
            need = 0;
        cp = need == 1 ? c & 0x1F : need == 2 ? c & 0x0F : c & 0x07;
        for (k = 1; k <= need; ++k) {
            if (i + k >= length || (source[i + k] & 0xC0) != 0x80)
                break;
            cp = (cp << 6) | (source[i + k] & 0x3F);
        }
        if (!need || k <= need) {
            out[n++] = '?';
            ++i;
            continue;
        }
        out[n++] = cp <= 0xFF ? (char)cp : '?';
        i += need + 1;
    }
    out[n] = 0;
    if (out_length)
        *out_length = n;
    return out;
}

char *ow_clipboard_get_text(size_t *out_length)
{
    ow_clip clip;
    ULONG head[3], chunk[2], total = 0;
    char *latin1 = NULL, *utf8;
    if (out_length)
        *out_length = 0;
    if (!clip_open(&clip)) {
        clip_close(&clip);
        return NULL;
    }
    clip.io->io_Offset = 0;
    clip.io->io_ClipID = 0;
    if (clip_read(&clip, head, 12) != 12 || head[0] != OW_CLIP_FORM ||
        head[2] != OW_CLIP_FTXT || head[1] > OW_CLIP_LIMIT + 64) {
        clip_finish_read(&clip);
        clip_close(&clip);
        return NULL;
    }
    latin1 = malloc((size_t)head[1] + 1);
    if (!latin1) {
        clip_finish_read(&clip);
        clip_close(&clip);
        return NULL;
    }
    while (clip_read(&clip, chunk, 8) == 8) {
        ULONG size = chunk[1], padded = size + (size & 1);
        if (chunk[0] == OW_CLIP_CHRS && total + size <= head[1]) {
            if (clip_read(&clip, latin1 + total, (LONG)size) != (LONG)size)
                break;
            total += size;
            if (size & 1) {
                UBYTE pad;
                clip_read(&clip, &pad, 1);
            }
        } else {
            UBYTE discard[64];
            while (padded) {
                LONG n = padded < sizeof discard ? (LONG)padded : (LONG)sizeof discard;
                if (clip_read(&clip, discard, n) != n)
                    break;
                padded -= (ULONG)n;
            }
        }
    }
    clip_finish_read(&clip);
    clip_close(&clip);
    utf8 = latin1_to_utf8(latin1, total, out_length);
    free(latin1);
    return utf8;
}

int ow_clipboard_set_text(const char *utf8, size_t length)
{
    ow_clip clip;
    ULONG form[3], chunk[2];
    UBYTE pad = 0;
    char *latin1;
    size_t latin1_length = 0;
    if (!utf8 || length > OW_CLIP_LIMIT ||
        !(latin1 = utf8_to_latin1(utf8, length, &latin1_length)))
        return 0;
    if (!clip_open(&clip)) {
        clip_close(&clip);
        free(latin1);
        return 0;
    }
    form[0] = OW_CLIP_FORM;
    form[1] = 4 + 8 + (ULONG)latin1_length + ((ULONG)latin1_length & 1);
    form[2] = OW_CLIP_FTXT;
    chunk[0] = OW_CLIP_CHRS;
    chunk[1] = (ULONG)latin1_length;
    clip.io->io_Offset = 0;
    clip.io->io_ClipID = 0;
    clip.io->io_Error = 0;
#define CLIP_WRITE(pointer,bytes) do { \
    clip.io->io_Command = CMD_WRITE; clip.io->io_Data = (STRPTR)(pointer); \
    clip.io->io_Length = (bytes); DoIO((struct IORequest *)clip.io); \
    if (clip.io->io_Error) goto write_fail; \
} while (0)
    CLIP_WRITE(form, 12);
    CLIP_WRITE(chunk, 8);
    if (latin1_length)
        CLIP_WRITE(latin1, (ULONG)latin1_length);
    if (latin1_length & 1)
        CLIP_WRITE(&pad, 1);
    clip.io->io_Command = CMD_UPDATE;
    DoIO((struct IORequest *)clip.io);
    if (clip.io->io_Error)
        goto write_fail;
#undef CLIP_WRITE
    clip_close(&clip);
    free(latin1);
    return 1;
write_fail:
#undef CLIP_WRITE
    clip_close(&clip);
    free(latin1);
    return 0;
}

#ifdef OW_CLIPBOARD_OBJECTS
unsigned char *ow_clipboard_get_deck_object(size_t *out_length, int *out_slide, int *out_obj)
{
    ow_clip clip;
    ULONG head[3], chunk[2], info[2] = { 0, 0 };
    unsigned char *odp = NULL;
    size_t odp_length = 0;
    int have_info = 0;
    if (out_length) *out_length = 0;
    if (out_slide) *out_slide = -1;
    if (out_obj) *out_obj = -1;
    if (!clip_open(&clip)) { clip_close(&clip); return NULL; }
    clip.io->io_Offset = 0;
    clip.io->io_ClipID = 0;
    if (clip_read(&clip, head, 12) != 12 || head[0] != OW_CLIP_FORM ||
        head[2] != OW_CLIP_GFDO || head[1] > OW_CLIP_OBJECT_LIMIT + 64) {
        clip_finish_read(&clip);
        clip_close(&clip);
        return NULL;
    }
    while (clip_read(&clip, chunk, 8) == 8) {
        ULONG size = chunk[1], padded = size + (size & 1);
        if (chunk[0] == OW_CLIP_INFO && size == sizeof info) {
            if (clip_read(&clip, info, sizeof info) != sizeof info) break;
            have_info = 1;
        } else if (chunk[0] == OW_CLIP_ODP && size > 0 && size <= OW_CLIP_OBJECT_LIMIT) {
            unsigned char *data = malloc((size_t)size);
            if (!data || clip_read(&clip, data, (LONG)size) != (LONG)size) {
                free(data);
                break;
            }
            free(odp);
            odp = data;
            odp_length = (size_t)size;
        } else {
            UBYTE discard[64];
            while (padded) {
                LONG n = padded < sizeof discard ? (LONG)padded : (LONG)sizeof discard;
                if (clip_read(&clip, discard, n) != n) break;
                padded -= (ULONG)n;
            }
            continue;
        }
        if (size & 1) {
            UBYTE pad;
            if (clip_read(&clip, &pad, 1) != 1) break;
        }
    }
    clip_finish_read(&clip);
    clip_close(&clip);
    if (!odp || !have_info || info[0] > 0x7fffffffUL || info[1] > 0x7fffffffUL) {
        free(odp);
        return NULL;
    }
    if (out_length) *out_length = odp_length;
    if (out_slide) *out_slide = (int)info[0];
    if (out_obj) *out_obj = (int)info[1];
    return odp;
}

int ow_clipboard_set_deck_object(const void *odp, size_t length, int slide, int obj)
{
    ow_clip clip;
    ULONG form[3], info_chunk[2], info[2], odp_chunk[2];
    UBYTE pad = 0;
    if (!odp || !length || length > OW_CLIP_OBJECT_LIMIT || slide < 0 || obj < 0)
        return 0;
    if (!clip_open(&clip)) { clip_close(&clip); return 0; }
    form[0] = OW_CLIP_FORM;
    form[1] = 4 + 8 + sizeof info + 8 + (ULONG)length + ((ULONG)length & 1);
    form[2] = OW_CLIP_GFDO;
    info_chunk[0] = OW_CLIP_INFO; info_chunk[1] = sizeof info;
    info[0] = (ULONG)slide; info[1] = (ULONG)obj;
    odp_chunk[0] = OW_CLIP_ODP; odp_chunk[1] = (ULONG)length;
    clip.io->io_Offset = 0;
    clip.io->io_ClipID = 0;
    clip.io->io_Error = 0;
#define OBJECT_WRITE(pointer,bytes) do { \
    clip.io->io_Command = CMD_WRITE; clip.io->io_Data = (STRPTR)(pointer); \
    clip.io->io_Length = (bytes); DoIO((struct IORequest *)clip.io); \
    if (clip.io->io_Error) goto object_write_fail; \
} while (0)
    OBJECT_WRITE(form, sizeof form);
    OBJECT_WRITE(info_chunk, sizeof info_chunk);
    OBJECT_WRITE(info, sizeof info);
    OBJECT_WRITE(odp_chunk, sizeof odp_chunk);
    OBJECT_WRITE((void *)odp, (ULONG)length);
    if (length & 1) OBJECT_WRITE(&pad, 1);
    clip.io->io_Command = CMD_UPDATE;
    DoIO((struct IORequest *)clip.io);
    if (clip.io->io_Error) goto object_write_fail;
#undef OBJECT_WRITE
    clip_close(&clip);
    return 1;
object_write_fail:
#undef OBJECT_WRITE
    clip_close(&clip);
    return 0;
}
#endif
