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
 * dicttest.c - the unit tests of the personal word list (src/dictlist.c),
 * the part of Maintain Dictionaries that has no screen.
 *
 * The tests work in a directory made under TMPDIR (or /tmp when it is not
 * set); STRATA_HOME names it, so that the real settings are never touched.
 *
 *     dicttest       the tests
 */
#include "../src/util.h"
#include "../src/config.h"
#include "../src/spell.h"
#include "../src/dictlist.h"
#include "../src/hyphlist.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static int fails = 0;
static int checks = 0;
static char home[512];

#define CHECK(c, ...)                                                          \
    do {                                                                       \
        checks++;                                                              \
        if (!(c)) {                                                            \
            printf("FAIL line %d: ", __LINE__);                                \
            printf(__VA_ARGS__);                                               \
            printf("\n");                                                      \
            fails++;                                                           \
        }                                                                      \
    } while (0)


/* The time in milliseconds. */
static long
now_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);

    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}


/* A clean settings directory. */
static void
fresh_home(void)
{
    static int n;
    const char *tmp = getenv("TMPDIR");
    char root[400];

    if (home[0] == '\0') {
        snprintf(root, sizeof root, "%s/dicttest-XXXXXX",
                 tmp != NULL && tmp[0] != '\0' ? tmp : "/tmp");

        if (mkdtemp(root) == NULL) {
            printf("cannot make a directory\n");
            exit(2);
        }

        str_copy(home, sizeof home, root);
    }

    n++;

    char sub[640];

    snprintf(sub, sizeof sub, "%s/h%d", home, n);
    mkdir(sub, 0700);
    setenv("STRATA_HOME", sub, 1);
}


static void
dict_file(char *out, size_t cap)
{
    char dir[PATH_MAX - 64];

    config_directory(dir, sizeof dir);
    snprintf(out, cap, "%s/dictionary", dir);
}


static void
put_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");

    if (f == NULL) {
        printf("cannot write %s\n", path);
        exit(2);
    }

    fputs(text, f);
    fclose(f);
}


static void
read_file(const char *path, char *out, size_t cap)
{
    FILE *f = fopen(path, "r");
    size_t n = 0;

    out[0] = '\0';

    if (f == NULL) {
        return;
    }

    n = fread(out, 1, cap - 1, f);
    out[n] = '\0';
    fclose(f);
}


static int
has(const SpellDict *d, const char *w)
{
    return dictlist_find(d, w) >= 0;
}


static void
test_words(void)
{
    char why[96];
    char longw[100];

    CHECK(dictlist_check_word("hello", why, sizeof why) == 1, "a word: %s",
          why);
    CHECK(dictlist_check_word("don't", why, sizeof why) == 1, "apostrophe");
    CHECK(dictlist_check_word("well-known", why, sizeof why) == 1, "hyphen");
    CHECK(dictlist_check_word("Caf\x82", why, sizeof why) == 1,
          "an accented letter of CP437");
    CHECK(dictlist_check_word("", why, sizeof why) == 0 &&
              strstr(why, "Type a word") != NULL,
          "empty: %s", why);
    CHECK(dictlist_check_word("two words", why, sizeof why) == 0 &&
              strstr(why, "no spaces") != NULL,
          "a space: %s", why);
    CHECK(dictlist_check_word("abc123", why, sizeof why) == 0 &&
              strstr(why, "no digits") != NULL,
          "a digit: %s", why);
    CHECK(dictlist_check_word("a_b", why, sizeof why) == 0 &&
              strstr(why, "letters, apostrophes and hyphens") != NULL,
          "underscore: %s", why);
    CHECK(dictlist_check_word("-ab", why, sizeof why) == 0 &&
              strstr(why, "begins and ends") != NULL,
          "begins with a hyphen: %s", why);
    CHECK(dictlist_check_word("ab'", why, sizeof why) == 0, "ends with '");
    CHECK(dictlist_check_word("'-'", why, sizeof why) == 0, "no letter");

    memset(longw, 'a', sizeof longw);
    longw[SPELL_MAXWORD] = '\0';
    CHECK(dictlist_check_word(longw, why, sizeof why) == 1,
          "the longest word, %d letters", SPELL_MAXWORD);
    longw[SPELL_MAXWORD] = 'a';
    longw[SPELL_MAXWORD + 1] = '\0';
    CHECK(dictlist_check_word(longw, why, sizeof why) == 0 &&
              strstr(why, "at most") != NULL,
          "one letter too many: %s", why);
}


