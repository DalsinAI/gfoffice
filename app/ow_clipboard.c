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
