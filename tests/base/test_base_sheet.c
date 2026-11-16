/*
 * GFOpenBase's sheets in and out on the PC (include/openbase_sheet.h): a
 * table, a view of it and a query written as .xlsx and .ods and read back by
 * libowf (types, styles, header), round trips into a fresh table and into a
 * new table guessed from the sheet, workbooks made here with a title above a
 * gap, dates, times, money, TRUE and FALSE, errors, formulas and UTF-8 text,
 * LibreOffice's own files, and every refusal in plain words. Against the
 * pinned SQLite and libowf (mk/base-sheet.mk).
 *
 *   test-base-sheet [DIR]   DIR: a scratch drawer (default: a new one in /tmp)
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "openbase_sheet.h"
#include "owf.h"

static int fails, checks;
static char dir[512];

#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)
#define CHECK_ERR(p, want) do { checks++; if (!strstr(ob_error(p), want)) { fails++; \
    printf("FAIL %s:%d: error \"%s\", want \"%s\"\n", __FILE__, __LINE__, ob_error(p), want); } } while (0)

static const char *path(const char *file)
{
    static char p[4][600];
    static int k;
    k = (k + 1) % 4;
    snprintf(p[k], sizeof p[k], "%s/%s", dir, file);
    return p[k];
}

static ob_project *fresh(const char *file)
{
    char err[OB_ERR_MAX];
    ob_project *p;
    remove(path(file));
    p = ob_open(path(file), OB_OPEN_CREATE, err, sizeof err);
    CHECK(p != NULL, "create %s: %s", file, err);
    return p;
}

static int row_cb(void *u, ob_int key, int n, const char *const *v)
{
    char *out = u;
    int i;
    size_t len = strlen(out);
    snprintf(out + len, 2048 - len, "%lld:", key);
    for (i = 0; i < n; i++) {
        len = strlen(out);
        snprintf(out + len, 2048 - len, "%s%s", i ? "|" : "", v[i] ? v[i] : "-");
    }
    len = strlen(out);
    snprintf(out + len, 2048 - len, ";");
    return 0;
}

static const char *all_rows(ob_project *p, const char *table)
{
    static char got[2048];
    got[0] = 0;
    if (ob_rows(p, table, 0, 50, row_cb, got) < 0) snprintf(got, sizeof got, "error: %s", ob_error(p));
    return got;
}

/* The table every test writes: a field of each kind a sheet holds. */
static void people(ob_project *p)
{
    static ob_table t;
    static const char *const f[] = { "Name", "Town", "Joined", "Starts", "Seen", "Member", "Credit", "Visits",
                                     "Height", "Notes" };
    static const char *const rows[][10] = {
        { "Ann", "Leeds", "2025-12-31", "09:30:00", "2026-11-16 14:05:09", "Yes", "1,500.00", "3", "1.62", "likes tea" },
        { "Bob", "York", "1999-02-28", "17:00:00", "", "No", "-5.25", "0", "", "" },
        { "Jos\xE9", "Hull", "", "", "2000-01-01 00:00:00", "Yes", "", "12", "0.1", "caf\xE9 \"au lait\"" },
    };
    ob_field *x;
    int i;
    ob_table_init(&t, "People");
    ob_table_add_field(&t, "Name", OB_TEXT)->required = 1;
    x = ob_table_add_field(&t, "Town", OB_CHOICE);
    strcpy(x->choices, "Leeds|York|Hull");
    ob_table_add_field(&t, "Joined", OB_DATE);
    ob_table_add_field(&t, "Starts", OB_TIME);
    ob_table_add_field(&t, "Seen", OB_DATETIME);
    ob_table_add_field(&t, "Member", OB_YESNO);
    ob_table_add_field(&t, "Credit", OB_CURRENCY);
    ob_table_add_field(&t, "Visits", OB_INTEGER);
    ob_table_add_field(&t, "Height", OB_NUMBER);
    ob_table_add_field(&t, "Notes", OB_MEMO);
    ob_table_add_field(&t, "Photo", OB_OBJECT);
    CHECK(ob_table_create(p, &t), "People: %s", ob_error(p));
    for (i = 0; i < 3; i++) CHECK(ob_row_add(p, "People", 10, f, rows[i], NULL), "row %d: %s", i, ob_error(p));
}

