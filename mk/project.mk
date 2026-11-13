# GFOpenProject (amicradle-teams designs/GFOPENPROJECT-20261113.md): the plan model and its scheduler
# (libowf/project), host tests plain and under ASan and UBSan.
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

project-test: $(OUT)/test-project
	$(OUT)/test-project

project-check: $(OUT)/test-project-asan
	$(OUT)/test-project-asan

test: project-test
check: project-check

.PHONY: project-test project-check