static void
test_order(void)
{
    SpellDict d;

    memset(&d, 0, sizeof d);
    CHECK(dictlist_add(&d, "pear") == 1 && dictlist_add(&d, "apple") == 1 &&
              dictlist_add(&d, "Zed") == 1 && dictlist_add(&d, "banana") == 1,
          "four words are added");
    CHECK(d.n == 4 && strcmp(d.w[0], "apple") == 0 &&
              strcmp(d.w[1], "banana") == 0 && strcmp(d.w[2], "pear") == 0 &&
              strcmp(d.w[3], "Zed") == 0,
          "the words are in order without regard to case: %s %s %s %s",
          d.w[0], d.w[1], d.w[2], d.w[3]);

    CHECK(dictlist_add(&d, "pear") == 0, "a word twice is refused");
    CHECK(dictlist_add(&d, "Pear") == 0,
          "a capital form is covered by the lower case entry");
    CHECK(dictlist_covers(&d, "Apple") == 1 && dictlist_covers(&d, "APPLE") == 0,
          "an initial capital is covered, all capitals are not");
    CHECK(dictlist_add(&d, "ZED") == 1 && dictlist_add(&d, "zed") == 1,
          "a lower case entry is allowed beside a capital entry");
    CHECK(dictlist_find(&d, "zed") >= 0 && dictlist_find(&d, "Zed") >= 0 &&
              dictlist_find(&d, "nothing") < 0,
          "find is exact");

    CHECK(dictlist_prefix(&d, "ba") == dictlist_find(&d, "banana"),
          "the first word that begins with ba");
    CHECK(dictlist_prefix(&d, "BAN") == dictlist_find(&d, "banana"),
          "the prefix is without regard to case");
    CHECK(dictlist_prefix(&d, "q") < 0, "no word begins with q");
    CHECK(dictlist_prefix(&d, "") == 0, "an empty prefix is the first word");

    dictlist_remove(&d, dictlist_find(&d, "banana"));
    CHECK(d.n == 5 && dictlist_find(&d, "banana") < 0 &&
              strcmp(d.w[1], "pear") == 0,
          "a word is taken out");
    dictlist_remove(&d, 99);
    dictlist_remove(&d, -1);
    CHECK(d.n == 5, "an entry that is not there is ignored");
    spell_dict_free(&d);
}


static void
test_save_load(void)
{
    SpellDict d;
    SpellDict e;
    char path[PATH_MAX];
    char text[256];
    char err[200];
    struct stat st;

    fresh_home();
    dict_file(path, sizeof path);
    memset(&d, 0, sizeof d);
    dictlist_load(&d);
    CHECK(d.n == 0, "a missing file is an empty list");

    dictlist_add(&d, "zebra");
    dictlist_add(&d, "Caf\x82");
    dictlist_add(&d, "aardvark");
    CHECK(dictlist_save(&d, err, sizeof err) == 0, "save: %s", err);
    read_file(path, text, sizeof text);
    CHECK(strcmp(text, "aardvark\nCaf\xC3\xA9\nzebra\n") == 0,
          "the file is UTF-8, one word on a line, in order: %s", text);
    CHECK(stat(path, &st) == 0 && (st.st_mode & 0777) == 0600,
          "the file is private");

    memset(&e, 0, sizeof e);
    dictlist_load(&e);
    CHECK(e.n == 3 && has(&e, "Caf\x82") && has(&e, "zebra"),
          "the words come back, the accent too");

    /* What the check screen's Define does works on the same file. */
    CHECK(spell_dict_add(&e, "quokka", err, sizeof err) == 0, "define: %s",
          err);
    spell_dict_free(&d);
    dictlist_load(&d);
    CHECK(d.n == 4 && has(&d, "quokka"),
          "a word that Define added is in the list");
    CHECK(spell_dict_has(&d, "Quokka") == 1 && spell_dict_has(&d, "zebra"),
          "the spell check reads the words");
    spell_dict_free(&d);
    spell_dict_free(&e);

    /* An old file in any order, with blank lines and a word with a space. */
    put_file(path, "pear\n\nApple\r\nbad word\nbanana\n");
    memset(&d, 0, sizeof d);
    dictlist_load(&d);
    CHECK(d.n == 4 && strcmp(d.w[0], "Apple") == 0 &&
              strcmp(d.w[1], "bad word") == 0,
          "an old file is read as it is and put in order, the damaged line "
          "too: n=%d",
          d.n);
    dictlist_remove(&d, dictlist_find(&d, "bad word"));
    CHECK(dictlist_save(&d, err, sizeof err) == 0, "save again");
    read_file(path, text, sizeof text);
    CHECK(strcmp(text, "Apple\nbanana\npear\n") == 0,
          "the damaged line is gone after a delete: %s", text);
    spell_dict_free(&d);

    /* An empty list is an empty file. */
    memset(&d, 0, sizeof d);
    CHECK(dictlist_save(&d, err, sizeof err) == 0, "save an empty list");
    CHECK(stat(path, &st) == 0 && st.st_size == 0, "an empty file");
}