static owf_book *read_book(const char *file)
{
    owf_book *b = NULL;
    owf_report *notes = owf_report_new();
    int rc = owf_import_book_file(path(file), NULL, &b, notes, NULL);
    owf_report_free(notes);
    CHECK(rc == OWF_OK && b, "%s read back: %d", file, rc);
    return rc == OWF_OK ? b : NULL;
}

static const char *fmt_of(const owf_book *b, const owf_cell *c)
{
    int nf;
    if (!c || c->style <= 0 || c->style >= b->nstyles) return "General";
    nf = b->styles[c->style].number_format;
    return nf > 0 && nf < b->nnumfmts ? b->numfmts[nf] : "General";
}

static void check_written(const char *file)
{
    owf_book *b = read_book(file);
    const owf_sheet *s;
    const owf_cell *c;
    if (!b) return;
    CHECK(b->nsheets == 1 && !strcmp(b->sheets[0].name, "People"), "%s: one sheet People", file);
    s = &b->sheets[0];
    c = owf_sheet_find(s, 0, 0);
    CHECK(c && c->type == OWF_VALUE_TEXT && !strcmp(c->text, "id"), "%s: A1 id", file);
    CHECK(c && b->styles[c->style].font.flags & OWF_BOLD, "%s: the header is bold", file);
    c = owf_sheet_find(s, 0, 10);
    CHECK(c && !strcmp(c->text, "Notes"), "%s: K1 Notes (the Photo left out)", file);
    CHECK(!owf_sheet_find(s, 0, 11), "%s: no L1", file);
    CHECK(s->freeze_rows == 1, "%s: the header stays in view", file);
    c = owf_sheet_find(s, 1, 0);
    CHECK(c && c->type == OWF_VALUE_NUMBER && c->number == 1, "%s: A2 the Counter, a number", file);
    c = owf_sheet_find(s, 1, 3);
    CHECK(c && c->type == OWF_VALUE_NUMBER && c->number == 46022 && strstr(fmt_of(b, c), "yy"),
          "%s: D2 a date (%g, %s)", file, c ? c->number : -1, fmt_of(b, c));
    c = owf_sheet_find(s, 1, 4);
    CHECK(c && c->type == OWF_VALUE_NUMBER && fabs(c->number - 9.5 / 24) < 1e-9, "%s: E2 a time", file);
    c = owf_sheet_find(s, 1, 5);
    CHECK(c && c->type == OWF_VALUE_NUMBER && fabs(c->number - (46342 + (14 * 3600 + 5 * 60 + 9) / 86400.0)) < 1e-6,
          "%s: F2 a date and time (%.9f)", file, c ? c->number : -1);
    c = owf_sheet_find(s, 1, 6);
    CHECK(c && c->type == OWF_VALUE_BOOL && c->number == 1, "%s: G2 TRUE", file);
    c = owf_sheet_find(s, 2, 6);
    CHECK(c && c->type == OWF_VALUE_BOOL && c->number == 0, "%s: G3 FALSE", file);
    c = owf_sheet_find(s, 1, 7);
    CHECK(c && c->type == OWF_VALUE_NUMBER && c->number == 1500 && strstr(fmt_of(b, c), "0.00"),
          "%s: H2 money (%s)", file, fmt_of(b, c));
    c = owf_sheet_find(s, 2, 7);
    CHECK(c && c->number == -5.25, "%s: H3 -5.25", file);
    c = owf_sheet_find(s, 3, 9);
    CHECK(c && c->type == OWF_VALUE_NUMBER && c->number == 0.1, "%s: J4 0.1", file);
    c = owf_sheet_find(s, 3, 1);
    CHECK(c && c->type == OWF_VALUE_TEXT && !strcmp(c->text, "Jos\xC3\xA9"), "%s: B4 UTF-8", file);
    CHECK(!owf_sheet_find(s, 2, 5) || owf_sheet_find(s, 2, 5)->type == OWF_VALUE_EMPTY, "%s: F3 empty", file);
    CHECK(!owf_sheet_find(s, 4, 0), "%s: three rows", file);
    owf_book_free(b);
}

