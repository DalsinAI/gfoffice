# GFOffice's icons (icons/, drawn by tools/icons/gfoffice_icons_art.py: docs/ICONS.md): the icon files and
# pictures are all there and keep to AmiCradle's icon guide. Python's standard library only.
#   make icons-test / icons-check    tests/icons/test_icons.py

icons-test:
	python3 tests/icons/test_icons.py

icons-check: icons-test

test: icons-test
check: icons-check
.PHONY: icons-test icons-check
