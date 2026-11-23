/*
 * Strata is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * Strata is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along
 * with Strata.  If not, see <https://www.gnu.org/licenses/>.
 */
/* Copyright (c) 2026 Logic Magicians Software */

/*
 * Unit tests of src/latex.c: the escaping of the characters that
 * TeX treats in a special way, the conversion from CP437 to UTF-8, the
 * placeholder of a character that the fonts lack, the check of equations
 * and the text that the writer makes for the landscape pages, the
 * Whitespace units, the Keycap fonts, the counters and the Keep markers.
 * The program is linked with the document modules and latex.c only, not
 * with the editor.
 * Its argument is the directory of the test documents (tests/data); without
 * it the tests of the writer are skipped.
 */
#include "../src/util.h"
#include "../src/latex.h"
#include "../src/layout.h"
#include "../src/marker.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;
static int checks;
static const char *opt_dir;


static void
check_str(const char *what, const char *got, const char *want)
{
    checks++;

    if (strcmp(got, want) != 0) {
        printf("FAIL %s: got [%s], want [%s]\n", what, got, want);
        failures++;
    }
}


static void
check_int(const char *what, long got, long want)
{
    checks++;

    if (got != want) {
        printf("FAIL %s: got %ld, want %ld\n", what, got, want);
        failures++;
    }
}


/* Escape a text and compare with what is wanted. */
static void
esc(const char *in, const char *want)
{
    char out[512];

    latex_escape(in, out, sizeof out);
    check_str(in, out, want);
}


static void
test_escape(void)
{
    esc("plain text", "plain text");
    esc("#", "\\#");
    esc("$", "\\$");
    esc("%", "\\%");
    esc("&", "\\&");
    esc("_", "\\_");
    esc("{}", "\\{\\}");
    esc("~", "\\textasciitilde{}");
    esc("^", "\\textasciicircum{}");
    esc("\\", "\\textbackslash{}");
    esc("50% of $5 & a_b #1", "50\\% of \\$5 \\& a\\_b \\#1");
    esc("\\input{x}", "\\textbackslash{}input\\{x\\}");
    esc("[a]", "{[}a{]}");
    esc("a  b", "a \\ b");
    esc("a   b", "a \\ \\ b");
    esc("", "");
    esc("a-b", "a-b");
    esc("-- ``quoted'' <x> |y|",
        "-{}- `{}`quoted'{}' <x> |y|");
    esc("a--b", "a-{}-b");
    esc("a---b", "a-{}-{}-b");
    esc("a----b", "a-{}-{}-{}-b");
    esc("``", "`{}`");
    esc("''", "'{}'");
    esc(",,", ",{},");
    esc("<<", "<{}<");
    esc(">>", ">{}>");
    esc("!`", "!{}`");
    esc("?`", "?{}`");
    esc("`'", "`'");
    esc("`", "`");
    esc("<>", "<>");
    esc("a - - b", "a - - b");
}


static void
test_truncate(void)
{
    char out[8];
    size_t n = latex_escape("abcdefghijkl", out, sizeof out);

    check_int("truncated length", isz(n), 7);
    check_str("truncated text", out, "abcdefg");
    check_int("cap 0", isz(latex_escape("abc", out, 0)), 0);
    n = latex_escape("######", out, sizeof out);
    check_int("escape cut at cap", isz(n), 7);
}


static void
test_cp437(void)
{
    char u[4];

    check_int("ascii length", isz(latex_cp437_utf8('A', u)), 1);
    check_str("ascii", u, "A");
    check_int("e acute length", isz(latex_cp437_utf8(0x82, u)), 2);
    check_str("e acute", u, "\xC3\xA9");
    latex_cp437_utf8(0x84, u);
    check_str("a umlaut", u, "\xC3\xA4");
    latex_cp437_utf8(0x81, u);
    check_str("u umlaut", u, "\xC3\xBC");
    latex_cp437_utf8(0xE1, u);
    check_str("sharp s", u, "\xC3\x9F");
    check_int("box length", isz(latex_cp437_utf8(0xC4, u)), 3);
    check_str("box horizontal", u, "\xE2\x94\x80");
    latex_cp437_utf8(0xDB, u);
    check_str("full block", u, "\xE2\x96\x88");
    latex_cp437_utf8(0xE3, u);
    check_str("pi", u, "\xCF\x80");
    latex_cp437_utf8(0xFB, u);
    check_str("square root", u, "\xE2\x88\x9A");
    latex_cp437_utf8(0x07, u);
    check_str("bullet", u, "\xE2\x80\xA2");

    /* The byte FF is a no-break space in the table; the Printer keeps it a
       plain space, so that no printed page changes. */
    check_int("FF length", isz(latex_cp437_utf8(0xFF, u)), 1);
    check_str("FF is a plain space for the Printer", u, " ");

    /* Every byte gives one to three bytes and a NUL. */
    for (int c = 0; c < 256; c++) {
        size_t n = latex_cp437_utf8(c, u);

        checks++;

        if (n < 1 || n > 3 || u[n] != '\0') {
            printf("FAIL byte %d: length %zu\n", c, n);
            failures++;
        }
    }
}


