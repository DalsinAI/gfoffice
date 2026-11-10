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

A harness built with clang can also run under libFuzzer
(`-fsanitize=fuzzer`): both define `LLVMFuzzerTestOneInput`.