static void test_export(void)
{
    ob_project *p = fresh("out.oadb");
    ob_view v;
    ob_sheet_opts o;
    owf_book *b;
    if (!p) return;
    people(p);
    CHECK(ob_sheet_export(p, "People", NULL, path("people.xlsx"), NULL) == 3, "xlsx: %s", ob_error(p));
    check_written("people.xlsx");
    CHECK(ob_sheet_export(p, "People", NULL, path("people.ods"), NULL) == 3, "ods: %s", ob_error(p));
    check_written("people.ods");
    /* the format asked for, whatever the name; a view's rows, in its order */
    ob_sheet_opts_init(&o);
    o.format = "ods";
    strcpy(o.sheet_name, "Members: [A-Z]?");
    ob_view_init(&v);
    strcpy(v.sort, "Name");
    v.desc = 1;
    strcpy(v.field, "Member");
    v.op = OB_FILTER_EQUALS;
    strcpy(v.value, "Yes");
    CHECK(ob_sheet_export(p, "People", &v, path("members.dat"), &o) == 2, "view: %s", ob_error(p));
    if ((b = read_book("members.dat"))) {
        const owf_cell *c = owf_sheet_find(&b->sheets[0], 1, 1);
        CHECK(!strcmp(b->sheets[0].name, "Members A-Z"), "sheet name cleaned: %s", b->sheets[0].name);
        CHECK(c && !strcmp(c->text, "Jos\xC3\xA9"), "Z to A: Jos\xE9 first");
        CHECK(!owf_sheet_find(&b->sheets[0], 3, 0), "two rows");
        owf_book_free(b);
    }
    /* refusals */
    CHECK(ob_sheet_export(p, "People", NULL, path("people.csv"), NULL) == -1, "csv refused");
    CHECK_ERR(p, "sheets are written as .ods or .xlsx");
    CHECK(ob_sheet_export(p, "Nobody", NULL, path("x.xlsx"), NULL) == -1, "no table");
    CHECK(ob_error(p)[0], "says why: %s", ob_error(p));
    strcpy(v.field, "Shoe");
    CHECK(ob_sheet_export(p, "People", &v, path("x.xlsx"), NULL) == -1, "a bad view");
    CHECK(ob_sheet_export(p, "People", NULL, path("no/such/drawer/x.xlsx"), NULL) == -1, "can't write");
    CHECK_ERR(p, "could not be written");
    ob_close(p);
}

