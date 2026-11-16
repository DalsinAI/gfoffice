/*
 * test_opr_spell: spelling through a deck (src/deck/opr_spell.c) with
 * GFOpenWrite's dictionary (src/core/ow_spell_dict.c):
 *
 *   - the words of a paragraph, as the wavy underline wants them: UTF-8
 *     letters kept whole, apostrophes at a word's ends left off, numbers,
 *     single letters and short capitals left alone, any case;
 *   - the next misspelt word through each slide's text boxes and shapes in
 *     order, then its notes, wrapping once; a word added to the user's list
 *     is no longer found; a line, a hidden box and a placeholder showing its
 *     prompt are not checked; a clean deck says so;
 *   - one object's marks for the wavy underline as you type, the word at
 *     the caret (still being typed) left out, the notes' too;
 *   - Ignore All: the word passes from then on and is not saved;
 *   - ARexx SPELL: COUNT, LIST, CHECK, SUGGEST, ADD and IGNORE, the
 *     answers and the return codes, LIST cut to its room.
 *
 * Prints what fails; exit 1 when anything did.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "openpresentation_core.h"
#include "ow_spell_dict.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures, checks;

#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

static const char *dict_path = "/tmp/test-opr-spell.words", *user_path = "/tmp/test-opr-spell.user";

static int add_text(owf_deck *d, int slide, owf_obj_kind kind, const char *text)
{
    owf_obj *o = owf_slide_add(&d->slides[slide], kind);
    int obj;
    if (!o) return -1;
    obj = d->slides[slide].nobjs - 1;
    if (kind == OWF_OBJ_SHAPE) o->shape = OWF_SHAPE_RECT;
    if (opr_text_set(d, slide, obj, text) < 0) return -1;
    return obj;
}

static void test_words(ow_spell *sp)
{
    opr_spell_range r[8];
    const char *t = "Helo wrld, it's 42 NASA x caf\xc3\xa9 'quoted' OpenWrite";
    int n = opr_spell_words(sp, t, strlen(t), r, 8);
    CHECK(n == 2, "two misspelt: %d", n);
    CHECK(n >= 2 && r[0].start == 0 && r[0].end == 4, "Helo at 0..4: %u..%u", (unsigned)r[0].start, (unsigned)r[0].end);
    CHECK(n >= 2 && r[1].start == 5 && r[1].end == 9, "wrld at 5..9: %u..%u", (unsigned)r[1].start, (unsigned)r[1].end);
    CHECK(opr_spell_words(sp, t, strlen(t), r, 1) == 2 && r[0].start == 0, "only one written, two counted");
    CHECK(opr_spell_words(sp, t, strlen(t), NULL, 0) == 2, "counted with no room");
    CHECK(opr_spell_words(sp, "hello world", 11, r, 8) == 0, "clean");
    CHECK(opr_spell_words(sp, "hello wrld", 5, r, 8) == 0, "length ends the text");
    CHECK(opr_spell_words(sp, "caf\xc3\xa8", 5, r, 8) == 1 && r[0].end == 5, "a UTF-8 word whole");
    CHECK(opr_spell_words(sp, "'tis", 4, r, 8) == 1 && r[0].start == 1, "leading apostrophe left off");
    CHECK(opr_spell_words(NULL, "wrld", 4, r, 8) == 0 && opr_spell_words(sp, NULL, 4, r, 8) == 0, "no checker or text");
    CHECK(ow_spell_word_ok(sp, "WORLD", 5) && ow_spell_word_ok(sp, "QWZ", 3) && !ow_spell_word_ok(sp, "QWZXYV", 6),
          "case and short capitals");
}

static void test_deck(ow_spell *sp)
{
    owf_deck *d = owf_deck_new(10000, 7500);
    opr_find_result f;
    opr_pos at = { 0, 0 };
    char word[16];
    int a, b, line, hidden, prompt, rc;
    owf_obj *o;
    if (!d || !owf_deck_add_slide(d, -1, -1) || !owf_deck_add_slide(d, -1, -1)) {
        CHECK(0, "deck");
        owf_deck_free(d);
        return;
    }
    a = add_text(d, 0, OWF_OBJ_TEXT, "hello wrld\nworld helo");
    line = add_text(d, 0, OWF_OBJ_SHAPE, "lnie");
    if (line >= 0) d->slides[0].objs[line].shape = OWF_SHAPE_LINE;
    hidden = add_text(d, 0, OWF_OBJ_TEXT, "hiden");
    if (hidden >= 0) d->slides[0].objs[hidden].flags |= OWF_OBJ_HIDDEN;
    prompt = add_text(d, 1, OWF_OBJ_TEXT, "Clik to add text");
    if (prompt >= 0) d->slides[1].objs[prompt].flags |= OWF_OBJ_EMPTY;
    b = add_text(d, 1, OWF_OBJ_SHAPE, "a shpae");
    CHECK(a == 0 && line == 1 && hidden == 2 && prompt == 0 && b == 1, "objects %d %d %d %d %d", a, line, hidden, prompt, b);
    CHECK(opr_notes_set(NULL, d, 0, "notse here") == 0, "notes");

    rc = opr_spell_next(d, sp, 0, 0, at, &f, word, sizeof word);
    CHECK(rc == 1 && f.slide == 0 && f.obj == 0 && f.selection.anchor.para == 0 && f.selection.anchor.offset == 6
          && f.selection.focus.offset == 10 && !strcmp(word, "wrld"), "wrld first: %d %d/%d %s", rc, f.slide, f.obj, word);
    at.para = 0; at.offset = 10;
    rc = opr_spell_next(d, sp, 0, 0, at, &f, word, sizeof word);
    CHECK(rc == 1 && f.obj == 0 && f.selection.anchor.para == 1 && f.selection.anchor.offset == 6 && !strcmp(word, "helo"),
          "then helo in the next paragraph: %s", word);
    at.para = 1; at.offset = 10;
    rc = opr_spell_next(d, sp, 0, 0, at, &f, word, sizeof word);
    CHECK(rc == 1 && f.slide == 0 && f.obj == OPR_NOTES && f.selection.anchor.offset == 0 && !strcmp(word, "notse"),
          "then the notes, past the line and the hidden box: %d %s", f.obj, word);
    at.para = 0; at.offset = 5;
    rc = opr_spell_next(d, sp, 0, OPR_NOTES, at, &f, word, sizeof word);
    CHECK(rc == 1 && f.slide == 1 && f.obj == 1 && f.selection.anchor.offset == 2 && !strcmp(word, "shpae"),
          "then slide 2's shape, past its prompt: %d/%d %s", f.slide, f.obj, word);
    at.para = 0; at.offset = 7;
    rc = opr_spell_next(d, sp, 1, 1, at, &f, word, 3);
    CHECK(rc == 1 && f.slide == 0 && f.obj == 0 && f.selection.anchor.offset == 6 && !strcmp(word, "wr"),
          "wraps to the start; the word cut to its room: %s", word);
    at.para = 0; at.offset = 0;
    rc = opr_spell_next(d, sp, 5, 3, at, &f, NULL, 0);
    CHECK(rc == 1 && f.slide == 0 && f.selection.anchor.offset == 6, "a start off the deck: the first slide");

    CHECK(ow_spell_add(sp, "wrld") && ow_spell_add(sp, "helo") && ow_spell_add(sp, "notse"), "added");
    at.para = 0; at.offset = 0;
    rc = opr_spell_next(d, sp, 0, 0, at, &f, word, sizeof word);
    CHECK(rc == 1 && f.slide == 1 && !strcmp(word, "shpae"), "added words pass: %s", word);
    CHECK(opr_text_set(d, 1, 1, "a shape") == 0, "fixed");
    rc = opr_spell_next(d, sp, 0, 0, at, &f, word, sizeof word);
    CHECK(rc == 0, "clean deck: %d %s", rc, word);
    o = &d->slides[1].objs[0];
    o->flags &= ~(unsigned)OWF_OBJ_EMPTY;
    rc = opr_spell_next(d, sp, 0, 0, at, &f, word, sizeof word);
    CHECK(rc == 1 && f.slide == 1 && f.obj == 0 && !strcmp(word, "Clik"), "typed-in text is checked: %s", word);
    CHECK(opr_spell_next(NULL, sp, 0, 0, at, &f, word, sizeof word) == -1 && opr_spell_next(d, NULL, 0, 0, at, &f, NULL, 0) == -1
          && opr_spell_next(d, sp, 0, 0, at, NULL, NULL, 0) == -1, "bad input");
    owf_deck_free(d);
}

static void test_marks(ow_spell *sp)
{
    owf_deck *d = owf_deck_new(10000, 7500);
    opr_spell_mark m[4];
    opr_pos caret;
    int obj, line, n;
    if (!d || !owf_deck_add_slide(d, -1, -1)) {
        CHECK(0, "deck");
        owf_deck_free(d);
        return;
    }
    obj = add_text(d, 0, OWF_OBJ_TEXT, "hellp world\nwrlds and mroe");
    line = add_text(d, 0, OWF_OBJ_SHAPE, "lnie");
    if (line >= 0) d->slides[0].objs[line].shape = OWF_SHAPE_LINE;
    n = opr_spell_marks(d, sp, 0, obj, NULL, m, 4);
    CHECK(n == 3 && m[0].para == 0 && m[0].start == 0 && m[0].end == 5 && m[1].para == 1 && m[1].start == 0
          && m[2].para == 1 && m[2].start == 10 && m[2].end == 14, "three marks: %d", n);
    caret.para = 1; caret.offset = 14;
    n = opr_spell_marks(d, sp, 0, obj, &caret, m, 4);
    CHECK(n == 2 && m[1].para == 1 && m[1].start == 0, "the word just typed left out: %d", n);
    caret.para = 0; caret.offset = 2;
    n = opr_spell_marks(d, sp, 0, obj, &caret, m, 4);
    CHECK(n == 2 && m[0].para == 1, "the caret in a word leaves it out: %d", n);
    caret.para = 1; caret.offset = 7;
    CHECK(opr_spell_marks(d, sp, 0, obj, &caret, m, 4) == 3, "the caret between words leaves all in");
    CHECK(opr_spell_marks(d, sp, 0, obj, NULL, m, 1) == 3 && m[0].start == 0, "one written, three counted");
    CHECK(opr_spell_marks(d, sp, 0, obj, NULL, NULL, 0) == 3, "counted with no room");
    CHECK(opr_spell_marks(d, sp, 0, line, NULL, m, 4) == 0, "a line has none");
    CHECK(opr_notes_set(NULL, d, 0, "speker notes") == 0, "notes");
    n = opr_spell_marks(d, sp, 0, OPR_NOTES, NULL, m, 4);
    CHECK(n == 1 && m[0].start == 0 && m[0].end == 6, "the notes' marks: %d", n);
    CHECK(ow_spell_ignore(sp, "MROE") && opr_spell_marks(d, sp, 0, obj, NULL, m, 4) == 2, "ignored in any case");
    {
        FILE *u = fopen(user_path, "r");
        char buf[256];
        size_t got = u ? fread(buf, 1, sizeof buf - 1, u) : 0;
        if (u) fclose(u);
        buf[got] = 0;
        CHECK(!strstr(buf, "mroe") && strstr(buf, "wrld"), "ignored words are not saved, added ones are: %s", buf);
    }
    CHECK(!ow_spell_ignore(sp, "") && !ow_spell_ignore(NULL, "x"), "nothing to ignore");
    CHECK(ow_spell_add(sp, "mroe"), "an ignored word added");
    {
        FILE *u = fopen(user_path, "r");
        char buf[256];
        size_t got = u ? fread(buf, 1, sizeof buf - 1, u) : 0;
        if (u) fclose(u);
        buf[got] = 0;
        CHECK(strstr(buf, "mroe\n") != NULL, "an ignored word, added, is saved: %s", buf);
    }
    d->slides[0].objs[obj].flags |= OWF_OBJ_EMPTY;
    CHECK(opr_spell_marks(d, sp, 0, obj, NULL, m, 4) == 0, "a prompt has none");
    CHECK(opr_spell_marks(d, sp, 0, 9, NULL, m, 4) == -1 && opr_spell_marks(d, sp, 3, 0, NULL, m, 4) == -1
          && opr_spell_marks(d, NULL, 0, 0, NULL, m, 4) == -1 && opr_spell_marks(NULL, sp, 0, 0, NULL, m, 4) == -1,
          "bad input");
    owf_deck_free(d);
}

static void test_rexx(ow_spell *sp)
{
    owf_deck *d = owf_deck_new(10000, 7500);
    char out[256], small[20];
    if (!d || !owf_deck_add_slide(d, -1, -1) || !owf_deck_add_slide(d, -1, -1)) {
        CHECK(0, "deck");
        owf_deck_free(d);
        return;
    }
    add_text(d, 0, OWF_OBJ_TEXT, "hello qzxa");
    add_text(d, 1, OWF_OBJ_TEXT, "world");
    add_text(d, 1, OWF_OBJ_SHAPE, "a shape qzxb");
    opr_notes_set(NULL, d, 1, "qzxc here");
    CHECK(opr_spell_rexx(d, sp, "", out, sizeof out) == 0 && !strcmp(out, "3"), "SPELL: %s", out);
    CHECK(opr_spell_rexx(d, sp, "  count", out, sizeof out) == 0 && !strcmp(out, "3"), "SPELL COUNT: %s", out);
    CHECK(opr_spell_rexx(d, sp, "LIST", out, sizeof out) == 0 && !strcmp(out, "1 1 qzxa\n2 2 qzxb\n2 NOTES qzxc"),
          "SPELL LIST: %s", out);
    CHECK(opr_spell_rexx(d, sp, "LIST", small, sizeof small) == 0 && !strcmp(small, "1 1 qzxa\n2 2 qzxb"),
          "LIST cut to its room: %s", small);
    CHECK(opr_spell_rexx(d, sp, "CHECK World", out, sizeof out) == 0 && !strcmp(out, "1"), "CHECK World: %s", out);
    CHECK(opr_spell_rexx(d, sp, "check \"qzxa\"", out, sizeof out) == 0 && !strcmp(out, "0"), "CHECK qzxa: %s", out);
    CHECK(opr_spell_rexx(d, sp, "SUGGEST wordd", out, sizeof out) == 0 && !strcmp(out, "world"),
          "SUGGEST wordd: %s", out);
    CHECK(opr_spell_rexx(d, sp, "SUGGEST zzzzzzzz", out, sizeof out) == 0 && !out[0], "no suggestions: %s", out);
    CHECK(opr_spell_rexx(d, sp, "IGNORE qzxa", out, sizeof out) == 0 && opr_spell_rexx(d, sp, "", out, sizeof out) == 0
          && !strcmp(out, "2"), "IGNORE: %s", out);
    CHECK(opr_spell_rexx(d, sp, "ADD qzxb", out, sizeof out) == 0 && opr_spell_rexx(d, sp, "COUNT", out, sizeof out) == 0
          && !strcmp(out, "1"), "ADD: %s", out);
    CHECK(opr_spell_rexx(d, sp, "CHECK", out, sizeof out) == 10 && strstr(out, "CHECK needs a word"), "a word needed: %s", out);
    CHECK(opr_spell_rexx(d, sp, "FIX qzxc", out, sizeof out) == 10 && strstr(out, "SPELL takes"), "unknown: %s", out);
    CHECK(opr_spell_rexx(d, NULL, "", out, sizeof out) == 5, "no dictionary");
    CHECK(opr_spell_rexx(NULL, sp, "COUNT", out, sizeof out) == 10 && opr_spell_rexx(NULL, sp, "CHECK hello", out, sizeof out) == 0,
          "no deck: counting refused, checking a word still works");
    CHECK(opr_spell_rexx(d, sp, "", NULL, 0) == 10, "no room");
    owf_deck_free(d);
}

int main(void)
{
    FILE *f = fopen(dict_path, "w");
    ow_spell *sp;
    char status[128];
    if (!f) return 1;
    fputs("hello\nworld\nit's\nopenwrite\nquoted\ncaf\xc3\xa9\nhere\nand\nnotes\na\nshape\nto\nadd\ntext\nclick\n", f);
    fclose(f);
    remove(user_path);
    sp = ow_spell_open(dict_path, user_path, status, sizeof status);
    CHECK(sp != NULL, "open: %s", status);
    if (sp) {
        test_words(sp);
        test_deck(sp);
        test_marks(sp);
        test_rexx(sp);
        ow_spell_close(sp);
    }
    remove(dict_path);
    remove(user_path);
    printf("test-opr-spell: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
