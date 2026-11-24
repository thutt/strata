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
 * The DVI specials: papersize, colour, the pdf: family of dvipdfmx (pictures,
 * transformations, raw PDF operators), the dvips specials (psfile, ps:, ps::,
 * the header lines, landscape) and emTeX graphics.  Pictures and graphics
 * become items of the display list; nothing is drawn here.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "dviview.h"

#define PI_VALUE 3.14159265358979323846


/* Parses a length such as 8.5in and returns it in inches. */
static int
parse_length(const char *s, double *out)
{
    char *end;
    double v = strtod(s, &end);

    if (end == s) {
        return 0;
    }

    if (strncmp(end, "in", 2) == 0) {
        v *= 72.27;
    } else if (strncmp(end, "cm", 2) == 0) {
        v *= 72.27 / 2.54;
    } else if (strncmp(end, "mm", 2) == 0) {
        v *= 72.27 / 25.4;
    } else if (strncmp(end, "bp", 2) == 0) {
        v *= 72.27 / 72.0;
    }

    *out = v / 72.27;

    return 1;
}


/* A length in bp; a number without a unit is in bp. */
static double
len_bp(const char *s)
{
    double in;
    char *end;
    double v = strtod(s, &end);

    if (end != s && *end == '\0') {
        return v;
    }

    return parse_length(s, &in) ? in * 72.0 : 0.0;
}


static unsigned long
pack3(double r, double g, double b)
{
    return rgb_pack(r, g, b);
}


static int
named_color(const char *n, unsigned long *rgb)
{
    static const struct {
        const char *name;
        unsigned long rgb;
    } tab[] = {
        { "black", 0x000000UL }, { "white", 0xffffffUL },
        { "red", 0xff0000UL }, { "green", 0x00ff00UL },
        { "blue", 0x0000ffUL }, { "cyan", 0x00ffffUL },
        { "magenta", 0xff00ffUL }, { "yellow", 0xffff00UL },
        { "gray", 0x808080UL }
    };
    size_t i;

    for (i = 0; i < sizeof tab / sizeof tab[0]; i++) {
        if (strcasecmp(n, tab[i].name) == 0) {
            *rgb = tab[i].rgb;
            return 1;
        }
    }

    return 0;
}


/* Turns numbers into a colour: one is gray, three rgb, four cmyk. */
static int
color_of(const double *v, int nv, unsigned long *rgb)
{
    if (nv == 1) {
        *rgb = pack3(v[0], v[0], v[0]);
    } else if (nv == 3) {
        *rgb = pack3(v[0], v[1], v[2]);
    } else if (nv == 4) {
        *rgb = cmyk_pack(v[0], v[1], v[2], v[3]);
    } else {
        return 0;
    }

    return 1;
}


static void
push_color(Ctx *c, unsigned long rgb)
{
    GState *s = gfx_cur(&c->g);

    if (c->ncolors < 256) {
        c->colors[c->ncolors][0] = s->fill;
        c->colors[c->ncolors][1] = s->stroke;
        c->ncolors++;
    }

    s->fill = rgb;
    s->stroke = rgb;
}


static void
pop_color(Ctx *c)
{
    GState *s = gfx_cur(&c->g);

    if (c->ncolors > 0) {
        c->ncolors--;
        s->fill = c->colors[c->ncolors][0];
        s->stroke = c->colors[c->ncolors][1];
    }
}


/* Handles a "color push" special; the words follow the word push. */
static void
color_push(Ctx *c, char *w2, char **save)
{
    double v[4] = { 0.0, 0.0, 0.0, 0.0 };
    unsigned long rgb = gfx_cur(&c->g)->fill;
    int i;
    int nv = strcmp(w2, "rgb") == 0 ? 3 : strcmp(w2, "gray") == 0 ?
             1 : strcmp(w2, "cmyk") == 0 ? 4 : 0;
    int ok = 0;

    for (i = 0; i < nv; i++) {
        char *t = strtok_r(NULL, " \t", save);

        if (t == NULL) {
            break;
        }

        v[i] = atof(t);

        if (i == nv - 1) {
            ok = 1;
        }
    }

    if (ok) {
        color_of(v, nv, &rgb);
    } else if (nv != 0 || !named_color(w2, &rgb)) {
        char what[96];

        snprintf(what, sizeof what, "color push %s", w2);
        dvi_report_special(what);
    }

    push_color(c, rgb);
}


