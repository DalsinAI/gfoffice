# Building and installing GFOffice

## Installing a release

The `library` drawer holds the shared libraries for the library builds; copy
them to `LIBS:` if you use those. GFOpenPrint is needed for printing laid-out
pages, and the ARexx examples are in the `Rexx` drawer.

## Building from source

On Linux, with Python 3 and zlib, the host tools and every unit test:

```
make            # host tools
make test       # all unit tests
make check      # the same under AddressSanitizer and UBSan
```

For AmigaOS, with the os32-gcc16 cross compiler (GFOpenAmigaGCC 16) and
[GFOpenLayout](https://github.com/DalsinAI/gfopenlayout) checked out beside
this repository:

```
./build-os3.sh                 # each program with libowf linked in
OWF=library ./build-os3.sh     # each program on its shared library
./build-library.sh [OUT_DIR]   # the shared libraries alone
```

Set `OS32_GCC16` to the compiler's folder and `OPENLAYOUT_SRC` to the
GFOpenLayout checkout if they are elsewhere. The programs land in
`build/os3/`. The formats, designs and architecture are documented in
this folder, starting with [`FORMATS.md`](FORMATS.md) and
[`suite/owf-libraries.md`](suite/owf-libraries.md).
