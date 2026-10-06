# Third-party parts

OpenWrite's own code and documents are MIT, Copyright (c) 2026 Dalsin
Limited (`LICENSE`). The source copy of OpenGadTools is present under
`third_party/opengadtools`; the other entries are current or planned runtime
dependencies and each keeps its own licence.

| Part | What for | Licence | How |
| --- | --- | --- | --- |
| OpenGadTools (`DalsinAI/opengadtools`) | Native UI/theme drawing | MIT, Dalsin Limited | Source copy from main `d5f4dff` |
| OpenRTG (`DalsinAI/openamigartg`) | Accelerated true-colour display path | MIT, Dalsin Limited | Runtime/system service |
| OpenDatatypes / OpenImage | Embedded media and object decoding | MIT plus datatype-specific licences | Runtime/system service |
| OpenPrint | Printing, PDF and network output | MIT, Dalsin Limited | Runtime/system service |
| FreeType / HarfBuzz (later native text shaping) | Document fonts and shaping | FreeType License; MIT | Planned native renderer dependency |
| WebCore/WTF/JavaScriptCore (optional future component) | Optional rich/HTML view, not the baseline editor | LGPL-2 and BSD-style | Not required by native OpenWrite |
| zlib | Zip containers (ODT, DOCX) | zlib | Linked |
| libpng, libjpeg (`openamigaimage`) | Pictures inside documents | libpng; IJG | Linked |
| Hunspell (phase W5) | Spelling | MPL-1.1 (of its MPL/GPL/LGPL choice) | Linked |
| Liberation fonts, Carlito | Fonts shipped with OpenWrite | SIL Open Font License 1.1 | Shipped as files |
| Caladea | Font shipped with OpenWrite | Apache-2.0 | Shipped as files |
| DejaVu fonts | Font shipped with OpenWrite | DejaVu Fonts License (free) | Shipped as files |

**Reference only** (read for understanding; no code copied unless its
licence allows, and then with credit):

| Work | Licence | For |
| --- | --- | --- |
| fw2odf (github.com/hornc/fw2odf) | MIT | The Final Writer format |
| libwpd, libmwaw (Document Liberation Project) | MPL-2.0 / LGPL | WordPerfect and old word processor formats |
| The IFF specifications (EA IFF 85; New Horizons' FORM WORD) | Published specifications | IFF FTXT and ProWrite |
