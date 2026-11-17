/*
 * GFOffice's one spell checker (libowf/src/owf_spell.c, owfspell.library)
 * on its own: word lists, word rules, scanning, suggestions, the user's
 * words, Ignore All, and one list shared by two openers (two programs on
 * the library). With an argument, that word list (Dictionaries/en_GB.words)
 * is loaded and checked too.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "owf_spell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;
#define CHECK(c, what)                                                    \
    do {                                                                  \
        if (!(c)) {                                                       \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, what);          \
            failures++;                                                   \
        }                                                                 \
    } while (0)

static char dict_path[256], user_path[256];

static void write_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(2); }
    fputs(text, f);
    fclose(f);
}

static char *read_all(const char *path)
{
    static char buf[4096];
    FILE *f = fopen(path, "rb");
    size_t n;
    buf[0] = 0;
    if (!f) return buf;
    n = fread(buf, 1, sizeof buf - 1, f);
    buf[n] = 0;
    fclose(f);
    return buf;
}

/* Not sorted, with CRLF, Hunspell flags, a count line, spaces and capitals:
 * all of them as owf_spell_open takes them. */
static const char *const LIST =
    "6\r\n"
    "world/M\r\n"
    "  Hello\r\n"
    "word\n"
    "\n"
    "amiga/S\n"
    "colour\n"
    "can't\n"
    "zebra   \n"
    "apple";

static void test_open(void)
{
    char status[128];
    owf_spell *s;
    CHECK(!owf_spell_open("/nonexistent/none.words", NULL, status, sizeof status), "a missing list");
    CHECK(strstr(status, "Dictionary not found: /nonexistent/none.words") != NULL, "says which");
    CHECK(!owf_spell_open(NULL, NULL, status, sizeof status) && !owf_spell_open("", NULL, NULL, 0), "no path");
    s = owf_spell_open(dict_path, NULL, status, sizeof status);
    CHECK(s != NULL, "opened");
    CHECK(owf_spell_count(s) == 8, "eight words, the count line and the blank line left out");
    CHECK(!strcmp(status, "8 dictionary words loaded."), "the status line");
    CHECK(owf_spell_count(NULL) == 0, "no dictionary, no words");
    owf_spell_close(s);
    owf_spell_close(NULL);
    /* no PROGDIR: on the PC: none of the default lists is there */
    CHECK(!owf_spell_open_default(status, sizeof status) && strstr(status, "No dictionary found") != NULL,
          "the default lists missing");
}

static void test_rules(owf_spell *s)
{
    CHECK(owf_spell_word_ok(s, "hello", 5) && owf_spell_word_ok(s, "Hello", 5) && owf_spell_word_ok(s, "HELLO", 5),
          "any case");
    CHECK(owf_spell_word_ok(s, "world", 5) && owf_spell_word_ok(s, "amiga", 5), "flags cut off");
    CHECK(owf_spell_word_ok(s, "zebra", 5) && owf_spell_word_ok(s, "apple", 5), "trailing spaces, last line");
    CHECK(owf_spell_word_ok(s, "can't", 5), "an apostrophe inside");
    CHECK(!owf_spell_word_ok(s, "wrold", 5), "a misspelt word");
    CHECK(owf_spell_word_ok(s, "x", 1) && owf_spell_word_ok(s, "abc1", 4), "one letter, digits");
    CHECK(owf_spell_word_ok(s, "QWZXY", 5) && !owf_spell_word_ok(s, "QWZXYV", 6), "capitals of five or fewer");
    CHECK(owf_spell_word_ok(s, "", 0) && owf_spell_word_ok(s, NULL, 3), "nothing to check");
    CHECK(owf_spell_word_ok(s, "6", 1), "a number");
}