static void
do_color(Ctx *c, char *s, size_t k)
{
    char *save = NULL;
    char *w1 = strtok_r(s + k, " \t", &save);
    char *w2 = strtok_r(NULL, " \t", &save);

    if (w1 != NULL && strcmp(w1, "pop") == 0) {
        pop_color(c);
    } else if (w1 != NULL && strcmp(w1, "push") == 0 && w2 != NULL) {
        color_push(c, w2, &save);
    } else {
        dvi_report_special("color");
    }
}


/* The description of a picture as the specials give it. */
typedef struct {
    char name[600];
    int page;
    int media_box;
    int has_bbox;
    double bbox[4];
    int clip;
    double width;
    double height;
    double xscale;
    double yscale;
    double angle;
} Pic;


static void
pic_init(Pic *p)
{
    memset(p, 0, sizeof *p);
    p->page = 1;
    p->xscale = 1.0;
    p->yscale = 1.0;
}


/* Puts the picture at the current position of the page. */
static void
add_picture(Ctx *c, const Pic *p)
{
    ItemList *l = c->out;
    ImgItem *im;
    Item it;
    Mat rot = mat_ident();
    Mat pre;
    GState *s = gfx_cur(&c->g);

    l->imgs = xrealloc(l->imgs, to_size(l->nimgs + 1) * sizeof *l->imgs);
    im = &l->imgs[l->nimgs];
    memset(im, 0, sizeof *im);
    im->name = xstrdup(p->name);
    im->page = p->page;
    im->media_box = p->media_box;
    im->has_bbox = p->has_bbox;
    memcpy(im->bbox, p->bbox, sizeof im->bbox);
    im->clip = p->clip;

    if (fabs(p->angle) > 1e-9) {
        double t = p->angle * PI_VALUE / 180.0;

        /* The picture turns counterclockwise as seen on the page. */
        rot.a = cos(t);
        rot.b = -sin(t);
        rot.c = sin(t);
        rot.d = cos(t);
    }

    pre = mat_mul(mat_mul(rot, mat_translate(c->g.px, c->g.py)), s->ctm);
    im->m = pre;
    im->width = p->width;
    im->height = p->height;
    im->xscale = p->xscale;
    im->yscale = p->yscale;
    memset(&it, 0, sizeof it);
    it.type = IT_IMAGE;
    it.ref = l->nimgs;
    it.clip = s->clip;
    l->nimgs++;
    dvi_add_item(c, &it);
}


/* Reads the next word or the (file name) of a pdf:image special. */
static const char *
next_word(const char *s, char *w, size_t sz)
{
    size_t k = 0;

    while (*s == ' ' || *s == '\t') {
        s++;
    }

    if (*s == '(') {
        int depth = 0;

        do {
            if (*s == '(') {
                depth++;

                if (depth == 1) {
                    w[k++] = '(';
                    s++;
                    continue;
                }
            } else if (*s == ')') {
                depth--;

                if (depth == 0) {
                    break;
                }
            }

            if (k + 2 < sz) {
                w[k++] = *s;
            }

            s++;
        } while (*s != '\0');

        if (*s == ')') {
            s++;
        }

        w[k] = '\0';

        return s;
    }

    while (*s != '\0' && *s != ' ' && *s != '\t' && k + 1 < sz) {
        w[k++] = *s++;
    }

    w[k] = '\0';

    return s;
}


