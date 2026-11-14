/*
 * odg_stencil.c: GFOpenDiag's shapes palette (opendiag_stencil.h). The styles are draw.io's own words for its
 * flowchart shapes, as its files carry them; each is one the canvas (src/diag/odg_view.c) draws.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stddef.h>
#include "opendiag_stencil.h"

static const odg_stencil shapes[] = {
    { "Process", "rounded=0;whiteSpace=wrap;html=1;", 120, 60 },
    { "Decision", "rhombus;whiteSpace=wrap;html=1;", 80, 80 },
    { "Terminator", "rounded=1;whiteSpace=wrap;html=1;arcSize=50;", 120, 40 },
    { "Data", "shape=parallelogram;perimeter=parallelogramPerimeter;whiteSpace=wrap;html=1;fixedSize=1;", 120, 60 },
    { "Predefined Process", "shape=process;whiteSpace=wrap;html=1;backgroundOutline=1;", 120, 60 },
    { "Preparation", "shape=hexagon;perimeter=hexagonPerimeter2;whiteSpace=wrap;html=1;fixedSize=1;", 120, 60 },
    { "Triangle", "triangle;whiteSpace=wrap;html=1;", 60, 80 },
    { "Circle", "ellipse;whiteSpace=wrap;html=1;aspect=fixed;", 80, 80 },
};

#define NSHAPES ((int)(sizeof shapes / sizeof shapes[0]))

int odg_stencil_count(void) { return NSHAPES; }

const odg_stencil *odg_stencil_at(int i)
{
    return i >= 0 && i < NSHAPES ? &shapes[i] : NULL;
}

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

int odg_stencil_find(const char *name)
{
    int i;
    for (i = 0; name && i < NSHAPES; i++) {
        const char *a = shapes[i].name, *b = name;
        while (*a && lower((unsigned char)*a) == lower((unsigned char)*b)) a++, b++;
        if (!*a && !*b)
            return i;
    }
    return -1;
}
