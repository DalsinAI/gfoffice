# Third-party parts

OpenWrite's own code and documents are MIT, Copyright (c) 2026 Dalsin
Limited (`LICENSE`). Third-party code, data and runtime services retain their
own licences.

## Included or linked in OpenWrite 1.0

| Part | What for | Licence | How |
| --- | --- | --- | --- |
| OpenGadTools (`DalsinAI/opengadtools`) | Native UI/theme drawing | MIT, Dalsin Limited | Source copy under `third_party/opengadtools` |
| zlib | ZIP containers used by ODT and DOCX | zlib | Linked into the format layer/build |
| SCOWL/Aspell English word list | Bundled `en_GB.words` spelling dictionary | SCOWL/Aspell word-list permissions and upstream component notices | Shipped as data; full notice in `licenses/SCOWL-ASPELL-English.txt` |

## Runtime/system integrations

| Part | What for | Licence | How |
| --- | --- | --- | --- |
| OpenRTG | Accelerated true-colour document canvas where installed | MIT, Dalsin Limited | Runtime/system service |
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
