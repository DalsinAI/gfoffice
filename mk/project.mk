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
