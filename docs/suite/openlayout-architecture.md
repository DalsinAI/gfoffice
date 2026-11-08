# How GFOpenLayout works in GFOffice

[Download the rendered PNG](openlayout-architecture.png). The editable
Graphviz source is `openlayout-architecture.dot`.

```mermaid
flowchart TB
    subgraph Apps[GFOffice applications]
        Writer[OpenWriter]
        Calc[OpenCalc]
        Present[OpenPresentation]
        Publisher[OpenPublisher]
    end

    subgraph Shared[Shared office libraries]
        OWF[libowf\ndocument, workbook and deck models\nimport, export and PDF]
        OL[GFOpenLayout\nnode tree to integer layout to display list\nshared clipping, shape paths and rotation]
        OGT[GFOpenGadTools\nmenus, toolbars, gadgets and icons]
    end

    subgraph Adapters[Application adapters and backends]
        WriterCanvas[Writer page canvas]
        CalcCanvas[Calc grid and charts]
        DeckCanvas[Presentation and Publisher canvas]
        PrintPDF[GFOpenPrint and searchable PDF]
        Media[GFOpenDatatypes adapter\npicture decode and metadata]
        Fonts[GFOpenFont and diskfont adapter\nmeasurement and TextFont drawing]
        GPUAdapter[Optional GFOpenRTG and GFOpenGPU adapter\ncomposite, scale, paths and cache]
    end

    subgraph OS[AmigaOS service layer]
        ExecDOS[exec.library and dos.library]
        UI[intuition.library, gadtools.library and asl.library]
        Raster[graphics.library and layers.library\nRastPort, BitMap, regions and blits]
        DT[datatypes.library and picture classes]
        FontOS[diskfont.library and font engines]
        RTG[cybergraphics.library and RTG API\nchunky pixels and ScalePixelArray]
        GPU[gfopengpu.library\ncapability-gated command batches]
    end

    subgraph Drivers[Display and accelerator drivers]
        PlanarDriver[Native planar chipset driver]
        RTGDriver[CyberGraphX or Picasso96-class RTG driver]
        ACRTG[AmiCradle ACRTG.gpu]
        PiStorm[PiStorm or accelerated host driver]
    end

    subgraph Hardware[Boards and display hardware]
        Native[OCS, ECS and AGA]
        Zorro[Zorro RTG boards\nPicasso IV and CyberVision family]
        PCI[PCI RTG boards\nRadeon and Voodoo families]
        Hosted[PiStorm and AmiCradle hosted graphics]
    end

    Writer --> OWF
    Calc --> OWF
    Present --> OWF
    Publisher --> OWF
    Writer --> OGT
    Calc --> OGT
    Present --> OGT
    Publisher --> OGT
    Writer --> OL
    Present --> OL
    Publisher --> OL

    OL --> WriterCanvas
    OL --> DeckCanvas
    OWF --> CalcCanvas
    OWF --> PrintPDF
    WriterCanvas --> Raster
    CalcCanvas --> Raster
    DeckCanvas --> Raster
    WriterCanvas --> Media
    DeckCanvas --> Media
    WriterCanvas --> Fonts
    CalcCanvas --> Fonts
    DeckCanvas --> Fonts
    WriterCanvas -. accelerated path .-> GPUAdapter
    CalcCanvas -. accelerated path .-> GPUAdapter
    DeckCanvas -. accelerated path .-> GPUAdapter

    OGT --> UI
    Media --> DT
    Fonts --> FontOS
    GPUAdapter --> RTG
    GPUAdapter -. capability gated .-> GPU
    PrintPDF --> ExecDOS
    Raster --> PlanarDriver
    Raster --> RTGDriver
    RTG --> RTGDriver
    GPU --> ACRTG
    GPU --> PiStorm

    PlanarDriver --> Native
    RTGDriver --> Zorro
    RTGDriver --> PCI
    ACRTG --> Hosted
    PiStorm --> Hosted
    UI --> ExecDOS
    DT --> ExecDOS
    FontOS --> ExecDOS
    Raster --> ExecDOS

```

GFOpenLayout owns layout decisions and renderer-neutral geometry. It stores a
node tree, computes pages and boxes using integer units, and emits an immutable
display list. Backends consume that list; they still own platform details such
as pens, fonts, raster images, PDF commands, arrowhead painting and partial
redraw policy.

`ol_shape_path` is the shared boundary for vector shapes. It converts a shape's
normalized points or primitive type into bounded device coordinates and applies
rotation about the box centre without allocation or floating point. The screen
and PDF backends therefore cannot drift into different rectangle, ellipse,
rounded-corner, polygon or rotation implementations.

Writer and the Presentation/Publisher deck adapter use the OpenLayout document
and display-list pipeline. Publisher inherits deck rendering from Presentation.
Calc currently keeps its interactive cell grid and charts in its own viewport
renderer: that is deliberate because OpenLayout does not yet model virtualized
spreadsheet grids, cell selection or chart scenes. The applications' backends,
not OpenLayout, use DataTypes, TextFonts, graphics/layers, CyberGraphX and the
optional GFOpenGPU path. Native planar drawing remains the semantic fallback;
RTG and GPU drivers accelerate the same document geometry when available.

The clean extension points are new size-versioned style fields, appended
display operations, and appended resident-library calls. Likely next shared
additions are curve/path segments, resolved source/destination image geometry,
text/object transforms, and stable display-op-to-node inspection helpers.
