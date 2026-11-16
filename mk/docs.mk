# README checks (tests/docs): the version README.md gives each program is the one its app/*.c names in VERSION_TEXT.
# Run on its own for now: not yet part of `make test`, so the version bumps already in review (Publisher 0.3.24 to
# 0.3.26) don't fail on a README line they never touched. Add it to test and check once those have merged.
.PHONY: docs-test
docs-test:
	python3 tests/docs/check_readme_versions.py --self-test

# GFOpenWrite's about box and $VER come from VERSION_TEXT, and VERSION_TEXT is not behind the source's 2.0.N tags.
.PHONY: write-version-check
write-version-check:
	python3 tests/docs/check_write_version.py --self-test
check: write-version-check
