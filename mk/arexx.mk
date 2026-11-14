# ARexx test scripts (tests/arexx): no string runs into a B or X. ARexx reads 'x'b as a binary string and 'x'x as
# a hex string, so a'x'b stops a script with Error 8 on the Amiga, where nothing on the PC would see it.
.PHONY: rexx-test
rexx-test:
	python3 tests/arexx/check_rexx_tokens.py --self-test

test: rexx-test
check: rexx-test