static void test_scan(owf_spell *s)
{
    const char *t = "Hello wrold, 'colour' 'wrod' cant 'apple's";
    size_t a = 99, b = 99, n = strlen(t);
    owf_spell_range r[2];
    CHECK(owf_spell_scan(s, t, 0, n, &a, &b) && a == 6 && b == 11, "wrold found");
    CHECK(owf_spell_scan(s, t, b, n, &a, &b) && !strncmp(t + a, "wrod", 4) && b - a == 4,
          "the quotes left off");
    CHECK(owf_spell_scan(s, t, b, n, &a, &b) && !strncmp(t + a, "cant", 4), "cant");
    CHECK(owf_spell_scan(s, t, b, n, &a, &b) && !strncmp(t + a, "apple's", 7), "apple's is not apple");
    CHECK(!owf_spell_scan(s, t, b, n, &a, &b), "no more");
    CHECK(!owf_spell_scan(s, t, 0, 5, &a, &b), "the range is kept to");
    CHECK(!owf_spell_scan(NULL, t, 0, n, &a, &b) && !owf_spell_scan(s, NULL, 0, n, &a, &b), "bad input");
    CHECK(owf_spell_words(s, t, n, r, 2) == 4, "four in all");
    CHECK(r[0].start == 6 && r[0].end == 11 && !strncmp(t + r[1].start, "wrod", 4), "the first two written");
    CHECK(owf_spell_words(s, t, n, NULL, 0) == 4, "counted with nowhere to write");
    CHECK(owf_spell_words(s, "caf\xc3\xa9 hello", 11, r, 2) == 1 && r[0].end == 5, "UTF-8 letters stay whole");
}

static void test_suggest(owf_spell *s)
{
    char sug[4][OWF_SPELL_WORD];
    int n = owf_spell_suggest(s, "wrold", sug[0], 4);
    CHECK(n == 2 && !strcmp(sug[0], "word") && !strcmp(sug[1], "world"), "two one edit away");
    n = owf_spell_suggest(s, "Wrold", sug[0], 1);
    CHECK(n == 1 && !strcmp(sug[0], "word"), "any case, max kept");
    CHECK(owf_spell_suggest(s, "qqqqq", sug[0], 4) == 0, "nothing near");
    CHECK(owf_spell_suggest(s, "wrold", sug[0], 0) == 0 && owf_spell_suggest(s, "wrold", NULL, 4) == 0, "nowhere");
}

static void test_user(void)
{
    char status[128];
    owf_spell *a, *b;
    remove(user_path);
    write_file(user_path, "Kickstart\r\nworkbench");   /* no newline at the end */
    a = owf_spell_open(dict_path, user_path, status, sizeof status);
    b = owf_spell_open(dict_path, user_path, status, sizeof status);
    CHECK(a && b, "two openers");
    CHECK(owf_spell_word_ok(a, "kickstart", 9) && owf_spell_word_ok(b, "Workbench", 9), "the user's words read");
    CHECK(!owf_spell_word_ok(a, "agnus", 5), "not yet");
    CHECK(owf_spell_add(a, "Agnus"), "added");
    CHECK(owf_spell_word_ok(a, "agnus", 5), "known at once");
    CHECK(owf_spell_add(b, "paula"), "the other adds one");
    CHECK(!strcmp(read_all(user_path), "Kickstart\r\nworkbench\nagnus\npaula\n"),
          "both saved, the file's own lines kept");
    CHECK(owf_spell_add(a, "agnus") && owf_spell_add(a, "hello") &&
              !strcmp(read_all(user_path), "Kickstart\r\nworkbench\nagnus\npaula\n"),
          "a known word is not saved again");
    CHECK(owf_spell_ignore(a, "Denise") && owf_spell_word_ok(a, "DENISE", 6), "ignored in any case");
    CHECK(!owf_spell_word_ok(b, "denise", 6), "only by the one that ignored it");
    CHECK(!strstr(read_all(user_path), "denise"), "an ignored word is not saved");
    CHECK(owf_spell_add(a, "denise") && strstr(read_all(user_path), "denise\n"), "an ignored word added is saved");
    CHECK(!owf_spell_add(a, "") && !owf_spell_add(NULL, "x") && !owf_spell_ignore(a, "") &&
              !owf_spell_ignore(NULL, "x"),
          "nothing to add or ignore");
    /* the shared list outlives the first closer */
    owf_spell_close(a);
    CHECK(owf_spell_word_ok(b, "world", 5) && owf_spell_count(b) == 8, "still there for the other");
    owf_spell_close(b);
    a = owf_spell_open(dict_path, user_path, NULL, 0);
    CHECK(a && owf_spell_word_ok(a, "paula", 5) && owf_spell_word_ok(a, "denise", 6), "read back");
    owf_spell_close(a);
    /* no user list: words added are kept in memory only */
    a = owf_spell_open(dict_path, NULL, NULL, 0);
    CHECK(a && owf_spell_add(a, "lisa") && owf_spell_word_ok(a, "lisa", 4), "in memory");
    owf_spell_close(a);
}

