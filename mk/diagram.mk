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

# Step D5: outlines (FreeMind .mm, OPML, Markdown).
$(OUT)/test-doutline: $(DIAG_DEPS) $(OWF_SRC) tests/diagram/test_doutline.c
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(OWF_SRC) $(DIAG_SRC) \
		tests/diagram/test_doutline.c -lz -o $@

$(OUT)/test-doutline-asan: $(DIAG_DEPS) $(OWF_SRC) tests/diagram/test_doutline.c
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -DOWF_HAVE_ZLIB \
		-Ilibowf/include -Ilibowf/src $(OWF_SRC) $(DIAG_SRC) tests/diagram/test_doutline.c -lz -o $@

# Step D6: PDF.
$(OUT)/test-dpdf: $(DIAG_DEPS) $(OWF_SRC) tests/diagram/test_dpdf.c
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(OWF_SRC) $(DIAG_SRC) \
		tests/diagram/test_dpdf.c -lz -o $@

$(OUT)/test-dpdf-asan: $(DIAG_DEPS) $(OWF_SRC) tests/diagram/test_dpdf.c
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -DOWF_HAVE_ZLIB \
		-Ilibowf/include -Ilibowf/src $(OWF_SRC) $(DIAG_SRC) tests/diagram/test_dpdf.c -lz -o $@

# Step D6: EPS.
$(OUT)/test-deps: $(DIAG_DEPS) $(OWF_SRC) tests/diagram/test_deps.c
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(OWF_SRC) $(DIAG_SRC) \
		tests/diagram/test_deps.c -lz -o $@

$(OUT)/test-deps-asan: $(DIAG_DEPS) $(OWF_SRC) tests/diagram/test_deps.c
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -DOWF_HAVE_ZLIB \
		-Ilibowf/include -Ilibowf/src $(OWF_SRC) $(DIAG_SRC) tests/diagram/test_deps.c -lz -o $@

# Step D6: Mermaid and DOT.
$(OUT)/test-dgraph: $(DIAG_DEPS) $(OWF_SRC) tests/diagram/test_dgraph.c
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(OWF_SRC) $(DIAG_SRC) \
		tests/diagram/test_dgraph.c -lz -o $@

$(OUT)/test-dgraph-asan: $(DIAG_DEPS) $(OWF_SRC) tests/diagram/test_dgraph.c
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -DOWF_HAVE_ZLIB \
		-Ilibowf/include -Ilibowf/src $(OWF_SRC) $(DIAG_SRC) tests/diagram/test_dgraph.c -lz -o $@

# Step D6: Excalidraw.
$(OUT)/test-dexcal: $(DIAG_DEPS) $(OWF_SRC) tests/diagram/test_dexcal.c
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(OWF_SRC) $(DIAG_SRC) \
		tests/diagram/test_dexcal.c -lz -o $@

$(OUT)/test-dexcal-asan: $(DIAG_DEPS) $(OWF_SRC) tests/diagram/test_dexcal.c
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -DOWF_HAVE_ZLIB \
		-Ilibowf/include -Ilibowf/src $(OWF_SRC) $(DIAG_SRC) tests/diagram/test_dexcal.c -lz -o $@

diagram-test: $(OUT)/test-diagram $(OUT)/test-diagram-nozlib $(OUT)/test-dexport $(OUT)/test-doutline $(OUT)/test-dpdf $(OUT)/test-deps $(OUT)/test-dgraph $(OUT)/test-dexcal
	$(OUT)/test-diagram
	$(OUT)/test-diagram-nozlib
	$(OUT)/test-dexport
	$(OUT)/test-doutline
	$(OUT)/test-dpdf
	$(OUT)/test-deps
	$(OUT)/test-dgraph
	$(OUT)/test-dexcal

diagram-check: $(OUT)/test-diagram-asan $(OUT)/test-dexport-asan $(OUT)/test-doutline-asan $(OUT)/test-dpdf-asan $(OUT)/test-deps-asan $(OUT)/test-dgraph-asan $(OUT)/test-dexcal-asan
	$(OUT)/test-diagram-asan
	$(OUT)/test-dexport-asan
	$(OUT)/test-doutline-asan
	$(OUT)/test-dpdf-asan
	$(OUT)/test-deps-asan
	$(OUT)/test-dgraph-asan
	$(OUT)/test-dexcal-asan

test: diagram-test
check: diagram-check

.PHONY: diagram-test diagram-check

# The shapes palette (src/diag/odg_stencil.c), in sets, each named back to draw.io by the shapes' outlines.
ODG_STENCIL_DEPS = $(DIAG_DEPS) src/diag/odg_stencil.c include/opendiag_stencil.h tests/diag/test_odg_stencil.c

