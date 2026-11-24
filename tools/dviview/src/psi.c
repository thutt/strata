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
 * A small PostScript interpreter for the "ps:" specials that dvips writes
 * (rotation and scaling of text, and the pgf graphics of its dvips driver).
 * It handles numbers, names, procedures, arrays, dictionaries, the stack and
 * control operators, and the path, painting, colour and matrix operators.
 * Everything is drawn through the graphics state of gstate.c.  Memory of
 * procedures is not given back; a page needs a few kilobytes at most.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "dviview.h"

#define PS_STACK 600
#define PS_DEPTH 48
#define PS_DSTACK 24
#define PI_VALUE 3.14159265358979323846

enum { T_NUM, T_BOOL, T_NAME, T_EXEC, T_ARR, T_PROC, T_MARK, T_DICT, T_OP,
       T_SAVE, T_NULL };

struct Dict;
struct Arr;

typedef struct {
    int t;
    double n;
    long aux;
    const char *s;
    struct Arr *a;
    struct Dict *d;
} Obj;

typedef struct Arr {
    Obj *v;
    long n;
} Arr;

typedef struct Dict {
    const char **key;
    Obj *val;
    long n;
    long cap;
} Dict;

typedef struct {
    Dict *d;
    const char *key;
    Obj old;
    int had;
} Undo;

struct Psi {
    Undo *undo;
    long nundo;
    long capundo;
    long nsaves;
    Obj st[PS_STACK];
    long sp;
    Dict *ds[PS_DSTACK];
    long nds;
    Dict *sys;
    Dict *user;
    Dict *glob;
    Gfx *g;
    int exiting;
    int failed;
    long steps;
    const char **names;
    long nnames;
    long capnames;
};

/* The operators, in the order of the table below. */
enum {
    O_POP, O_EXCH, O_DUP, O_COPY, O_INDEX, O_ROLL, O_CLEAR, O_COUNT, O_MARK,
    O_ARRAYSTART, O_ARRAYEND, O_CLEARTOMARK, O_COUNTTOMARK, O_ADD, O_SUB,
    O_MUL, O_DIV, O_IDIV, O_MOD, O_ATAN, O_EXP, O_MAX, O_MIN, O_BITSHIFT,
    O_NEG, O_ABS, O_CEILING, O_FLOOR, O_ROUND, O_TRUNCATE, O_SQRT, O_SIN,
    O_COS, O_LN, O_LOG, O_CVI, O_CVR, O_EQ, O_NE, O_GT, O_GE, O_LT, O_LE,
    O_AND, O_OR, O_NOT, O_XOR, O_IF, O_IFELSE, O_FOR, O_REPEAT, O_LOOP,
    O_EXIT, O_EXEC, O_BIND, O_CVX, O_DEF, O_STORE, O_LOAD, O_KNOWN, O_WHERE,
    O_DICT, O_BEGIN, O_END, O_CURRENTDICT, O_USERDICT, O_SYSTEMDICT,
    O_GLOBALDICT, O_PUT, O_GET, O_ARRAY, O_LENGTH, O_GSAVE, O_GRESTORE,
    O_SAVE, O_RESTORE, O_NEWPATH, O_MOVETO, O_RMOVETO, O_LINETO, O_RLINETO,
    O_CURVETO, O_RCURVETO, O_CLOSEPATH, O_ARC, O_ARCN, O_CURRENTPOINT,
    O_FILL, O_EOFILL, O_STROKE, O_CLIP, O_EOCLIP, O_RECTFILL, O_RECTSTROKE,
    O_RECTCLIP, O_INITCLIP, O_TRANSLATE, O_SCALE, O_ROTATE, O_CONCAT,
    O_INITMATRIX, O_TRANSFORM, O_ITRANSFORM, O_SETLINEWIDTH,
    O_CURRENTLINEWIDTH, O_SETLINECAP, O_SETLINEJOIN, O_SETMITER, O_SETDASH,
    O_SETGRAY, O_SETRGB, O_SETCMYK, O_SETHSB, O_BEGINSPECIAL, O_ENDSPECIAL,
    O_IGNORE1, O_IGNORE0, O_SHOW, O_TRUE, O_FALSE, O_STRING, O_GETINTERVAL,
    O_CURRENTFLAT
};