/* Reads a pdf:image special: its words, then the (file) name. */
static void
do_pdf_image(Ctx *c, const char *s)
{
    Pic p;
    char w[640];
    char v[640];
    int i;

    pic_init(&p);

    for (;;) {
        s = next_word(s, w, sizeof w);

        if (w[0] == '\0' || (w[0] == '<' && w[1] == '<')) {
            break;
        }

        if (w[0] == '(') {
            snprintf(p.name, sizeof p.name, "%.590s", w + 1);
        } else if (strcmp(w, "page") == 0) {
            s = next_word(s, v, sizeof v);
            p.page = atoi(v) > 0 ? atoi(v) : 1;
        } else if (strcmp(w, "pagebox") == 0) {
            s = next_word(s, v, sizeof v);
            p.media_box = strcmp(v, "mediabox") == 0;
        } else if (strcmp(w, "bbox") == 0) {
            for (i = 0; i < 4; i++) {
                s = next_word(s, v, sizeof v);
                p.bbox[i] = len_bp(v);
            }

            p.has_bbox = 1;
        } else if (strcmp(w, "clip") == 0) {
            s = next_word(s, v, sizeof v);
            p.clip = atoi(v);
        } else if (strcmp(w, "width") == 0 || strcmp(w, "height") == 0) {
            double len;

            s = next_word(s, v, sizeof v);
            len = len_bp(v);

            if (w[0] == 'w') {
                p.width = len;
            } else {
                p.height = len;
            }
        } else if (strcmp(w, "scale") == 0 || strcmp(w, "xscale") == 0 ||
                   strcmp(w, "yscale") == 0) {
            double f;

            s = next_word(s, v, sizeof v);
            f = atof(v);

            if (w[0] == 's' || w[0] == 'x') {
                p.xscale = f;
            }

            if (w[0] == 's' || w[0] == 'y') {
                p.yscale = f;
            }
        } else if (strcmp(w, "rotate") == 0) {
            s = next_word(s, v, sizeof v);
            p.angle = atof(v);
        }
    }

    if (p.name[0] != '\0') {
        add_picture(c, &p);
    }
}


/* Reads key=value words of a psfile special into a picture. */
static void
do_psfile(Ctx *c, const char *s)
{
    Pic p;
    double hs = 100.0;
    double vs = 100.0;
    double rwi = 0.0;
    double rhi = 0.0;
    double v[4] = { 0.0, 0.0, 0.0, 0.0 };
    int have_box = 0;
    const char *q = s;

    pic_init(&p);

    while (*q != '\0') {
        char key[64];
        char val[600];
        size_t k = 0;
        size_t n = 0;

        while (*q == ' ' || *q == '\t') {
            q++;
        }

        while (*q != '\0' && *q != '=' && *q != ' ' && k + 1 < sizeof key) {
            key[k++] = *q++;
        }

        key[k] = '\0';

        if (*q == '=') {
            q++;

            if (*q == '"') {
                q++;

                while (*q != '\0' && *q != '"' && n + 1 < sizeof val) {
                    val[n++] = *q++;
                }

                if (*q == '"') {
                    q++;
                }
            } else {
                while (*q != '\0' && *q != ' ' && n + 1 < sizeof val) {
                    val[n++] = *q++;
                }
            }
        }

        val[n] = '\0';

        if (strcasecmp(key, "psfile") == 0) {
            snprintf(p.name, sizeof p.name, "%.590s", val);
        } else if (strcmp(key, "llx") == 0) {
            v[0] = atof(val);
            have_box = 1;
        } else if (strcmp(key, "lly") == 0) {
            v[1] = atof(val);
        } else if (strcmp(key, "urx") == 0) {
            v[2] = atof(val);
        } else if (strcmp(key, "ury") == 0) {
            v[3] = atof(val);
        } else if (strcmp(key, "rwi") == 0) {
            rwi = atof(val) / 10.0;
        } else if (strcmp(key, "rhi") == 0) {
            rhi = atof(val) / 10.0;
        } else if (strcmp(key, "hscale") == 0) {
            hs = atof(val);
        } else if (strcmp(key, "vscale") == 0) {
            vs = atof(val);
        } else if (strcmp(key, "angle") == 0) {
            p.angle = atof(val);
        } else if (strcmp(key, "clip") == 0) {
            p.clip = 1;
        } else if (strcmp(key, "page") == 0) {
            p.page = atoi(val) > 0 ? atoi(val) : 1;
        }
    }

    if (have_box) {
        memcpy(p.bbox, v, sizeof p.bbox);
        p.has_bbox = 1;
    }

    p.width = rwi;
    p.height = rhi;

    if (rwi <= 0.0 && rhi <= 0.0) {
        p.xscale = hs / 100.0;
        p.yscale = vs / 100.0;
    }

    if (p.name[0] != '\0') {
        add_picture(c, &p);
    }
}


