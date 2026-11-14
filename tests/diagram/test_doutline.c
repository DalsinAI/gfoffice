/*
 * The outlines of libowf/diagram/owf_doutline.c on the PC: FreeMind .mm, OPML and Markdown read (by hand-written
 * files in the shapes those programs write), written, and read back the same; the kind guessed; one root made;
 * damaged and empty files. Run under ASan and UBSan by make diagram-check.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include <string.h>
#include "owf_diagram.h"

static int fails, checks;

#define CHECK(cond) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_N(got, want) do { long g_ = (long)(got), w_ = (long)(want); checks++; if (g_ != w_) { fails++; \
    printf("FAIL %s:%d: %s is %ld, want %ld\n", __FILE__, __LINE__, #got, g_, w_); } } while (0)
#define CHECK_S(got, want) do { const char *g_ = (got), *w_ = (want); checks++; if (!g_ || strcmp(g_, w_) != 0) { \
    fails++; printf("FAIL %s:%d: %s is \"%s\", want \"%s\"\n", __FILE__, __LINE__, #got, g_ ? g_ : "(null)", w_); } } while (0)

/* The items as "depth:text" lines, folded ones with a *, for checking a tree in one go. */
static const char *shape(const owf_ditem *it, int n)
{
    static char out[2000];
    size_t at = 0;
    int i;
    out[0] = 0;
    for (i = 0; i < n && at < sizeof out - 100; i++)
        at += (size_t)snprintf(out + at, sizeof out - at, "%s%d:%s%s", i ? " | " : "", it[i].depth, it[i].text,
                               it[i].folded ? "*" : "");
    return out;
}

static int read_s(const char *text, owf_outline_kind k, const char *title, owf_ditem **it, int *n)
{
    return owf_outline_read(text, strlen(text), k, title, it, n);
}

static void test_mm(void)
{
    static const char mm[] =
        "<map version=\"1.0.1\">\n"
        "<!-- To view this file, download free mind mapping software FreeMind -->\n"
        "<node CREATED=\"1\" ID=\"ID_1\" MODIFIED=\"1\" TEXT=\"Holiday\">\n"
        "<node ID=\"ID_2\" POSITION=\"right\" TEXT=\"Travel &amp; tickets\" FOLDED=\"true\">\n"
        "<node ID=\"ID_3\" TEXT=\"Train\"/>\n"
        "<icon BUILTIN=\"idea\"/>\n"
        "</node>\n"
        "<node ID=\"ID_4\" POSITION=\"left\" TEXT=\"Caf&#xe9;s\"/>\n"
        "<node ID=\"ID_5\" POSITION=\"left\"><richcontent TYPE=\"NODE\"><html><head></head><body>"
        "<p>Rich</p><p>  text</p></body></html></richcontent>\n"
        "<richcontent TYPE=\"NOTE\"><html><body><p>a note</p></body></html></richcontent></node>\n"
        "</node>\n"
        "</map>\n";
    owf_ditem *it;
    int n;
    char *out;
    size_t len;
    CHECK_N(read_s(mm, OWF_OUTLINE_MM, NULL, &it, &n), OWF_OK);
    CHECK_N(n, 5);
    CHECK_S(shape(it, n), "0:Holiday | 1:Travel & tickets* | 2:Train | 1:Caf\xc3\xa9s | 1:Rich text");
    CHECK_N(owf_outline_write(it, n, OWF_OUTLINE_MM, NULL, &out, &len), OWF_OK);
    CHECK_N(len, strlen(out));
    CHECK_S(out, "<map version=\"1.0.1\">\n"
                 "<node TEXT=\"Holiday\">\n"
                 " <node TEXT=\"Travel &amp; tickets\" FOLDED=\"true\">\n"
                 "  <node TEXT=\"Train\"/>\n"
                 " </node>\n"
                 " <node TEXT=\"Caf\xc3\xa9s\"/>\n"
                 " <node TEXT=\"Rich text\"/>\n"
                 "</node>\n"
                 "</map>\n");
    {                                                     /* read back: the same tree */
        owf_ditem *back;
        int m;
        CHECK_N(read_s(out, OWF_OUTLINE_MM, NULL, &back, &m), OWF_OK);
        CHECK_S(shape(back, m), "0:Holiday | 1:Travel & tickets* | 2:Train | 1:Caf\xc3\xa9s | 1:Rich text");
        owf_outline_free(back, m);
    }
    owf_free(out);
    owf_outline_free(it, n);

    CHECK_N(read_s("<map version=\"1.0.1\"></map>", OWF_OUTLINE_MM, NULL, &it, &n), OWF_ERR_FORMAT);
    CHECK(it == NULL && n == 0);
    CHECK_N(read_s("<opml><body><outline text=\"x\"/></body></opml>", OWF_OUTLINE_MM, NULL, &it, &n), OWF_ERR_FORMAT);
    CHECK_N(read_s("<map><node TEXT=\"x\">", OWF_OUTLINE_MM, NULL, &it, &n), OWF_ERR_CORRUPT);
    CHECK_N(read_s("<map><node TEXT=\"a\"/><node TEXT=\"b\"/></map>", OWF_OUTLINE_MM, "Two", &it, &n), OWF_OK);
    CHECK_S(shape(it, n), "0:Two | 1:a | 1:b");
    owf_outline_free(it, n);
    {                                                     /* a line break in a node: kept, written as a reference */
        static const char br[] = "<map><node TEXT=\"one&#xa;two\"/></map>";
        CHECK_N(read_s(br, OWF_OUTLINE_MM, NULL, &it, &n), OWF_OK);
        CHECK_S(it[0].text, "one\ntwo");
        CHECK_N(owf_outline_write(it, n, OWF_OUTLINE_MM, NULL, &out, NULL), OWF_OK);
        CHECK(strstr(out, "TEXT=\"one&#xa;two\"") != NULL);
        owf_free(out);
        owf_outline_free(it, n);
    }
}

