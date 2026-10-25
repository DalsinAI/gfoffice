/*
 * libowf: what every part of it shares, whichever of GFOffice's libraries
 * it is built into (docs/suite/owf-libraries.md): the error texts and
 * names, and matching format names and extensions. They were owf_format.c's
 * and owf_book.c's; here, a library without the document or workbook
 * format tables links none of them.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf_internal.h"

#include <string.h>

const char *owf_error_text(int error)
{
    switch (error) {
    case OWF_OK: return "no error";
    case OWF_ERR_IO: return "the file could not be read or written";
    case OWF_ERR_FORMAT: return "not a document format OpenWrite knows";
    case OWF_ERR_CORRUPT: return "the document is damaged";
    case OWF_ERR_MEMORY: return "not enough memory";
    case OWF_ERR_UNSUPPORTED: return "OpenWrite cannot do that with this format yet";
    default: return "unknown error";
    }
}

const char *owf_error_name(int error)
{
    switch (error) {
    case OWF_ERROR_NULL: return "#NULL!";
    case OWF_ERROR_DIV0: return "#DIV/0!";
    case OWF_ERROR_VALUE: return "#VALUE!";
    case OWF_ERROR_REF: return "#REF!";
    case OWF_ERROR_NAME: return "#NAME?";
    case OWF_ERROR_NUM: return "#NUM!";
    case OWF_ERROR_NA: return "#N/A";
    }
    return NULL;
}

int owf_same_word(const char *a, const char *b)
{
    while (*a && *b) {
        if (owf_lower((unsigned char)*a) != owf_lower((unsigned char)*b))
            return 0;
        a++;
        b++;
    }
    return *a == *b;
}

int owf_ext_in_list(const char *list, const char *ext)
{
    size_t n = strlen(ext);
    while (*list) {
        size_t w = strcspn(list, " ");
        if (w == n) {
            size_t i;
            for (i = 0; i < n && owf_lower((unsigned char)list[i]) == owf_lower((unsigned char)ext[i]); i++)
                ;
            if (i == n)
                return 1;
        }
        list += w;
        while (*list == ' ')
            list++;
    }
    return 0;
}