static void test_round_trip(void)
{
    ob_project *p = fresh("trip.oadb"), *q = fresh("trip2.oadb");
    static ob_table t, g;
    const char *exts[2] = { "trip.xlsx", "trip.ods" };
    char want[2048];
    int k, i;
    if (!p || !q) return;
    people(p);
    snprintf(want, sizeof want, "%s", all_rows(p, "People"));
    CHECK(ob_table_get(p, "People", &t), "design");
    CHECK(ob_table_create(q, &t), "the same design in a fresh project: %s", ob_error(q));
    for (k = 0; k < 2; k++) {
        ob_sheet_opts o;
        CHECK(ob_sheet_export(p, "People", NULL, path(exts[k]), NULL) == 3, "%s: %s", exts[k], ob_error(p));
        ob_sheet_opts_init(&o);
        CHECK(ob_sheet_import(q, "People", path(exts[k]), &o) == 3 && o.found_header == 1 && !strcmp(o.found_sheet, "People"),
              "%s in: %s", exts[k], ob_error(q));
        CHECK(!strcmp(all_rows(q, "People"), want), "%s: same rows and keys\n%s\nwant\n%s", exts[k], all_rows(q, "People"), want);
        /* a new table guessed from it */
        ob_sheet_opts_init(&o);
        CHECK(ob_sheet_guess(q, path(exts[k]), k ? "Guessed ODS" : "Guessed XLSX", &o, &g), "guess %s: %s", exts[k], ob_error(q));
        CHECK(g.nfields == 11, "%s: %d fields", exts[k], g.nfields);
        if (g.nfields == 11) {
            static const ob_type types[] = { OB_COUNTER, OB_TEXT, OB_TEXT, OB_DATE, OB_TIME, OB_DATETIME, OB_YESNO,
                                             OB_CURRENCY, OB_INTEGER, OB_NUMBER, OB_TEXT };
            for (i = 0; i < 11; i++)
                CHECK(g.fields[i].type == types[i], "%s: %s is %s, want %s", exts[k], g.fields[i].name,
                      ob_type_name(g.fields[i].type), ob_type_name(types[i]));
        }
        CHECK(ob_table_create(q, &g), "create: %s", ob_error(q));
        CHECK(ob_sheet_import(q, g.name, path(exts[k]), NULL) == 3, "into %s: %s", g.name, ob_error(q));
        CHECK(strstr(all_rows(q, g.name), "1:1|Ann|Leeds|2025-12-31|09:30:00|2026-11-16 14:05:09|Yes|1,500.00|3|1.62|likes tea;"),
              "%s guessed rows:\n%s", exts[k], all_rows(q, g.name));
        CHECK(ob_undo_count(q) >= 1 && !strcmp(ob_undo_name(q), ob_undo_name(q)), "on the Undo list");
        CHECK(ob_undo(q) && ob_row_total(q, g.name) == 0, "undone: %s", ob_error(q));
        /* all or none: the rows are there already (same keys) */
        CHECK(ob_sheet_import(q, "People", path(exts[k]), NULL) == -1, "%s twice refused", exts[k]);
        CHECK_ERR(q, "Row 2:");
        CHECK(ob_row_total(q, "People") == 3, "none added");
        /* clear People for the next format */
        for (i = 1; i <= 3; i++) ob_row_delete(q, "People", i);
    }
    ob_close(p);
    ob_close(q);
}

/* A workbook made here: a title, a gap, then a header in B3 and rows. */
static void made_book(const char *file)
{
    owf_book *b = owf_book_new();
    owf_sheet *s, *other;
    owf_cell *c;
    owf_cellstyle st;
    int date, time, money, dt, pct;
    owf_report *notes = owf_report_new();
    int rc;
    other = owf_book_add_sheet(b, "Notes");
    s = owf_book_add_sheet(b, "Stock \xE2\x82\xAC");
    (void)other;
    owf_cellstyle_init(&st);
    st.number_format = owf_book_numfmt(b, "dd/mm/yyyy");
    date = owf_book_style(b, &st);
    st.number_format = owf_book_numfmt(b, "h:mm AM/PM");
    time = owf_book_style(b, &st);
    st.number_format = owf_book_numfmt(b, "\"\xC2\xA3\"#,##0.00");
    money = owf_book_style(b, &st);
    st.number_format = owf_book_numfmt(b, "yyyy-mm-dd hh:mm");
    dt = owf_book_style(b, &st);
    st.number_format = owf_book_numfmt(b, "0%");
    pct = owf_book_style(b, &st);
    b->active = 1;
    owf_cell_set_text(owf_sheet_cell(s, 0, 1, 1), "Stock list", 10);
#define TXT(r, col, t) owf_cell_set_text(owf_sheet_cell(s, r, col, 1), t, strlen(t))
#define NUM(r, col, v, sty) do { c = owf_sheet_cell(s, r, col, 1); owf_cell_set_number(c, v); c->style = sty; } while (0)
    TXT(2, 1, "Item"); TXT(2, 2, "Bought"); TXT(2, 3, "Opens"); TXT(2, 4, "Price"); TXT(2, 5, "In stock");
    TXT(2, 6, "Count"); TXT(2, 7, "Margin"); TXT(2, 8, "Checked"); TXT(2, 9, "Code");
    TXT(3, 1, "Caf\xC3\xA9 cr\xC3\xA8me \xE2\x82\xAC"); NUM(3, 2, 46342, date); NUM(3, 3, 0.375, time);
    NUM(3, 4, 2.5, money); owf_cell_set_bool(owf_sheet_cell(s, 3, 5, 1), 1); NUM(3, 6, 12, 0);
    NUM(3, 7, 0.25, pct); NUM(3, 8, 46342.5, dt);
    c = owf_sheet_cell(s, 3, 9, 1); owf_cell_set_error(c, OWF_ERROR_NA);
    /* row 5 left empty */
    TXT(5, 1, "Scone"); NUM(5, 2, 36526, date); NUM(5, 3, 0.75, time); NUM(5, 4, 1234.5, money);
    owf_cell_set_bool(owf_sheet_cell(s, 5, 5, 1), 0);
    c = owf_sheet_cell(s, 5, 6, 1); owf_cell_set_number(c, 7); owf_cell_set_formula(c, "3+4");
    NUM(5, 7, 0.1, pct); TXT(5, 9, "S-1");
#undef TXT
#undef NUM
    rc = owf_export_book_file(b, path(file), NULL, notes);
    CHECK(rc == OWF_OK, "made %s: %d", file, rc);
    owf_report_free(notes);
    owf_book_free(b);
}