static void
test_cp437_escape(void)
{
    esc("caf\x82", "caf\xC3\xA9");

    /* Box drawing and symbols are UTF-8; the preamble declares them. */
    esc("\xB3", "\xE2\x94\x82");
    esc("\x1A", "\xE2\x86\x92");
    esc("\xE0", "\xCE\xB1");

    /* Characters that no font has become a placeholder. */
    esc("\xF4", "\\strmiss{2320}");
    esc("a\x7F" "b", "a\\strmiss{2302}b");
    esc("\x9E", "\\strmiss{20A7}");
    esc("\x01", "\xE2\x98\xBA");
}


static void
test_math(void)
{
    check_int("simple", latex_math_ok("x^2 + y_1"), 1);
    check_int("frac", latex_math_ok("\\frac{a}{b}"), 1);
    check_int("greek", latex_math_ok("\\alpha + \\beta"), 1);
    check_int("matrix",
              latex_math_ok("\\begin{pmatrix}1&2\\\\3&4\\end{pmatrix}"), 1);
    check_int("empty", latex_math_ok(""), 0);
    check_int("blank", latex_math_ok("   "), 0);
    check_int("open brace", latex_math_ok("\\frac{a}{b"), 0);
    check_int("close brace", latex_math_ok("a}"), 0);
    check_int("comment", latex_math_ok("a % b"), 0);
    check_int("dollar", latex_math_ok("a $ b"), 0);
    check_int("hash", latex_math_ok("#1"), 0);
    check_int("input", latex_math_ok("\\input{/etc/passwd}"), 0);
    check_int("write", latex_math_ok("\\immediate\\write18{ls}"), 0);
    check_int("def", latex_math_ok("\\def\\x{1}"), 0);
    check_int("pdf", latex_math_ok("\\pdfshellescape"), 0);
    check_int("verbatim", latex_math_ok("\\begin{verbatim}x\\end{verbatim}"),
              0);
    check_int("caret caret", latex_math_ok("^^5cinput"), 0);
    check_int("high byte", latex_math_ok("a\x82"), 0);
    check_int("escaped brace", latex_math_ok("\\{ x \\}"), 1);
    check_int("trailing backslash", latex_math_ok("x\\"), 0);
}


/* The LaTeX text of a document of the directory dir, or NULL. */
static char *
tex_of(const char *dir, const char *name, LatexOpts *o)
{
    char path[1024];
    char err[256];
    char *mem = NULL;
    size_t len = 0;
    Doc *d;
    FILE *m;

    snprintf(path, sizeof path, "%s/%s", dir, name);
    d = doc_load(path, err, sizeof err);

    if (d == NULL) {
        printf("FAIL %s: %s\n", path, err);
        failures++;

        return NULL;
    }

    memset(o, 0, sizeof *o);
    o->docdir = dir;
    m = open_memstream(&mem, &len);

    if (m == NULL || latex_write(d, m, o) != 0) {
        printf("FAIL %s: latex_write\n", path);
        failures++;
    }

    if (m != NULL) {
        fclose(m);
    }

    doc_free(d);

    return mem;
}


/* How many times the text has the string. */
static int
count_of(const char *text, const char *s)
{
    int n = 0;

    for (const char *p = strstr(text, s); p != NULL; p = strstr(p + 1, s)) {
        n++;
    }

    return n;
}


/* Is the first string before the second one in the text? */
static int
before(const char *text, const char *a, const char *b)
{
    const char *pa = strstr(text, a);
    const char *pb = strstr(text, b);

    return pa != NULL && pb != NULL && pa < pb;
}


static void
check_has(const char *what, const char *text, const char *s, int want)
{
    check_int(what, count_of(text, s), want);
}


/* The preamble: the engine test and fonts, and nothing of xetex. */
static void
test_preamble(const char *dir)
{
    LatexOpts o;
    char *t = tex_of(dir, "print_orient.doc", &o);

    if (t == NULL) {
        return;
    }

    check_has("pdf test", t,
              "\\ifdefined\\pdfoutput\\ifnum\\pdfoutput>0 ", 1);
    check_has("dvipdfmx graphicx", t,
              "\\PassOptionsToPackage{dvipdfmx}{graphicx}", 1);
    check_has("T1", t, "\\usepackage[T1]{fontenc}", 1);
    check_has("inputenc", t, "\\usepackage[utf8]{inputenc}", 1);
    check_has("lmodern", t, "\\usepackage{lmodern}", 1);
    check_has("pmboxdraw", t, "\\usepackage{pmboxdraw}", 1);
    check_has("pdflscape", t, "\\usepackage{pdflscape}", 1);
    check_has("lscape", t, "\\usepackage{lscape}", 1);
    check_has("rotate special", t, "<</Rotate 90>>", 1);
    check_has("no fontspec", t, "fontspec", 0);
    check_has("no xetex", t, "xetex", 0);
    check_has("no system fonts", t, "\\setmainfont", 0);
    free(t);
}