static void
test_import_export(void)
{
    SpellDict d;
    DictImport r;
    char path[PATH_MAX];
    char imp[PATH_MAX];
    char err[200];
    char big[2200];

    fresh_home();
    snprintf(imp, sizeof imp, "%s/words.txt", home);
    memset(&d, 0, sizeof d);
    dictlist_add(&d, "lemon");
    dictlist_add(&d, "mango");

    memset(big, 'x', sizeof big - 1);
    big[sizeof big - 1] = '\0';

    put_file(imp, "\xEF\xBB\xBFkiwi\r\nLemon\nmango\nfig\nfig\n\n  papaya  \n"
                  "two words\nroom101\n-dash\nna\xC3\xAFve\nGuava\nguava\n");
    CHECK(dictlist_import(&d, imp, &r, err, sizeof err) == 0, "import: %s",
          err);
    CHECK(r.added == 5, "added kiwi, fig, papaya, naive, guava: %d", r.added);
    CHECK(r.duplicate == 4, "Lemon is covered, mango is there, fig twice and "
                            "Guava is covered by guava: %d",
          r.duplicate);
    CHECK(r.refused == 3, "a space, a digit, a hyphen first: %d", r.refused);
    CHECK(has(&d, "kiwi") && has(&d, "papaya") && has(&d, "na\x8Bve") != 0,
          "the words are in the list");
    CHECK(has(&d, "guava") && !has(&d, "Guava"),
          "the lower case entry stands for the capital one");

    /* The list of the earlier program: CR LF lines and a Ctrl-Z at the end. */
    {
        SpellDict old;

        memset(&old, 0, sizeof old);
        put_file(imp, "Plumquat\r\nplumquat\r\nBLORF\r\nquimbleton\r\n\x1a");
        CHECK(dictlist_import(&old, imp, &r, err, sizeof err) == 0,
              "import 1A");
        CHECK(r.added == 3 && r.refused == 0 && r.duplicate == 1,
              "Ctrl-Z ends the list and is not counted as a line: added %d "
              "refused %d duplicate %d",
              r.added, r.refused, r.duplicate);
        spell_dict_free(&old);
    }

    {
        FILE *f = fopen(imp, "w");

        fputs("short\n", f);
        fputs(big, f);
        fputs("\nlast\n", f);
        fclose(f);
    }

    CHECK(dictlist_import(&d, imp, &r, err, sizeof err) == 0, "import long");
    CHECK(r.added == 2 && r.refused == 1,
          "a line of 2000 bytes is refused, the lines round it are read: "
          "added %d refused %d",
          r.added, r.refused);

    CHECK(dictlist_import(&d, "/nonexistent/words.txt", &r, err,
                          sizeof err) == -1 &&
              strstr(err, "Cannot read") != NULL,
          "a file that is not there: %s", err);

    snprintf(path, sizeof path, "%s/out.txt", home);
    CHECK(dictlist_export(&d, path, err, sizeof err) == 0, "export: %s", err);
    {
        char t2[2048];

        read_file(path, t2, sizeof t2);
        CHECK(strncmp(t2, "fig\nguava\nkiwi\nlast\nlemon\nmango\n", 20) == 0,
              "the export is one word on a line, in order: %.60s", t2);
    }

    {
        SpellDict e;
        DictImport r2;

        memset(&e, 0, sizeof e);
        CHECK(dictlist_import(&e, path, &r2, err, sizeof err) == 0 &&
                  r2.added == d.n && r2.refused == 0 && r2.duplicate == 0,
              "the export can be imported again: %d of %d", r2.added, d.n);
        spell_dict_free(&e);
    }

    snprintf(path, sizeof path, "%s/no-such-dir/out.txt", home);
    CHECK(dictlist_export(&d, path, err, sizeof err) == -1 &&
              strstr(err, "Cannot write") != NULL,
          "export to a directory that is not there: %s", err);

    /* An export over a file replaces it whole and leaves no scrap. */
    snprintf(path, sizeof path, "%s/over.txt", home);
    put_file(path, "old\n");
    CHECK(dictlist_export(&d, path, err, sizeof err) == 0, "export over");
    {
        char t3[2048];
        char scrap[PATH_MAX + 64];
        long pid = getpid();

        read_file(path, t3, sizeof t3);
        CHECK(strstr(t3, "old") == NULL && strstr(t3, "mango") != NULL,
              "the old file is replaced");
        snprintf(scrap, sizeof scrap, "%s.%ld.tmp", path, pid);
        CHECK(access(scrap, F_OK) != 0, "no scrap file is left");
    }

    spell_dict_free(&d);
}


