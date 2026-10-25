# Third-party parts

OpenWrite's own code and documents are MIT, Copyright (c) 2026 AmigaChrome
Team (`LICENSE`). Third-party code, data and runtime services retain their
own licences.

## Included or linked in OpenWrite 1.0

| Part | What for | Licence | How |
| --- | --- | --- | --- |
| OpenGadTools (`DalsinAI/opengadtools`) | Native UI/theme drawing | MIT, AmigaChrome Team | Source copy under `third_party/opengadtools` |
| zlib | ZIP containers used by ODT and DOCX, in the PC build and tests | zlib | The system zlib, linked into the PC build only; the Amiga build uses our z.library (`DalsinAI/amigachrome-guest` `libraries/openzlib`) and carries no zlib source |
| SCOWL/Aspell English word list | Bundled `en_GB.words` spelling dictionary | SCOWL/Aspell word-list permissions and upstream component notices | Shipped as data; full notice in `licenses/SCOWL-ASPELL-English.txt` |

## Test data only (not shipped)

| Part | What for | Licence | How |
| --- | --- | --- | --- |
| Apache POI's Publisher test files | `tests/pub/samples`: the .pub reader's tests | Apache-2.0 (`licenses/Apache-2.0.txt`; POI's notice in `tests/pub/samples/NOTICE-Apache-POI.txt`) | Sample files only, no code; sources and checksums in `tests/pub/CORPUS.md` |
| gabriel-vasile/mimetype's `testdata/pub.pub` | `tests/pub/samples/mimetype-pub.pub`: a cut-short file | MIT (`licenses/MIT-gabriel-vasile-mimetype.txt`) | The same |

## Runtime/system integrations

| Part | What for | Licence | How |
| --- | --- | --- | --- |
| OpenRTG | Accelerated true-colour document canvas where installed | MIT, AmigaChrome Team | Runtime/system service |
| Amiga datatypes / OpenDatatypes | Embedded image/media decoding | System/datatype-specific licences | Runtime service |
| OpenPrint / OpenAmigaPrint | PDF/IPP/network printer backends when configured | Backend-specific licence | Selected through standard `printer.device` output |

No WebCore, JavaScriptCore, GTK, Qt or Unix GUI compatibility layer is required
by OpenWrite 1.0.

## Reference only

The following work was read for format understanding. Its source is not copied
into OpenWrite unless separately identified above under a compatible licence.

| Work | Licence/status | For |
| --- | --- | --- |
| fw2odf (github.com/hornc/fw2odf) | MIT | Final Writer format research |
| libwpd, libmwaw (Document Liberation Project) | MPL-2.0 / LGPL | WordPerfect and older word-processor format research |
| EA IFF 85 and New Horizons FORM WORD specifications | Published specifications | IFF FTXT and ProWrite |
