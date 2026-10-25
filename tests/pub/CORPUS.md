# The .pub corpus (OpenPublisher, phase 0)

Sample Microsoft Publisher files for learning the format and testing the
reader (`libowf/src/in_pub.c`). Only the files were taken from these
repositories, never their code: each was cloned anonymously with
`git clone --depth 1 --filter=blob:none --sparse` and only the sample files
were checked out. Nothing here was opened in another program that reads
.pub.

"Version" is what our reader finds: the format's build mark and the version
of the program that wrote the file, from the file's own summary (see
`docs/gfoffice/PUB-FORMAT.md`, section 3). "Written by 12.0" means Publisher
2007 saved it, also when it saved it in an older format.

## Checked in (`tests/pub/samples`)

Small files whose licence allows it: Apache POI's test data (Apache-2.0,
`licenses/Apache-2.0.txt`, POI's notice in `samples/NOTICE-Apache-POI.txt`)
and one MIT file.

| File | Source | Licence | Version (written by) | Bytes | SHA-256 |
|---|---|---|---|---|---|
| `Simple.pub` | apache/poi `test-data/publisher/Simple.pub` | Apache-2.0 | 2007 format (12.0) | 65536 | `2606f530052a818c7949b88c9bf245dbc0a74ddc6b5a60004f37b387fe4b955d` |
| `Sample.pub` | apache/poi `test-data/publisher/Sample.pub` | Apache-2.0 | 2007 format (12.0) | 72192 | `6fefdef46b87c767150878dc384549cb2d2ec2ac54de25f8ddb3a5628301107e` |
| `Sample_2010.pub` | apache/poi `test-data/publisher/Sample_2010.pub` | Apache-2.0 | 2010 format (14.0) | 72704 | `f0a90aa566d72eb822eb48b4f378c33007610e35f3f1b999cdb88f2c9683858b` |
| `Sample98.pub` | apache/poi `test-data/publisher/Sample98.pub` | Apache-2.0 | 98 format (12.0) | 61440 | `8912f295ff0cd1f55b873b3b98a182218f33c420877e025d2f0fd795f12dd0c0` |
| `Sample2000.pub` | apache/poi `test-data/publisher/Sample2000.pub` | Apache-2.0 | 2000 format (12.0) | 61440 | `40701ca47b26d04771cdd58467764e9ab39d3da69b59fbc529afd16d263d2f86` |
| `51318.pub` | apache/poi `test-data/publisher/51318.pub` | Apache-2.0 | 2002/2003 format (11.9999) | 88576 | `3ab75a6a9196e0a51fc9b0aa759459501c71d313030c06652aabffbae0a2ab09` |
| `60685.pub` | apache/poi `test-data/publisher/60685.pub` | Apache-2.0 | 2013-or-later format (15.0) | 119296 | `0d3cecf0ff000a20931d0de3ebc48040bea10e03833ade3f9902f996c20e7184` |
| `clusterfuzz-testcase-minimized-POIHPBFFuzzer-4918886059278336.pub` | apache/poi `test-data/publisher/` | Apache-2.0 | damaged (a fuzzer's): 2010 marks | 32832 | `2641d2d48a345f0eb52b4dd0151330142e3037505fea614c272ec7eba4b3e9b0` |
| `clusterfuzz-testcase-minimized-POIHPBFFuzzer-6325615354773504.pub` | apache/poi `test-data/publisher/` | Apache-2.0 | damaged (a fuzzer's): not readable | 2042 | `e06b81d1a11a592d890e8d77c7f243c0529a36a0ec9bc4f18e8b1afb9e4f7bd2` |
| `mimetype-pub.pub` | gabriel-vasile/mimetype `testdata/pub.pub` | MIT (Copyright (c) 2018 Gabriel Vasile; `licenses/MIT-gabriel-vasile-mimetype.txt`) | cut short: the first 4096 bytes of a .pub | 4096 | `62f55a46d8443891c0f102599e64309cb4dbeda882c286ac216c1f028aecbb5f` |

Commits: apache/poi `4f3c25563919e7efa4a75feccf6cef1ac3d05a04` (24 Oct 2026),
gabriel-vasile/mimetype `7feb76447ef040104e1f283c376ec51f625a7a21` (19 Oct 2026).

## Listed, fetched by `tests/pub/fetch_samples.sh`

Larger files, or files whose licence for us is unclear. `fetch_samples.sh DIR`
puts them in DIR; `PUB_CORPUS=DIR make pub-test` then reads them too.

| File | Source | Licence | Version (written by) | Bytes | SHA-256 |
|---|---|---|---|---|---|
| `Sample2.pub` | apache/poi `test-data/publisher/` | Apache-2.0 | 2007 (12.0) | 72704 | `c87ea7cc5606023c5fa99376631bbd45a8e9bdaba34c958057995b0e7ac8cc86` |
| `Sample3.pub` | apache/poi | Apache-2.0 | 2007 (12.0) | 72192 | `424c69173ff08948c2529c8084b4ac2403f1ff1057146f4edd02fc29b44481fc` |
| `Sample4.pub` | apache/poi | Apache-2.0 | 2007 (12.0) | 72192 | `42195f7ad23d911219fea3ec88e66e867e9b9a6821a16dd1b535e2aa9d57a11b` |
| `Sample2_2010.pub` | apache/poi | Apache-2.0 | 2010 (14.0) | 73216 | `931ff20980b8f72aab227fa3cbe61b017be8d17c337fecb9aca3c97075940777` |
| `Sample3_2010.pub` | apache/poi | Apache-2.0 | 2010 (14.0) | 72704 | `baa9555a72d9e4264e3fd722896e2e43b960d20b4afdb96f75cb721a6133ff91` |
| `Sample4_2010.pub` | apache/poi | Apache-2.0 | 2010 (14.0) | 72704 | `544e2043f91aed664c0a09f9ae45da42291b6730f107f99f3cdf9462ed38fb02` |
| `SampleBrochure.pub` | apache/poi | Apache-2.0 | 2002/2003 (11.5703) | 161792 | `ffed034ac87e679f0bd08ff9cf74ad11c0e0e510a42b1bc1a7502415f6c29c87` |
| `SampleNewsletter.pub` | apache/poi | Apache-2.0 | 2002/2003 (11.5606) | 291840 | `6a825ba26ba35d6e885acdc62e859591ed37cb0ff7480b554b9cb362b644dfcf` |
| `LinkAt0And10.pub` | apache/poi | Apache-2.0 | 2007 (12.0) | 64512 | `c59b7160c21a531e5872e76141c4cd43a4d74f30d78401f64175467fa40c44fc` |
| `LinkAt10.pub` | apache/poi | Apache-2.0 | 2007 (12.0) | 64512 | `f1b7f65cb9646fddc2cfc77a795109bb958f629e4fbf7c9fc7a4d1b045074c35` |
| `LinkAt10And20And30.pub` | apache/poi | Apache-2.0 | 2007 (12.0) | 64512 | `202ac6f82e2a33ef0181158728b5c4df6781ccb105e012f520d331acafbcebf1` |
| `LinkAt10And20And30And40.pub` | apache/poi | Apache-2.0 | 2007 (12.0) | 64512 | `b54068d4f82dd0ee533f2a655a7edd2acbdc02619621b32fb23cb9ce842ed669` |
| `LinkAt10Longer.pub` | apache/poi | Apache-2.0 | 2007 (12.0) | 64512 | `a917a9e575f5cfc1ba8e78226981b662b06f96da2d7f2eb3e2b00a4b4f3dada5` |
| `LinkAt20.pub` | apache/poi | Apache-2.0 | 2007 (12.0) | 64512 | `cec79a2bd2f6bc71dd276bd6584c34d66fa6f710d9adbed951bd9cb32c5b2fed` |
| `clusterfuzz-testcase-minimized-POIHPBFFuzzer-4701121678278656.pub` | apache/poi | Apache-2.0 | damaged (a fuzzer's): 2007 marks | 331491 | `07b48fcd8a3b85a83b054ea9722352d5269591d2482fb710002794da49f48b26` |
| `testPUBLISHER.pub` | apache/tika `tika-parsers/tika-parsers-standard/tika-parsers-standard-modules/tika-parser-microsoft-module/src/test/resources/test-documents/` (commit `eb8d704c64fede93f6b8c10f2380c14f2a41304c`) | Apache-2.0 | the same bytes as POI's `Simple.pub` | 65536 | `2606f530052a818c7949b88c9bf245dbc0a74ddc6b5a60004f37b387fe4b955d` |
| `fdo59355-1.pub` | LibreOffice/core `writerperfect/qa/unit/data/draw/libmspub/pass/` (commit `7d2daf6ce9564281493a6b644b3a56cd08490e76`) | the repository is MPL-2.0; the file came from a bug report: unclear for us | an older format (no text storage): Publisher 97 or older, a hypothesis | 10752 | `79f9a8d99ed18faa174fa7200dbe91627015e2b77439a20421119abc128d62f7` |
| `EDB-22310-1.pub`, `EDB-22655-1.pub`, `EDB-29664-1.pub` | LibreOffice/core, the same folder | as above | not usable: stored scrambled (every one starts `99 9a c3 4c`, not a compound file), so its own test tools must unscramble them, and we don't read those | 577536, 577536, 89600 | `22ed7d33…6217c`, `7a0debfe…7cef6`, `ff968a8b…8732b` |
| `christmas.pub` | aspose-pub/Aspose.PUB-for-.NET `Examples/Data/` (commit `beee619f9a4b7e2c8908e0fa132f4f4650b08975`); the same bytes in aspose-pub/Aspose.PUB-for-Java `Examples/src/main/resources/` (commit `7209b3647a1f48eddd04598b84ca75774906c960`) | the repositories are MIT; the flyers look like Publisher's own templates, so unclear for us | 2013 or later (15.0) | 717312 | `550226a1c5d1d3bcb08b31863e60045f85b03061899b2f330217125e808d06d6` |
| `computer-classes-flyer.pub` | the same | as above | 2013 or later (15.0) | 211456 | `f934cdd537d42e0448052ad6617db089b6523db8f7f2cfb392b2679a1a691991` |
| `easter-egg-hunt-flyer.pub` | the same | as above | 2010 (14.0) | 212480 | `08e099914fd5deff723aa6e749bba35e9050c4171c1c2ed90e527a35eee360a5` |
| `florist-flyer.pub` | the same | as above | 2007 (12.0) | 1424384 | `7b7e132df247c306bffac0647cb282fb844811f4d65a35c9ba07fdcca46acd85` |
| `halloween-flyer.pub` | the same | as above | 2010 (14.0) | 306176 | `f079765650af152e1ae1fbfded757f679c588ceea884b77329a240c578884c25` |

POI's folder also has `Sample.txt`, `Sample2.txt`, `Sample3.txt` and
`Sample4.txt`. They look like another program's text output, so they were
not opened and are not used.

## Counts

- 34 distinct files found on GitHub (35 with Tika's copy of `Simple.pub`).
- 27 are whole, readable Publisher files: 3 in the 2002/2003 format, 12 in
  2007's, 6 in 2010's, 3 in 2013's or later, 1 each in the 98 and 2000
  formats (both saved by Publisher 2007), and 1 older file (Publisher 97 or
  older, a hypothesis).
- 3 are a fuzzer's damaged files, 1 is cut short, 3 are scrambled.
- No Publisher 2002 file (the summary would say 10.x) and no file from
  Publisher 2016 or later (16.x) was found: see "Needed" below.

## Not reachable from here: for Dale's PC

The session's shell reaches GitHub only. `fetch_samples.sh` also tries these,
for running on a PC; it downloads them as they are and extracts nothing.

- **.pub:** the attachments of freedesktop.org's bug 82362
  (https://bugs.freedesktop.org/show_bug.cgi?id=82362), through the bug
  tracker's REST interface. Download the files only; the bug's discussion is
  about another implementation and is not to be read for the format.
- **PageStream (Amiga):** Aminet's `text/dtp` packages `pgs22demo` (the
  PageStream 2.2 demo, https://aminet.net/package/text/dtp/pgs22demo),
  `pgs3`, `pgs3f` and `pgsmodam` (https://aminet.net/package/text/dtp/pgs3 and
  so on); demos usually carry sample documents. And the PageStream 3 beta's
  help archive on an Amiga cover CD, on discmaster.textfiles.com
  (`EnigmA AMIGA RUN 02 (1995)`, `earcd/patch/ps3b3.lha`).
  No PageStream document was found on GitHub.

## Needed

- **From Dale:** his .pub files, especially Publisher 2002 (XP), Publisher
  2016/2019/2021/365, files with linked text frames, catalogs or mail merge,
  and the PDF or a screenshot of each as Publisher shows it.
- **PageStream samples from Dale:** documents from PageStream 1-2 (`.pgs`
  era), 3, 4 and 5, with a picture of each page.