/* The emTeX special of the form: em: graph file,width,height. */
static void
do_em_graph(Ctx *c, const char *s)
{
    Pic p;
    char buf[640];
    char *comma;
    char *comma2;

    pic_init(&p);
    snprintf(buf, sizeof buf, "%s", s);
    comma = strchr(buf, ',');

    if (comma != NULL) {
        *comma++ = '\0';
        comma2 = strchr(comma, ',');

        if (comma2 != NULL) {
            *comma2++ = '\0';
            p.height = len_bp(comma2);
        }

        p.width = len_bp(comma);
    }

    snprintf(p.name, sizeof p.name, "%.590s", buf);
    add_picture(c, &p);
}


/* Parses /Rotate from the text of a page dictionary. */
static void
find_rotate(Ctx *c, const char *s)
{
    const char *r = strstr(s, "/Rotate");

    if (r != NULL) {
        long v = atol(r + 7);

        c->out->rotate = to_int(((v % 360) + 360) % 360);
    }
}


/* The numbers of a colour: after the first "[" if any, words are skipped. */
static int
read_array(const char *s, double *v, int max)
{
    int n = 0;
    char *end;
    const char *br = strchr(s, '[');

    if (br != NULL) {
        s = br + 1;
    }

    while (n < max && *s != '\0' && *s != ']') {
        double x;

        while (*s == ' ' || *s == '\t') {
            s++;
        }

        x = strtod(s, &end);

        if (end == s) {
            if (*s == ']' || *s == '\0') {
                break;
            }

            while (*s != '\0' && *s != ' ' && *s != '\t' && *s != ']') {
                s++;
            }

            continue;
        }

        v[n++] = x;
        s = end;
    }

    return n;
}


/* Handles pdf:btrans; its words act around the current point. */
static void
do_btrans(Ctx *c, const char *s)
{
    Gfx *g = &c->g;
    char w[128];
    char cmd[2400];
    double px = g->px - 72.0;
    double py = 72.0 - g->py;
    char ops[1600];

    ops[0] = '\0';
    gfx_save(g);

    for (;;) {
        double m[6] = { 1.0, 0.0, 0.0, 1.0, 0.0, 0.0 };
        char v[128];
        char one[400];
        int take = 0;

        s = next_word(s, w, sizeof w);

        if (w[0] == '\0') {
            break;
        }

        if (strcmp(w, "rotate") == 0) {
            double t;

            s = next_word(s, v, sizeof v);
            t = atof(v) * PI_VALUE / 180.0;
            m[0] = cos(t);
            m[1] = sin(t);
            m[2] = -sin(t);
            m[3] = cos(t);
            take = 1;
        } else if (strcmp(w, "scale") == 0 || strcmp(w, "xscale") == 0 ||
                   strcmp(w, "yscale") == 0) {
            s = next_word(s, v, sizeof v);

            if (w[0] == 's') {
                const char *t;

                m[0] = atof(v);
                m[3] = atof(v);
                t = next_word(s, v, sizeof v);

                if (v[0] != '\0' && strchr("0123456789.-", v[0]) != NULL) {
                    m[3] = atof(v);
                    s = t;
                }
            } else if (w[0] == 'x') {
                m[0] = atof(v);
            } else {
                m[3] = atof(v);
            }

            take = 1;
        } else if (strcmp(w, "matrix") == 0) {
            int i;

            for (i = 0; i < 6; i++) {
                s = next_word(s, v, sizeof v);
                m[i] = atof(v);
            }

            take = 1;
        }

        if (take) {
            snprintf(one, sizeof one, " %.9g %.9g %.9g %.9g %.9g %.9g cm",
                     m[0], m[1], m[2], m[3], m[4], m[5]);
            strncat(ops, one, sizeof ops - strlen(ops) - 1);
        }
    }

    snprintf(cmd, sizeof cmd, "1 0 0 1 %.9g %.9g cm%s 1 0 0 1 %.9g %.9g cm",
             px, py, ops, -px, -py);
    pdf_run(g, cmd);
}