/* The placeholder: each character is listed once. */
static void
test_missing(const char *dir)
{
    LatexOpts o;
    char *t = tex_of(dir, "print_chars.doc", &o);

    if (t == NULL) {
        return;
    }

    check_has("placeholder 2320", t, "\\strmiss{2320}", 4);
    check_has("placeholder 2302", t, "\\strmiss{2302}", 1);
    check_has("placeholder macro", t, "\\newcommand{\\strmiss}[1]", 1);
    check_int("missing count", o.nmissing, 5);
    check_int("missing first", o.missing[0], 0x2302);
    check_int("missing 2320 once", o.missing[3], 0x2320);
    check_int("missing last", o.missing[4], 0x2321);
    check_has("drawn smiley", t, "\\DeclareUnicodeCharacter{263A}", 1);
    check_has("declared arrow", t, "\\DeclareUnicodeCharacter{2192}", 1);
    free(t);
}


/* The landscape text: opened at a landscape marker, closed at a portrait
   marker or at the end, and at the start of the block of a marker in the
   middle of a block. */
static void
test_landscape(const char *dir)
{
    LatexOpts o;
    char *t = tex_of(dir, "print_orient.doc", &o);

    if (t == NULL) {
        return;
    }

    check_has("landscape opens", t, "\\strlandbegin\n", 2);
    check_has("landscape closes", t, "\\strlandend\n", 2);
    check_has("landscape macro", t,
              "\\newcommand{\\strlandbegin}{\\clearpage\\strlandtrue", 1);
    check_int("open before text 1",
              before(t, "\\strlandbegin\n", "LANDSCAPE ONE"), 1);
    check_int("close before portrait 2",
              before(t, "\\strlandend\n", "PORTRAIT TWO"), 1);
    check_int("portrait 2 before the mixed block",
              before(t, "PORTRAIT TWO", "A block with a marker"), 1);
    check_int("mixed block starts the page",
              before(strstr(t, "PORTRAIT TWO"), "\\strlandbegin\n",
                     "A block with a marker"), 1);
    check_int("last close at the end",
              before(t, "open at the end", "\\end{document}"), 1);
    free(t);
}


/* The Whitespace units: no record is inches, field U is a TeX unit. */
static void
test_whitespace(const char *dir)
{
    static const char *const units[] = {"in", "cm", "mm", "pt", "bp", "pc",
                                        "dd", "cc", "sp", "em", "ex"};
    LatexOpts o;
    char *t = tex_of(dir, "print_whitespace.doc", &o);
    char want[64];

    if (t == NULL) {
        return;
    }

    /* The marker is written as \strws{size}{treatment}: the treatment 1,
       Place All, when the marker has none. */
    for (int i = 0; i < 11; i++) {
        snprintf(want, sizeof want, "\\strws{%s%s}{1}",
                 i == 8 ? "4000.000" : "1.500", units[i]);
        check_has(want, t, want, 1);
    }

    check_has("em at the top", t, "\\strws{1.250em}{1}", 1);
    check_has("small in", t, "\\strws{0.010in}{1}", 1);
    check_has("small pt", t, "\\strws{0.010pt}{1}", 1);
    check_has("zero prints nothing", t, "\\strws{0.000", 0);
    check_has("zero prints nothing 2", t, "\\strws{}", 0);
    check_int("marker unit name 3", strcmp(marker_unit_name(3), "pt"), 0);
    check_int("marker unit name bad", strcmp(marker_unit_name(40), "in"), 0);
    free(t);
}


/* The Keycap font: its number picks the family of the legend. */
static void
test_keycap(const char *dir)
{
    LatexOpts o;
    char *t = tex_of(dir, "print_keycap.doc", &o);

    if (t == NULL) {
        return;
    }

    check_has("font 1 headline", t, "{\\sffamily }{Key1}", 1);
    check_has("font 2 text", t, "{\\rmfamily }{Key2}", 1);
    check_has("font 3 header", t, "{\\rmfamily }{Key3}", 1);
    check_has("font 4 mono", t, "{\\ttfamily }{Key4}", 1);
    check_has("font 5 sans", t, "{\\sffamily }{Key5}", 1);
    check_has("font 6 roman", t, "{\\rmfamily }{Key6}", 1);
    check_has("font 7 listing", t, "{\\ttfamily }{Key7}", 1);
    check_has("font 8 arial", t, "{\\sffamily }{Key8}", 1);
    check_has("font 9 not named", t, "{\\rmfamily }{Key9}", 1);
    check_has("font 20 not named", t, "{\\rmfamily }{Key20}", 1);
    check_has("gray keycap", t, "{0.300}{0.800}{0.750}{\\ttfamily }{Esc}", 2);
    free(t);
}


/*
 * A keycap and a text that holds the pairs of characters that TeX would
 * join into a ligature: each is written with an empty group between its
 * two characters, so that it is printed as typed.
 */
static void
test_ligatures(const char *dir)
{
    LatexOpts o;
    char *t = tex_of(dir, "print_hyphens.doc", &o);

    if (t == NULL) {
        return;
    }

    check_has("Alt-- is two hyphens", t, "{Alt-{}-}", 1);
    check_has("Alt-+ is not changed", t, "{Alt-+}", 1);
    check_has("Ctrl-- is two hyphens", t, "{Ctrl-{}-}", 1);
    check_has("Shift-- is two hyphens", t, "{Shift-{}-}", 1);
    check_has("ff of a legend is kept apart", t, "{Of{}f}", 1);
    check_has("a legend without a pair is plain", t, "{Fill}", 1);
    check_has("two hyphens in text", t, "a -{}- b", 1);
    check_has("three hyphens in text", t, "b -{}-{}- c", 1);
    check_has("a single hyphen in text", t, "x-y.", 1);
    check_has("two backticks", t, "`{}`a", 1);
    check_has("two apostrophes", t, "a'{}'", 1);
    check_has("two commas", t, ",{}, ", 1);
    check_has("two less-than signs", t, "<{}< ", 1);
    check_has("two greater-than signs", t, ">{}> ", 1);
    check_has("exclamation mark and backtick", t, "!{}` ", 1);
    check_has("question mark and backtick", t, "?{}` ", 1);
    check_has("words keep their fi and ff", t, "office fish waffle", 1);
    free(t);
}