/* A word of letters only, number i. */
static void
word_of(int i, char *out)
{
    out[0] = 'w';

    for (int k = 4; k >= 1; k--) {
        out[k] = chr('a' + i % 26);
        i /= 26;
    }

    out[5] = '\0';
}


static void
test_big(void)
{
    SpellDict d;
    SpellDict e;
    DictImport r;
    char imp[PATH_MAX];
    char err[200];
    FILE *f;
    long t0;
    long t_import;
    long t_save;
    long t_load;
    long t_find;

    fresh_home();
    snprintf(imp, sizeof imp, "%s/big.txt", home);
    f = fopen(imp, "w");

    for (int i = 0; i < 100000; i++) {
        char w[8];

        word_of((i * 7919) % 100000, w);
        fprintf(f, "%s\n", w);
    }

    fclose(f);
    memset(&d, 0, sizeof d);
    t0 = now_ms();
    CHECK(dictlist_import(&d, imp, &r, err, sizeof err) == 0, "import big");
    t_import = now_ms() - t0;
    CHECK(r.added == 100000 && d.n == 100000 && r.duplicate == 0,
          "100000 words: added %d, n %d", r.added, d.n);

    for (int i = 1; i < d.n; i++) {
        if (strcmp(d.w[i - 1], d.w[i]) >= 0) {
            CHECK(0, "the big list is in order at %d", i);
            break;
        }
    }

    t0 = now_ms();
    CHECK(dictlist_save(&d, err, sizeof err) == 0, "save big: %s", err);
    t_save = now_ms() - t0;
    memset(&e, 0, sizeof e);
    t0 = now_ms();
    dictlist_load(&e);
    t_load = now_ms() - t0;
    CHECK(e.n == 100000, "the big list is read back: %d", e.n);

    t0 = now_ms();

    for (int i = 0; i < 1000; i++) {
        char w[8];

        word_of(i * 97, w);

        if (dictlist_find(&e, w) < 0 || dictlist_prefix(&e, "wbc") < 0) {
            CHECK(0, "find in the big list");
            break;
        }
    }

    t_find = now_ms() - t0;
    CHECK(dictlist_add(&e, "zzzz") == 1 && dictlist_find(&e, "zzzz") >= 0,
          "a word is added to the big list");
    printf("big list of 100000 words: import %ld ms, save %ld ms, load %ld ms, "
           "1000 finds %ld ms\n",
           t_import, t_save, t_load, t_find);
    CHECK(t_import < 5000 && t_save < 5000 && t_load < 5000,
          "the big list is handled in seconds");
    spell_dict_free(&d);
    spell_dict_free(&e);
}