static const struct {
    const char *name;
    int op;
} op_table[] = {
    { "pop", O_POP }, { "exch", O_EXCH }, { "dup", O_DUP },
    { "copy", O_COPY }, { "index", O_INDEX }, { "roll", O_ROLL },
    { "clear", O_CLEAR }, { "count", O_COUNT }, { "mark", O_MARK },
    { "[", O_ARRAYSTART }, { "]", O_ARRAYEND },
    { "cleartomark", O_CLEARTOMARK }, { "counttomark", O_COUNTTOMARK },
    { "add", O_ADD }, { "sub", O_SUB }, { "mul", O_MUL }, { "div", O_DIV },
    { "idiv", O_IDIV }, { "mod", O_MOD }, { "atan", O_ATAN },
    { "exp", O_EXP }, { "max", O_MAX }, { "min", O_MIN },
    { "bitshift", O_BITSHIFT }, { "neg", O_NEG }, { "abs", O_ABS },
    { "ceiling", O_CEILING }, { "floor", O_FLOOR }, { "round", O_ROUND },
    { "truncate", O_TRUNCATE }, { "sqrt", O_SQRT }, { "sin", O_SIN },
    { "cos", O_COS }, { "ln", O_LN }, { "log", O_LOG }, { "cvi", O_CVI },
    { "cvr", O_CVR }, { "eq", O_EQ }, { "ne", O_NE }, { "gt", O_GT },
    { "ge", O_GE }, { "lt", O_LT }, { "le", O_LE }, { "and", O_AND },
    { "or", O_OR }, { "not", O_NOT }, { "xor", O_XOR }, { "if", O_IF },
    { "ifelse", O_IFELSE }, { "for", O_FOR }, { "repeat", O_REPEAT },
    { "loop", O_LOOP }, { "exit", O_EXIT }, { "exec", O_EXEC },
    { "bind", O_BIND }, { "cvx", O_CVX }, { "def", O_DEF },
    { "store", O_STORE }, { "load", O_LOAD }, { "known", O_KNOWN },
    { "where", O_WHERE }, { "dict", O_DICT }, { "begin", O_BEGIN },
    { "end", O_END }, { "currentdict", O_CURRENTDICT },
    { "userdict", O_USERDICT }, { "systemdict", O_SYSTEMDICT },
    { "globaldict", O_GLOBALDICT }, { "put", O_PUT }, { "get", O_GET },
    { "array", O_ARRAY }, { "length", O_LENGTH }, { "gsave", O_GSAVE },
    { "grestore", O_GRESTORE }, { "save", O_SAVE },
    { "restore", O_RESTORE }, { "newpath", O_NEWPATH },
    { "moveto", O_MOVETO }, { "rmoveto", O_RMOVETO },
    { "lineto", O_LINETO }, { "rlineto", O_RLINETO },
    { "curveto", O_CURVETO }, { "rcurveto", O_RCURVETO },
    { "closepath", O_CLOSEPATH }, { "arc", O_ARC }, { "arcn", O_ARCN },
    { "currentpoint", O_CURRENTPOINT }, { "fill", O_FILL },
    { "eofill", O_EOFILL }, { "stroke", O_STROKE }, { "clip", O_CLIP },
    { "eoclip", O_EOCLIP }, { "rectfill", O_RECTFILL },
    { "rectstroke", O_RECTSTROKE }, { "rectclip", O_RECTCLIP },
    { "initclip", O_INITCLIP }, { "translate", O_TRANSLATE },
    { "scale", O_SCALE }, { "rotate", O_ROTATE }, { "concat", O_CONCAT },
    { "initmatrix", O_INITMATRIX }, { "transform", O_TRANSFORM },
    { "itransform", O_ITRANSFORM }, { "setlinewidth", O_SETLINEWIDTH },
    { "currentlinewidth", O_CURRENTLINEWIDTH },
    { "setlinecap", O_SETLINECAP }, { "setlinejoin", O_SETLINEJOIN },
    { "setmiterlimit", O_SETMITER }, { "setdash", O_SETDASH },
    { "setgray", O_SETGRAY }, { "setrgbcolor", O_SETRGB },
    { "setcmykcolor", O_SETCMYK }, { "sethsbcolor", O_SETHSB },
    { "@beginspecial", O_BEGINSPECIAL }, { "@endspecial", O_ENDSPECIAL },
    { "setflat", O_IGNORE1 }, { "setstrokeadjust", O_IGNORE1 },
    { "setoverprint", O_IGNORE1 }, { ".setopacityalpha", O_IGNORE1 },
    { ".setblendmode", O_IGNORE1 }, { "@setspecial", O_IGNORE0 },
    { "showpage", O_IGNORE0 }, { "show", O_SHOW }, { "true", O_TRUE },
    { "false", O_FALSE }, { "string", O_STRING },
    { "getinterval", O_GETINTERVAL }, { "currentflat", O_CURRENTFLAT }
};


static Obj
num(double v)
{
    Obj o;

    memset(&o, 0, sizeof o);
    o.t = T_NUM;
    o.n = v;

    return o;
}


static Obj
boolean(int v)
{
    Obj o;

    memset(&o, 0, sizeof o);
    o.t = T_BOOL;
    o.n = v != 0 ? 1.0 : 0.0;

    return o;
}


static const char *
intern(Psi *ps, const char *s)
{
    long i;

    for (i = 0; i < ps->nnames; i++) {
        if (strcmp(ps->names[i], s) == 0) {
            return ps->names[i];
        }
    }

    if (ps->nnames >= ps->capnames) {
        ps->capnames = ps->capnames * 2 + 256;
        ps->names = xrealloc(ps->names,
                             to_size(ps->capnames) * sizeof *ps->names);
    }

    ps->names[ps->nnames] = xstrdup(s);

    return ps->names[ps->nnames++];
}


static Dict *
dict_new(void)
{
    return xcalloc(1, sizeof(Dict));
}


static Obj *
dict_find(Dict *d, const char *key)
{
    long i;

    for (i = 0; i < d->n; i++) {
        if (d->key[i] == key) {
            return &d->val[i];
        }
    }

    return NULL;
}


static void
dict_put(Dict *d, const char *key, Obj v)
{
    Obj *o = dict_find(d, key);

    if (o != NULL) {
        *o = v;
        return;
    }

    if (d->n >= d->cap) {
        d->cap = d->cap * 2 + 16;
        d->key = xrealloc(d->key, to_size(d->cap) * sizeof *d->key);
        d->val = xrealloc(d->val, to_size(d->cap) * sizeof *d->val);
    }

    d->key[d->n] = key;
    d->val[d->n] = v;
    d->n++;
}


/* Defines a name; inside save and restore the old value is remembered. */
static void
def_logged(Psi *ps, Dict *d, const char *key, Obj v)
{
    if (ps->nsaves > 0) {
        Obj *o = dict_find(d, key);

        if (ps->nundo >= ps->capundo) {
            ps->capundo = ps->capundo * 2 + 64;
            ps->undo = xrealloc(ps->undo,
                                to_size(ps->capundo) * sizeof *ps->undo);
        }

        ps->undo[ps->nundo].d = d;
        ps->undo[ps->nundo].key = key;
        ps->undo[ps->nundo].had = o != NULL;

        if (o != NULL) {
            ps->undo[ps->nundo].old = *o;
        } else {
            memset(&ps->undo[ps->nundo].old, 0, sizeof(Obj));
        }

        ps->nundo++;
    }

    dict_put(d, key, v);
}