/* A no-break space, as the character 1E or as the byte FF, is the tie. */
static void
test_nbsp(const char *dir)
{
    LatexOpts o;
    char *t = tex_of(dir, "print_nbsp.doc", &o);

    if (t == NULL) {
        return;
    }

    check_has("the character 1E is a tie", t, "Mr.~Smith", 1);
    check_has("the byte FF is a tie", t, "10~km.", 1);
    check_has("a pair tied by 1E", t, "alpha~beta and more", 80);
    check_has("a pair tied by FF", t, "iota~kappa and more", 80);
    check_has("a plain space stays a space", t, "gamma delta and more", 80);
    check_has("no tie is escaped as text", t, "textasciitilde", 0);
    free(t);
}


/* The counters: a Set Value is printed and the next use adds one. */
static void
test_counter(const char *dir)
{
    LatexOpts o;
    char *t = tex_of(dir, "counters.doc", &o);

    if (t == NULL) {
        return;
    }

    check_has("counter 1", t, "Item 1. first", 1);
    check_has("counter 2", t, "Item 2. second", 1);
    check_has("counter set value", t, "Item 10. set to ten", 1);
    check_has("counter after set", t, "Item 11. after", 1);
    check_has("second counter", t, "Part I. roman", 1);
    check_has("second counter next", t, "Part II. next", 1);
    free(t);
}


/*
 * A counter counts up to 4294967295 (the largest unsigned 32-bit number) and
 * stops there: a value above 2147483647 works, the next use does not wrap,
 * and the styles that cannot show a large number show it in digits.
 */
static void
test_countermax(const char *dir)
{
    LatexOpts o;
    char *t = tex_of(dir, "countermax.doc", &o);

    if (t == NULL) {
        return;
    }

    check_has("counter set to 4294967294", t, "Item 4294967294. last but one",
              1);
    check_has("the next use is 4294967295", t, "Item 4294967295. last", 1);
    check_has("a further use stays at 4294967295", t,
              "Item 4294967295. stays", 1);
    check_has("a large number in roman is in digits", t,
              "Part 4294967295", 1);
    check_has("a large number in letters", t, "Letter MWLQKWU.", 1);
    check_has("3999 is the largest roman numeral", t, "Roman MMMCMXCIX.", 1);
    check_has("4000 is in digits", t, "Roman 4000.", 1);
    free(t);
}


/* A table on a landscape page is as wide as the turned page (the last column
   takes what is left of the 9 inches after the two columns of 2 inches and
   the space of the three columns, 3 times 0.167 inch), a space after a
   footnote is kept, and a picture below the document directory is found. */
static void
test_panels(const char *dir)
{
    LatexOpts o;
    char *t = tex_of(dir, "print_panels.doc", &o);

    if (t == NULL) {
        return;
    }

    check_has("landscape table fills the page", t,
              "p{2.000in}p{2.000in}p{4.499in}", 1);
    check_has("a space after a footnote is kept", t, "}%\n{} after the note",
              1);
    check_has("a picture below the document is found", t, "/pics/p.png", 1);
    free(t);
}


/* The Keep marker: its label is never written, and the blocks between a
   Start and an End are set in a box that is placed by \strkeepplace (or
   \strkeepfloat for a floating Start). */
static void
test_keep(const char *dir)
{
    static const struct {
        const char *name;
        int boxes;
        int floats;
    } docs[] = {{"keep_fixed.doc", 1, 0}, {"keep_text.doc", 1, 0},
                {"keep_float.doc", 1, 1}, {"keep_floatend.doc", 1, 1},
                {"keep_unmatched.doc", 1, 0}, {"keep_stray.doc", 0, 0},
                {"keep_nested.doc", 1, 0}, {"keep_long.doc", 1, 0},
                {"keep_table.doc", 1, 0}, {"keep_sections.doc", 2, 0},
                {"keep_notes.doc", 1, 0}};

    for (size_t i = 0; i < sizeof docs / sizeof docs[0]; i++) {
        LatexOpts o;
        char *t = tex_of(dir, docs[i].name, &o);
        char what[96];

        if (t == NULL) {
            continue;
        }

        snprintf(what, sizeof what, "%s: boxes", docs[i].name);
        check_has(what, t, "\\setbox\\strkeepbox\\vbox{", docs[i].boxes);
        snprintf(what, sizeof what, "%s: placed", docs[i].name);
        check_has(what, t, "\\endgroup\n", docs[i].boxes);
        snprintf(what, sizeof what, "%s: floating", docs[i].name);
        check_has(what, t, "\\strkeepfloat\\endgroup", docs[i].floats);
        snprintf(what, sizeof what, "%s: no label", docs[i].name);
        check_has(what, t, "Keep", 0);
        check_has(what, t, "\\strlong{", 0);
        check_has(what, t, "\\strlongbr{", 0);
        free(t);
    }

    {
        LatexOpts o;
        char *t = tex_of(dir, "keep_table.doc", &o);

        if (t != NULL) {
            check_has("keep table: a tabular for each row", t,
                      "\\begin{tabular}[t]{", 4);
            check_has("keep table: no longtable", t, "\\begin{longtable}", 0);
            free(t);
        }

        t = tex_of(dir, "keep_notes.doc", &o);

        if (t != NULL) {
            check_has("keep notes: the note text waits for the box", t,
                      "\\strfntext{", 1);
            check_int("keep notes: it comes after the box",
                      before(t, "\\strkeepplace\\endgroup", "\\strfntext{"),
                      1);
            free(t);
        }

        t = tex_of(dir, "keep_fixed.doc", &o);

        if (t != NULL) {
            check_has("keep fixed: placed by strkeepplace", t,
                      "\\strkeepplace\\endgroup", 1);
            check_has("keep fixed: not floating", t, "\\strkeepfloat\\endgroup",
                      0);
            free(t);
        }
    }
}