static void test_command(owf_spell *s)
{
    char out[64];
    CHECK(owf_spell_command(s, "COUNT", out, sizeof out) == -1 && owf_spell_command(s, "", out, sizeof out) == -1,
          "not a word command");
    CHECK(owf_spell_command(s, "check world", out, sizeof out) == 0 && !strcmp(out, "1"), "CHECK, any case");
    CHECK(owf_spell_command(s, "CHECK wrold", out, sizeof out) == 0 && !strcmp(out, "0"), "CHECK a misspelt word");
    CHECK(owf_spell_command(s, "SUGGEST wrold", out, sizeof out) == 0 && !strcmp(out, "word world"), "SUGGEST");
    CHECK(owf_spell_command(s, "SUGGEST wrold", out, 8) == 0 && !strcmp(out, "word"), "SUGGEST cut to fit");
    CHECK(owf_spell_command(s, "IGNORE \"Zorg\"", out, sizeof out) == 0 && owf_spell_word_ok(s, "zorg", 4),
          "IGNORE, quoted");
    CHECK(owf_spell_command(s, "ADD", out, sizeof out) == 10 && strstr(out, "needs a word"), "ADD needs a word");
    CHECK(owf_spell_command(NULL, "CHECK x", out, sizeof out) == 5 && !strcmp(out, "No dictionary"), "no dictionary");
    CHECK(owf_spell_command(s, "CHECKS x", out, sizeof out) == -1, "a longer word is not CHECK");
}

static void test_real(const char *path)
{
    char status[128], sug[8][OWF_SPELL_WORD];
    owf_spell *s = owf_spell_open(path, NULL, status, sizeof status);
    int n, i, found = 0;
    CHECK(s != NULL, "the shipped list opened");
    if (!s) return;
    CHECK(owf_spell_count(s) > 100000, "over 100,000 words");
    CHECK(owf_spell_word_ok(s, "colour", 6) && owf_spell_word_ok(s, "organisation", 12) &&
              owf_spell_word_ok(s, "aa's", 4),
          "British spellings");
    CHECK(!owf_spell_word_ok(s, "colur", 5), "colur is misspelt");
    n = owf_spell_suggest(s, "colur", sug[0], 8);
    for (i = 0; i < n; ++i) found |= !strcmp(sug[i], "colour");
    CHECK(found, "colour suggested");
    owf_spell_close(s);
}

int main(int argc, char **argv)
{
    owf_spell *s;
    snprintf(dict_path, sizeof dict_path, "/tmp/owf-spell-%ld.words", (long)getpid());
    snprintf(user_path, sizeof user_path, "/tmp/owf-spell-user-%ld.words", (long)getpid());
    write_file(dict_path, LIST);
    test_open();
    s = owf_spell_open(dict_path, NULL, NULL, 0);
    CHECK(s != NULL, "opened for the rules");
    if (s) {
        test_rules(s);
        test_scan(s);
        test_suggest(s);
        test_command(s);
        owf_spell_close(s);
    }
    test_user();
    if (argc > 1) test_real(argv[1]);
    remove(dict_path);
    remove(user_path);
    if (failures) {
        printf("owf_spell: %d failed\n", failures);
        return 1;
    }
    puts("owf_spell tests passed");
    return 0;
}