/* Undoes the definitions made since the log had n entries. */
static void
undo_to(Psi *ps, long n)
{
    while (ps->nundo > n) {
        Undo *u = &ps->undo[--ps->nundo];

        if (u->had) {
            dict_put(u->d, u->key, u->old);
        } else {
            long i;

            for (i = 0; i < u->d->n; i++) {
                if (u->d->key[i] == u->key) {
                    u->d->key[i] = intern(ps, "");
                }
            }
        }
    }
}


static void
push(Psi *ps, Obj o)
{
    if (ps->sp >= PS_STACK) {
        ps->failed = 1;
        return;
    }

    ps->st[ps->sp++] = o;
}


static Obj
pop(Psi *ps)
{
    if (ps->sp <= 0) {
        ps->failed = 1;

        return num(0.0);
    }

    return ps->st[--ps->sp];
}


static double
pop_num(Psi *ps)
{
    Obj o = pop(ps);

    return o.n;
}


/* Pops n numbers into v, the last pushed ending up in v[n - 1]. */
static void
pop_nums(Psi *ps, double *v, int n)
{
    int i;

    for (i = n - 1; i >= 0; i--) {
        v[i] = pop_num(ps);
    }
}


static int
same_obj(const Obj *a, const Obj *b)
{
    if (a->t == T_NAME || a->t == T_EXEC) {
        return (b->t == T_NAME || b->t == T_EXEC) && a->s == b->s;
    }

    if (a->t == T_ARR || a->t == T_PROC) {
        return a->a == b->a;
    }

    if (a->t == T_DICT) {
        return a->d == b->d;
    }

    return fabs(a->n - b->n) < 1e-9;
}


static void exec_obj(Psi *ps, Obj o, int depth);
static void exec_arr(Psi *ps, const Arr *a, int depth);
static int next_token(Psi *ps, const char **sp, Obj *out, int depth);


static int
is_delim(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '(' ||
           c == ')' || c == '<' || c == '>' || c == '[' || c == ']' ||
           c == '{' || c == '}' || c == '/' || c == '%' || c == '\0';
}


static int
read_proc(Psi *ps, const char **sp, Obj *out, int depth)
{
    Arr *a;
    long cap = 0;

    if (depth > 200) {
        ps->failed = 1;
        memset(out, 0, sizeof *out);
        out->t = T_NULL;

        return 0;
    }

    a = xcalloc(1, sizeof *a);

    for (;;) {
        Obj o;
        const char *s = *sp;

        while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') {
            s++;
        }

        if (*s == '}') {
            *sp = s + 1;
            break;
        }

        if (!next_token(ps, sp, &o, depth + 1)) {
            break;
        }

        if (a->n >= cap) {
            cap = cap * 2 + 8;
            a->v = xrealloc(a->v, to_size(cap) * sizeof *a->v);
        }

        a->v[a->n++] = o;
    }

    memset(out, 0, sizeof *out);
    out->t = T_PROC;
    out->a = a;

    return 1;
}


/* Skips a string in parentheses; returns the position after it. */
static const char *
skip_string(const char *s)
{
    int level = 0;

    do {
        if (*s == '(') {
            level++;
        } else if (*s == ')') {
            level--;
        } else if (*s == '\\' && s[1] != '\0') {
            s++;
        }

        s++;
    } while (*s != '\0' && level > 0);

    return s;
}


static int
next_token(Psi *ps, const char **sp, Obj *out, int depth)
{
    const char *s = *sp;
    char buf[128];
    size_t k;
    char *end;
    double v;

    memset(out, 0, sizeof *out);

    for (;;) {
        while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') {
            s++;
        }

        if (*s != '%') {
            break;
        }

        while (*s != '\0' && *s != '\n') {
            s++;
        }
    }

    if (*s == '\0') {
        *sp = s;

        return 0;
    }

    if (*s == '{') {
        *sp = s + 1;

        return read_proc(ps, sp, out, depth);
    }

    if (*s == '}') {
        *sp = s + 1;
        out->t = T_NULL;

        return 1;
    }

    if (*s == '(') {
        *sp = skip_string(s);
        out->t = T_NULL;

        return 1;
    }

    if (*s == '<' || *s == '>') {
        if (s[1] == s[0]) {
            *sp = s + 2;
            out->t = T_EXEC;
            out->s = intern(ps, s[0] == '<' ? "mark" : "cleartomark");

            return 1;
        }

        while (*s != '\0' && *s != '>') {
            s++;
        }

        *sp = *s == '>' ? s + 1 : s;
        out->t = T_NULL;

        return 1;
    }

    if (*s == '[' || *s == ']') {
        buf[0] = *s;
        buf[1] = '\0';
        *sp = s + 1;
        out->t = T_EXEC;
        out->s = intern(ps, buf);

        return 1;
    }

    if (*s == '/') {
        s++;
        k = 0;

        while (!is_delim(s[k]) && k < sizeof buf - 1) {
            buf[k] = s[k];
            k++;
        }

        buf[k] = '\0';
        *sp = s + k;
        out->t = T_NAME;
        out->s = intern(ps, buf);

        return 1;
    }

    k = 0;

    while (!is_delim(s[k]) && k < sizeof buf - 1) {
        buf[k] = s[k];
        k++;
    }

    if (k == 0) {
        *sp = s + 1;
        out->t = T_NULL;

        return 1;
    }

    buf[k] = '\0';
    *sp = s + k;
    v = strtod(buf, &end);

    if (*end == '\0') {
        out->t = T_NUM;
        out->n = v;
    } else {
        out->t = T_EXEC;
        out->s = intern(ps, buf);
    }

    return 1;
}