/* ===================================================== hyphenation list */
static void
test_hyph_words(void)
{
    char why[96];
    char plain[HYPH_MAXWORD + 2];
    int at[8];
    char longw[100];

    CHECK(hyph_check("hy-phen-a-tion", why, sizeof why) == 1, "hyph: %s", why);
    CHECK(hyph_check("Jacksonville", why, sizeof why) == 1,
          "hyph: a word with no hyphen (never broken)");
    CHECK(hyph_check("=Jacksonville", why, sizeof why) == 1,
          "hyph: the equals sign in front");
    CHECK(hyph_check("caf\x82-in", why, sizeof why) == 1,
          "hyph: an accented letter of code page 437");
    CHECK(hyph_check("", why, sizeof why) == 0, "hyph: empty");
    CHECK(hyph_check("=", why, sizeof why) == 0, "hyph: only the equals sign");
    CHECK(hyph_check("-ab", why, sizeof why) == 0, "hyph: a leading hyphen");
    CHECK(hyph_check("ab-", why, sizeof why) == 0, "hyph: a final hyphen");
    CHECK(hyph_check("ab--cd", why, sizeof why) == 0, "hyph: two in a row");
    CHECK(hyph_check("ab cd", why, sizeof why) == 0, "hyph: a space");
    CHECK(hyph_check("ab1-cd", why, sizeof why) == 0 &&
              strstr(why, "digit") != NULL,
          "hyph: a digit, with the reason: %s", why);
    CHECK(hyph_check("don't", why, sizeof why) == 0 &&
              strstr(why, "apostrophe") != NULL,
          "hyph: an apostrophe, with the reason: %s", why);
    CHECK(hyph_check("=ab-cd", why, sizeof why) == 0,
          "hyph: never break and a break place");
    CHECK(hyph_check("a=b", why, sizeof why) == 0, "hyph: = inside");
    memset(longw, 'a', sizeof longw - 1);
    longw[sizeof longw - 1] = '\0';
    CHECK(hyph_check(longw, why, sizeof why) == 0, "hyph: too long");

    CHECK(hyph_same_word("hy-phen", "HYPHEN") == 1 &&
              hyph_same_word("hy-phen", "hyp-hen") == 1 &&
              hyph_same_word("=hyphen", "hyphen") == 1 &&
              hyph_same_word("hyphen", "hyphens") == 0,
          "hyph: the same letters are the same word");
    CHECK(hyph_compare("ab-c", "abd") < 0 && hyph_compare("abd", "ab-c") > 0 &&
              hyph_compare("Ab", "aB") != 0,
          "hyph: the order goes by the letters, then as typed");
    CHECK(hyph_places("hy-phen-a-tion", plain, sizeof plain, at, 8) == 3 &&
              strcmp(plain, "hyphenation") == 0 && at[0] == 2 && at[1] == 6 &&
              at[2] == 7,
          "hyph: the places are 2, 6, 7 in '%s'", plain);
    CHECK(hyph_places("=Jacksonville", plain, sizeof plain, at, 8) == 0 &&
              strcmp(plain, "Jacksonville") == 0,
          "hyph: a never-break word has no place");
}