/* The LaTeX text of a document with the options of o (not cleared). */
static char *
tex_with(const char *dir, const char *name, LatexOpts *o)
{
    char path[1024];
    char err[256];
    char *mem = NULL;
    size_t len = 0;
    Doc *d;
    FILE *m;

    snprintf(path, sizeof path, "%s/%s", dir, name);
    d = doc_load(path, err, sizeof err);

    if (d == NULL) {
        printf("FAIL %s: %s\n", path, err);
        failures++;

        return NULL;
    }

    m = open_memstream(&mem, &len);

    if (m == NULL || latex_write(d, m, o) != 0) {
        printf("FAIL %s: latex_write\n", path);
        failures++;
    }

    if (m != NULL) {
        fclose(m);
    }

    doc_free(d);

    return mem;
}


/* The export of a LaTeX file: the header, the pictures and the list. */
static void
test_export(const char *dir)
{
    LatexOpts o;
    char pics[PATH_MAX];
    char png[300];
    char *t;
    char *plain;
    char *pd;

    tmp_path(pics, sizeof pics, "lxtest-export.XXXXXX");
    pd = mkdtemp(pics);

    if (pd == NULL) {
        printf("FAIL export: no temporary directory\n");
        failures++;

        return;
    }

    memset(&o, 0, sizeof o);
    o.docdir = dir;
    o.export_name = "two\nlines.doc";
    o.export_tex = "x.tex";
    o.export_date = "2026-01-02";
    o.picdir = pd;
    o.picref = "x_files";
    o.copy_pictures = 1;
    t = tex_with(dir, "print_panels.doc", &o);

    if (t == NULL) {
        return;
    }

    check_int("export: the header starts the file",
              strncmp(t, "% LaTeX file exported by Strata.\n", 33), 0);
    check_int("export: a name cannot end the comment",
              strstr(t, "% Source document: two?lines.doc\n") != NULL, 1);
    check_int("export: the day is in the header",
              strstr(t, "% Exported on: 2026-01-02\n") != NULL, 1);
    check_int("export: the command to compile is in the header",
              strstr(t, "%     pdflatex x.tex\n") != NULL, 1);
    check_int("export: the picture is named in the folder",
              strstr(t, "{\\detokenize{x_files/pic1.png}}") != NULL, 1);
    check_int("export: the list of pictures is at the end",
              strstr(t, "\\end{document}\n% Pictures used by this file") !=
              NULL && strstr(t, "% x_files/pic1.png = pics/p.png\n") != NULL,
              1);
    snprintf(png, sizeof png, "%s/pic1.png", pd);
    check_int("export: the picture is copied", access(png, R_OK), 0);
    free(t);
    unlink(png);

    /* Pictures in place: nothing is copied, the name is the real path. */
    o.copy_pictures = 0;
    t = tex_with(dir, "print_panels.doc", &o);

    if (t != NULL) {
        check_int("export in place: nothing is copied", access(png, F_OK),
                  -1);
        check_int("export in place: the picture is named where it is",
                  strstr(t, "/pics/p.png}") != NULL &&
                  strstr(t, "x_files/pic1.png") == NULL, 1);
        free(t);
    }

    /* Without export_name the file is the file of a print: no header. */
    memset(&o, 0, sizeof o);
    o.docdir = dir;
    plain = tex_with(dir, "print_panels.doc", &o);

    if (plain != NULL) {
        check_int("print: there is no header and no list",
                  strstr(plain, "LaTeX file exported") == NULL &&
                  strstr(plain, "Pictures used by this file") == NULL, 1);
        free(plain);
    }

    rmdir(pd);
}


/*
 * The LaTeX of a test document with some global F records set first: the
 * pairs of code and value in set, ended by a NULL code.
 */