static Obj *
lookup(Psi *ps, const char *name)
{
    long i;

    for (i = ps->nds - 1; i >= 0; i--) {
        Obj *o = dict_find(ps->ds[i], name);

        if (o != NULL) {
            return o;
        }
    }

    return NULL;
}


/* Returns the user-space current point of the path. */
static int
user_cp(Psi *ps, double *x, double *y)
{
    GState *s = gfx_cur(ps->g);
    Mat inv;

    if (!s->have_cp || !mat_inv(&s->ctm, &inv)) {
        return 0;
    }

    mat_pt(&inv, s->cx, s->cy, x, y);

    return 1;
}


static void
set_gray(Psi *ps, unsigned long c)
{
    GState *s = gfx_cur(ps->g);

    s->fill = c;
    s->stroke = c;
}


static unsigned long
hsb_pack(double h, double sat, double b)
{
    double rgb[3];
    double f;
    double p;
    double q;
    double t;
    long i;

    rgb[0] = b;
    rgb[1] = b;
    rgb[2] = b;

    if (sat > 1e-9) {
        h = (h - floor(h)) * 6.0;
        i = floor_long(h);
        f = h - dbl(i);
        p = b * (1.0 - sat);
        q = b * (1.0 - sat * f);
        t = b * (1.0 - sat * (1.0 - f));

        switch (i % 6) {
        case 0:
            rgb[0] = b;
            rgb[1] = t;
            rgb[2] = p;
            break;
        case 1:
            rgb[0] = q;
            rgb[1] = b;
            rgb[2] = p;
            break;
        case 2:
            rgb[0] = p;
            rgb[1] = b;
            rgb[2] = t;
            break;
        case 3:
            rgb[0] = p;
            rgb[1] = q;
            rgb[2] = b;
            break;
        case 4:
            rgb[0] = t;
            rgb[1] = p;
            rgb[2] = b;
            break;
        default:
            rgb[0] = b;
            rgb[1] = p;
            rgb[2] = q;
            break;
        }
    }

    return rgb_pack(rgb[0], rgb[1], rgb[2]);
}


static void
op_arith(Psi *ps, int op)
{
    double a = pop_num(ps);
    double b = pop_num(ps);
    double r = 0.0;

    switch (op) {
    case O_ADD:
        r = b + a;
        break;
    case O_SUB:
        r = b - a;
        break;
    case O_MUL:
        r = b * a;
        break;
    case O_DIV:
        r = fabs(a) > 1e-300 ? b / a : 0.0;
        break;
    case O_IDIV:
        r = fabs(a) > 0.5 ? trunc(b / a) : 0.0;
        break;
    case O_MOD:
        r = fabs(a) > 0.5 ? fmod(b, a) : 0.0;
        break;
    case O_ATAN:
        r = atan2(b, a) * 180.0 / PI_VALUE;

        if (r < 0.0) {
            r += 360.0;
        }

        break;
    case O_EXP:
        r = pow(b, a);
        break;
    case O_MAX:
        r = a > b ? a : b;
        break;
    case O_MIN:
        r = a < b ? a : b;
        break;
    case O_BITSHIFT: {
        long bv = round_long(b);
        long sh = round_long(a);

        if (sh > 30) {
            sh = 30;
        }

        if (sh < -30) {
            sh = -30;
        }

        r = dbl(sh >= 0 ? bv << sh : bv >> -sh);
        break;
    }
    default:
        break;
    }

    push(ps, num(r));
}


static void
op_unary(Psi *ps, int op)
{
    double a = pop_num(ps);

    switch (op) {
    case O_NEG:
        a = -a;
        break;
    case O_ABS:
        a = fabs(a);
        break;
    case O_CEILING:
        a = ceil(a);
        break;
    case O_FLOOR:
        a = floor(a);
        break;
    case O_ROUND:
        a = floor(a + 0.5);
        break;
    case O_TRUNCATE:
    case O_CVI:
        a = trunc(a);
        break;
    case O_SQRT:
        a = a > 0.0 ? sqrt(a) : 0.0;
        break;
    case O_SIN:
        a = sin(a * PI_VALUE / 180.0);
        break;
    case O_COS:
        a = cos(a * PI_VALUE / 180.0);
        break;
    case O_LN:
        a = a > 0.0 ? log(a) : 0.0;
        break;
    case O_LOG:
        a = a > 0.0 ? log10(a) : 0.0;
        break;
    default:
        break;
    }

    push(ps, num(a));
}


static void
op_compare(Psi *ps, int op)
{
    Obj b = pop(ps);
    Obj a = pop(ps);
    int r;

    switch (op) {
    case O_EQ:
        r = same_obj(&a, &b);
        break;
    case O_NE:
        r = !same_obj(&a, &b);
        break;
    case O_GT:
        r = a.n > b.n;
        break;
    case O_GE:
        r = a.n >= b.n;
        break;
    case O_LT:
        r = a.n < b.n;
        break;
    default:
        r = a.n <= b.n;
        break;
    }

    push(ps, boolean(r));
}


static void
op_logic(Psi *ps, int op)
{
    if (op == O_NOT) {
        Obj a = pop(ps);

        if (a.t == T_BOOL) {
            push(ps, boolean(a.n < 0.5));
        } else {
            push(ps, num(dbl(~round_long(a.n))));
        }
    } else {
        Obj b = pop(ps);
        Obj a = pop(ps);
        long x = round_long(a.n);
        long y = round_long(b.n);
        long r = op == O_AND ? (x & y) : (op == O_OR ? (x | y) : (x ^ y));

        if (a.t == T_BOOL) {
            push(ps, boolean(r != 0));
        } else {
            push(ps, num(dbl(r)));
        }
    }
}