static void
test_hyph_list(void)
{
    SpellDict d;
    DictImport r;
    char err[200];
    char imp[PATH_MAX];
    char hf[PATH_MAX - 64];
    char text[512];

    fresh_home();
    memset(&d, 0, sizeof d);
    CHECK(dicthyph_add(&d, "pro-ject") == 1 && dicthyph_add(&d, "ab-cd") == 1 &&
              dicthyph_add(&d, "=Zed") == 1,
          "hyph list: add");
    CHECK(dicthyph_add(&d, "proj-ect") == 0 && dicthyph_add(&d, "PROJECT") == 0,
          "hyph list: the same letters are the same word (the first stays)");
    CHECK(d.n == 3 && strcmp(d.w[0], "ab-cd") == 0 &&
              strcmp(d.w[1], "pro-ject") == 0 && strcmp(d.w[2], "=Zed") == 0,
          "hyph list: in order of the letters: %s %s %s", d.w[0], d.w[1],
          d.w[2]);
    CHECK(dicthyph_find(&d, "zed") == 2 && dicthyph_find(&d, "nope") == -1,
          "hyph list: find by the letters");
    CHECK(dicthyph_prefix(&d, "pr") == 1 && dicthyph_prefix(&d, "q") == -1,
          "hyph list: the prefix skips the hyphens");
    CHECK(dicthyph_save(&d, err, sizeof err) == 0, "hyph list: save: %s", err);
    config_directory(hf, sizeof hf);
    snprintf(imp, sizeof imp, "%s/hyphenation", hf);
    read_file(imp, text, sizeof text);
    CHECK(strcmp(text, "ab-cd\npro-ject\n=Zed\n") == 0,
          "hyph list: the file has one entry on a line: '%s'", text);
    spell_dict_free(&d);
    dicthyph_load(&d);
    CHECK(d.n == 3 && strcmp(d.w[1], "pro-ject") == 0,
          "hyph list: loaded again");
    dictlist_remove(&d, 1);
    CHECK(d.n == 2 && dicthyph_find(&d, "project") == -1, "hyph list: remove");

    /* A line of the file that is not an entry is not loaded. */
    put_file(imp, "good-word\nbad word\nx1-y\n\n-bad\nfine\n");
    spell_dict_free(&d);
    dicthyph_load(&d);
    CHECK(d.n == 2 && strcmp(d.w[0], "fine") == 0 &&
              strcmp(d.w[1], "good-word") == 0,
          "hyph list: lines that are no entry are left out: %d", d.n);
    spell_dict_free(&d);

    /* Import of a text file, in UTF-8 and in code page 437. */
    memset(&d, 0, sizeof d);
    snprintf(imp, sizeof imp, "%s/hyphwords.txt", home);
    put_file(imp, "\xEF\xBB\xBF" "for-mat\nFOR-MAT\nin-ter-na-tion-al\n"
                  "=Texas\ncaf\xC3\xA9-in\nbad1\n");
    CHECK(dicthyph_import(&d, imp, &r, err, sizeof err) == 0, "hyph import");
    CHECK(r.added == 4 && r.duplicate == 1 && r.refused == 1,
          "hyph import: added %d duplicate %d refused %d", r.added,
          r.duplicate, r.refused);
    CHECK(dicthyph_find(&d, "caf\x82in") >= 0,
          "hyph import: UTF-8 accents become code page 437 letters");
    spell_dict_free(&d);
    memset(&d, 0, sizeof d);
    put_file(imp, "caf\x82-in\nstra\xe1" "e\nna\x8b-ve\n");
    CHECK(dicthyph_import(&d, imp, &r, err, sizeof err) == 0 && r.added == 3 &&
              r.refused == 0 && dicthyph_find(&d, "caf\x82in") >= 0 &&
              dicthyph_find(&d, "stra\xe1" "e") >= 0,
          "hyph import: a list in code page 437 keeps its letters (added %d "
          "refused %d)",
          r.added, r.refused);
    spell_dict_free(&d);

    /* The dictionary of the earlier program. */
    memset(&d, 0, sizeof d);
    CHECK(dicthyph_import(&d, "tests/data/custom.hyc", &r, err,
                          sizeof err) == 0,
          "hyc import: %s", err);
    CHECK(r.added == 5 && r.refused == 1 && r.duplicate == 0,
          "hyc import: the word with a digit is refused: added %d refused %d",
          r.added, r.refused);
    CHECK(dicthyph_find(&d, "zorbaflux") >= 0 &&
              strcmp(d.w[dicthyph_find(&d, "zorbaflux")], "zor-ba-flux") == 0 &&
              strcmp(d.w[dicthyph_find(&d, "xy")], "x-y") == 0 &&
              strcmp(d.w[dicthyph_find(&d, "prefixed")], "pre-fix-ed") == 0,
          "hyc import: the places of the mask");
    CHECK(dicthyph_import(&d, "tests/data/custom.hyc", &r, err,
                          sizeof err) == 0 &&
              r.added == 0 && r.duplicate == 5,
          "hyc import again adds nothing: %d", r.added);
    spell_dict_free(&d);
    memset(&d, 0, sizeof d);
    CHECK(dicthyph_import(&d, "tests/data/custom.hyc.missing", &r, err,
                          sizeof err) == -1,
          "hyc import: a file that is not there");
    snprintf(imp, sizeof imp, "%s/short.hyc", home);
    put_file(imp, "not a dictionary");
    CHECK(dicthyph_import(&d, imp, &r, err, sizeof err) == -1 &&
              strstr(err, "hyphenation dictionary") != NULL,
          "hyc import: a damaged file is refused: %s", err);
    spell_dict_free(&d);
}


