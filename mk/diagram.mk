# GFOpenDiag (amicradle-teams designs/GFOPENDIAG-20261113.md), step D1: the diagram model and the .drawio
# file (libowf/diagram); host tests plain, with no zlib (libowf's own inflate, stored blocks), and under
# ASan and UBSan.
DIAG_SRC = $(wildcard libowf/diagram/*.c)
DIAG_DEPS = $(DIAG_SRC) $(wildcard libowf/diagram/*.h) libowf/include/owf_diagram.h libowf/include/owf.h \
	tests/diagram/test_diagram.c

$(OUT)/test-diagram: $(DIAG_DEPS) $(OWF_SRC)
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(OWF_SRC) $(DIAG_SRC) \
		tests/diagram/test_diagram.c -lz -o $@

$(OUT)/test-diagram-nozlib: $(DIAG_DEPS) $(OWF_SRC)
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -DOWF_TEST_NO_ZLIBRARY -Ilibowf/include -Ilibowf/src $(OWF_SRC) \
		$(DIAG_SRC) tests/diagram/test_diagram.c -lz -o $@

$(OUT)/test-diagram-asan: $(DIAG_DEPS) $(OWF_SRC)
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -DOWF_HAVE_ZLIB \
		-Ilibowf/include -Ilibowf/src $(OWF_SRC) $(DIAG_SRC) tests/diagram/test_diagram.c -lz -o $@

# Step D2: SVG and PNG with the diagram inside.
$(OUT)/test-dexport: $(DIAG_DEPS) $(OWF_SRC) tests/diagram/test_dexport.c
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(OWF_SRC) $(DIAG_SRC) \
		tests/diagram/test_dexport.c -lz -o $@

$(OUT)/test-dexport-asan: $(DIAG_DEPS) $(OWF_SRC) tests/diagram/test_dexport.c
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -DOWF_HAVE_ZLIB \
		-Ilibowf/include -Ilibowf/src $(OWF_SRC) $(DIAG_SRC) tests/diagram/test_dexport.c -lz -o $@

diagram-test: $(OUT)/test-diagram $(OUT)/test-diagram-nozlib $(OUT)/test-dexport
	$(OUT)/test-diagram
	$(OUT)/test-diagram-nozlib
	$(OUT)/test-dexport

diagram-check: $(OUT)/test-diagram-asan $(OUT)/test-dexport-asan
	$(OUT)/test-diagram-asan
	$(OUT)/test-dexport-asan

test: diagram-test
check: diagram-check

.PHONY: diagram-test diagram-check

# Step D3: the editing core (src/diag): operations with Undo, ports and glue, orthogonal routing, snap.
ODG_SRC = src/diag/odg_core.c
ODG_DEPS = $(DIAG_DEPS) $(ODG_SRC) include/opendiag_core.h tests/diag/test_odg.c

$(OUT)/test-odg: $(ODG_DEPS) $(OWF_SRC)
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Iinclude -Ilibowf/include -Ilibowf/src $(OWF_SRC) $(DIAG_SRC) \
		$(ODG_SRC) tests/diag/test_odg.c -lz -o $@

$(OUT)/test-odg-asan: $(ODG_DEPS) $(OWF_SRC)
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -DOWF_HAVE_ZLIB \
		-Iinclude -Ilibowf/include -Ilibowf/src $(OWF_SRC) $(DIAG_SRC) $(ODG_SRC) tests/diag/test_odg.c -lz -o $@

ODG_REXX_DEPS = $(ODG_DEPS) src/diag/odg_rexx.c include/opendiag_rexx.h tests/diag/test_odg_rexx.c

$(OUT)/test-odg-rexx: $(ODG_REXX_DEPS) $(OWF_SRC)
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Iinclude -Ilibowf/include -Ilibowf/src $(OWF_SRC) $(DIAG_SRC) \
		$(ODG_SRC) src/diag/odg_rexx.c tests/diag/test_odg_rexx.c -lz -o $@

$(OUT)/test-odg-rexx-asan: $(ODG_REXX_DEPS) $(OWF_SRC)
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -DOWF_HAVE_ZLIB \
		-Iinclude -Ilibowf/include -Ilibowf/src $(OWF_SRC) $(DIAG_SRC) $(ODG_SRC) src/diag/odg_rexx.c \
		tests/diag/test_odg_rexx.c -lz -o $@

# The Style window's sums (src/diag/odg_style.c).
ODG_STYLE_DEPS = $(ODG_DEPS) src/diag/odg_style.c include/opendiag_style.h tests/diag/test_odg_style.c

$(OUT)/test-odg-style: $(ODG_STYLE_DEPS) $(OWF_SRC)
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Iinclude -Ilibowf/include -Ilibowf/src $(OWF_SRC) $(DIAG_SRC) \
		$(ODG_SRC) src/diag/odg_style.c tests/diag/test_odg_style.c -lz -o $@

$(OUT)/test-odg-style-asan: $(ODG_STYLE_DEPS) $(OWF_SRC)
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -DOWF_HAVE_ZLIB \
		-Iinclude -Ilibowf/include -Ilibowf/src $(OWF_SRC) $(DIAG_SRC) $(ODG_SRC) src/diag/odg_style.c \
		tests/diag/test_odg_style.c -lz -o $@

diagram-core-test: $(OUT)/test-odg $(OUT)/test-odg-rexx $(OUT)/test-odg-style
	$(OUT)/test-odg
	$(OUT)/test-odg-rexx
	$(OUT)/test-odg-style

diagram-core-check: $(OUT)/test-odg-asan $(OUT)/test-odg-rexx-asan $(OUT)/test-odg-style-asan
	$(OUT)/test-odg-asan
	$(OUT)/test-odg-rexx-asan
	$(OUT)/test-odg-style-asan

test: diagram-core-test
check: diagram-core-check

.PHONY: diagram-core-test diagram-core-check

# Step D4: the canvas (src/diag/odg_view.c), drawn through GFOpenLayout's drawing calls, so it is tested when
# GFOpenLayout is there (OPENLAYOUT_SRC) with the lines, polygons and ellipses it draws with.
ODG_OLDRAW = $(wildcard $(OPENLAYOUT_SRC)/draw/oldraw.c)
ifneq ($(ODG_OLDRAW),)
ODG_VIEW_DEPS = $(ODG_DEPS) src/diag/odg_view.c include/opendiag_view.h tests/diag/test_odg_view.c
ODG_VIEW_FLAGS = -Iinclude -Ilibowf/include -Ilibowf/src -I$(OPENLAYOUT_SRC)/include -I$(OPENLAYOUT_SRC)/draw

$(OUT)/test-odg-view: $(ODG_VIEW_DEPS) $(OWF_SRC)
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB $(ODG_VIEW_FLAGS) $(OWF_SRC) $(DIAG_SRC) $(ODG_SRC) src/diag/odg_view.c \
		$(ODG_OLDRAW) tests/diag/test_odg_view.c -lz -o $@

$(OUT)/test-odg-view-asan: $(ODG_VIEW_DEPS) $(OWF_SRC)
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -DOWF_HAVE_ZLIB \
		$(ODG_VIEW_FLAGS) $(OWF_SRC) $(DIAG_SRC) $(ODG_SRC) src/diag/odg_view.c $(ODG_OLDRAW) tests/diag/test_odg_view.c \
		-lz -o $@

# The mouse on the canvas (src/diag/odg_edit.c), over the canvas's places.
ODG_EDIT_DEPS = $(ODG_VIEW_DEPS) src/diag/odg_edit.c include/opendiag_edit.h src/diag/odg_stencil.c \
	include/opendiag_stencil.h tests/diag/test_odg_edit.c

$(OUT)/test-odg-edit: $(ODG_EDIT_DEPS) $(OWF_SRC)
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB $(ODG_VIEW_FLAGS) $(OWF_SRC) $(DIAG_SRC) $(ODG_SRC) src/diag/odg_view.c \
		src/diag/odg_edit.c src/diag/odg_stencil.c $(ODG_OLDRAW) tests/diag/test_odg_edit.c -lz -o $@

$(OUT)/test-odg-edit-asan: $(ODG_EDIT_DEPS) $(OWF_SRC)
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -DOWF_HAVE_ZLIB \
		$(ODG_VIEW_FLAGS) $(OWF_SRC) $(DIAG_SRC) $(ODG_SRC) src/diag/odg_view.c src/diag/odg_edit.c src/diag/odg_stencil.c $(ODG_OLDRAW) \
		tests/diag/test_odg_edit.c -lz -o $@

diagram-view-test: $(OUT)/test-odg-view $(OUT)/test-odg-edit
	$(OUT)/test-odg-view
	$(OUT)/test-odg-edit

diagram-view-check: $(OUT)/test-odg-view-asan $(OUT)/test-odg-edit-asan
	$(OUT)/test-odg-view-asan
	$(OUT)/test-odg-edit-asan

test: diagram-view-test
check: diagram-view-check

.PHONY: diagram-view-test diagram-view-check
endif
