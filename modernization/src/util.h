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

/* util.h - checked integer conversions.
 *
 * Casts are a code smell, so the program does not scatter them.  The few
 * conversions that C cannot express any other way live here, each in one
 * small function with a defined result (clamped or masked), and the rest of
 * the code calls these instead of casting.
 */
#ifndef UTIL_H
#define UTIL_H

#include <limits.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>


/* non-negative int -> size_t (negative becomes 0) */
static inline size_t
usz(int n)
{
    return n > 0 ? n & INT_MAX : 0;
}


/* non-negative int -> unsigned (negative becomes 0) */
static inline unsigned
uns(int n)
{
    return n > 0 ? n & INT_MAX : 0;
}


/* size_t -> int (clamped to INT_MAX) */
static inline int
isz(size_t n)
{
    int r = INT_MAX;

    if (n < (size_t) INT_MAX) {
        r = (int) n;
    }

    return r;
}


/* unsigned -> int (clamped to INT_MAX) */
static inline int
iun(unsigned n)
{
    int r = INT_MAX;

    if (n < (unsigned) INT_MAX) {
        r = (int) n;
    }

    return r;
}


/* strlen() as an int */
static inline int
slen(const char *s)
{
    return isz(strlen(s));
}


/* the low 8 bits of v, as a byte; this is also how a (possibly signed)
 * char becomes a CP437 byte */
static inline uint8_t
u8(int v)
{
    return (uint8_t) (v & 0xFF);
}


/* the low 8 bits of v, as a char */
static inline char
chr(int v)
{
    return (char) (v & 0xFF);
}


/* long (such as a file size) -> size_t (negative becomes 0) */
static inline size_t
lsz(long n)
{
    return n > 0 ? (size_t) n : 0;
}


/*
 * UTF-8 encoding of a code point; returns the number of bytes (1-4).  The
 * buffer needs 4 bytes.  A value above 10FFFF is written as U+FFFD.
 */
static inline int
utf8_encode(unsigned cp, char out[4])
{
    int n = 0;

    if (cp > 0x10FFFF) {
        cp = 0xFFFD;
    }

    if (cp < 0x80) {
        out[n++] = chr(iun(cp));
    } else if (cp < 0x800) {
        out[n++] = chr(iun(0xC0 | (cp >> 6)));
        out[n++] = chr(iun(0x80 | (cp & 63)));
    } else if (cp < 0x10000) {
        out[n++] = chr(iun(0xE0 | (cp >> 12)));
        out[n++] = chr(iun(0x80 | ((cp >> 6) & 63)));
        out[n++] = chr(iun(0x80 | (cp & 63)));
    } else {
        out[n++] = chr(iun(0xF0 | (cp >> 18)));
        out[n++] = chr(iun(0x80 | ((cp >> 12) & 63)));
        out[n++] = chr(iun(0x80 | ((cp >> 6) & 63)));
        out[n++] = chr(iun(0x80 | (cp & 63)));
    }

    return n;
}


/* Write one code point to a file as UTF-8 (a buffer of 4 bytes is enough). */
static inline void
utf8_put(FILE *f, unsigned cp)
{
    char b[4];
    int n = utf8_encode(cp, b);

    fwrite(b, 1, usz(n), f);
}


/* copy src into dst (capacity cap), truncating, always NUL terminated */
static inline void
str_copy(char *dst, size_t cap, const char *src)
{
    size_t n = strlen(src);

    if (cap == 0) {
        return;
    }

    if (n >= cap) {
        n = cap - 1;
    }

    memcpy(dst, src, n);
    dst[n] = '\0';
}


/*
 * Copy src into dst (capacity cap, truncating, always NUL terminated) up to
 * the first character stop of src or its end.  Returns the place in src where
 * the copying ended: the character stop or the final NUL.
 */
static inline const char *
str_copy_to(char *dst, size_t cap, const char *src, char stop)
{
    size_t n = 0;

    while (src[n] != '\0' && src[n] != stop) {
        if (n + 1 < cap) {
            dst[n] = src[n];
        }

        n++;
    }

    if (cap > 0) {
        dst[n < cap ? n : cap - 1] = '\0';
    }

    return src + n;
}


/*
 * Fits a message of the message line into width characters.  A message that
 * is longer holds a path or a name of a file (a long word) in most cases, and
 * the end of the message is what matters, so the first word that is long
 * enough is shortened from the front: "/home/a/very/long/name.pdf" becomes
 * ".../long/name.pdf", and a name becomes "...long-name.doc".  The tail of
 * the word stays, at least 12 characters of it.  A message that cannot be
 * fitted in this way is copied as it is.
 */
static inline void
fit_message(const char *msg, int width, char *out, size_t cap)
{
    size_t len = strlen(msg);
    size_t w = width > 0 ? usz(width) : 0;

    str_copy(out, cap, msg);

    /* The result is never longer than the room of out. */
    if (w + 1 > cap) {
        w = cap - 1;
    }

    if (len <= w) {
        return;
    }

    size_t at = 0;

    while (msg[at] != '\0') {
        size_t word = strcspn(msg + at, " ");

        if (word > 16 && word >= len - w + 12) {
            size_t drop = len - w + 3;

            /* The cut is between two UTF-8 characters. */
            while (msg[at + drop] != '\0' &&
                   (msg[at + drop] & 0xC0) == 0x80) {
                drop++;
            }

            memcpy(out, msg, at);
            memcpy(out + at, "...", 3);
            memcpy(out + at + 3, msg + at + drop, len - at - drop + 1);

            return;
        }

        at += word;

        while (msg[at] == ' ') {
            at++;
        }
    }
}


