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
 * The graphics state shared by the PDF operators of the dvipdfmx specials
 * and the PostScript operators of the dvips specials.  Paths are turned into
 * page coordinates (bp, y down) as they are built, and painting adds items
 * to the display list.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "dviview.h"

#define PI_VALUE 3.14159265358979323846


unsigned long
rgb_pack(double r, double g, double b)
{
    unsigned long ir;
    unsigned long ig;
    unsigned long ib;

    r = r < 0.0 ? 0.0 : (r > 1.0 ? 1.0 : r);
    g = g < 0.0 ? 0.0 : (g > 1.0 ? 1.0 : g);
    b = b < 0.0 ? 0.0 : (b > 1.0 ? 1.0 : b);
    ir = to_u8(round_long(r * 255.0));
    ig = to_u8(round_long(g * 255.0));
    ib = to_u8(round_long(b * 255.0));

    return ir * 65536UL + ig * 256UL + ib;
}


unsigned long
cmyk_pack(double c, double m, double y, double k)
{
    return rgb_pack((1.0 - c) * (1.0 - k), (1.0 - m) * (1.0 - k),
                    (1.0 - y) * (1.0 - k));
}


void
itemlist_reset(ItemList *l)
{
    long i;

    for (i = 0; i < l->npaths; i++) {
        path_free(&l->paths[i].path);
    }

    for (i = 0; i < l->nclips; i++) {
        path_free(&l->clips[i].path);
    }

    for (i = 0; i < l->nimgs; i++) {
        free(l->imgs[i].name);
    }

    l->n = 0;
    l->npaths = 0;
    l->nclips = 0;
    l->nimgs = 0;
    l->nxfs = 0;
    l->rotate = 0;
    l->has_bg = 0;
}


void
gfx_unsupported(const char *what)
{
    dvi_report_special(what);
}


static void
state_default(GState *s)
{
    memset(s, 0, sizeof *s);
    s->ctm = mat_ident();
    s->fill = 0UL;
    s->stroke = 0UL;
    s->st.lw = 1.0;
    s->st.miter = 10.0;
}


void
gfx_init(Gfx *g, ItemList *out, double paper_h)
{
    memset(g, 0, sizeof *g);
    g->out = out;
    g->paper_h = paper_h;
    g->capstack = 16;
    g->stack = xcalloc(to_size(g->capstack), sizeof *g->stack);
    g->nstack = 1;
    state_default(&g->stack[0]);
}


void
gfx_done(Gfx *g)
{
    long i;

    for (i = 0; i < g->nstack; i++) {
        path_free(&g->stack[i].path);
    }

    free(g->stack);
    memset(g, 0, sizeof *g);
}


GState *
gfx_cur(Gfx *g)
{
    return &g->stack[g->nstack - 1];
}


void
gfx_save(Gfx *g)
{
    GState *top;

    if (g->nstack >= g->capstack) {
        g->capstack *= 2;
        g->stack = xrealloc(g->stack,
                            to_size(g->capstack) * sizeof *g->stack);
    }

    top = &g->stack[g->nstack - 1];
    g->stack[g->nstack] = *top;
    memset(&g->stack[g->nstack].path, 0, sizeof(Path));
    path_copy(&g->stack[g->nstack].path, &top->path);
    g->nstack++;
}


/* Returns to the saved state; PostScript restores the path, PDF does not. */
void
gfx_restore(Gfx *g, int with_path)
{
    GState *top;
    GState *below;

    if (g->nstack <= 1) {
        return;
    }

    top = &g->stack[g->nstack - 1];
    below = &g->stack[g->nstack - 2];

    if (!with_path) {
        path_copy(&below->path, &top->path);
        below->cx = top->cx;
        below->cy = top->cy;
        below->have_cp = top->have_cp;
    }

    path_free(&top->path);
    g->nstack--;
}


void
gfx_set_pos(Gfx *g, double px, double py)
{
    g->px = px;
    g->py = py;
}


/* The index of the current matrix in the page, plus one; 0 is identity. */
int
gfx_xf(Gfx *g)
{
    const Mat *m = &gfx_cur(g)->ctm;
    ItemList *l = g->out;

    if (mat_is_ident(m)) {
        return 0;
    }

    if (g->last_xf > 0 && g->last_xf <= l->nxfs) {
        const Mat *o = &l->xfs[g->last_xf - 1];

        if (fabs(o->a - m->a) < 1e-9 && fabs(o->b - m->b) < 1e-9 &&
            fabs(o->c - m->c) < 1e-9 && fabs(o->d - m->d) < 1e-9 &&
            fabs(o->e - m->e) < 1e-6 && fabs(o->f - m->f) < 1e-6) {
            return g->last_xf;
        }
    }

    l->xfs = xrealloc(l->xfs, to_size(l->nxfs + 1) * sizeof *l->xfs);
    l->xfs[l->nxfs++] = *m;
    g->last_xf = to_int(l->nxfs);

    return g->last_xf;
}