static void test_opml(void)
{
    static const char opml[] =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<opml version=\"2.0\">\n"
        "  <head><title>Week &lt;plan&gt;</title><expansionState>0</expansionState></head>\n"
        "  <body>\n"
        "    <outline text=\"Monday\">\n"
        "      <outline text=\"Shop\" _note=\"milk\"/>\n"
        "      <outline title=\"Gym\"/>\n"
        "    </outline>\n"
        "    <outline text=\"Tuesday\"/>\n"
        "  </body>\n"
        "</opml>\n";
    owf_ditem *it;
    int n;
    char *out;
    CHECK_N(read_s(opml, OWF_OUTLINE_OPML, NULL, &it, &n), OWF_OK);
    CHECK_S(shape(it, n), "0:Week <plan> | 1:Monday | 2:Shop | 2:Gym | 1:Tuesday");   /* two at the top */
    CHECK_N(owf_outline_write(it, n, OWF_OUTLINE_OPML, NULL, &out, NULL), OWF_OK);
    CHECK_S(out, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<opml version=\"2.0\">\n<head>\n"
                 "<title>Week &lt;plan&gt;</title>\n</head>\n<body>\n"
                 "<outline text=\"Week &lt;plan&gt;\">\n"
                 " <outline text=\"Monday\">\n"
                 "  <outline text=\"Shop\"/>\n"
                 "  <outline text=\"Gym\"/>\n"
                 " </outline>\n"
                 " <outline text=\"Tuesday\"/>\n"
                 "</outline>\n"
                 "</body>\n</opml>\n");
    {
        owf_ditem *back;
        int m;
        CHECK_N(read_s(out, OWF_OUTLINE_OPML, NULL, &back, &m), OWF_OK);
        CHECK_S(shape(back, m), "0:Week <plan> | 1:Monday | 2:Shop | 2:Gym | 1:Tuesday");
        owf_outline_free(back, m);
    }
    owf_free(out);
    CHECK_N(owf_outline_write(it, n, OWF_OUTLINE_OPML, "Mine", &out, NULL), OWF_OK);
    CHECK(strstr(out, "<title>Mine</title>") != NULL);
    owf_free(out);
    owf_outline_free(it, n);
    CHECK_N(read_s("<opml><head/><body/></opml>", OWF_OUTLINE_OPML, NULL, &it, &n), OWF_ERR_FORMAT);
    CHECK_N(read_s("<opml><body><outline text=\"a\"/><outline text=\"b\"/></body></opml>", OWF_OUTLINE_OPML, NULL,
                   &it, &n), OWF_OK);
    CHECK_S(shape(it, n), "0:Outline | 1:a | 1:b");      /* no title */
    owf_outline_free(it, n);
}