static void
op_roll(Psi *ps)
{
    long j = round_long(pop_num(ps));
    long n = round_long(pop_num(ps));
    Obj *tmp;
    long i;

    if (n <= 0 || n > ps->sp) {
        return;
    }

    tmp = xcalloc(to_size(n), sizeof *tmp);
    j = ((j % n) + n) % n;

    for (i = 0; i < n; i++) {
        tmp[(i + j) % n] = ps->st[ps->sp - n + i];
    }

    for (i = 0; i < n; i++) {
        ps->st[ps->sp - n + i] = tmp[i];
    }

    free(tmp);
}


static void
op_stack(Psi *ps, int op)
{
    Obj a;
    Obj b;
    long n;
    long i;

    switch (op) {
    case O_POP:
        pop(ps);
        break;
    case O_EXCH:
        b = pop(ps);
        a = pop(ps);
        push(ps, b);
        push(ps, a);
        break;
    case O_DUP:
        a = pop(ps);
        push(ps, a);
        push(ps, a);
        break;
    case O_COPY:
        n = round_long(pop_num(ps));

        if (n < 0 || n > ps->sp) {
            ps->failed = 1;
            break;
        }

        for (i = 0; i < n; i++) {
            push(ps, ps->st[ps->sp - n]);
        }

        break;
    case O_INDEX:
        n = round_long(pop_num(ps));

        if (n < 0 || n >= ps->sp) {
            ps->failed = 1;
            break;
        }

        push(ps, ps->st[ps->sp - 1 - n]);
        break;
    case O_ROLL:
        op_roll(ps);
        break;
    case O_CLEAR:
        ps->sp = 0;
        break;
    case O_COUNT:
        push(ps, num(dbl(ps->sp)));
        break;
    default:
        memset(&a, 0, sizeof a);
        a.t = T_MARK;
        push(ps, a);
        break;
    }
}


static long
find_mark(const Psi *ps)
{
    long i;

    for (i = ps->sp - 1; i >= 0; i--) {
        if (ps->st[i].t == T_MARK) {
            return i;
        }
    }

    return -1;
}


static void
op_mark_ops(Psi *ps, int op)
{
    long m = find_mark(ps);

    if (m < 0) {
        ps->failed = 1;
        return;
    }

    if (op == O_CLEARTOMARK) {
        ps->sp = m;
    } else if (op == O_COUNTTOMARK) {
        push(ps, num(dbl(ps->sp - m - 1)));
    } else {
        Arr *a = xcalloc(1, sizeof *a);
        Obj o;

        a->n = ps->sp - m - 1;
        a->v = xcalloc(to_size(a->n), sizeof *a->v);
        memcpy(a->v, &ps->st[m + 1], to_size(a->n) * sizeof *a->v);
        ps->sp = m;
        memset(&o, 0, sizeof o);
        o.t = T_ARR;
        o.a = a;
        push(ps, o);
    }
}


static Obj
dict_obj(Dict *d)
{
    Obj o;

    memset(&o, 0, sizeof o);
    o.t = T_DICT;
    o.d = d;

    return o;
}


static void
op_where(Psi *ps)
{
    Obj k = pop(ps);
    long i;

    for (i = ps->nds - 1; i >= 0; i--) {
        if (dict_find(ps->ds[i], k.s) != NULL) {
            push(ps, dict_obj(ps->ds[i]));
            push(ps, boolean(1));

            return;
        }
    }

    push(ps, boolean(0));
}


static void
op_put_get(Psi *ps, int op)
{
    Obj v;
    Obj k;
    Obj c;

    if (op == O_PUT) {
        v = pop(ps);
        k = pop(ps);
        c = pop(ps);

        if (c.t == T_DICT && c.d != NULL) {
            if (k.t == T_NAME || k.t == T_EXEC) {
                def_logged(ps, c.d, k.s, v);
            }
        } else if ((c.t == T_ARR || c.t == T_PROC) && c.a != NULL &&
                   round_long(k.n) >= 0 && round_long(k.n) < c.a->n) {
            c.a->v[round_long(k.n)] = v;
        }

        return;
    }

    k = pop(ps);
    c = pop(ps);

    if (c.t == T_DICT && c.d != NULL && k.s != NULL) {
        Obj *o = dict_find(c.d, k.s);

        push(ps, o != NULL ? *o : num(0.0));
    } else if ((c.t == T_ARR || c.t == T_PROC) && c.a != NULL &&
               round_long(k.n) >= 0 && round_long(k.n) < c.a->n) {
        push(ps, c.a->v[round_long(k.n)]);
    } else {
        push(ps, num(0.0));
    }
}


static void
op_dict(Psi *ps, int op)
{
    Obj v;
    Obj k;
    Obj *o;

    switch (op) {
    case O_DEF:
        v = pop(ps);
        k = pop(ps);

        if (k.t == T_NAME || k.t == T_EXEC) {
            def_logged(ps, ps->ds[ps->nds - 1], k.s, v);
        }

        break;
    case O_STORE:
        v = pop(ps);
        k = pop(ps);
        o = lookup(ps, k.s);

        {
            long i;
            Dict *target = ps->ds[ps->nds - 1];

            for (i = ps->nds - 1; i >= 0 && o != NULL; i--) {
                if (dict_find(ps->ds[i], k.s) == o) {
                    target = ps->ds[i];
                    break;
                }
            }

            def_logged(ps, target, k.s, v);
        }

        break;
    case O_LOAD:
        k = pop(ps);
        o = lookup(ps, k.s);

        if (o != NULL) {
            push(ps, *o);
        } else {
            ps->failed = 1;
        }

        break;
    case O_KNOWN:
        k = pop(ps);
        v = pop(ps);
        push(ps, boolean(v.d != NULL && dict_find(v.d, k.s) != NULL));
        break;
    case O_WHERE:
        op_where(ps);
        break;
    case O_DICT:
        pop(ps);
        push(ps, dict_obj(dict_new()));
        break;
    case O_BEGIN:
        v = pop(ps);

        if (v.d != NULL && ps->nds < PS_DSTACK) {
            ps->ds[ps->nds++] = v.d;
        }

        break;
    case O_END:
        if (ps->nds > 3) {
            ps->nds--;
        }

        break;
    case O_CURRENTDICT:
        push(ps, dict_obj(ps->ds[ps->nds - 1]));
        break;
    case O_USERDICT:
        push(ps, dict_obj(ps->user));
        break;
    case O_SYSTEMDICT:
        push(ps, dict_obj(ps->sys));
        break;
    default:
        push(ps, dict_obj(ps->glob));
        break;
    }
}


