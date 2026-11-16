/* The shared mail merge table (src/core/ow_merge.c). MIT, Copyright (c) 2026 AmiCradle Team. */
#include <stdio.h>
#include <string.h>
#include "ow_merge.h"

static int checks, failed;

#define CHECK(c) do { ++checks; if (!(c)) { ++failed; fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static ow_merge_table *table(const char *text)
{
    int memory = 0;
    ow_merge_table *t = ow_merge_table_from_text(text, strlen(text), &memory);
    CHECK(!memory);
    return t;
}

static int same(const char *a, const char *b) { return a && b && !strcmp(a, b); }

static void comma_and_quotes(void)
{
    ow_merge_table *t = table("Name,Town\r\n\"Smith, Jo\",\"Say \"\"hi\"\"\"\r\nAl,\"two\nlines\"\r\n");
    CHECK(t && t->ncols == 2 && t->nrows == 2);
    CHECK(same(ow_merge_value(t, 0, "Name"), "Smith, Jo"));
    CHECK(same(ow_merge_value(t, 0, "Town"), "Say \"hi\""));
    CHECK(same(ow_merge_value(t, 1, "Town"), "two\nlines"));
    CHECK(ow_merge_value(t, 2, "Name") == NULL);
    CHECK(ow_merge_value(t, 0, "Nope") == NULL);
    CHECK(ow_merge_value(t, -1, "Name") == NULL);
    ow_merge_table_free(t);
}

static void separators(void)
{
    ow_merge_table *t = table("a;b;c\n1;2,5;3\n");
    CHECK(t && t->ncols == 3 && t->nrows == 1);
    CHECK(same(ow_merge_value(t, 0, "b"), "2,5"));
    ow_merge_table_free(t);
    t = table("a\tb\n1\t2\n");
    CHECK(t && t->ncols == 2 && same(ow_merge_value(t, 0, "b"), "2"));
    ow_merge_table_free(t);
}

static void bom_and_latin1(void)
{
    ow_merge_table *t = table("\xEF\xBB\xBFName\nZo\xC3\xAB\n");
    CHECK(t && same(t->cells[0], "Name"));
    CHECK(same(ow_merge_value(t, 0, "Name"), "Zo\xC3\xAB"));
    ow_merge_table_free(t);
    t = table("Name\nZo\xEB\n");                     /* not UTF-8: read as Latin-1 */
    CHECK(t && same(ow_merge_value(t, 0, "Name"), "Zo\xC3\xAB"));
    ow_merge_table_free(t);
}

static void empty_rows_and_names(void)
{
    ow_merge_table *t = table("First,,Last \n\n,,\nA,B,C,D\nE\n");
    CHECK(t && t->ncols == 3 && t->nrows == 2);
    CHECK(same(t->cells[1], "Column2"));
    CHECK(same(t->cells[2], "Last"));             /* names are trimmed */
    CHECK(same(ow_merge_value(t, 0, "Column2"), "B"));
    CHECK(same(ow_merge_value(t, 0, "last"), "C")); /* case does not matter */
    CHECK(same(ow_merge_value(t, 1, "First"), "E"));
    CHECK(same(ow_merge_value(t, 1, "Last"), "")); /* a short row */
    CHECK(ow_merge_column(t, "LAST") == 2);
    ow_merge_table_free(t);
}

static void no_table(void)
{
    int memory = 1;
    CHECK(ow_merge_table_from_text("", 0, &memory) == NULL && !memory);
    CHECK(ow_merge_table_from_text("\n\n", 2, &memory) == NULL && !memory);
    CHECK(ow_merge_table_from_text(NULL, 0, &memory) == NULL && !memory);
    CHECK(ow_merge_column(NULL, "a") == -1);
    ow_merge_table_free(NULL);
}

static void names_only(void)
{
    ow_merge_table *t = table("a,b");
    CHECK(t && t->ncols == 2 && t->nrows == 0);
    CHECK(ow_merge_value(t, 0, "a") == NULL);
    ow_merge_table_free(t);
}

int main(void)
{
    comma_and_quotes();
    separators();
    bom_and_latin1();
    empty_rows_and_names();
    no_table();
    names_only();
    if (failed) { fprintf(stderr, "merge table: %d of %d checks failed\n", failed, checks); return 1; }
    printf("merge table tests passed (%d checks)\n", checks);
    return 0;
}