static char *
tex_set(const char *dir, const char *name, LatexOpts *o,
        const char *const *set)
{
    char path[1024];
    char err[256];
    char *mem = NULL;
    size_t len = 0;
    Doc *d;
    FILE *m;

    snprintf(path, sizeof path, "%s/%s", dir, name);
    d = doc_load(path, err, sizeof err);

    if (d == NULL) {
        printf("FAIL %s: %s\n", path, err);
        failures++;

        return NULL;
    }

    for (int i = 0; set[i] != NULL; i += 2) {
        doc_fmt_set(d, 0, set[i], set[i + 1]);
    }

    memset(o, 0, sizeof *o);
    o->docdir = dir;
    m = open_memstream(&mem, &len);

    if (m == NULL || latex_write(d, m, o) != 0) {
        printf("FAIL %s: latex_write\n", path);
        failures++;
    }

    if (m != NULL) {
        fclose(m);
    }

    doc_free(d);

    return mem;
}


/* The LaTeX of print_lineno.doc with the settings, checked for one text. */
static void
lineno_has(const char *what, const char *const *set, const char *s, int want,
           int pagerefs)
{
    LatexOpts o;
    char *t = tex_set(opt_dir, "print_lineno.doc", &o, set);

    if (t == NULL) {
        return;
    }

    check_has(what, t, s, want);
    check_int(what, o.pagerefs, pagerefs);
    free(t);
}


static void
test_lineno(const char *dir)
{
    static const char *const none[] = {NULL};
    static const char *const pagewise[] = {"ZM", "1", NULL};
    static const char *const running[] = {"ZM", "2", NULL};
    static const char *const section[] = {"ZM", "3", NULL};
    static const char *const right[] = {"ZM", "1", "ZS", "1", NULL};
    static const char *const outer[] = {"ZM", "1", "ZS", "2", NULL};
    static const char *const inner[] = {"ZM", "2", "ZS", "3", NULL};
    static const char *const every[] = {"ZM", "1", "ZE", "5", NULL};
    static const char *const look[] = {"ZM", "1", "ZZ", "2", "ZY", "1", NULL};
    static const char *const roman[] = {"ZM", "1", "ZF", "1", NULL};
    static const char *const capital[] = {"ZM", "1", "ZF", "2", NULL};
    static const char *const dist[] = {"ZM", "2", "ZD", "30", NULL};
    static const char *const eqs[] = {"ZM", "1", "ZQ", "1", NULL};
    static const char *const bad[] = {"ZM", "9", NULL};
    static const char *const off[] = {"ZM", "0", "ZE", "7", NULL};

    opt_dir = dir;
    lineno_has("no record: no lineno", none, "{lineno}", 0, 0);
    lineno_has("no record: no \\linenumbers", none, "\\linenumbers", 0, 0);
    lineno_has("Off with other records: no lineno", off, "{lineno}", 0, 0);
    lineno_has("a mode out of range is Off", bad, "{lineno}", 0, 0);
    lineno_has("pagewise: the package", pagewise,
               "\\usepackage[pagewise,sep=0.15in]{lineno}", 1, 1);
    lineno_has("pagewise: the start", pagewise, "\\linenumbers\n", 1, 1);
    lineno_has("pagewise: the default font", pagewise,
               "\\def\\linenumberfont{\\normalfont\\tiny\\sffamily}", 1, 1);
    lineno_has("pagewise: every line", pagewise, "\\modulolinenumbers", 0, 1);
    lineno_has("running: the package", running,
               "\\usepackage[running,sep=0.15in]{lineno}", 1, 0);
    lineno_has("each section: running numbers", section,
               "\\usepackage[running,sep=0.15in]{lineno}", 1, 0);
    lineno_has("each section: the reset at a first level headline", section,
               "\\resetlinenumber\\fi\\strheadnumbered", 1, 0);
    lineno_has("pagewise: no reset", pagewise, "\\resetlinenumber", 0, 1);
    lineno_has("right: the side", right,
               "\\usepackage[pagewise,right,sep=0.15in]{lineno}", 1, 1);
    lineno_has("outer: switch and pagewise", outer,
               "\\usepackage[switch,pagewise,sep=0.15in]{lineno}", 1, 1);
    lineno_has("inner with running numbers: switch*", inner,
               "\\usepackage[switch*,sep=0.15in]{lineno}", 1, 1);
    lineno_has("every fifth line", every, "\\modulolinenumbers[5]", 1, 1);
    lineno_has("size Footnote and font Roman", look,
               "\\def\\linenumberfont{\\normalfont\\footnotesize\\rmfamily}",
               1, 1);
    lineno_has("format roman", roman,
               "\\renewcommand{\\thelinenumber}{\\ifnum"
               "\\value{linenumber}>3999 \\arabic{linenumber}"
               "\\else\\roman{linenumber}\\fi}", 1, 1);
    lineno_has("format capital roman", capital,
               "\\renewcommand{\\thelinenumber}{\\ifnum"
               "\\value{linenumber}>3999 \\arabic{linenumber}"
               "\\else\\Roman{linenumber}\\fi}", 1, 1);
    lineno_has("format arabic needs no command", pagewise, "thelinenumber", 0,
               1);
    lineno_has("distance 0.30", dist, "sep=0.30in]{lineno}", 1, 0);
    lineno_has("equations: mathlines", eqs, ",mathlines,sep=", 1, 1);
    lineno_has("no equations: no mathlines", pagewise, "mathlines", 0, 1);
    lineno_has("Keep blocks put their lines on the page", pagewise,
               "\\unvbox\\strkeepbox\\fi\\fi}", 1, 1);
    lineno_has("a Keep part with a table is placed as the box", pagewise,
               "\\ifstrkeeptable\\box\\strkeepbox\\else", 1, 1);
    {
        LatexOpts ko;
        char *kt = tex_set(opt_dir, "keep_table.doc", &ko, pagewise);
        char *kn = tex_set(opt_dir, "keep_table.doc", &ko, none);

        if (kt != NULL && kn != NULL) {
            check_has("a table in a Keep part sets the switch once", kt,
                      "\\global\\strkeeptabletrue", 1);
            check_has("each Keep part clears the switch", kt,
                      "\\global\\strkeeptablefalse", 1);
            check_has("without numbering no Keep switch is written", kn,
                      "strkeeptable", 0);
        }

        free(kt);
        free(kn);
    }
}