static void
op_array(Psi *ps, int op)
{
    if (op == O_ARRAY) {
        long n = round_long(pop_num(ps));
        Arr *a = xcalloc(1, sizeof *a);
        Obj o;

        a->n = n < 0 ? 0 : n;
        a->v = xcalloc(to_size(a->n), sizeof *a->v);
        memset(&o, 0, sizeof o);
        o.t = T_ARR;
        o.a = a;
        push(ps, o);
    } else {
        Obj c = pop(ps);

        if (c.t == T_DICT && c.d != NULL) {
            push(ps, num(dbl(c.d->n)));
        } else {
            push(ps, num(c.a != NULL ? dbl(c.a->n) : 0.0));
        }
    }
}


/* Runs the body of a loop; returns 1 to stop the loop. */
static int
run_body(Psi *ps, Obj body, int depth)
{
    exec_obj(ps, body, depth + 1);

    if (ps->exiting) {
        ps->exiting = 0;

        return 1;
    }

    return ps->failed;
}


static void
op_for(Psi *ps, int depth)
{
    Obj p = pop(ps);
    double lim = pop_num(ps);
    double inc = pop_num(ps);
    double v = pop_num(ps);
    long guard = 0;

    while (((inc > 0.0 && v <= lim + 1e-9) ||
            (inc < 0.0 && v >= lim - 1e-9)) && guard++ < 1000000) {
        push(ps, num(v));

        if (run_body(ps, p, depth)) {
            break;
        }

        v += inc;
    }
}


static void
op_control(Psi *ps, int op, int depth)
{
    Obj p;
    Obj p2;
    Obj c;
    long n;
    long i;

    switch (op) {
    case O_IF:
        p = pop(ps);
        c = pop(ps);

        if (c.n > 0.5) {
            exec_obj(ps, p, depth + 1);
        }

        break;
    case O_IFELSE:
        p2 = pop(ps);
        p = pop(ps);
        c = pop(ps);
        exec_obj(ps, c.n > 0.5 ? p : p2, depth + 1);
        break;
    case O_FOR:
        op_for(ps, depth);
        break;
    case O_REPEAT:
        p = pop(ps);
        n = round_long(pop_num(ps));

        for (i = 0; i < n && i < 1000000; i++) {
            if (run_body(ps, p, depth)) {
                break;
            }
        }

        break;
    case O_LOOP:
        p = pop(ps);

        for (i = 0; i < 1000000; i++) {
            if (run_body(ps, p, depth)) {
                break;
            }
        }

        break;
    case O_EXIT:
        ps->exiting = 1;
        break;
    case O_EXEC:
        p = pop(ps);
        exec_obj(ps, p, depth + 1);
        break;
    default:
        break;
    }
}


static void
op_path(Psi *ps, int op)
{
    Gfx *g = ps->g;
    GState *s = gfx_cur(g);
    double v[6];
    double ux = 0.0;
    double uy = 0.0;
    int have;

    switch (op) {
    case O_NEWPATH:
        path_clear(&s->path);
        s->have_cp = 0;
        break;
    case O_MOVETO:
        pop_nums(ps, v, 2);
        gfx_moveto(g, v[0], v[1]);
        break;
    case O_LINETO:
        pop_nums(ps, v, 2);
        gfx_lineto(g, v[0], v[1]);
        break;
    case O_CURVETO:
        pop_nums(ps, v, 6);
        gfx_curveto(g, v);
        break;
    case O_CLOSEPATH:
        gfx_closepath(g);
        break;
    case O_ARC:
    case O_ARCN:
        pop_nums(ps, v, 5);
        gfx_arc(g, v, op == O_ARC);
        break;
    case O_CURRENTPOINT:
        have = user_cp(ps, &ux, &uy);
        push(ps, num(ux));
        push(ps, num(uy));
        ps->failed = ps->failed || !have;
        break;
    default:
        /* The relative operators work from the user-space current point. */
        pop_nums(ps, v, op == O_RCURVETO ? 6 : 2);

        if (!user_cp(ps, &ux, &uy)) {
            ps->failed = 1;
        } else if (op == O_RMOVETO) {
            gfx_moveto(g, ux + v[0], uy + v[1]);
        } else if (op == O_RLINETO) {
            gfx_lineto(g, ux + v[0], uy + v[1]);
        } else {
            v[0] += ux;
            v[1] += uy;
            v[2] += ux;
            v[3] += uy;
            v[4] += ux;
            v[5] += uy;
            gfx_curveto(g, v);
        }

        break;
    }
}


static void
op_paint(Psi *ps, int op)
{
    Gfx *g = ps->g;
    GState *s = gfx_cur(g);
    double v[4];

    switch (op) {
    case O_FILL:
    case O_EOFILL:
    case O_STROKE:
        gfx_paint(g, op != O_STROKE, op == O_STROKE, op == O_EOFILL);
        path_clear(&s->path);
        s->have_cp = 0;
        break;
    case O_CLIP:
    case O_EOCLIP:
        gfx_add_clip(g, op == O_EOCLIP);
        break;
    case O_INITCLIP:
        s->clip = 0;
        break;
    default:
        pop_nums(ps, v, 4);
        path_clear(&s->path);
        gfx_rect(g, v[0], v[1], v[2], v[3]);

        if (op == O_RECTCLIP) {
            gfx_add_clip(g, 0);
        } else {
            gfx_paint(g, op == O_RECTFILL, op == O_RECTSTROKE, 0);
        }

        path_clear(&s->path);
        s->have_cp = 0;
        break;
    }
}