/* The words of the file as the Printer writes them. */
static void
test_hyph_tex(void)
{
    int skipped = 0;
    char *t;

    t = hyph_tex_of("pro-ject\n=Texas\nre-fresh\nbad word\ncaf\xC3\xA9-in\n"
                    "\xE2\x88\x91-sum\n",
                    &skipped);
    CHECK(t != NULL && strcmp(t, "\\hyphenation{pro-ject Texas re-fresh "
                                 "caf\xC3\xA9-in}\n") == 0,
          "tex: the words, a never-break word without the sign: %s",
          t != NULL ? t : "(none)");
    CHECK(skipped == 2, "tex: a line with a space and one with a character "
                        "outside Latin-1 are left out: %d", skipped);
    free(t);
    CHECK(hyph_tex_of("\n \nbad word\n", &skipped) == NULL,
          "tex: no usable word, no command");
    t = hyph_tex_of("a-b\r\nc-d\r\n", &skipped);
    CHECK(t != NULL && strcmp(t, "\\hyphenation{a-b c-d}\n") == 0,
          "tex: CR LF lines");
    free(t);

    fresh_home();
    CHECK(hyph_tex() == NULL, "tex: no file, no command");

    char hf[PATH_MAX - 64];
    char path[PATH_MAX];

    config_directory(hf, sizeof hf);
    snprintf(path, sizeof path, "%s/hyphenation", hf);
    put_file(path, "pro-ject\n");
    t = hyph_tex();
    CHECK(t != NULL && strcmp(t, "\\hyphenation{pro-ject}\n") == 0,
          "tex: read from the settings folder");
    free(t);
}


/* The words of a list of the earlier program have their accents. */
static void
test_old_codepage(void)
{
    SpellDict d;
    DictImport r;
    char err[200];
    char imp[PATH_MAX];

    fresh_home();
    memset(&d, 0, sizeof d);
    snprintf(imp, sizeof imp, "%s/old.spc", home);
    put_file(imp, "Caf\x82\r\n\x81" "ber\r\nna\x8b" "ve\r\nstra\xe1" "e\r\n"
                  "plain\r\n\x1a");
    CHECK(dictlist_import(&d, imp, &r, err, sizeof err) == 0, "old: %s", err);
    CHECK(r.added == 5 && r.refused == 0,
          "old code page: the accents are letters: added %d refused %d",
          r.added, r.refused);
    CHECK(has(&d, "Caf\x82") && has(&d, "\x81" "ber") &&
              has(&d, "stra\xe1" "e"),
          "old code page: the words are in the list as typed");
    spell_dict_free(&d);

    /* The same words in UTF-8 are the same words. */
    memset(&d, 0, sizeof d);
    put_file(imp, "Caf\xC3\xA9\n\xC3\xBC" "ber\nstra\xC3\x9F" "e\n");
    CHECK(dictlist_import(&d, imp, &r, err, sizeof err) == 0 && r.added == 3,
          "UTF-8: three words (added %d)", r.added);
    CHECK(has(&d, "Caf\x82") && has(&d, "\x81" "ber") &&
              has(&d, "stra\xe1" "e"),
          "UTF-8 and the old code page give the same entries");
    spell_dict_free(&d);

    /* One byte that is not UTF-8 makes the whole file the old code page. */
    memset(&d, 0, sizeof d);
    put_file(imp, "na\xC3\xAFve\ncaf\x82\n");
    CHECK(dictlist_import(&d, imp, &r, err, sizeof err) == 0, "mixed");
    CHECK(r.refused == 1 && r.added == 1 && has(&d, "caf\x82"),
          "a file with a byte that is not UTF-8 is read as code page 437: "
          "added %d refused %d",
          r.added, r.refused);
    spell_dict_free(&d);
}


int
main(void)
{
    test_words();
    test_order();
    test_save_load();
    test_import_export();
    test_big();
    test_hyph_words();
    test_hyph_list();
    test_hyph_tex();
    test_old_codepage();
    printf("dicttest: %d checks, %d failures\n", checks, fails);

    return fails != 0 ? 1 : 0;
}
