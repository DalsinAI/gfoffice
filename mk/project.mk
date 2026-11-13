# GFOpenProject (amicradle-teams designs/GFOPENPROJECT-20261113.md): the plan model and its scheduler
# (libowf/project) and its files (.gfproj, MSPDI, CSV); host tests plain and under ASan and UBSan.
PROJECT_SRC = $(wildcard libowf/project/*.c)
PROJECT_DEPS = $(PROJECT_SRC) libowf/include/owf_project.h libowf/include/owf.h

$(OUT)/test-project: $(PROJECT_DEPS) $(OWF_SRC) tests/project/test_project.c
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(OWF_SRC) $(PROJECT_SRC) \
		tests/project/test_project.c -lz -o $@

$(OUT)/test-project-asan: $(PROJECT_DEPS) $(OWF_SRC) tests/project/test_project.c
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -DOWF_HAVE_ZLIB \
		-Ilibowf/include -Ilibowf/src $(OWF_SRC) $(PROJECT_SRC) tests/project/test_project.c -lz -o $@

$(OUT)/test-pfile: $(PROJECT_DEPS) $(OWF_SRC) tests/project/test_pfile.c
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(OWF_SRC) $(PROJECT_SRC) \
		tests/project/test_pfile.c -lz -o $@

$(OUT)/test-pfile-asan: $(PROJECT_DEPS) $(OWF_SRC) tests/project/test_pfile.c
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -DOWF_HAVE_ZLIB \
		-Ilibowf/include -Ilibowf/src $(OWF_SRC) $(PROJECT_SRC) tests/project/test_pfile.c -lz -o $@

project-test: $(OUT)/test-project $(OUT)/test-pfile
	$(OUT)/test-project
	$(OUT)/test-pfile

project-check: $(OUT)/test-project-asan $(OUT)/test-pfile-asan
	$(OUT)/test-project-asan
	$(OUT)/test-pfile-asan

test: project-test
check: project-check

.PHONY: project-test project-check

# Step 2: the editing core and the Gantt view (src/project), drawn through GFOpenLayout's drawing calls,
# so these run when GFOpenLayout is there (OPENLAYOUT_SRC), as the deck tests do.
OPJ_SRC = $(wildcard src/project/*.c)
OLDRAW_SRC = $(wildcard $(OPENLAYOUT_SRC)/draw/oldraw.c)
ifneq ($(OLDRAW_SRC),)
OPJ_DEPS = $(PROJECT_DEPS) $(OPJ_SRC) include/openproject_core.h tests/project/test_opj.c
OPJ_FLAGS = -Iinclude -Ilibowf/include -Ilibowf/src -I$(OPENLAYOUT_SRC)/include -I$(OPENLAYOUT_SRC)/draw

$(OUT)/test-opj: $(OPJ_DEPS) $(OWF_SRC)
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB $(OPJ_FLAGS) $(OWF_SRC) $(PROJECT_SRC) $(OPJ_SRC) $(OLDRAW_SRC) \
		tests/project/test_opj.c -lz -o $@

$(OUT)/test-opj-asan: $(OPJ_DEPS) $(OWF_SRC)
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -DOWF_HAVE_ZLIB \
		$(OPJ_FLAGS) $(OWF_SRC) $(PROJECT_SRC) $(OPJ_SRC) $(OLDRAW_SRC) tests/project/test_opj.c -lz -o $@

$(OUT)/test-opj-rexx: $(OPJ_DEPS) $(OWF_SRC) tests/project/test_opj_rexx.c
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB $(OPJ_FLAGS) $(OWF_SRC) $(PROJECT_SRC) $(OPJ_SRC) $(OLDRAW_SRC) \
		tests/project/test_opj_rexx.c -lz -o $@

$(OUT)/test-opj-rexx-asan: $(OPJ_DEPS) $(OWF_SRC) tests/project/test_opj_rexx.c
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -DOWF_HAVE_ZLIB \
		$(OPJ_FLAGS) $(OWF_SRC) $(PROJECT_SRC) $(OPJ_SRC) $(OLDRAW_SRC) tests/project/test_opj_rexx.c -lz -o $@

project-view-test: $(OUT)/test-opj $(OUT)/test-opj-rexx
	$(OUT)/test-opj
	$(OUT)/test-opj-rexx

project-view-check: $(OUT)/test-opj-asan $(OUT)/test-opj-rexx-asan
	$(OUT)/test-opj-asan
	$(OUT)/test-opj-rexx-asan

test: project-view-test
check: project-view-check

.PHONY: project-view-test project-view-check
endif