static void
add_list_item(ItemList *l, const Item *it)
{
    if (l->n >= l->cap) {
        l->cap = l->cap * 2 + 1024;
        l->v = xrealloc(l->v, to_size(l->cap) * sizeof *l->v);
    }

    l->v[l->n++] = *it;
}


void
gfx_add_clip(Gfx *g, int evenodd)
{
    GState *s = gfx_cur(g);
    ItemList *l = g->out;
    ClipItem *c;

    if (s->path.nop == 0) {
        return;
    }

    l->clips = xrealloc(l->clips, to_size(l->nclips + 1) * sizeof *l->clips);
    c = &l->clips[l->nclips];
    memset(c, 0, sizeof *c);
    path_copy(&c->path, &s->path);
    c->evenodd = evenodd;
    c->parent = s->clip;
    l->nclips++;
    s->clip = to_int(l->nclips);
}


void
gfx_paint(Gfx *g, int fill, int stroke, int evenodd)
{
    GState *s = gfx_cur(g);
    ItemList *l = g->out;
    PathItem *pi;
    Item it;
    double scale = mat_det_scale(&s->ctm);
    int i;

    if (s->path.nop > 0 && (fill || stroke)) {
        l->paths = xrealloc(l->paths,
                            to_size(l->npaths + 1) * sizeof *l->paths);
        pi = &l->paths[l->npaths];
        memset(pi, 0, sizeof *pi);
        path_copy(&pi->path, &s->path);
        pi->fill = s->fill;
        pi->stroke = s->stroke;
        pi->do_fill = fill;
        pi->do_stroke = stroke;
        pi->evenodd = evenodd;
        pi->st = s->st;
        pi->st.lw = s->st.lw * scale;

        for (i = 0; i < pi->st.ndash; i++) {
            pi->st.dash[i] = s->st.dash[i] * scale;
        }

        pi->st.phase = s->st.phase * scale;
        memset(&it, 0, sizeof it);
        it.type = IT_PATH;
        it.ref = l->npaths;
        it.clip = s->clip;
        l->npaths++;
        add_list_item(l, &it);
    }

    if (g->pend_clip != 0) {
        gfx_add_clip(g, g->pend_clip == 2);
        g->pend_clip = 0;
    }
}


void
gfx_moveto(Gfx *g, double x, double y)
{
    GState *s = gfx_cur(g);
    double dx;
    double dy;

    mat_pt(&s->ctm, x, y, &dx, &dy);
    path_move(&s->path, dx, dy);
    s->cx = dx;
    s->cy = dy;
    s->have_cp = 1;
}


void
gfx_lineto(Gfx *g, double x, double y)
{
    GState *s = gfx_cur(g);
    double dx;
    double dy;

    if (!s->have_cp) {
        return;
    }

    mat_pt(&s->ctm, x, y, &dx, &dy);
    path_line(&s->path, dx, dy);
    s->cx = dx;
    s->cy = dy;
}


void
gfx_curveto(Gfx *g, const double *c)
{
    GState *s = gfx_cur(g);
    double d[6];
    int i;

    if (!s->have_cp) {
        return;
    }

    for (i = 0; i < 3; i++) {
        mat_pt(&s->ctm, c[2 * i], c[2 * i + 1], &d[2 * i], &d[2 * i + 1]);
    }

    path_curve(&s->path, d);
    s->cx = d[4];
    s->cy = d[5];
}


void
gfx_closepath(Gfx *g)
{
    GState *s = gfx_cur(g);

    if (s->have_cp && s->path.nop > 0) {
        path_close(&s->path);
    }
}


void
gfx_rect(Gfx *g, double x, double y, double w, double h)
{
    gfx_moveto(g, x, y);
    gfx_lineto(g, x + w, y);
    gfx_lineto(g, x + w, y + h);
    gfx_lineto(g, x, y + h);
    gfx_closepath(g);
}


void
gfx_concat(Gfx *g, const Mat *m)
{
    GState *s = gfx_cur(g);

    s->ctm = mat_mul(*m, s->ctm);
}