static void
op_concat(Psi *ps)
{
    Obj a = pop(ps);
    Mat m;

    if (a.a != NULL && a.a->n >= 6) {
        m.a = a.a->v[0].n;
        m.b = a.a->v[1].n;
        m.c = a.a->v[2].n;
        m.d = a.a->v[3].n;
        m.e = a.a->v[4].n;
        m.f = a.a->v[5].n;
        gfx_concat(ps->g, &m);
    }
}


static void
op_transform(Psi *ps, int op)
{
    GState *s = gfx_cur(ps->g);
    double v[2];
    double ox;
    double oy;
    Mat inv;

    pop_nums(ps, v, 2);

    if (op == O_TRANSFORM) {
        mat_pt(&s->ctm, v[0], v[1], &ox, &oy);
    } else {
        if (!mat_inv(&s->ctm, &inv)) {
            inv = mat_ident();
        }

        mat_pt(&inv, v[0], v[1], &ox, &oy);
    }

    push(ps, num(ox));
    push(ps, num(oy));
}


static void
op_matrix(Psi *ps, int op)
{
    Gfx *g = ps->g;
    double v[2];
    Mat m;

    switch (op) {
    case O_TRANSLATE:
        pop_nums(ps, v, 2);
        m = mat_translate(v[0], v[1]);
        gfx_concat(g, &m);
        break;
    case O_SCALE:
        pop_nums(ps, v, 2);
        m = mat_scale(v[0], v[1]);
        gfx_concat(g, &m);
        break;
    case O_ROTATE:
        v[0] = pop_num(ps) * PI_VALUE / 180.0;
        m = mat_ident();
        m.a = cos(v[0]);
        m.b = sin(v[0]);
        m.c = -sin(v[0]);
        m.d = cos(v[0]);
        gfx_concat(g, &m);
        break;
    case O_CONCAT:
        op_concat(ps);
        break;
    case O_INITMATRIX:
        gfx_cur(g)->ctm = mat_ident();
        break;
    default:
        op_transform(ps, op);
        break;
    }
}


static void
op_dash(Psi *ps)
{
    GState *s = gfx_cur(ps->g);
    double ph = pop_num(ps);
    Obj a = pop(ps);
    long i;

    s->st.ndash = 0;
    s->st.phase = ph;

    if (a.a != NULL) {
        for (i = 0; i < a.a->n && i < 8; i++) {
            s->st.dash[i] = a.a->v[i].n;
            s->st.ndash++;
        }
    }
}


static void
op_style(Psi *ps, int op)
{
    GState *s = gfx_cur(ps->g);
    double v[4];

    switch (op) {
    case O_SETLINEWIDTH:
        s->st.lw = pop_num(ps);
        break;
    case O_CURRENTLINEWIDTH:
        push(ps, num(s->st.lw));
        break;
    case O_SETLINECAP:
        s->st.cap = to_int(round_long(pop_num(ps)));
        break;
    case O_SETLINEJOIN:
        s->st.join = to_int(round_long(pop_num(ps)));
        break;
    case O_SETMITER:
        s->st.miter = pop_num(ps);
        break;
    case O_SETDASH:
        op_dash(ps);
        break;
    case O_SETGRAY:
        pop_nums(ps, v, 1);
        set_gray(ps, rgb_pack(v[0], v[0], v[0]));
        break;
    case O_SETRGB:
        pop_nums(ps, v, 3);
        set_gray(ps, rgb_pack(v[0], v[1], v[2]));
        break;
    case O_SETCMYK:
        pop_nums(ps, v, 4);
        set_gray(ps, cmyk_pack(v[0], v[1], v[2], v[3]));
        break;
    default:
        pop_nums(ps, v, 3);
        set_gray(ps, hsb_pack(v[0], v[1], v[2]));
        break;
    }
}


/* The dvips specials: the origin moves to the current point, y goes up. */
static void
op_special(Psi *ps, int op)
{
    Gfx *g = ps->g;

    if (op == O_BEGINSPECIAL) {
        double ux = 0.0;
        double uy = 0.0;
        Mat m;

        user_cp(ps, &ux, &uy);
        gfx_save(g);
        m = mat_scale(1.0, -1.0);
        m.e = ux;
        m.f = uy;
        gfx_concat(g, &m);
        path_clear(&gfx_cur(g)->path);
        gfx_cur(g)->have_cp = 0;
    } else {
        gfx_restore(g, 1);
    }
}


static void
op_state(Psi *ps, int op)
{
    Gfx *g = ps->g;
    Obj o;

    switch (op) {
    case O_GSAVE:
        gfx_save(g);
        break;
    case O_GRESTORE:
        gfx_restore(g, 1);
        break;
    case O_SAVE:
        memset(&o, 0, sizeof o);
        o.t = T_SAVE;
        o.n = dbl(g->nstack);
        o.aux = ps->nundo;
        ps->nsaves++;
        push(ps, o);
        break;
    default:
        o = pop(ps);
        undo_to(ps, o.aux);

        if (ps->nsaves > 0) {
            ps->nsaves--;
        }

        while (g->nstack > round_long(o.n) && g->nstack > 1) {
            gfx_restore(g, 1);
        }

        break;
    }
}


/* Operators that need no graphics state and no control flow. */
static void
do_plain(Psi *ps, int op)
{
    if (op == O_ARRAYEND || op == O_CLEARTOMARK || op == O_COUNTTOMARK) {
        op_mark_ops(ps, op);
    } else if (op <= O_MARK || op == O_ARRAYSTART) {
        op_stack(ps, op);
    } else if (op <= O_BITSHIFT) {
        op_arith(ps, op);
    } else if (op <= O_LOG || op == O_CVI) {
        op_unary(ps, op);
    } else if (op >= O_EQ && op <= O_LE) {
        op_compare(ps, op);
    } else {
        op_logic(ps, op);
    }
}