/*
 * The LaTeX marker: its text arrives in the file as it was typed, on lines
 * of its own, not escaped, not wrapped, with a byte above 7F as UTF-8, and
 * a print that does not allow the markers writes the words instead.
 */
static void
test_latex_marker(const char *dir)
{
    LatexOpts o;
    char *t;

    memset(&o, 0, sizeof o);
    o.docdir = dir;
    o.posmarks = 1;
    t = tex_with(dir, "print_latex.doc", &o);

    if (t == NULL) {
        return;
    }

    check_has("latex marker: the rule is written byte for byte", t,
              "%\n\\begin{center}\\rule{6cm}{0.8pt}\\end{center}%\n{}", 1);
    check_has("latex marker: the special characters are not escaped", t,
              "\\textbf{raw bold} \\& 100\\% of it%\n{}", 1);
    check_has("latex marker: the lines of a table are kept", t,
              "\\begin{tabular}{|l|r|}\\hline\nA\\ B & 1 \\\\ \\hline\n"
              "C & 2 \\\\ \\hline\n\\end{tabular}%\n{}", 1);
    check_has("latex marker: a byte above 7F is UTF-8", t,
              "caf\xC3\xA9 and \\textsc{small caps}", 1);
    check_int("latex marker: a position comment comes before the text",
              before(t, "%@P", "\\begin{center}\\rule"), 1);
    check_has("latex marker: no word of the long form is printed", t,
              "LaTeX instructions", 0);
    free(t);

    /* Not allowed: the text is not written, the words are. */
    memset(&o, 0, sizeof o);
    o.docdir = dir;
    o.no_latex = 1;
    t = tex_with(dir, "print_latex.doc", &o);

    if (t != NULL) {
        check_has("latex marker off: the text is not written", t,
                  "\\rule{6cm}", 0);
        check_has("latex marker off: the words are written", t,
                  "LaTeX instructions", 4);
        free(t);
    }
}


/*
 * Page Break Level: a headline of the level or a higher one starts a page
 * (the documents pb_*.doc are made by tests/mkpbdata.py).
 */
static void
test_pagebreak(const char *dir)
{
    static const char *const brk = "\\clearpage\n\\needspace{4\\baselineskip}"
                                   "\n\\strhead{";
    static const char *const names[] = {"pb_0.doc", "pb_1.doc", "pb_2.doc",
                                        "pb_title.doc"};
    static const int want[] = {0, 2, 8, 3};

    for (int i = 0; i < 4; i++) {
        LatexOpts o;
        char what[96];
        char *t;

        memset(&o, 0, sizeof o);
        o.docdir = dir;
        t = tex_with(dir, names[i], &o);

        if (t == NULL) {
            continue;
        }

        snprintf(what, sizeof what, "page break level: %s starts %d pages",
                 names[i], want[i]);
        check_has(what, t, brk, want[i]);

        if (i == 1 || i == 2) {
            snprintf(what, sizeof what,
                     "page break level: %s, the first headline starts "
                     "no page", names[i]);
            check_int(what, before(t, "\\strhead{1}", brk), 1);
        }

        free(t);
    }

    /* A draft has the page breaks too: they are the structure. */
    {
        LatexOpts o;
        char *t;

        memset(&o, 0, sizeof o);
        o.docdir = dir;
        o.draft = 1;
        t = tex_with(dir, "pb_1.doc", &o);

        if (t != NULL) {
            check_has("page break level: a draft starts the pages too", t,
                      brk, 2);
            free(t);
        }
    }
}


/*
 * The line and block spacing of a block: its own, or that of its named
 * format, wins over the spacing of the document (sp_*.doc, 40 blocks each).
 */
static void
test_block_spacing(const char *dir)
{
    static const char *const names[] = {"sp_0.doc", "sp_line2.doc",
                                        "sp_block3.doc", "sp_override.doc",
                                        "sp_named.doc"};
    static const int stretch2[] = {0, 40, 0, 40, 0};
    static const int space2[] = {0, 0, 40, 40, 0};

    for (int i = 0; i < 5; i++) {
        LatexOpts o;
        char what[96];
        char *t;

        memset(&o, 0, sizeof o);
        o.docdir = dir;
        t = tex_with(dir, names[i], &o);

        if (t == NULL) {
            continue;
        }

        snprintf(what, sizeof what, "spacing: %s has %d blocks of 2.00 lines",
                 names[i], stretch2[i]);
        check_has(what, t, "\\setstretch{2.00}", stretch2[i]);
        snprintf(what, sizeof what, "spacing: %s has %d spaces of 2.00 lines",
                 names[i], space2[i]);
        check_has(what, t, "\\vspace{2.00\\baselineskip}", space2[i]);
        free(t);
    }

    /* A draft has one line and no space, whatever a block says. */
    {
        LatexOpts o;
        char *t;

        memset(&o, 0, sizeof o);
        o.docdir = dir;
        o.draft = 1;
        t = tex_with(dir, "sp_override.doc", &o);

        if (t != NULL) {
            check_has("spacing: a draft ignores the stretch of a block", t,
                      "\\setstretch{2.00}", 0);
            check_has("spacing: a draft ignores the space of a block", t,
                      "\\vspace{2.00\\baselineskip}", 0);
            free(t);
        }
    }
}