/*
 * The PostScript arc: a = x, y, radius, start and end angle in degrees.
 * The curve is built in segments of at most 90 degrees.
 */
void
gfx_arc(Gfx *g, const double *a, int ccw)
{
    double a0 = a[3] * PI_VALUE / 180.0;
    double a1 = a[4] * PI_VALUE / 180.0;
    double span;
    long n;
    long i;
    double step;
    double k;
    GState *s = gfx_cur(g);

    if (ccw) {
        while (a1 < a0) {
            a1 += 2.0 * PI_VALUE;
        }
    } else {
        while (a1 > a0) {
            a1 -= 2.0 * PI_VALUE;
        }
    }

    span = a1 - a0;
    n = ceil_long(fabs(span) / (PI_VALUE / 2.0) - 1e-9);

    if (n < 1) {
        n = 1;
    }

    step = span / dbl(n);
    k = 4.0 / 3.0 * tan(step / 4.0);

    if (s->have_cp) {
        gfx_lineto(g, a[0] + a[2] * cos(a0), a[1] + a[2] * sin(a0));
    } else {
        gfx_moveto(g, a[0] + a[2] * cos(a0), a[1] + a[2] * sin(a0));
    }

    for (i = 0; i < n; i++) {
        double t0 = a0 + step * dbl(i);
        double t1 = t0 + step;
        double c[6];

        c[0] = a[0] + a[2] * (cos(t0) - k * sin(t0));
        c[1] = a[1] + a[2] * (sin(t0) + k * cos(t0));
        c[2] = a[0] + a[2] * (cos(t1) + k * sin(t1));
        c[3] = a[1] + a[2] * (sin(t1) - k * cos(t1));
        c[4] = a[0] + a[2] * cos(t1);
        c[5] = a[1] + a[2] * sin(t1);
        gfx_curveto(g, c);
    }
}


/* The state of the PDF operator interpreter: operands and the last name. */
typedef struct {
    double v[40];
    int n;
    char name[64];
    double dash[8];
    int ndash;
    int in_array;
} Args;


/*
 * The user space of PDF operators in dvipdfmx has its origin at the origin
 * of the DVI page (1 in from the left and the top), y up.  A point of it
 * becomes a point of the page in bp, y down, as (72 + x, 72 - y).
 */
static double
pdf_x(double x)
{
    return x + 72.0;
}


static double
pdf_y(double y)
{
    return 72.0 - y;
}


static void
pdf_cm(Gfx *g, const double *v)
{
    Mat m;
    Mat f;
    Mat fi;
    Mat md;

    m.a = v[0];
    m.b = v[1];
    m.c = v[2];
    m.d = v[3];
    m.e = v[4];
    m.f = v[5];
    f.a = 1.0;
    f.b = 0.0;
    f.c = 0.0;
    f.d = -1.0;
    f.e = 72.0;
    f.f = 72.0;
    fi.a = 1.0;
    fi.b = 0.0;
    fi.c = 0.0;
    fi.d = -1.0;
    fi.e = -72.0;
    fi.f = 72.0;
    md = mat_mul(fi, mat_mul(m, f));
    gfx_concat(g, &md);
}


static void
set_color(Gfx *g, const Args *a, int is_fill)
{
    unsigned long c = 0UL;
    GState *s = gfx_cur(g);

    if (a->n == 1) {
        c = rgb_pack(a->v[0], a->v[0], a->v[0]);
    } else if (a->n == 3) {
        c = rgb_pack(a->v[0], a->v[1], a->v[2]);
    } else if (a->n == 4) {
        c = cmyk_pack(a->v[0], a->v[1], a->v[2], a->v[3]);
    } else {
        gfx_unsupported("pdf color (pattern or shading)");
        return;
    }

    if (is_fill) {
        s->fill = c;
    } else {
        s->stroke = c;
    }
}