static void test_import(void)
{
    ob_project *p = fresh("in.oadb");
    static ob_table g;
    const char *files[2] = { "made.xlsx", "made.ods" };
    char names[4][OB_SHEET_NAME_MAX];
    int k;
    if (!p) return;
    for (k = 0; k < 2; k++) {
        ob_sheet_opts o;
        char tname[32];
        made_book(files[k]);
        CHECK(ob_sheet_list(p, path(files[k]), names, 4) == 2, "%s: two sheets: %s", files[k], ob_error(p));
        CHECK(!strcmp(names[0], "Notes") && !strcmp(names[1], "Stock ?"), "%s: names %s, %s", files[k], names[0], names[1]);
        /* the sheet the file opens at, named after it */
        ob_sheet_opts_init(&o);
        CHECK(ob_sheet_guess(p, path(files[k]), NULL, &o, &g), "%s guess: %s", files[k], ob_error(p));
        CHECK(!strcmp(o.found_sheet, "Stock ?") && o.found_header == 1, "%s: sheet %s, header %d", files[k], o.found_sheet, o.found_header);
        CHECK(!strcmp(g.name, "Stock"), "%s: table %s", files[k], g.name);
        CHECK(g.nfields == 10 && g.fields[2].type == OB_DATE && g.fields[3].type == OB_TIME && g.fields[4].type == OB_CURRENCY
              && g.fields[5].type == OB_YESNO && g.fields[6].type == OB_INTEGER && g.fields[7].type == OB_NUMBER
              && g.fields[8].type == OB_DATETIME && g.fields[9].type == OB_TEXT,
              "%s: %d fields: %s %s %s %s %s %s %s %s", files[k], g.nfields, ob_type_name(g.fields[2].type),
              ob_type_name(g.fields[3].type), ob_type_name(g.fields[4].type), ob_type_name(g.fields[5].type),
              ob_type_name(g.fields[6].type), ob_type_name(g.fields[7].type), ob_type_name(g.fields[8].type),
              ob_type_name(g.fields[9].type));
        snprintf(tname, sizeof tname, "Stock %d", k + 1);
        snprintf(g.name, sizeof g.name, "%s", tname);
        CHECK(ob_table_create(p, &g), "create: %s", ob_error(p));
        ob_sheet_opts_init(&o);
        strcpy(o.sheet_name, "stock ?");
        CHECK(ob_sheet_import(p, tname, path(files[k]), &o) == 2, "%s in: %s", files[k], ob_error(p));
        CHECK(!strcmp(all_rows(p, tname),
                      "1:1|Caf\xE9 cr\xE8me ?|2026-11-16|09:00:00|2.50|Yes|12|0.25|2026-11-16 12:00:00|#N/A;"
                      "2:2|Scone|2000-01-01|18:00:00|1,234.50|No|7|0.1|-|S-1;"),
              "%s rows:\n%s", files[k], all_rows(p, tname));
        /* by number: the first sheet is empty */
        ob_sheet_opts_init(&o);
        o.sheet = 0;
        CHECK(ob_sheet_import(p, tname, path(files[k]), &o) == -1, "%s: Notes is empty", files[k]);
        CHECK_ERR(p, "Notes is empty");
        o.sheet = 5;
        CHECK(ob_sheet_import(p, tname, path(files[k]), &o) == -1, "%s: no sheet 6", files[k]);
        CHECK_ERR(p, "has 2 sheets: there is no sheet 6");
        ob_sheet_opts_init(&o);
        strcpy(o.sheet_name, "Prices");
        CHECK(ob_sheet_import(p, tname, path(files[k]), &o) == -1, "%s: no Prices", files[k]);
        CHECK_ERR(p, "has no sheet named Prices");
        /* the sheet's own row numbers in messages, the good rows kept when asked */
        {
            static ob_table st;
            static const char *const heads[] = { "Item", "Bought", "Opens", "Price", "In stock", "Count", "Margin", "Checked" };
            int i;
            snprintf(tname, sizeof tname, "Strict %d", k + 1);
            ob_table_init(&st, tname);
            for (i = 0; i < 8; i++) ob_table_add_field(&st, heads[i], OB_TEXT);
            ob_table_add_field(&st, "Code", OB_INTEGER);
            CHECK(ob_table_create(p, &st), "strict: %s", ob_error(p));
            ob_sheet_opts_init(&o);
            CHECK(ob_sheet_import(p, tname, path(files[k]), &o) == -1, "%s: #N/A is not a number", files[k]);
            CHECK_ERR(p, "Row 4: Code must be");
            ob_sheet_opts_init(&o);
            o.skip_bad = 1;
            CHECK(ob_sheet_import(p, tname, path(files[k]), &o) == 0 && o.bad == 2, "%s skip: %s, %d bad", files[k],
                  ob_error(p), o.bad);
            CHECK(strstr(o.report, "Row 4: Code must be") && strstr(o.report, "Row 6: Code must be"), "rows 4 and 6:\n%s", o.report);
        }
    }
    /* not a workbook, not there */
    {
        FILE *f = fopen(path("words.xlsx"), "wb");
        if (f) { fputs("just some words\n", f); fclose(f); }
    }
    CHECK(ob_sheet_import(p, "Stock 1", path("words.xlsx"), NULL) == -1, "words");
    CHECK_ERR(p, "is not a workbook GFOpenBase reads");
    CHECK(ob_sheet_list(p, path("missing.ods"), names, 4) == -1, "missing");
    CHECK_ERR(p, "could not be opened");
    CHECK(ob_sheet_import(p, "Nobody", path("made.ods"), NULL) == -1, "no table");
    ob_close(p);
}