/*
 * The table of contents (the documents toc_*.doc are made by
 * tests/mktocdata.py): a line for each headline of the levels asked for,
 * each headline carries the label that its page number is read from, the
 * pages are in roman numerals, and a print that does not ask has none of it.
 */
static void
test_contents(const char *dir)
{
    LatexOpts o;
    char *t;

    memset(&o, 0, sizeof o);
    o.docdir = dir;
    t = tex_with(dir, "toc_ten.doc", &o);

    if (t != NULL) {
        check_has("contents: not asked, no line is written", t,
                  "\\@dottedtocline", 0);
        check_has("contents: not asked, no title is written", t,
                  "Contents}", 0);
        check_int("contents: not asked, no second run for page numbers",
                  o.pagerefs, 0);
        free(t);
    }

    memset(&o, 0, sizeof o);
    o.docdir = dir;
    o.contents = 3;
    t = tex_with(dir, "toc_ten.doc", &o);

    if (t != NULL) {
        check_has("contents: ten headlines, ten lines", t,
                  "\\@dottedtocline", 10);
        check_has("contents: each line reads the page of its headline", t,
                  "{\\pageref{s", 10);
        check_has("contents: each headline carries its label", t,
                  "\\label{s", 10);
        check_has("contents: one title", t, "Contents}", 1);
        check_has("contents: the pages are in roman numerals", t,
                  "\\pagenumbering{roman}", 1);
        check_int("contents: a second run fills the page numbers in",
                  o.pagerefs, 1);
        check_int("contents: the lines come before the first headline",
                  before(t, "\\@dottedtocline", "\\strhead{1}"), 1);
        free(t);
    }

    memset(&o, 0, sizeof o);
    o.docdir = dir;
    o.contents = 1;
    t = tex_with(dir, "toc_ten.doc", &o);

    if (t != NULL) {
        check_has("contents: level 1 lists the four chapters", t,
                  "\\@dottedtocline", 4);
        check_has("contents: level 1 labels only those", t, "\\label{s", 4);
        free(t);
    }

    memset(&o, 0, sizeof o);
    o.docdir = dir;
    o.contents = 2;
    t = tex_with(dir, "toc_deep.doc", &o);

    if (t != NULL) {
        check_has("contents: three levels, level 2 lists four of six", t,
                  "\\@dottedtocline", 4);
        free(t);
    }

    memset(&o, 0, sizeof o);
    o.docdir = dir;
    o.contents = 3;
    t = tex_with(dir, "toc_title.doc", &o);

    if (t != NULL) {
        check_int("contents: they follow the title page",
                  before(t, "\\setcounter{page}{1}",
                         "\\pagenumbering{roman}"), 1);
        check_int("contents: and come before the body",
                  before(t, "\\pagenumbering{roman}", "\\strhead{1}"), 1);
        free(t);
    }

    memset(&o, 0, sizeof o);
    o.docdir = dir;
    o.contents = 3;
    t = tex_with(dir, "toc_flat.doc", &o);

    if (t != NULL) {
        check_has("contents: no headline, a line that says so", t,
                  "no headlines to list", 1);
        check_has("contents: no headline, no entry", t, "\\@dottedtocline",
                  0);
        free(t);
    }

    /* A draft has the table too. */
    memset(&o, 0, sizeof o);
    o.docdir = dir;
    o.draft = 1;
    o.contents = 2;
    t = tex_with(dir, "toc_ten.doc", &o);

    if (t != NULL) {
        check_has("contents: a draft has the lines too", t,
                  "\\@dottedtocline", 10);
        free(t);
    }
}


int
main(int argc, char **argv)
{
    test_escape();
    test_truncate();
    test_cp437();
    test_cp437_escape();
    test_math();

    if (argc > 1) {
        test_preamble(argv[1]);
        test_missing(argv[1]);
        test_landscape(argv[1]);
        test_whitespace(argv[1]);
        test_keycap(argv[1]);
        test_ligatures(argv[1]);
        test_nbsp(argv[1]);
        test_counter(argv[1]);
        test_countermax(argv[1]);
        test_panels(argv[1]);
        test_keep(argv[1]);
        test_export(argv[1]);
        test_latex_marker(argv[1]);
        test_lineno(argv[1]);
        test_pagebreak(argv[1]);
        test_block_spacing(argv[1]);
        test_contents(argv[1]);
    }

    printf("lxtest: %d checks, %d failures\n", checks, failures);

    return failures != 0 ? 1 : 0;
}