static void
pdf_op(Gfx *g, const char *op, Args *a)
{
    GState *s = gfx_cur(g);
    const double *v = a->v;

    if (strcmp(op, "q") == 0) {
        gfx_save(g);
    } else if (strcmp(op, "Q") == 0) {
        gfx_restore(g, 0);
    } else if (strcmp(op, "cm") == 0 && a->n >= 6) {
        pdf_cm(g, v + a->n - 6);
    } else if (strcmp(op, "w") == 0 && a->n >= 1) {
        s->st.lw = v[a->n - 1];
    } else if (strcmp(op, "J") == 0 && a->n >= 1) {
        s->st.cap = to_int(round_long(v[a->n - 1]));
    } else if (strcmp(op, "j") == 0 && a->n >= 1) {
        s->st.join = to_int(round_long(v[a->n - 1]));
    } else if (strcmp(op, "M") == 0 && a->n >= 1) {
        s->st.miter = v[a->n - 1];
    } else if (strcmp(op, "d") == 0) {
        int i;

        s->st.ndash = a->ndash;
        s->st.phase = a->n >= 1 ? v[a->n - 1] : 0.0;

        for (i = 0; i < a->ndash; i++) {
            s->st.dash[i] = a->dash[i];
        }
    } else if (strcmp(op, "m") == 0 && a->n >= 2) {
        gfx_moveto(g, pdf_x(v[a->n - 2]), pdf_y(v[a->n - 1]));
    } else if (strcmp(op, "l") == 0 && a->n >= 2) {
        gfx_lineto(g, pdf_x(v[a->n - 2]), pdf_y(v[a->n - 1]));
    } else if (strcmp(op, "c") == 0 && a->n >= 6) {
        double c[6];
        int i;

        for (i = 0; i < 3; i++) {
            c[2 * i] = pdf_x(v[a->n - 6 + 2 * i]);
            c[2 * i + 1] = pdf_y(v[a->n - 5 + 2 * i]);
        }

        gfx_curveto(g, c);
    } else if ((strcmp(op, "v") == 0 || strcmp(op, "y") == 0) && a->n >= 4) {
        double c[6];
        const double *q = v + a->n - 4;

        if (strcmp(op, "v") == 0) {
            c[0] = s->cx;
            c[1] = s->cy;
            c[2] = pdf_x(q[0]);
            c[3] = pdf_y(q[1]);
            c[4] = pdf_x(q[2]);
            c[5] = pdf_y(q[3]);
            /* The current point is already in page coordinates. */
            {
                Mat inv;
                double ux;
                double uy;

                if (mat_inv(&s->ctm, &inv)) {
                    mat_pt(&inv, s->cx, s->cy, &ux, &uy);
                    c[0] = ux;
                    c[1] = uy;
                }
            }
        } else {
            c[0] = pdf_x(q[0]);
            c[1] = pdf_y(q[1]);
            c[2] = pdf_x(q[2]);
            c[3] = pdf_y(q[3]);
            c[4] = pdf_x(q[2]);
            c[5] = pdf_y(q[3]);
        }

        gfx_curveto(g, c);
    } else if (strcmp(op, "h") == 0) {
        gfx_closepath(g);
    } else if (strcmp(op, "re") == 0 && a->n >= 4) {
        double x = v[a->n - 4];
        double y = v[a->n - 3];
        double w = v[a->n - 2];
        double h = v[a->n - 1];

        gfx_moveto(g, pdf_x(x), pdf_y(y));
        gfx_lineto(g, pdf_x(x + w), pdf_y(y));
        gfx_lineto(g, pdf_x(x + w), pdf_y(y + h));
        gfx_lineto(g, pdf_x(x), pdf_y(y + h));
        gfx_closepath(g);
    } else if (strcmp(op, "S") == 0) {
        gfx_paint(g, 0, 1, 0);
        path_clear(&s->path);
    } else if (strcmp(op, "s") == 0) {
        gfx_closepath(g);
        gfx_paint(g, 0, 1, 0);
        path_clear(&s->path);
    } else if (strcmp(op, "f") == 0 || strcmp(op, "F") == 0) {
        gfx_paint(g, 1, 0, 0);
        path_clear(&s->path);
    } else if (strcmp(op, "f*") == 0) {
        gfx_paint(g, 1, 0, 1);
        path_clear(&s->path);
    } else if (strcmp(op, "B") == 0) {
        gfx_paint(g, 1, 1, 0);
        path_clear(&s->path);
    } else if (strcmp(op, "B*") == 0) {
        gfx_paint(g, 1, 1, 1);
        path_clear(&s->path);
    } else if (strcmp(op, "b") == 0) {
        gfx_closepath(g);
        gfx_paint(g, 1, 1, 0);
        path_clear(&s->path);
    } else if (strcmp(op, "b*") == 0) {
        gfx_closepath(g);
        gfx_paint(g, 1, 1, 1);
        path_clear(&s->path);
    } else if (strcmp(op, "n") == 0) {
        gfx_paint(g, 0, 0, 0);
        path_clear(&s->path);
    } else if (strcmp(op, "W") == 0) {
        g->pend_clip = 1;
    } else if (strcmp(op, "W*") == 0) {
        g->pend_clip = 2;
    } else if (strcmp(op, "g") == 0 || strcmp(op, "rg") == 0 ||
               strcmp(op, "k") == 0) {
        int need = op[0] == 'g' ? 1 : (op[0] == 'r' ? 3 : 4);

        if (a->n >= need) {
            Args b = *a;

            memmove(b.v, a->v + a->n - need, to_size(need) * sizeof b.v[0]);
            b.n = need;
            set_color(g, &b, 1);
        }
    } else if (strcmp(op, "G") == 0 || strcmp(op, "RG") == 0 ||
               strcmp(op, "K") == 0) {
        int need = op[0] == 'G' ? 1 : (op[0] == 'R' ? 3 : 4);

        if (a->n >= need) {
            Args b = *a;

            memmove(b.v, a->v + a->n - need, to_size(need) * sizeof b.v[0]);
            b.n = need;
            set_color(g, &b, 0);
        }
    } else if (strcmp(op, "sc") == 0 || strcmp(op, "scn") == 0) {
        set_color(g, a, 1);
    } else if (strcmp(op, "SC") == 0 || strcmp(op, "SCN") == 0) {
        set_color(g, a, 0);
    } else if (strcmp(op, "gs") == 0) {
        gfx_unsupported("pdf gs (transparency, blend modes)");
    } else if (strcmp(op, "sh") == 0) {
        gfx_unsupported("pdf sh (shading)");
    } else if (strcmp(op, "Do") == 0) {
        gfx_unsupported("pdf Do (form or image object)");
    } else if (strcmp(op, "cs") == 0 || strcmp(op, "CS") == 0 ||
               strcmp(op, "ri") == 0 || strcmp(op, "i") == 0 ||
               strcmp(op, "BMC") == 0 || strcmp(op, "BDC") == 0 ||
               strcmp(op, "EMC") == 0) {
        /* These need no action. */
    } else {
        char what[96];

        snprintf(what, sizeof what, "pdf operator %.20s", op);
        gfx_unsupported(what);
    }
}


