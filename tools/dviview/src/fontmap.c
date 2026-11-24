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
 * The pdftex.map file and the encoding vectors it names.
 */
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "dviview.h"

static char *map_text;
static int map_tried;


static void
map_read(void)
{
    char *path;
    size_t len;

    map_tried = 1;
    path = kpse_find("pdftex.map", NULL, NULL);

    if (path == NULL) {
        viewer_tell("note",
                    "pdftex.map not found: the Viewer cannot tell which "
                    "outline fonts to use and draws other shapes; the PDF "
                    "is not affected.  Check that TeX Live is installed");
        return;
    }

    map_text = (char *)read_file(path, &len);
    free(path);
}


/* Interprets the quoted part of a map line: SlantFont and ExtendFont. */
static void
parse_quoted(char *q, MapEntry *m)
{
    char *save = NULL;
    char *prev = NULL;
    char *tok = strtok_r(q, " \t", &save);

    while (tok != NULL) {
        if (prev != NULL && strcmp(tok, "SlantFont") == 0) {
            m->slant = atof(prev);
        } else if (prev != NULL && strcmp(tok, "ExtendFont") == 0) {
            m->extend = atof(prev);
        }

        prev = tok;
        tok = strtok_r(NULL, " \t", &save);
    }
}


static int
parse_line(char *line, const char *name, MapEntry *m)
{
    char *p = line;
    size_t n = strlen(name);

    if (strncmp(p, name, n) != 0 || (p[n] != ' ' && p[n] != '\t')) {
        return 0;
    }

    p += n;
    m->slant = 0.0;
    m->extend = 1.0;
    m->pfb = NULL;
    m->enc = NULL;

    while (*p != '\0') {
        while (*p == ' ' || *p == '\t') {
            p++;
        }

        if (*p == '"') {
            char *e = strchr(p + 1, '"');
            char tmp[512];
            size_t l;

            if (e == NULL) {
                break;
            }

            l = to_size(e - p - 1);

            if (l >= sizeof tmp) {
                l = sizeof tmp - 1;
            }

            memcpy(tmp, p + 1, l);
            tmp[l] = '\0';
            parse_quoted(tmp, m);
            p = e + 1;
        } else if (*p == '<') {
            char *e = p;
            char file[256];
            size_t l;
            size_t k;

            while (*e != '\0' && *e != ' ' && *e != '\t') {
                e++;
            }

            k = 0;

            while (p[k] == '<' || p[k] == '[') {
                k++;
            }

            l = to_size(e - p) - k;

            if (l >= sizeof file) {
                l = sizeof file - 1;
            }

            memcpy(file, p + k, l);
            file[l] = '\0';
            p = e;

            if (l > 4 && strcmp(file + l - 4, ".enc") == 0) {
                m->enc = xstrdup(file);
            } else if (l > 4 && (strcmp(file + l - 4, ".pfb") == 0 ||
                                 strcmp(file + l - 4, ".pfa") == 0)) {
                m->pfb = xstrdup(file);
            }
        } else {
            while (*p != '\0' && *p != ' ' && *p != '\t') {
                p++;
            }
        }
    }

    return 1;
}


int
map_lookup(const char *tfmname, MapEntry *out)
{
    const char *p;

    if (!map_tried) {
        map_read();
    }

    if (map_text == NULL) {
        return 0;
    }

    p = map_text;

    while (*p != '\0') {
        const char *e = strchr(p, '\n');
        size_t l = e != NULL ? to_size(e - p) : strlen(p);

        if (l > 0 && l < 1000 && strncmp(p, tfmname, strlen(tfmname)) == 0) {
            char line[1024];

            memcpy(line, p, l);
            line[l] = '\0';

            if (parse_line(line, tfmname, out)) {
                return out->pfb != NULL;
            }
        }

        if (e == NULL) {
            break;
        }

        p = e + 1;
    }

    return 0;
}


/* Reads an encoding vector: 256 glyph names (NULL for .notdef). */
char **
enc_load(const char *name)
{
    char *path = kpse_find(name, NULL, NULL);
    size_t len;
    char *txt;
    char **v;
    char *p;
    char *e;
    int i;

    if (path == NULL) {
        return NULL;
    }

    txt = (char *)read_file(path, &len);
    free(path);

    if (txt == NULL) {
        return NULL;
    }

    /* Remove comments so that their text is never taken as glyph names. */
    for (p = txt; *p != '\0'; p++) {
        if (*p == '%') {
            while (*p != '\0' && *p != '\n') {
                *p++ = ' ';
            }

            if (*p == '\0') {
                break;
            }
        }
    }

    v = xcalloc(256, sizeof *v);
    p = strchr(txt, '[');

    if (p == NULL) {
        free(txt);
        free(v);
        return NULL;
    }

    e = strchr(p, ']');
    i = 0;
    p++;

    while (i < 256 && *p != '\0' && (e == NULL || p < e)) {
        if (*p == '/') {
            char *s = p + 1;

            p = s;

            while (*p != '\0' && !isspace((unsigned char)*p) &&
                   *p != '/' && *p != ']') {
                p++;
            }

            {
                size_t l = to_size(p - s);

                if (l > 0 && !(l == 7 && strncmp(s, ".notdef", 7) == 0)) {
                    v[i] = xcalloc(l + 1, 1);
                    memcpy(v[i], s, l);
                }
            }

            i++;
        } else {
            p++;
        }
    }

    free(txt);

    return v;
}