static void test_md(void)
{
    static const char md[] =
        "# Garden\n"
        "\n"
        "Some words that are not part of the tree.\n"
        "## Front\n"
        "- Roses\n"
        "  - Red\n"
        "  - White ##\n"
        "- Lawn\n"
        "## Back ##\n"
        "1. Shed\n"
        "\t* Tools\n"
        "    + Spade\n"
        "2) Pond\n"
        "#hashtag is not a heading\n"
        "-not an item\n";
    owf_ditem *it;
    int n;
    char *out;
    CHECK_N(read_s(md, OWF_OUTLINE_MD, NULL, &it, &n), OWF_OK);
    CHECK_S(shape(it, n), "0:Garden | 1:Front | 2:Roses | 3:Red | 3:White ## | 2:Lawn | 1:Back | 2:Shed | 3:Tools | "
                          "3:Spade | 2:Pond");
    CHECK_N(owf_outline_write(it, n, OWF_OUTLINE_MD, NULL, &out, NULL), OWF_OK);
    CHECK_S(out, "# Garden\n\n- Front\n  - Roses\n    - Red\n    - White ##\n  - Lawn\n- Back\n  - Shed\n"
                 "    - Tools\n    - Spade\n  - Pond\n");
    {                                                     /* read back: the same tree */
        owf_ditem *back;
        int m;
        CHECK_N(read_s(out, OWF_OUTLINE_MD, NULL, &back, &m), OWF_OK);
        CHECK_S(shape(back, m), shape(it, n));
        owf_outline_free(back, m);
    }
    owf_free(out);
    owf_outline_free(it, n);

    CHECK_N(read_s("- one\n- two\n  - deeper\n", OWF_OUTLINE_MD, "List", &it, &n), OWF_OK);
    CHECK_S(shape(it, n), "0:List | 1:one | 1:two | 2:deeper");   /* no heading: a root on top */
    owf_outline_free(it, n);
    CHECK_N(read_s("### Deep first\n- x\n", OWF_OUTLINE_MD, NULL, &it, &n), OWF_OK);
    CHECK_S(shape(it, n), "0:Deep first | 1:x");
    owf_outline_free(it, n);
    /* A closing run of # goes only with a space before it (Hollister's #200 note): "C#" keeps its #. */
    CHECK_N(read_s("# C#\n## Title ##\n## #\n## Bug # \n## x\t#\n- F#\n", OWF_OUTLINE_MD, NULL, &it, &n), OWF_OK);
    CHECK_S(shape(it, n), "0:C# | 1:Title | 1: | 1:Bug | 1:x | 2:F#");
    owf_outline_free(it, n);
    {                                                     /* a root ending in # round-trips (Budgie: "C #" too) */
        static const char *const roots[3] = { "C#", "C #", "##" };
        int k;
        for (k = 0; k < 3; k++) {
            owf_ditem one[1];
            char c[8];
            strcpy(c, roots[k]);
            one[0].depth = 0, one[0].text = c, one[0].folded = 0;
            CHECK_N(owf_outline_write(one, 1, OWF_OUTLINE_MD, NULL, &out, NULL), OWF_OK);
            CHECK_N(read_s(out, OWF_OUTLINE_MD, NULL, &it, &n), OWF_OK);
            CHECK_N(n, 1);
            if (n == 1)
                CHECK_S(it[0].text, roots[k]);
            owf_outline_free(it, n);
            owf_free(out);
        }
    }
    CHECK_N(read_s("# A\r\n- b\r\n", OWF_OUTLINE_MD, NULL, &it, &n), OWF_OK);   /* DOS line ends */
    CHECK_S(shape(it, n), "0:A | 1:b");
    owf_outline_free(it, n);
    CHECK_N(read_s("just words\n\n", OWF_OUTLINE_MD, NULL, &it, &n), OWF_ERR_FORMAT);
    CHECK_N(read_s("", OWF_OUTLINE_MD, NULL, &it, &n), OWF_ERR_FORMAT);
    {                                                     /* a line break in a node's text is a space in Markdown */
        owf_ditem one[2];
        char a[] = "two\nlines", b[] = "x";
        one[0].depth = 0, one[0].text = a, one[0].folded = 0;
        one[1].depth = 1, one[1].text = b, one[1].folded = 1;
        CHECK_N(owf_outline_write(one, 2, OWF_OUTLINE_MD, NULL, &out, NULL), OWF_OK);
        CHECK_S(out, "# two lines\n\n- x\n");
        owf_free(out);
        CHECK_N(owf_outline_write(one, 2, OWF_OUTLINE_MM, NULL, &out, NULL), OWF_OK);
        CHECK(strstr(out, "FOLDED") == NULL);             /* a leaf has nothing to fold */
        owf_free(out);
    }
}