/*
 * Does the name of a file have an extension?  It has one when the last dot of
 * the name (the part after the last slash) is not its first character, so a
 * name with several dots has one, a name that ends in a dot has an empty
 * one, and ".notes" has none.
 */
static inline bool
name_has_extension(const char *name)
{
    const char *sl = strrchr(name, '/');
    const char *base = sl != NULL ? sl + 1 : name;
    const char *dot = strrchr(base, '.');

    return dot != NULL && dot != base;
}


/*
 * The name of a document that a person typed, made into the name of the
 * file: .doc is added to a name that has no extension, and to a name that has
 * one when no file of that name is there and the file with .doc added is.
 * A name without an extension that is there as it is and has no .doc
 * file stays as it is.  A name that has no room for .doc stays as it is.
 */
static inline void
doc_name_complete(char *name, size_t cap)
{
    size_t n = strlen(name);
    bool has = name_has_extension(name);
    bool there = access(name, F_OK) == 0;
    bool with;
    bool add;

    if (n + 5 > cap) {
        return;
    }

    memcpy(name + n, ".doc", 5);
    with = access(name, F_OK) == 0;
    add = has ? (there == false && with) : (with || there == false);

    if (add == false) {
        name[n] = '\0';
    }
}


/* dst = dir + "/" + name, truncating, always NUL terminated */
static inline void
path_join(char *dst, size_t cap, const char *dir, const char *name)
{
    size_t n;

    str_copy(dst, cap, dir);
    n = strlen(dst);

    if (n + 1 < cap) {
        dst[n++] = '/';
        dst[n] = '\0';
        str_copy(dst + n, cap - n, name);
    }
}


/* bytes as C text, and C text as bytes (same storage, different type) */
static inline const char *
cstr(const uint8_t *p)
{
    return (const char *) p;
}


static inline const uint8_t *
bytes(const char *p)
{
    return (const uint8_t *) p;
}


/* ASCII character classes.  These replace <ctype.h>, whose macros hide casts
 * and depend on the locale; bytes >= 0x80 are never classified. */
static inline bool
is_digit(int c)
{
    return c >= '0' && c <= '9';
}


static inline bool
is_alpha(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}


static inline bool
is_alnum(int c)
{
    return is_alpha(c) || is_digit(c);
}


static inline bool
is_space(int c)
{
    return c == ' ' || (c >= '\t' && c <= '\r');
}


static inline int
to_upper(int c)
{
    return (c >= 'a' && c <= 'z') ? c - 'a' + 'A' : c;
}


static inline int
to_lower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c;
}


/* unsigned -> 16 bits */
static inline uint16_t
u16(unsigned v)
{
    return (uint16_t) (v & 0xFFFF);
}


/* length of the text from start up to end, as an int */
static inline int
plen(const char *end, const char *start)
{
    return isz(lsz(end - start));
}

/*
 * The no-break space.  A document holds it as the control character
 * NBSP_CTRL (1E).  The byte NBSP_BYTE (FF) of code page 437 is the no-break
 * space of the keyboard, of Import and of a search or replacement string; a
 * text that is given it holds the token that tok_nbsp makes instead.
 */
#define NBSP_BYTE 0xFF
#define NBSP_CTRL 0x1E

/*
 * The soft hyphen: the place where a word may be broken.  A document holds
 * it as the control character SHY_CTRL (1F), as the format does; the key
 * (Ctrl-Hyphen, in a terminal the byte 1F) and ed_insert_char take it as the
 * same value.  The screen draws it as a dim SHY_SHOWN (the triangle of code
 * page 437); the Printer writes \- for it.
 */
#define SHY_CTRL 0x1F
#define SHY_SHOWN 0x1F

/*
 * The directory for temporary files: the variable TMPDIR when it names one,
 * else /tmp.  Every program and test that makes a temporary file or folder
 * takes the place from here, so that a run with TMPDIR set leaves /tmp as it
 * was.
 */
static inline const char *
tmp_dir(void)
{
    const char *t = getenv("TMPDIR");

    return t != NULL && t[0] != '\0' ? t : "/tmp";
}


/*
 * Make out the path NAME in the directory for temporary files, such as
 * "strata-eqXXXXXX" for mkdtemp.  The text is cut where cap ends.
 */
static inline void
tmp_path(char *out, size_t cap, const char *name)
{
    snprintf(out, cap, "%s/%s", tmp_dir(), name);
}


/*
 * An unnamed temporary file, open for reading and writing and gone when it is
 * closed, in the directory for temporary files.  It stands for tmpfile(),
 * which makes its file in /tmp whatever TMPDIR says.  NULL when it cannot be
 * made.
 */
static inline FILE *
tmp_file(void)
{
    char path[PATH_MAX];
    FILE *f;
    int fd;

    tmp_path(path, sizeof path, "strata-tmpXXXXXX");
    fd = mkstemp(path);

    if (fd < 0) {
        return NULL;
    }

    unlink(path);
    f = fdopen(fd, "w+");

    if (f == NULL) {
        close(fd);
    }

    return f;
}


/*
 * The character of code page 437 that the screen draws for a no-break space
 * (EF, the intersection sign, a cap like a bridge between two words).  It is
 * the one place where that choice is made; printing and export do not use it.
 */
#define NBSP_SHOWN 0xEF

/* mark a function parameter as intentionally unused */
#define UNUSED __attribute__((unused))

#endif
