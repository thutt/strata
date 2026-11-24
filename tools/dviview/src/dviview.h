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
 * dviview: a viewer for TeX DVI files.  Shared declarations.
 */
#ifndef DVIVIEW_H
#define DVIVIEW_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "log.h"

/*
 * What the user must learn from the Viewer is not logged: it is told to the
 * Editor in the report file that the option --report names (one line for
 * each thing, "note: TEXT" for what the Viewer cannot show and "fail: TEXT"
 * for what it cannot go on with).  A debug or asan build also writes the
 * line to the console; a failure also goes to the system log.  A release
 * build writes nothing to the console.
 */
void viewer_report_to(const char *path);
void viewer_tell(const char *kind, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

/* Checked conversions: the only places where narrowing casts live. */
int to_int(long v);
unsigned to_uint(long v);
unsigned char to_u8(long v);
char to_char(long v);
double dbl(long v);
long round_long(double v);
long floor_long(double v);
long ceil_long(double v);

void *xcalloc(size_t n, size_t size);
void *xrealloc(void *p, size_t size);
char *xstrdup(const char *s);

/* Reads a whole file; returns NULL on failure. */
unsigned char *read_file(const char *path, size_t *len);

/* Runs kpsewhich with the given arguments; returns a path or NULL. */
char *kpse_find(const char *arg1, const char *arg2, const char *arg3);

/* Big-endian readers over a byte buffer. */
long get_u(const unsigned char *p, int n);
long get_s(const unsigned char *p, int n);

/* A TFM file: widths, heights and depths in units of the design size. */
typedef struct {
    long bc;
    long ec;
    double design;
    double *wd;
    double *ht;
    double *dp;
} Tfm;

Tfm *tfm_load(const char *path);

/* One glyph bitmap with its placement relative to the reference point. */
typedef struct {
    long left;
    long top;
    long w;
    long h;
    unsigned char *a;
} Glyph;

/* Parsed PK file: bitmaps of the characters at the file resolution. */
typedef struct {
    long ch;
    long w;
    long h;
    long hoff;
    long voff;
    unsigned char *bits;
} PkChar;

typedef struct {
    PkChar *chars;
    long n;
} Pk;

Pk *pk_load(const char *path);
Glyph *pk_glyph(const Pk *pk, long ch, double ratio);

/* The pdftex.map line of a font. */
typedef struct {
    char *pfb;
    char *enc;
    double slant;
    double extend;
} MapEntry;

int map_lookup(const char *tfmname, MapEntry *out);
char **enc_load(const char *name);

struct DFont;
typedef struct DFont DFont;

/* A virtual font: character packets with their own font table. */
typedef struct VfChar {
    long ch;
    double tfmw;
    const unsigned char *code;
    long len;
} VfChar;

typedef struct {
    struct DFont **f;
    long *num;
    long n;
} FTab;

typedef struct {
    VfChar *chars;
    long nchars;
    FTab ft;
    unsigned char *data;
} Vf;

/* A font as the DVI file uses it: a name at one scaled size. */
struct DFont {
    char *name;
    long size;
    long design;
    Tfm *tfm;
    Vf *vf;
    void *face;
    Pk *pk;
    int missing;
    int mk_tried;
    int loaded;
    int id;
    long cur_size64;
    char **enc;
    MapEntry map;
    int have_map;
};

DFont *font_get(const char *name, long size, long design);
Glyph *glyph_get(DFont *f, long ch, double px_per_unit);
void glyph_flush(void);
double font_width(const DFont *f, long ch);
void font_init(double unit, long mag);
void font_load(DFont *f);

/*
 * Making the bitmap of a font that has no outline and no bitmap (mkfont.c).
 * mk_request queues the font at the resolution DPI and returns 1; it returns
 * 0 when the font was tried before or the programs that make it are not
 * installed.  mk_poll goes on with the jobs and returns how many fonts got
 * their bitmap since the last call (the page must be built again); mk_status
 * is the sentence for the status bar while there is work, else NULL.
 */
int mk_request(DFont *f, long dpi);
int mk_poll(void);
int mk_active(void);
int mk_possible(void);
void mk_stop(void);
const char *mk_status(void);
size_t to_size(long v);
long to_long(size_t v);

/* An affine matrix: x' = a*x + c*y + e and y' = b*x + d*y + f. */
typedef struct {
    double a;
    double b;
    double c;
    double d;
    double e;
    double f;
} Mat;

Mat mat_ident(void);
Mat mat_mul(Mat first, Mat second);
Mat mat_translate(double tx, double ty);
Mat mat_scale(double sx, double sy);
int mat_inv(const Mat *m, Mat *out);
void mat_pt(const Mat *m, double x, double y, double *ox, double *oy);
double mat_det_scale(const Mat *m);
int mat_is_ident(const Mat *m);

/* A path: the operations M, L, C (three points) and Z. */
typedef struct {
    char *op;
    long nop;
    long capop;
    double *xy;
    long nxy;
    long capxy;
} Path;

void path_clear(Path *p);
void path_free(Path *p);
void path_copy(Path *dst, const Path *src);
void path_move(Path *p, double x, double y);
void path_line(Path *p, double x, double y);
void path_curve(Path *p, const double *c);
void path_close(Path *p);
void path_rect(Path *p, double x, double y, double w, double h);
void path_transform(Path *p, const Mat *m);

/* A coverage mask: one byte per pixel, 255 is fully covered. */
typedef struct {
    long x0;
    long y0;
    long w;
    long h;
    unsigned char *a;
} Cov;

typedef struct {
    double lw;
    double dash[8];
    int ndash;
    double phase;
    int cap;
    int join;
    double miter;
} StrokeStyle;

/* The bounds are a pixel box (x0, y0, x1, y1) the result is limited to. */
Cov *raster_fill(const Path *p, const Mat *view, int evenodd,
                 const long *bounds);
Cov *raster_stroke(const Path *p, const Mat *view, const StrokeStyle *st,
                   const long *bounds);
void cov_free(Cov *c);

/* Display list items. */
enum { IT_GLYPH, IT_RULE, IT_BOX, IT_PATH, IT_IMAGE };

typedef struct {
    int type;
    DFont *font;
    long ch;
    long x;
    long y;
    long w;
    long h;
    unsigned long rgb;
    int xf;
    int clip;
    long ref;
} Item;

/* A painted path; its coordinates are in bp on the page, y down. */
typedef struct {
    Path path;
    unsigned long fill;
    unsigned long stroke;
    int do_fill;
    int do_stroke;
    int evenodd;
    StrokeStyle st;
} PathItem;

/* A clip: the path, its fill rule, and the clip that was in force. */
typedef struct {
    Path path;
    int evenodd;
    int parent;
} ClipItem;

/* A picture: the file, the box of it that is shown and where it goes. */
typedef struct {
    char *name;
    int page;
    int media_box;
    int has_bbox;
    double bbox[4];
    int clip;
    double width;
    double height;
    double xscale;
    double yscale;
    Mat m;
} ImgItem;

typedef struct {
    Item *v;
    long n;
    long cap;
    PathItem *paths;
    long npaths;
    ClipItem *clips;
    long nclips;
    ImgItem *imgs;
    long nimgs;
    Mat *xfs;
    long nxfs;
    int rotate;
    int has_bg;
    unsigned long bg;
    double paper_w;
    double paper_h;
} ItemList;

void itemlist_reset(ItemList *l);

/* The graphics state shared by the PDF and the PostScript specials. */
typedef struct {
    Mat ctm;
    unsigned long fill;
    unsigned long stroke;
    StrokeStyle st;
    int clip;
    Path path;
    double cx;
    double cy;
    int have_cp;
} GState;

typedef struct {
    ItemList *out;
    GState *stack;
    long nstack;
    long capstack;
    double paper_h;
    double px;
    double py;
    int last_xf;
    int pend_clip;
} Gfx;

void gfx_init(Gfx *g, ItemList *out, double paper_h);
void gfx_done(Gfx *g);
GState *gfx_cur(Gfx *g);
void gfx_save(Gfx *g);
void gfx_restore(Gfx *g, int with_path);
void gfx_set_pos(Gfx *g, double px, double py);
int gfx_xf(Gfx *g);
void gfx_add_clip(Gfx *g, int evenodd);
void gfx_paint(Gfx *g, int fill, int stroke, int evenodd);
void gfx_moveto(Gfx *g, double x, double y);
void gfx_lineto(Gfx *g, double x, double y);
void gfx_curveto(Gfx *g, const double *c);
void gfx_closepath(Gfx *g);
void gfx_arc(Gfx *g, const double *a, int ccw);
void gfx_rect(Gfx *g, double x, double y, double w, double h);
void gfx_concat(Gfx *g, const Mat *m);
void gfx_unsupported(const char *what);
unsigned long rgb_pack(double r, double g, double b);
unsigned long cmyk_pack(double c, double m, double y, double k);

/* PDF content operators on a string; mode 1 flips y as PDF does. */
void pdf_run(Gfx *g, const char *s);
void pdf_literal_page(Gfx *g, const char *s);

/* The PostScript subset for the "ps:" specials of dvips. */
typedef struct Psi Psi;

Psi *psi_new(void);
void psi_begin_page(Psi *ps, Gfx *g);
void psi_run(Psi *ps, const char *s, int moveto_first);

/* Pictures. */
typedef struct {
    long w;
    long h;
    unsigned char *rgba;
    double ext[4];
    int ok;
} Image;

void image_set_dir(const char *dir);
Image *image_get(const char *name, int page, int media_box, double dpi);
void image_flush(void);
void image_cleanup(void);
int image_decode_jpeg(const unsigned char *d, size_t len, Image *im,
                      double *dpi);

/* Draws small text with the glyph cache; used for names of lost files. */
Glyph *glyph_get_xf(DFont *f, long ch, double px_per_unit, const Mat *lin);

/* The DVI file. */
typedef struct {
    unsigned char *data;
    size_t len;
    long npages;
    long *offs;
    double unit;
    long mag;
    double paper_w;
    double paper_h;
    FTab ft;
    char *dir;
    int prologue_done;
    Psi *ps;
    long failed; /* The errors that the print had, or 0 for a good print. */
    char *path; /* The name of the file, for what is reported about it. */
} Dvi;

Dvi *dvi_open(const char *path);
int dvi_page(Dvi *d, long page, ItemList *out);
void dvi_report_special(const char *kind);

/* The state of the interpreter while it makes the items of a page. */
typedef struct {
    Dvi *d;
    ItemList *out;
    Gfx g;
    unsigned long colors[256][2];
    int ncolors;
    int ps_bound;
    long page; /* The number of the page that is being made. */
} Ctx;

void dvi_add_item(Ctx *c, const Item *it);
void dvi_ps_prologue(Dvi *d);
void special_run(Ctx *c, const unsigned char *p, long len);
void dvi_special_context(const Ctx *c, const unsigned char *p, long len);
void special_prologue(Dvi *d, Gfx *g, const unsigned char *p, long len);

/* The window buffer the pages are drawn into: 32 bits per pixel. */
typedef struct {
    char *buf;
    long w;
    long h;
    int rb;
    int gb;
    int bb;
} Canvas;

/* Where the page is on the screen: zoom, offset, rotation. */
typedef struct {
    const Dvi *d;
    double eff;
    double ppu;
    long px0;
    long py0;
    long pw;
    long ph;
    int rotate;
    Mat view;
} View;

void canvas_fill_rect(const Canvas *cv, long x0, long y0, long x1, long y1,
                      unsigned long rgb);
void view_make(View *v, const Dvi *d, const ItemList *l, double eff, long w,
               long h, long scroll_x, long scroll_y);
void render_page(const Canvas *cv, const ItemList *l, const View *v);
int image_extent(const char *name, int page, int media_box, double *ext);

#endif