/* Handles x:scale, which the dvipdfmx driver puts after btrans. */
static void
do_x_scale(Ctx *c, const char *s)
{
    Gfx *g = &c->g;
    char v[64];
    char cmd[400];
    double sx;
    double sy;
    double px = g->px - 72.0;
    double py = 72.0 - g->py;

    s = next_word(s, v, sizeof v);
    sx = atof(v);
    s = next_word(s, v, sizeof v);
    sy = v[0] != '\0' ? atof(v) : sx;
    snprintf(cmd, sizeof cmd,
             "1 0 0 1 %.9g %.9g cm %.9g 0 0 %.9g 0 0 cm 1 0 0 1 %.9g %.9g cm",
             px, py, sx, sy, -px, -py);
    pdf_run(g, cmd);
}


static void
do_pdf(Ctx *c, const char *s)
{
    Gfx *g = &c->g;
    char kw[32];
    size_t k;
    const char *rest;
    char cmd[800];

    while (*s == ' ') {
        s++;
    }

    k = strcspn(s, " \t");

    if (k >= sizeof kw) {
        k = sizeof kw - 1;
    }

    memcpy(kw, s, k);
    kw[k] = '\0';
    rest = s + k;

    while (*rest == ' ' || *rest == '\t') {
        rest++;
    }

    if (strcmp(kw, "code") == 0) {
        pdf_run(g, rest);
    } else if (strcmp(kw, "literal") == 0 || strcmp(kw, "content") == 0) {
        int direct = strncmp(rest, "direct", 6) == 0;

        if (strncmp(rest, "direct", 6) == 0) {
            rest += 6;
        } else if (strncmp(rest, "page", 4) == 0) {
            rest += 4;
        }

        if (direct) {
            pdf_run(g, rest);
        } else {
            double py = 72.0 - g->py;

            snprintf(cmd, sizeof cmd, "1 0 0 1 %.9g %.9g cm", g->px - 72.0,
                     py);
            pdf_run(g, cmd);
            pdf_run(g, rest);
            snprintf(cmd, sizeof cmd, "1 0 0 1 %.9g %.9g cm", 72.0 - g->px,
                     -py);
            pdf_run(g, cmd);
        }
    } else if (strcmp(kw, "bcontent") == 0) {
        gfx_save(g);
        snprintf(cmd, sizeof cmd, "1 0 0 1 %.9g %.9g cm", g->px - 72.0,
                 72.0 - g->py);
        pdf_run(g, cmd);
    } else if (strcmp(kw, "econtent") == 0 || strcmp(kw, "etrans") == 0) {
        gfx_restore(g, 0);
    } else if (strcmp(kw, "btrans") == 0) {
        do_btrans(c, rest);
    } else if (strcmp(kw, "bcolor") == 0 || strcmp(kw, "bc") == 0) {
        double v[4];
        unsigned long rgb;
        int n = read_array(rest, v, 4);

        if (color_of(v, n, &rgb)) {
            push_color(c, rgb);
        }
    } else if (strcmp(kw, "ecolor") == 0 || strcmp(kw, "ec") == 0) {
        pop_color(c);
    } else if (strcmp(kw, "bgcolor") == 0) {
        double v[4];
        int n = read_array(rest, v, 4);

        if (color_of(v, n, &c->out->bg)) {
            c->out->has_bg = 1;
        }
    } else if (strcmp(kw, "image") == 0 || strcmp(kw, "epdf") == 0) {
        do_pdf_image(c, rest);
    } else if (strcmp(kw, "put") == 0) {
        if (strstr(rest, "@thispage") != NULL) {
            find_rotate(c, rest);
        }
    } else if (strcmp(kw, "bform") == 0 || strcmp(kw, "eform") == 0 ||
               strcmp(kw, "uxobj") == 0 || strcmp(kw, "stream") == 0 ||
               strcmp(kw, "fstream") == 0 || strcmp(kw, "ximage") == 0) {
        char what[64];

        snprintf(what, sizeof what, "pdf:%s", kw);
        dvi_report_special(what);
    }

    /* The rest (annotations, outlines, objects, destinations) draws nothing. */
}