static void
do_op(Psi *ps, int op, int depth)
{
    if (op == O_CVR || op == O_BIND || op == O_CVX || op == O_IGNORE0) {
        return;
    }

    if (op == O_ARRAYEND || op == O_CLEARTOMARK || op == O_COUNTTOMARK ||
        op <= O_LOG || op == O_CVI ||
        (op >= O_EQ && op <= O_XOR)) {
        do_plain(ps, op);
    } else if (op >= O_IF && op <= O_EXEC) {
        op_control(ps, op, depth);
    } else if (op >= O_DEF && op <= O_GLOBALDICT) {
        op_dict(ps, op);
    } else if (op == O_PUT || op == O_GET) {
        op_put_get(ps, op);
    } else if (op == O_ARRAY || op == O_LENGTH) {
        op_array(ps, op);
    } else if (op >= O_GSAVE && op <= O_RESTORE) {
        op_state(ps, op);
    } else if (op >= O_NEWPATH && op <= O_CURRENTPOINT) {
        op_path(ps, op);
    } else if (op >= O_FILL && op <= O_INITCLIP) {
        op_paint(ps, op);
    } else if (op >= O_TRANSLATE && op <= O_ITRANSFORM) {
        op_matrix(ps, op);
    } else if (op >= O_SETLINEWIDTH && op <= O_SETHSB) {
        op_style(ps, op);
    } else if (op == O_BEGINSPECIAL || op == O_ENDSPECIAL) {
        op_special(ps, op);
    } else if (op == O_IGNORE1) {
        pop(ps);
    } else if (op == O_TRUE || op == O_FALSE) {
        push(ps, boolean(op == O_TRUE));
    } else if (op == O_SHOW) {
        pop(ps);
        gfx_unsupported("ps show (text in PostScript)");
    } else if (op == O_CURRENTFLAT) {
        push(ps, num(1.0));
    } else {
        ps->failed = 1;
    }
}


static void
exec_obj(Psi *ps, Obj o, int depth)
{
    /* A budget of steps stops runaway recursion in a hostile file. */
    if (depth > PS_DEPTH || ps->failed || ++ps->steps > 3000000L) {
        ps->failed = 1;

        return;
    }

    if (o.t == T_PROC) {
        exec_arr(ps, o.a, depth);
    } else if (o.t == T_EXEC) {
        Obj *v = lookup(ps, o.s);

        if (v == NULL) {
            char what[96];

            snprintf(what, sizeof what, "ps name %.40s (undefined)", o.s);
            gfx_unsupported(what);
            ps->failed = 1;
        } else if (v->t == T_OP) {
            do_op(ps, to_int(round_long(v->n)), depth);
        } else if (v->t == T_PROC) {
            exec_arr(ps, v->a, depth);
        } else {
            push(ps, *v);
        }
    } else if (o.t != T_NULL) {
        push(ps, o);
    }
}


static void
exec_arr(Psi *ps, const Arr *a, int depth)
{
    long i;

    for (i = 0; i < a->n && !ps->exiting && !ps->failed; i++) {
        Obj o = a->v[i];

        /* A procedure inside a procedure is data for if, def and so on. */
        if (o.t == T_PROC) {
            push(ps, o);
        } else {
            exec_obj(ps, o, depth + 1);
        }
    }
}


static void
def_sys(Psi *ps, const char *name, Obj v)
{
    dict_put(ps->sys, intern(ps, name), v);
}


Psi *
psi_new(void)
{
    Psi *ps = xcalloc(1, sizeof *ps);
    size_t i;
    Obj o;

    ps->sys = dict_new();
    ps->glob = dict_new();
    ps->user = dict_new();
    ps->ds[0] = ps->sys;
    ps->ds[1] = ps->glob;
    ps->ds[2] = ps->user;
    ps->nds = 3;

    for (i = 0; i < sizeof op_table / sizeof op_table[0]; i++) {
        memset(&o, 0, sizeof o);
        o.t = T_OP;
        o.n = dbl(op_table[i].op);
        def_sys(ps, op_table[i].name, o);
    }

    def_sys(ps, "Resolution", num(72.0));
    def_sys(ps, "VResolution", num(72.0));
    def_sys(ps, "DVImag", num(1000.0));
    def_sys(ps, "magscale", boolean(0));
    def_sys(ps, "delta", num(0.0));

    return ps;
}


/* A new page: the stack and the graphics state start again. */
void
psi_begin_page(Psi *ps, Gfx *g)
{
    ps->g = g;
    ps->sp = 0;
    ps->nds = 3;
    ps->failed = 0;
    ps->exiting = 0;
    ps->nundo = 0;
    ps->nsaves = 0;
}


/*
 * Runs a special; moveto_first makes the current point the position of the
 * special on the page, as dvips does for "ps:" (but not for "ps::").
 */
void
psi_run(Psi *ps, const char *s, int moveto_first)
{
    Obj o;
    const char *p = s;

    if (moveto_first) {
        GState *st = gfx_cur(ps->g);

        path_clear(&st->path);
        st->have_cp = 0;
        gfx_moveto(ps->g, ps->g->px, ps->g->py);
    }

    ps->failed = 0;
    ps->steps = 0;

    while (next_token(ps, &p, &o, 0)) {
        /* A procedure met in the text is data, not something to run. */
        if (o.t == T_PROC) {
            push(ps, o);
        } else {
            exec_obj(ps, o, 0);
        }

        if (ps->failed) {
            ps->sp = 0;
            break;
        }
    }
}
