/*
 * odg_stencil.c: GFOpenDiag's shapes palette (opendiag_stencil.h), in sets. The styles are draw.io's own words for
 * its shapes, as its files carry them, so each opens there as the same shape; each is one the canvas
 * (src/diag/odg_view.c) and the writers draw, those beyond the first eight by their outlines (owf_dshape.c).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stddef.h>
#include "opendiag_stencil.h"

static const odg_stencil shapes[] = {
    /* Flowchart: the first eight keep their numbers (ARexx SHAPE and the menus name them) */
    { "Process", "rounded=0;whiteSpace=wrap;html=1;", 120, 60, ODG_SET_FLOWCHART },
    { "Decision", "rhombus;whiteSpace=wrap;html=1;", 80, 80, ODG_SET_FLOWCHART },
    { "Terminator", "rounded=1;whiteSpace=wrap;html=1;arcSize=50;", 120, 40, ODG_SET_FLOWCHART },
    { "Data", "shape=parallelogram;perimeter=parallelogramPerimeter;whiteSpace=wrap;html=1;fixedSize=1;", 120, 60, ODG_SET_FLOWCHART },
    { "Predefined Process", "shape=process;whiteSpace=wrap;html=1;backgroundOutline=1;", 120, 60, ODG_SET_FLOWCHART },
    { "Preparation", "shape=hexagon;perimeter=hexagonPerimeter2;whiteSpace=wrap;html=1;fixedSize=1;", 120, 60, ODG_SET_FLOWCHART },
    { "Triangle", "triangle;whiteSpace=wrap;html=1;", 60, 80, ODG_SET_FLOWCHART },
    { "Circle", "ellipse;whiteSpace=wrap;html=1;aspect=fixed;", 80, 80, ODG_SET_FLOWCHART },
    { "Document", "shape=document;whiteSpace=wrap;html=1;boundedLbl=1;", 120, 80, ODG_SET_FLOWCHART },
    { "Multi-Document", "shape=mxgraph.flowchart.multi-document;whiteSpace=wrap;html=1;", 88, 60, ODG_SET_FLOWCHART },
    { "Database", "shape=cylinder3;whiteSpace=wrap;html=1;boundedLbl=1;backgroundOutline=1;size=15;", 60, 80, ODG_SET_FLOWCHART },
    { "Manual Input", "shape=manualInput;whiteSpace=wrap;html=1;size=15;", 120, 60, ODG_SET_FLOWCHART },
    { "Manual Operation", "shape=trapezoid;perimeter=trapezoidPerimeter;whiteSpace=wrap;html=1;fixedSize=1;flipV=1;", 120, 60, ODG_SET_FLOWCHART },
    { "Stored Data", "shape=dataStorage;whiteSpace=wrap;html=1;fixedSize=1;", 100, 80, ODG_SET_FLOWCHART },
    { "Internal Storage", "shape=internalStorage;whiteSpace=wrap;html=1;backgroundOutline=1;", 80, 80, ODG_SET_FLOWCHART },
    { "Delay", "shape=delay;whiteSpace=wrap;html=1;", 100, 60, ODG_SET_FLOWCHART },
    { "Display", "shape=display;whiteSpace=wrap;html=1;", 120, 60, ODG_SET_FLOWCHART },
    { "Off-Page Reference", "shape=offPageConnector;whiteSpace=wrap;html=1;", 60, 60, ODG_SET_FLOWCHART },
    { "Card", "shape=card;whiteSpace=wrap;html=1;", 80, 100, ODG_SET_FLOWCHART },
    { "Paper Tape", "shape=tape;whiteSpace=wrap;html=1;", 120, 80, ODG_SET_FLOWCHART },
    { "Loop Limit", "shape=loopLimit;whiteSpace=wrap;html=1;", 100, 60, ODG_SET_FLOWCHART },
    { "Sort", "shape=sortShape;perimeter=rhombusPerimeter;whiteSpace=wrap;html=1;", 80, 80, ODG_SET_FLOWCHART },
    { "Collate", "shape=collate;whiteSpace=wrap;html=1;", 80, 80, ODG_SET_FLOWCHART },
    { "Summing Junction", "shape=sumEllipse;perimeter=ellipsePerimeter;whiteSpace=wrap;html=1;backgroundOutline=1;", 80, 80, ODG_SET_FLOWCHART },
    { "Or", "shape=orEllipse;perimeter=ellipsePerimeter;whiteSpace=wrap;html=1;backgroundOutline=1;", 80, 80, ODG_SET_FLOWCHART },
    { "Extract", "triangle;whiteSpace=wrap;html=1;direction=north;", 80, 60, ODG_SET_FLOWCHART },
    { "Merge", "triangle;whiteSpace=wrap;html=1;direction=south;", 80, 60, ODG_SET_FLOWCHART },
    /* Basic */
    { "Rectangle", "rounded=0;whiteSpace=wrap;html=1;", 120, 60, ODG_SET_BASIC },
    { "Rounded Rectangle", "rounded=1;whiteSpace=wrap;html=1;", 120, 60, ODG_SET_BASIC },
    { "Ellipse", "ellipse;whiteSpace=wrap;html=1;", 120, 80, ODG_SET_BASIC },
    { "Square", "whiteSpace=wrap;html=1;aspect=fixed;", 80, 80, ODG_SET_BASIC },
    { "Text", "text;html=1;align=center;verticalAlign=middle;whiteSpace=wrap;", 60, 30, ODG_SET_BASIC },
    { "Diamond", "rhombus;whiteSpace=wrap;html=1;", 80, 80, ODG_SET_BASIC },
    { "Parallelogram", "shape=parallelogram;perimeter=parallelogramPerimeter;whiteSpace=wrap;html=1;fixedSize=1;", 120, 60, ODG_SET_BASIC },
    { "Hexagon", "shape=hexagon;perimeter=hexagonPerimeter2;whiteSpace=wrap;html=1;fixedSize=1;", 120, 80, ODG_SET_BASIC },
    { "Cylinder", "shape=cylinder3;whiteSpace=wrap;html=1;boundedLbl=1;backgroundOutline=1;size=15;", 60, 80, ODG_SET_BASIC },
    { "Cloud", "ellipse;shape=cloud;whiteSpace=wrap;html=1;", 120, 80, ODG_SET_BASIC },
    { "Note", "shape=note;whiteSpace=wrap;html=1;backgroundOutline=1;size=20;", 80, 100, ODG_SET_BASIC },
    { "Cube", "shape=cube;whiteSpace=wrap;html=1;boundedLbl=1;backgroundOutline=1;size=10;", 120, 80, ODG_SET_BASIC },
    { "Step", "shape=step;perimeter=stepPerimeter;whiteSpace=wrap;html=1;fixedSize=1;", 120, 80, ODG_SET_BASIC },
    { "Trapezoid", "shape=trapezoid;perimeter=trapezoidPerimeter;whiteSpace=wrap;html=1;fixedSize=1;", 120, 60, ODG_SET_BASIC },
    { "Callout", "shape=callout;whiteSpace=wrap;html=1;perimeter=calloutPerimeter;", 120, 80, ODG_SET_BASIC },
    { "Actor", "shape=umlActor;verticalLabelPosition=bottom;verticalAlign=top;html=1;outlineConnect=0;", 30, 60, ODG_SET_BASIC },
    { "Cross", "shape=cross;whiteSpace=wrap;html=1;", 80, 80, ODG_SET_BASIC },
    { "Arrow", "shape=singleArrow;whiteSpace=wrap;html=1;", 100, 60, ODG_SET_BASIC },
    { "Double Arrow", "shape=doubleArrow;whiteSpace=wrap;html=1;", 100, 60, ODG_SET_BASIC },
};

#define NSHAPES ((int)(sizeof shapes / sizeof shapes[0]))

int odg_stencil_count(void) { return NSHAPES; }

const char *odg_stencil_set_name(odg_stencil_set set)
{
    switch (set) {
    case ODG_SET_FLOWCHART: return "Flowchart";
    case ODG_SET_BASIC: return "Basic";
    default: return NULL;
    }
}

int odg_stencil_in_set(odg_stencil_set set, int nth)
{
    int i, n = 0;
    for (i = 0; i < NSHAPES; i++)
        if (shapes[i].set == set && n++ == nth)
            return i;
    return -1;
}

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