/* Runs the PostScript of a ps: or ps:: special. */
static void
do_ps(Ctx *c, const char *s)
{
    int moveto = 1;
    Dvi *d = c->d;

    if (s[0] == ':') {
        moveto = 0;
        s++;
    }

    if (strncmp(s, "[begin]", 7) == 0 || strncmp(s, "[end]", 5) == 0) {
        return;
    }

    if (strstr(s, "pdfmark") != NULL) {
        find_rotate(c, s);

        return;
    }

    dvi_ps_prologue(d);

    if (!c->ps_bound) {
        psi_begin_page(d->ps, &c->g);
        c->ps_bound = 1;
    }

    psi_run(d->ps, s, moveto);
}


void
special_run(Ctx *c, const unsigned char *p, long len)
{
    char *text = xcalloc(to_size(len) + 1, 1);
    char kind[64];
    char *s;
    size_t k;

    memcpy(text, p, to_size(len));
    dvi_special_context(c, p, len);
    s = text;

    while (*s == ' ') {
        s++;
    }

    /* An empty special (LaTeX packages write them) has nothing to run. */
    if (*s == '\0') {
        free(text);

        return;
    }

    k = strcspn(s, " =:");

    if (k >= sizeof kind) {
        k = sizeof kind - 1;
    }

    memcpy(kind, s, k);
    kind[k] = '\0';

    if (strncmp(s, "papersize=", 10) == 0) {
        double w;
        double h;
        char *comma = strchr(s + 10, ',');

        if (comma != NULL && parse_length(s + 10, &w) &&
            parse_length(comma + 1, &h)) {
            c->d->paper_w = w;
            c->d->paper_h = h;
            c->g.paper_h = h * 72.0;
        }
    } else if (strcmp(kind, "strata") == 0) {
        /* The status of the print: the Viewer read it when it opened. */
    } else if (strcmp(kind, "color") == 0) {
        do_color(c, s, k);
    } else if (strcmp(kind, "pdf") == 0) {
        do_pdf(c, s + 4);
    } else if (strcasecmp(kind, "psfile") == 0) {
        do_psfile(c, s);
    } else if (strcmp(kind, "ps") == 0) {
        do_ps(c, s + 3);
    } else if (strcmp(kind, "x") == 0 && strncmp(s, "x:scale", 7) == 0) {
        do_x_scale(c, s + 7);
    } else if (strcmp(kind, "landscape") == 0) {
        /* The sheet is turned: the page is wider than high, text upright. */
        if (c->d->paper_w < c->d->paper_h) {
            double t = c->d->paper_w;

            c->d->paper_w = c->d->paper_h;
            c->d->paper_h = t;
            c->g.paper_h = c->d->paper_h * 72.0;
        }
    } else if (strcmp(kind, "em") == 0 && strncmp(s, "em: graph ", 10) == 0) {
        do_em_graph(c, s + 10);
    } else if (strcmp(kind, "background") == 0) {
        double v[4];
        int n = read_array(s + 10, v, 4);

        if (color_of(v, n, &c->out->bg)) {
            c->out->has_bg = 1;
        } else {
            dvi_report_special(kind);
        }
    } else if (s[0] == '"') {
        /* The dvips quote special: origin at the current point, y up. */
        size_t n = strlen(s + 1) + 64;
        char *buf = xcalloc(n, 1);

        snprintf(buf, n, "@beginspecial %s @endspecial", s + 1);
        dvi_ps_prologue(c->d);

        if (!c->ps_bound) {
            psi_begin_page(c->d->ps, &c->g);
            c->ps_bound = 1;
        }

        psi_run(c->d->ps, buf, 1);
        free(buf);
    } else if (s[0] == '!' || strcmp(kind, "header") == 0) {
        /* Header lines were run when the file was first scanned. */
    } else {
        dvi_report_special(kind);
    }

    free(text);
}


/* Runs one header special (a line that starts with "!") of the prologue. */
void
special_prologue(Dvi *d, Gfx *g, const unsigned char *p, long len)
{
    char *text;

    if (len < 2 || p[0] != '!') {
        return;
    }

    text = xcalloc(to_size(len) + 1, 1);
    memcpy(text, p + 1, to_size(len - 1));
    psi_begin_page(d->ps, g);
    psi_run(d->ps, text, 0);
    free(text);
}