$(OUT)/test-odg-stencil: $(ODG_STENCIL_DEPS) $(OWF_SRC)
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Iinclude -Ilibowf/include -Ilibowf/src $(OWF_SRC) $(DIAG_SRC) \
		src/diag/odg_stencil.c tests/diag/test_odg_stencil.c -lz -o $@

$(OUT)/test-odg-stencil-asan: $(ODG_STENCIL_DEPS) $(OWF_SRC)
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -DOWF_HAVE_ZLIB \
		-Iinclude -Ilibowf/include -Ilibowf/src $(OWF_SRC) $(DIAG_SRC) src/diag/odg_stencil.c \
		tests/diag/test_odg_stencil.c -lz -o $@

diagram-stencil-test: $(OUT)/test-odg-stencil
	$(OUT)/test-odg-stencil

diagram-stencil-check: $(OUT)/test-odg-stencil-asan
	$(OUT)/test-odg-stencil-asan

test: diagram-stencil-test
check: diagram-stencil-check

.PHONY: diagram-stencil-test diagram-stencil-check

# No state kept between calls in the diagram code a library is to hold (tests/diag/check_no_state.sh).
diagram-state-check:
	tests/diag/check_no_state.sh $(CC)

check: diagram-state-check

.PHONY: diagram-state-check

# The shapes' outlines (libowf/diagram/owf_dshape.c), shared by the canvas and the writers.
$(OUT)/test-dshape: $(DIAG_DEPS) $(OWF_SRC) tests/diagram/test_dshape.c
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(OWF_SRC) $(DIAG_SRC) \
		tests/diagram/test_dshape.c -lz -o $@

$(OUT)/test-dshape-asan: $(DIAG_DEPS) $(OWF_SRC) tests/diagram/test_dshape.c
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -DOWF_HAVE_ZLIB \
		-Ilibowf/include -Ilibowf/src $(OWF_SRC) $(DIAG_SRC) tests/diagram/test_dshape.c -lz -o $@

diagram-shape-test: $(OUT)/test-dshape
	$(OUT)/test-dshape

diagram-shape-check: $(OUT)/test-dshape-asan
	$(OUT)/test-dshape-asan

test: diagram-shape-test
check: diagram-shape-check

.PHONY: diagram-shape-test diagram-shape-check

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

ODG_ARRANGE_SRC = src/diag/odg_clip.c src/diag/odg_order.c src/diag/odg_align.c
ODG_ARRANGE_H = include/opendiag_clip.h include/opendiag_order.h include/opendiag_align.h
ODG_REXX_DEPS = $(ODG_DEPS) $(ODG_ARRANGE_SRC) $(ODG_ARRANGE_H) src/diag/odg_mind.c include/opendiag_mind.h src/diag/odg_rexx.c include/opendiag_rexx.h src/deck/ow_picture.c include/ow_picture.h tests/diag/test_odg_rexx.c

$(OUT)/test-odg-rexx: $(ODG_REXX_DEPS) $(OWF_SRC)
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Iinclude -Ilibowf/include -Ilibowf/src $(OWF_SRC) $(DIAG_SRC) \
		$(ODG_SRC) $(ODG_ARRANGE_SRC) src/diag/odg_mind.c src/diag/odg_rexx.c src/deck/ow_picture.c tests/diag/test_odg_rexx.c \
		-lz -o $@

$(OUT)/test-odg-rexx-asan: $(ODG_REXX_DEPS) $(OWF_SRC)
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -DOWF_HAVE_ZLIB \
		-Iinclude -Ilibowf/include -Ilibowf/src $(OWF_SRC) $(DIAG_SRC) $(ODG_SRC) $(ODG_ARRANGE_SRC) src/diag/odg_mind.c src/diag/odg_rexx.c \
		src/deck/ow_picture.c tests/diag/test_odg_rexx.c -lz -o $@

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

# The Arrange menu's Align and Distribute (src/diag/odg_align.c).
ODG_ALIGN_DEPS = $(ODG_DEPS) src/diag/odg_align.c include/opendiag_align.h tests/diag/test_odg_align.c

$(OUT)/test-odg-align: $(ODG_ALIGN_DEPS) $(OWF_SRC)
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Iinclude -Ilibowf/include -Ilibowf/src $(OWF_SRC) $(DIAG_SRC) \
		$(ODG_SRC) src/diag/odg_align.c tests/diag/test_odg_align.c -lz -o $@

$(OUT)/test-odg-align-asan: $(ODG_ALIGN_DEPS) $(OWF_SRC)
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -DOWF_HAVE_ZLIB \
		-Iinclude -Ilibowf/include -Ilibowf/src $(OWF_SRC) $(DIAG_SRC) $(ODG_SRC) src/diag/odg_align.c \
		tests/diag/test_odg_align.c -lz -o $@