/* LibreOffice's, openpyxl's and odfpy's own files, as libowf's tests keep
 * them: the first sheet of each makes a table and fills it. */
static void test_other_programs(void)
{
    static const char *const files[] = { "book-lo.ods", "book-lo.xlsx", "book-openpyxl.xlsx", "book-odfpy.ods" };
    ob_project *p = fresh("others.oadb");
    static ob_table g;
    int k;
    if (!p) return;
    for (k = 0; k < 4; k++) {
        char file[256], tname[32];
        ob_sheet_opts o;
        ob_int n;
        snprintf(file, sizeof file, "tests/book/fixtures/%s", files[k]);
        ob_sheet_opts_init(&o);
        o.sheet = 0;
        if (!ob_sheet_guess(p, file, NULL, &o, &g)) { CHECK(0, "%s: %s", files[k], ob_error(p)); continue; }
        snprintf(tname, sizeof tname, "Other %d", k);
        snprintf(g.name, sizeof g.name, "%s", tname);
        CHECK(ob_table_create(p, &g), "%s create: %s", files[k], ob_error(p));
        o.skip_bad = 1;
        n = ob_sheet_import(p, tname, file, &o);
        /* a row wider than the header is listed, as a CSV line would be */
        /* rows wider than the headings are listed, as CSV lines would be (openpyxl's
         * test sheet has one heading over seven columns) */
        CHECK(n >= (k == 2 ? 0 : 1) && (o.bad == 0 || strstr(o.report, "values: the table takes")),
              "%s: %lld rows, %d bad: %s %s", files[k], n, o.bad, ob_error(p), o.report);
    }
    ob_close(p);
}