/* Runs a string of PDF operators, such as the text of pdf:code. */
void
pdf_run(Gfx *g, const char *s)
{
    Args a;

    memset(&a, 0, sizeof a);

    while (*s != '\0') {
        if (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') {
            s++;
        } else if (*s == '%') {
            while (*s != '\0' && *s != '\n') {
                s++;
            }
        } else if (*s == '[') {
            a.in_array = 1;
            a.ndash = 0;
            s++;
        } else if (*s == ']') {
            a.in_array = 0;
            s++;
        } else if (*s == '(') {
            int depth = 0;

            do {
                if (*s == '(') {
                    depth++;
                } else if (*s == ')') {
                    depth--;
                } else if (*s == '\\' && s[1] != '\0') {
                    s++;
                }

                s++;
            } while (*s != '\0' && depth > 0);
        } else if (*s == '<') {
            if (s[1] == '<') {
                int depth = 0;

                while (*s != '\0') {
                    if (s[0] == '<' && s[1] == '<') {
                        depth++;
                        s += 2;
                    } else if (s[0] == '>' && s[1] == '>') {
                        depth--;
                        s += 2;

                        if (depth == 0) {
                            break;
                        }
                    } else {
                        s++;
                    }
                }
            } else {
                while (*s != '\0' && *s != '>') {
                    s++;
                }

                if (*s == '>') {
                    s++;
                }
            }
        } else if (*s == '/') {
            size_t k = strcspn(s + 1, " \t\n\r/[]<>()%");

            if (k >= sizeof a.name) {
                k = sizeof a.name - 1;
            }

            memcpy(a.name, s + 1, k);
            a.name[k] = '\0';
            s += 1 + strcspn(s + 1, " \t\n\r/[]<>()%");
        } else {
            size_t k = strcspn(s, " \t\n\r/[]<>()%");
            char tok[64];
            char *end;
            double val;

            if (k == 0) {
                s++;
                continue;
            }

            if (k >= sizeof tok) {
                k = sizeof tok - 1;
            }

            memcpy(tok, s, k);
            tok[k] = '\0';
            s += strcspn(s, " \t\n\r/[]<>()%");
            val = strtod(tok, &end);

            if (*end == '\0') {
                if (a.in_array && a.ndash < 8) {
                    a.dash[a.ndash++] = val;
                } else if (a.n < 40) {
                    a.v[a.n++] = val;
                }
            } else {
                pdf_op(g, tok, &a);
                a.n = 0;
                a.name[0] = '\0';
            }
        }
    }
}