diagram-align-test: $(OUT)/test-odg-align
	$(OUT)/test-odg-align

diagram-align-check: $(OUT)/test-odg-align-asan
	$(OUT)/test-odg-align-asan

test: diagram-align-test
check: diagram-align-check

.PHONY: diagram-align-test diagram-align-check

# Step D5: the mind-map mode (src/diag/odg_mind.c).
ODG_MIND_DEPS = $(ODG_DEPS) src/diag/odg_mind.c include/opendiag_mind.h tests/diag/test_odg_mind.c

$(OUT)/test-odg-mind: $(ODG_MIND_DEPS) $(OWF_SRC)
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Iinclude -Ilibowf/include -Ilibowf/src $(OWF_SRC) $(DIAG_SRC) \
		$(ODG_SRC) src/diag/odg_mind.c tests/diag/test_odg_mind.c -lz -o $@

$(OUT)/test-odg-mind-asan: $(ODG_MIND_DEPS) $(OWF_SRC)
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -DOWF_HAVE_ZLIB \
		-Iinclude -Ilibowf/include -Ilibowf/src $(OWF_SRC) $(DIAG_SRC) $(ODG_SRC) src/diag/odg_mind.c \
		tests/diag/test_odg_mind.c -lz -o $@

# The clipboard: Copy, Cut, Paste and Duplicate (src/diag/odg_clip.c).
ODG_CLIP_DEPS = $(ODG_DEPS) src/diag/odg_clip.c include/opendiag_clip.h tests/diag/test_odg_clip.c

$(OUT)/test-odg-clip: $(ODG_CLIP_DEPS) $(OWF_SRC)
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Iinclude -Ilibowf/include -Ilibowf/src $(OWF_SRC) $(DIAG_SRC) \
		$(ODG_SRC) src/diag/odg_clip.c tests/diag/test_odg_clip.c -lz -o $@

$(OUT)/test-odg-clip-asan: $(ODG_CLIP_DEPS) $(OWF_SRC)
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -DOWF_HAVE_ZLIB \
		-Iinclude -Ilibowf/include -Ilibowf/src $(OWF_SRC) $(DIAG_SRC) $(ODG_SRC) src/diag/odg_clip.c \
		tests/diag/test_odg_clip.c -lz -o $@

diagram-core-test: $(OUT)/test-odg $(OUT)/test-odg-rexx $(OUT)/test-odg-style $(OUT)/test-odg-mind $(OUT)/test-odg-clip
	$(OUT)/test-odg
	$(OUT)/test-odg-rexx
	$(OUT)/test-odg-style
	$(OUT)/test-odg-mind
	$(OUT)/test-odg-clip

diagram-core-check: $(OUT)/test-odg-asan $(OUT)/test-odg-rexx-asan $(OUT)/test-odg-style-asan $(OUT)/test-odg-mind-asan \
	$(OUT)/test-odg-clip-asan
	$(OUT)/test-odg-asan
	$(OUT)/test-odg-rexx-asan
	$(OUT)/test-odg-style-asan
	$(OUT)/test-odg-mind-asan
	$(OUT)/test-odg-clip-asan

test: diagram-core-test
check: diagram-core-check

.PHONY: diagram-core-test diagram-core-check

# The Arrange menu's drawing order and Group and Ungroup (src/diag/odg_order.c).
ODG_ORDER_DEPS = $(ODG_DEPS) src/diag/odg_order.c include/opendiag_order.h tests/diag/test_odg_order.c

$(OUT)/test-odg-order: $(ODG_ORDER_DEPS) $(OWF_SRC)
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Iinclude -Ilibowf/include -Ilibowf/src $(OWF_SRC) $(DIAG_SRC) \
		$(ODG_SRC) src/diag/odg_order.c tests/diag/test_odg_order.c -lz -o $@

$(OUT)/test-odg-order-asan: $(ODG_ORDER_DEPS) $(OWF_SRC)
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -DOWF_HAVE_ZLIB \
		-Iinclude -Ilibowf/include -Ilibowf/src $(OWF_SRC) $(DIAG_SRC) $(ODG_SRC) src/diag/odg_order.c \
		tests/diag/test_odg_order.c -lz -o $@

diagram-order-test: $(OUT)/test-odg-order
	$(OUT)/test-odg-order

diagram-order-check: $(OUT)/test-odg-order-asan
	$(OUT)/test-odg-order-asan

test: diagram-order-test
check: diagram-order-check

.PHONY: diagram-order-test diagram-order-check

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