static void test_query_export(void)
{
    ob_project *p = fresh("q.oadb");
    static ob_query q;
    ob_qcolumn *c;
    const char *ans[1] = { "Yes" };
    owf_book *b;
    if (!p) return;
    people(p);
    ob_query_init(&q, "Members");
    ob_query_add_table(&q, "People");
    ob_query_add_column(&q, "People", "Name");
    ob_query_add_column(&q, "People", "Credit");
    c = ob_query_add_column(&q, NULL, NULL);
    strcpy(c->expr, "Visits * 2");
    strcpy(c->heading, "Twice");
    c = ob_query_add_column(&q, NULL, NULL);
    strcpy(c->expr, "Name || '!'");
    strcpy(c->heading, "Shout");
    c = ob_query_add_column(&q, "People", "Member");
    c->show = 0;
    strcpy(c->criteria[0], "[Member?]");
    CHECK(ob_query_sheet_export(p, &q, ans, path("members.xlsx"), NULL) == 2, "query: %s", ob_error(p));
    if ((b = read_book("members.xlsx"))) {
        const owf_sheet *s = &b->sheets[0];
        const owf_cell *x;
        CHECK(!strcmp(s->name, "Members"), "named after the query: %s", s->name);
        x = owf_sheet_find(s, 0, 2);
        CHECK(x && !strcmp(x->text, "Twice"), "the heading");
        x = owf_sheet_find(s, 1, 1);
        CHECK(x && x->type == OWF_VALUE_NUMBER && x->number == 1500, "money as a number");
        x = owf_sheet_find(s, 1, 2);
        CHECK(x && x->type == OWF_VALUE_NUMBER && x->number == 6, "an expression's number: %g", x ? x->number : -1);
        x = owf_sheet_find(s, 1, 3);
        CHECK(x && x->type == OWF_VALUE_TEXT && !strcmp(x->text, "Ann!"), "an expression's text");
        CHECK(!owf_sheet_find(s, 0, 4), "the hidden column left out");
        owf_book_free(b);
    }
    {
        const char *wrong[1] = { "perhaps" };
        remove(path("bad.xlsx"));
        CHECK(ob_query_sheet_export(p, &q, wrong, path("bad.xlsx"), NULL) == -1, "a bad answer");
        CHECK(access(path("bad.xlsx"), 0) != 0, "no file made");
    }
    q.name[0] = 0;
    CHECK(ob_query_sheet_export(p, &q, ans, path("anon.ods"), NULL) == 2, "an unsaved query: %s", ob_error(p));
    if ((b = read_book("anon.ods"))) {
        CHECK(!strcmp(b->sheets[0].name, "Query"), "called Query: %s", b->sheets[0].name);
        owf_book_free(b);
    }
    ob_close(p);
}

int main(int argc, char **argv)
{
    if (argc > 1) {
        snprintf(dir, sizeof dir, "%s", argv[1]);
    } else {
        char t[] = "/tmp/ob-sheet-XXXXXX";
        if (!mkdtemp(t)) { perror("mkdtemp"); return 1; }
        snprintf(dir, sizeof dir, "%s", t);
    }
    test_export();
    test_round_trip();
    test_import();
    test_other_programs();
    test_query_export();
    printf("%s: %d checks, %d failed\n", fails ? "FAILED" : "base sheets", checks, fails);
    return fails ? 1 : 0;
}