static void test_deep_and_bad_lists(void)
{
    char buf[16000];
    size_t at = 0;
    int i, n;
    owf_ditem *it, bad[2];
    char *out = NULL;
    char t[] = "t";
    for (i = 0; i < 80; i++)                              /* deeper than 60: kept at 60 */
        at += (size_t)snprintf(buf + at, sizeof buf - at, "%*s- n%d\n", i * 2, "", i);
    CHECK_N(read_s(buf, OWF_OUTLINE_MD, "Deep", &it, &n), OWF_OK);
    CHECK_N(n, 80);
    CHECK_S(it[0].text, "n0");                            /* one at the top: it is the root */
    CHECK_N(it[79].depth, 60);
    CHECK_N(it[60].depth, 60);
    CHECK_N(it[59].depth, 59);
    {
        static const owf_outline_kind kinds[2] = { OWF_OUTLINE_MM, OWF_OUTLINE_OPML };
        int k;
        for (k = 0; k < 2; k++) {                         /* the deepest written reads back */
            owf_ditem *back;
            int m;
            CHECK_N(owf_outline_write(it, n, kinds[k], NULL, &out, NULL), OWF_OK);
            CHECK_N(read_s(out, kinds[k], NULL, &back, &m), OWF_OK);
            CHECK_N(m, 80);
            if (m == 80)
                CHECK_N(back[79].depth, 60);
            owf_outline_free(back, m);
            owf_free(out);
        }
    }
    owf_outline_free(it, n);
    {                                                     /* Budgie: 70 deep written is put at 60 and reads back */
        static owf_ditem deep[70];
        static const owf_outline_kind kinds[3] = { OWF_OUTLINE_MM, OWF_OUTLINE_OPML, OWF_OUTLINE_MD };
        int k;
        for (i = 0; i < 70; i++)
            deep[i].depth = i, deep[i].text = t, deep[i].folded = 1;
        for (k = 0; k < 3; k++) {
            owf_ditem *back;
            int m;
            CHECK_N(owf_outline_write(deep, 70, kinds[k], NULL, &out, NULL), OWF_OK);
            CHECK_N(read_s(out, kinds[k], NULL, &back, &m), OWF_OK);
            CHECK_N(m, 70);
            if (m == 70)
                CHECK_N(back[69].depth, 60);
            owf_outline_free(back, m);
            owf_free(out);
        }
    }
    out = NULL;

    bad[0].depth = 1, bad[0].text = t, bad[0].folded = 0;     /* not a tree from one root */
    bad[1].depth = 0, bad[1].text = t, bad[1].folded = 0;
    CHECK_N(owf_outline_write(bad, 2, OWF_OUTLINE_MM, NULL, &out, NULL), OWF_ERR_FORMAT);
    bad[0].depth = 0, bad[1].depth = 2;
    CHECK_N(owf_outline_write(bad, 2, OWF_OUTLINE_OPML, NULL, &out, NULL), OWF_ERR_FORMAT);
    bad[1].depth = 0;
    CHECK_N(owf_outline_write(bad, 2, OWF_OUTLINE_MD, NULL, &out, NULL), OWF_ERR_FORMAT);
    CHECK_N(owf_outline_write(bad, 0, OWF_OUTLINE_MD, NULL, &out, NULL), OWF_ERR_FORMAT);
    CHECK(out == NULL);
}

static void test_guess(void)
{
    CHECK_N(owf_outline_guess("Plan.MM", NULL, 0), OWF_OUTLINE_MM);
    CHECK_N(owf_outline_guess("work:feeds.opml", NULL, 0), OWF_OUTLINE_OPML);
    CHECK_N(owf_outline_guess("notes.markdown", NULL, 0), OWF_OUTLINE_MD);
    CHECK_N(owf_outline_guess("notes.md", "<map>", 5), OWF_OUTLINE_MD);   /* the name wins */
    CHECK_N(owf_outline_guess("x", "  <map version=\"1.0.1\">", 24), OWF_OUTLINE_MM);
    CHECK_N(owf_outline_guess(NULL, "\xEF\xBB\xBF<?xml version=\"1.0\"?>\n<opml version=\"2.0\">", 44), OWF_OUTLINE_OPML);
    CHECK_N(owf_outline_guess(NULL, "<?xml version=\"1.0\"?><map>", 26), OWF_OUTLINE_MM);
    CHECK_N(owf_outline_guess("x", "# Title", 7), OWF_OUTLINE_MD);
    CHECK_N(owf_outline_guess("x", "", 0), OWF_OUTLINE_MD);
}

int main(void)
{
    test_mm();
    test_opml();
    test_md();
    test_deep_and_bad_lists();
    test_guess();
    printf("diagram outlines: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
