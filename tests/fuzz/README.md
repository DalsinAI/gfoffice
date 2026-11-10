# Fuzzing libowf on the host

Two harnesses read a file from memory, the way GFOpenWrite and GFOpenCalc
open one, then write it out again in every format that can be written:

- `fuzz_doc.c`: documents (Write's and Presentation's formats);
- `fuzz_book.c`: workbooks (Calc's formats).

`driver.c` runs them under gcc's AddressSanitizer and UBSan, no libFuzzer
needed. It reads each file named, and with `-m N SEED` also N random
mutations of it (bytes flipped, set, cut and copied). `-w DIR` keeps the
input being tried in `DIR/last`, so the one that crashed is there.

    make fuzz
    build/fuzz/fuzz_doc tests/docs/*.rtf
    build/fuzz/fuzz_book -w /tmp/w -m 200 1 tests/book/fixtures/book.xls

`seeds/` holds inputs that once crashed or hung a filter, to be added to
any corpus: `xls-past-column-iv.xls` has a cell at row 32513, column 65279,
far past Excel 97's column IV (the CSV save padded it to billions of
commas; the cell is now dropped with a note); `rtf-empty-idat.rtf` has a picture
whose PNG starts with an empty IDAT chunk (the PDF export copied 0 bytes
from a NULL buffer, which UBSan stops on).

A harness built with clang can also run under libFuzzer
(`-fsanitize=fuzzer`): both define `LLVMFuzzerTestOneInput`.
