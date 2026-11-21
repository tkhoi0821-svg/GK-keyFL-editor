/* GK KeyFL Editor 0.1 - editor 2D bằng C thuần (lõi không cần thư viện; cửa sổ dùng SDL3 khi build với -DGK_SDL3).
 * Định dạng .gkproj v1: xem FORMAT.md. Giấy phép MIT. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <math.h>
#include <time.h>
#ifdef GK_SDL3
#include <SDL3/SDL.h>
#endif
#define GK_VERSION "0.1.0"
typedef uint32_t u32; typedef uint8_t u8;

/* ================= tài liệu ================= */
#define MAXL 16
#define UNDOMAX 32
typedef struct { char name[32]; u32 *px; int vis; } Layer;
typedef struct { char type[4]; u32 len; u8 *data; } Chunk;
typedef struct { int w, h, n, cur; Layer l[MAXL]; Chunk *ext; int next; } Doc;
typedef struct { int layer; u32 *px; } Snap;
static Doc D; static Snap us[UNDOMAX], rs[UNDOMAX]; static int un, rn;
static u32 *comp; static int comp_dirty = 1;

static u32 over(u32 d, u32 s) {
    int sa = s >> 24; if (sa == 255) return s; if (!sa) return d;
    int da = d >> 24, oa = sa + da * (255 - sa) / 255; if (!oa) return 0;
#define CH(sh) ((u32)((((s >> sh) & 255) * sa * 255 + ((d >> sh) & 255) * da * (255 - sa)) / (oa * 255)))
    return ((u32)oa << 24) | (CH(16) << 16) | (CH(8) << 8) | CH(0);
#undef CH
}
static void snap_free(Snap *s, int *n) { for (int i = 0; i < *n; i++) free(s[i].px); *n = 0; }
static void undo_clear(void) { snap_free(us, &un); snap_free(rs, &rn); }
static void doc_free(Doc *d) {
    for (int i = 0; i < d->n; i++) free(d->l[i].px);
    for (int i = 0; i < d->next; i++) free(d->ext[i].data);
    free(d->ext); memset(d, 0, sizeof *d);
}
static int layer_add(Doc *d, const char *name, u32 fill) {
    if (d->n >= MAXL) return 0;
    u32 *p = malloc((size_t)d->w * d->h * 4); if (!p) return 0;
    for (size_t i = 0; i < (size_t)d->w * d->h; i++) p[i] = fill;
    int at = d->n ? d->cur + 1 : 0;
    memmove(&d->l[at + 1], &d->l[at], sizeof(Layer) * (d->n - at));
    memset(&d->l[at], 0, sizeof(Layer)); snprintf(d->l[at].name, 32, "%s", name); d->l[at].px = p; d->l[at].vis = 1;
    d->n++; d->cur = at; return 1;
}
static void doc_new(int w, int h) {
    doc_free(&D); undo_clear(); D.w = w; D.h = h;
    layer_add(&D, "BACKGROUND", 0xffffffffu); layer_add(&D, "LAYER 1", 0); comp_dirty = 1;
}
static void layer_del(void) {
    if (D.n <= 1) return; undo_clear(); free(D.l[D.cur].px);
    memmove(&D.l[D.cur], &D.l[D.cur + 1], sizeof(Layer) * (D.n - D.cur - 1)); D.n--;
    if (D.cur >= D.n) D.cur = D.n - 1; comp_dirty = 1;
}
/* hoàn tác theo ảnh chụp lớp hiện tại */
static u32 *dup_layer(int li) { size_t b = (size_t)D.w * D.h * 4; u32 *p = malloc(b); if (p) memcpy(p, D.l[li].px, b); return p; }
static void push_undo(void) {
    snap_free(rs, &rn);
    if (un == UNDOMAX) { free(us[0].px); memmove(&us[0], &us[1], sizeof(Snap) * (UNDOMAX - 1)); un--; }
    us[un].layer = D.cur; us[un].px = dup_layer(D.cur); if (us[un].px) un++;
}
static int do_undo(int redo) {
    Snap *from = redo ? rs : us, *to = redo ? us : rs; int *nf = redo ? &rn : &un, *nt = redo ? &un : &rn;
    if (!*nf) return 0; Snap s = from[--*nf];
    if (s.layer >= D.n) { free(s.px); return 0; }
    if (*nt == UNDOMAX) { free(to[0].px); memmove(&to[0], &to[1], sizeof(Snap) * (UNDOMAX - 1)); (*nt)--; }
    to[*nt].layer = s.layer; to[*nt].px = dup_layer(s.layer); if (to[*nt].px) (*nt)++;
    memcpy(D.l[s.layer].px, s.px, (size_t)D.w * D.h * 4); free(s.px); D.cur = s.layer; comp_dirty = 1; return 1;
}

/* ================= công cụ vẽ ================= */
enum { T_PEN, T_ERASE, T_FILL, T_LINE, T_RECT, T_PICK, T_N };
static int tool = T_PEN, bsize = 4; static u32 color = 0xff202020u;
static void stamp(int cx, int cy, int size, u32 c, int erase) {
    u32 *p = D.l[D.cur].px; int r = size / 2;
    for (int dy = -r; dy <= r; dy++) for (int dx = -r; dx <= r; dx++) {
        if (size > 2 && dx * dx + dy * dy > r * r + r) continue;
        int x = cx + dx, y = cy + dy; if (x < 0 || y < 0 || x >= D.w || y >= D.h) continue;
        u32 *q = &p[(size_t)y * D.w + x]; *q = erase ? 0 : over(*q, c);
    }
    comp_dirty = 1;
}
static void draw_line(int x0, int y0, int x1, int y1, int size, u32 c, int erase) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) {
        stamp(x0, y0, size, c, erase); if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err; if (e2 >= dy) { err += dy; x0 += sx; } if (e2 <= dx) { err += dx; y0 += sy; }
    }
}
static void draw_rect(int x0, int y0, int x1, int y1, int size, u32 c) {
    draw_line(x0, y0, x1, y0, size, c, 0); draw_line(x1, y0, x1, y1, size, c, 0);
    draw_line(x1, y1, x0, y1, size, c, 0); draw_line(x0, y1, x0, y0, size, c, 0);
}
static void flood(int x, int y, u32 c) {
    if (x < 0 || y < 0 || x >= D.w || y >= D.h) return;
    u32 *p = D.l[D.cur].px, t = p[(size_t)y * D.w + x]; if (t == c) return;
    size_t cap = 1024, n = 0; int *st = malloc(cap * 2 * sizeof(int)); if (!st) return;
    st[0] = x; st[1] = y; n = 1;
    while (n) {
        n--; int px = st[n * 2], py = st[n * 2 + 1]; int l = px; if (p[(size_t)py * D.w + px] != t) continue;
        while (l > 0 && p[(size_t)py * D.w + l - 1] == t) l--;
        int r = px; while (r < D.w - 1 && p[(size_t)py * D.w + r + 1] == t) r++;
        for (int i = l; i <= r; i++) p[(size_t)py * D.w + i] = c;
        for (int dy = -1; dy <= 1; dy += 2) { int yy = py + dy; if (yy < 0 || yy >= D.h) continue; int in = 0;
            for (int i = l; i <= r; i++) { int m = p[(size_t)yy * D.w + i] == t;
                if (m && !in) { if (n + 1 > cap) { cap *= 2; int *q = realloc(st, cap * 2 * sizeof(int)); if (!q) { free(st); return; } st = q; }
                    st[n * 2] = i; st[n * 2 + 1] = yy; n++; }
                in = m; } }
    }
    free(st); comp_dirty = 1;
}
static void flatten(u32 *out, int checker) {
    for (int y = 0; y < D.h; y++) for (int x = 0; x < D.w; x++) {
        size_t i = (size_t)y * D.w + x; u32 b = checker ? ((((x >> 3) + (y >> 3)) & 1) ? 0xffc8c8c8u : 0xffffffffu) : 0;
        for (int k = 0; k < D.n; k++) if (D.l[k].vis) b = over(b, D.l[k].px[i]);
        out[i] = b;
    }
}
static void rebuild_comp(void) {
    comp = realloc(comp, (size_t)D.w * D.h * 4); if (comp) flatten(comp, 1); comp_dirty = 0;
}

/* ================= tệp .gkproj ================= */
typedef struct { u8 *p; size_t n, cap; } Buf;
static void bput(Buf *b, const void *d, size_t n) { if (b->n + n > b->cap) { b->cap = (b->n + n) * 2 + 256; b->p = realloc(b->p, b->cap); } memcpy(b->p + b->n, d, n); b->n += n; }
static void b32(Buf *b, u32 v) { u8 t[4] = {(u8)v, (u8)(v >> 8), (u8)(v >> 16), (u8)(v >> 24)}; bput(b, t, 4); }
static u32 r32(const u8 *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24); }
static u32 fnv(const char *k, u32 salt) { u32 h = 2166136261u ^ salt; for (; *k; k++) { h ^= (u8)*k; h *= 16777619u; } return h; }
static void ks_xor(u32 *s, u8 *d, size_t n) { for (size_t i = 0; i < n; i++) { u32 x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; *s = x; d[i] ^= (u8)(x >> 11); } }
static void put_chunk(Buf *o, const char *t, const u8 *d, u32 n, u32 *st) {
    bput(o, t, 4); b32(o, n); size_t at = o->n; bput(o, d, n); if (*st) ks_xor(st, o->p + at, n);
}
static void rand_bytes(void *p, size_t n) {
    FILE *f = fopen("/dev/urandom", "rb"); if (f) { size_t k = fread(p, 1, n, f); fclose(f); if (k == n) return; }
    u32 t = (u32)time(NULL) ^ (u32)clock(); memcpy(p, &t, n < 4 ? n : 4);
}
/* key: 4 ký tự (hoặc NULL/rỗng = không khoá). Đây là khoá nhẹ chống mở nhầm, KHÔNG phải mã hoá mạnh. */
static int doc_save(const char *path, const char *key) {
    int enc = key && *key; u32 salt = 0, st = 0; if (enc) { rand_bytes(&salt, 4); st = fnv(key, salt) | 1; }
    Buf o = {0}; bput(&o, "GKPJ", 4); u8 hd[4] = {1, (u8)enc, 0, 0}; bput(&o, hd, 4); b32(&o, D.w); b32(&o, D.h);
    if (enc) { b32(&o, salt); b32(&o, fnv(key, salt ^ 0x5bd1e995u)); }
    const char *meta = "GK KeyFL Editor " GK_VERSION; put_chunk(&o, "META", (const u8 *)meta, (u32)strlen(meta), &st);
    for (int i = 0; i < D.n; i++) {
        Buf lb = {0}; u8 v = (u8)D.l[i].vis; bput(&lb, &v, 1); bput(&lb, D.l[i].name, 32);
        size_t total = (size_t)D.w * D.h, k = 0; u32 *px = D.l[i].px;
        while (k < total) { size_t r = 1; while (k + r < total && r < 255 && px[k + r] == px[k]) r++; u8 c = (u8)r; bput(&lb, &c, 1); b32(&lb, px[k]); k += r; }
        put_chunk(&o, "LAYR", lb.p, (u32)lb.n, &st); free(lb.p);
    }
    for (int i = 0; i < D.next; i++) put_chunk(&o, D.ext[i].type, D.ext[i].data, D.ext[i].len, &st);
    FILE *f = fopen(path, "wb"); int ok = f && fwrite(o.p, 1, o.n, f) == o.n; if (f) fclose(f); free(o.p); return ok;
}
/* 0 ok, 1 lỗi đọc, 2 sai định dạng, 3 cần/ sai khoá */
static int doc_load(const char *path, const char *key) {
    FILE *f = fopen(path, "rb"); if (!f) return 1;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET); if (n < 20 || n > (1L << 30)) { fclose(f); return 2; }
    u8 *b = malloc(n); if (!b || fread(b, 1, n, f) != (size_t)n) { fclose(f); free(b); return 1; } fclose(f);
    int rc = 2; Doc T; memset(&T, 0, sizeof T);
    if (memcmp(b, "GKPJ", 4) || b[4] != 1) goto done;
    int enc = b[5] & 1; T.w = (int)r32(b + 8); T.h = (int)r32(b + 12); size_t pos = 16;
    if (T.w <= 0 || T.h <= 0 || T.w > 8192 || T.h > 8192 || (size_t)T.w * T.h > (16u << 20)) goto done;
    u32 st = 0;
    if (enc) {
        if (!key || !*key) { rc = 3; goto done; } if (pos + 8 > (size_t)n) goto done;
        u32 salt = r32(b + pos), chk = r32(b + pos + 4); pos += 8;
        if (fnv(key, salt ^ 0x5bd1e995u) != chk) { rc = 3; goto done; } st = fnv(key, salt) | 1;
    }
    while (pos + 8 <= (size_t)n) {
        u32 len = r32(b + pos + 4); if (len > (size_t)n - pos - 8) goto done;
        u8 *pl = malloc(len ? len : 1); memcpy(pl, b + pos + 8, len); if (st) ks_xor(&st, pl, len);
        const char *t = (const char *)b + pos; pos += 8 + len;
        if (!memcmp(t, "META", 4)) { free(pl); }
        else if (!memcmp(t, "LAYR", 4)) {
            size_t total = (size_t)T.w * T.h; if (len < 33 || T.n >= MAXL) { free(pl); goto done; }
            u32 *px = malloc(total * 4); size_t k = 0, q = 33;
            while (px && q + 5 <= len && k < total) { size_t r = pl[q]; u32 v = r32(pl + q + 1); q += 5; if (!r || k + r > total) break; while (r--) px[k++] = v; }
            if (!px || k != total) { free(px); free(pl); goto done; }
            Layer *L = &T.l[T.n++]; L->px = px; L->vis = pl[0] & 1; memcpy(L->name, pl + 1, 31); L->name[31] = 0; free(pl);
        } else {
            T.ext = realloc(T.ext, sizeof(Chunk) * (T.next + 1));
            memcpy(T.ext[T.next].type, t, 4); T.ext[T.next].len = len; T.ext[T.next].data = pl; T.next++;
        }
    }
    if (!T.n) goto done;
    doc_free(&D); D = T; D.cur = D.n - 1; undo_clear(); comp_dirty = 1; free(b); return 0;
done:
    doc_free(&T); free(b); return rc;
}
/* ================= xuất PNG (không nén, tự viết) ================= */
static u32 crc_tab[256];
static u32 crc32u(u32 c, const u8 *p, size_t n) {
    if (!crc_tab[1]) for (u32 i = 0; i < 256; i++) { u32 x = i; for (int k = 0; k < 8; k++) x = x & 1 ? 0xedb88320u ^ (x >> 1) : x >> 1; crc_tab[i] = x; }
    c = ~c; while (n--) c = crc_tab[(c ^ *p++) & 255] ^ (c >> 8); return ~c;
}
static void be32(Buf *b, u32 v) { u8 t[4] = {(u8)(v >> 24), (u8)(v >> 16), (u8)(v >> 8), (u8)v}; bput(b, t, 4); }
static void png_chunk(Buf *o, const char *t, const u8 *d, size_t n) { be32(o, (u32)n); size_t at = o->n; bput(o, t, 4); if (n) bput(o, d, n); be32(o, crc32u(0, o->p + at, n + 4)); }
static int export_png(const char *path) {
    u32 *fl = malloc((size_t)D.w * D.h * 4); if (!fl) return 0; flatten(fl, 0);
    size_t rl = (size_t)D.w * 4 + 1, rawn = rl * D.h; u8 *raw = malloc(rawn);
    for (int y = 0; y < D.h; y++) { u8 *r = raw + rl * y; *r++ = 0; for (int x = 0; x < D.w; x++) { u32 c = fl[(size_t)y * D.w + x]; *r++ = c >> 16; *r++ = c >> 8; *r++ = c; *r++ = c >> 24; } }
    free(fl);
    Buf z = {0}; u8 zh[2] = {0x78, 0x01}; bput(&z, zh, 2); u32 a = 1, bb = 0;
    for (size_t i = 0; i < rawn; i++) { a = (a + raw[i]) % 65521; bb = (bb + a) % 65521; }
    for (size_t i = 0; i < rawn || !i;) { size_t n = rawn - i > 65535 ? 65535 : rawn - i; u8 h[5] = {(u8)(i + n >= rawn), (u8)n, (u8)(n >> 8), (u8)~n, (u8)(~n >> 8)}; bput(&z, h, 5); bput(&z, raw + i, n); i += n; if (!n) break; }
    be32(&z, (bb << 16) | a); free(raw);
    Buf o = {0}; static const u8 sig[8] = {137, 80, 78, 71, 13, 10, 26, 10}; bput(&o, sig, 8);
    Buf ih = {0}; be32(&ih, D.w); be32(&ih, D.h); u8 t[5] = {8, 6, 0, 0, 0}; bput(&ih, t, 5);
    png_chunk(&o, "IHDR", ih.p, ih.n); png_chunk(&o, "IDAT", z.p, z.n); png_chunk(&o, "IEND", NULL, 0);
    FILE *f = fopen(path, "wb"); int ok = f && fwrite(o.p, 1, o.n, f) == o.n; if (f) fclose(f); free(ih.p); free(z.p); free(o.p); return ok;
}

/* ================= giao diện (vẽ phần mềm vào fb) ================= */
static u32 *fb; static int W = 1280, H = 720, S = 2;
static float zoom = 1, offx, offy; static int mx, my, panning, pan_ox, pan_oy, pan_mx, pan_my;
static char cur_path[512] = "untitled.gkproj", key4[8]; static char msg[96]; static int msg_ttl;
static void set_msg(const char *f, ...) { va_list a; va_start(a, f); vsnprintf(msg, sizeof msg, f, a); va_end(a); msg_ttl = 240; }
static void fb_resize(int w, int h) { free(fb); W = w; H = h; fb = calloc((size_t)w * h, 4); S = h / 360; if (S < 1) S = 1; if (S > 4) S = 4; }
static void fbrect(int x, int y, int w, int h, u32 c) {
    if (x < 0) { w += x; x = 0; } if (y < 0) { h += y; y = 0; } if (x + w > W) w = W - x; if (y + h > H) h = H - y;
    for (int j = 0; j < h; j++) { u32 *p = fb + (size_t)(y + j) * W + x; for (int i = 0; i < w; i++) p[i] = c; }
}
static const char GL[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ+-.:/ ";
static const u8 FNT[][5] = {
{0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},{0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},{0x18,0x14,0x12,0x7F,0x10},
{0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},{0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},
{0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},{0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},
{0x7F,0x09,0x09,0x09,0x01},{0x3E,0x41,0x49,0x49,0x7A},{0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},
{0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},{0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
{0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7F,0x01,0x01},
{0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},{0x63,0x14,0x08,0x14,0x63},{0x07,0x08,0x70,0x08,0x07},
{0x61,0x51,0x49,0x45,0x43},{0x08,0x08,0x3E,0x08,0x08},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},{0x00,0x36,0x36,0x00,0x00},
{0x20,0x10,0x08,0x04,0x02},{0,0,0,0,0}};
static int text_w(const char *s, int sc) { return (int)strlen(s) * 6 * sc; }
static void fbtext(int x, int y, const char *s, int sc, u32 c) {
    for (; *s; s++, x += 6 * sc) {
        char ch = *s; if (ch >= 'a' && ch <= 'z') ch -= 32; const char *g = strchr(GL, ch); if (!g || !ch) g = GL + 41;
        const u8 *f = FNT[g - GL];
        for (int i = 0; i < 5; i++) for (int j = 0; j < 7; j++) if (f[i] >> j & 1) fbrect(x + i * sc, y + j * sc, sc, sc, c);
    }
}
enum { B_NEW = 1, B_OPEN, B_SAVE, B_PNG, B_UNDO, B_REDO, B_ZOUT, B_ZIN, B_FIT, B_ADDL, B_DELL, B_SZM, B_SZP, B_TOOL = 20, B_PAL = 100, B_SEL = 200, B_EYE = 300 };
typedef struct { int x, y, w, h, id; } Btn; static Btn btns[96]; static int nb;
static const u32 PAL[16] = {0xff202020,0xff808080,0xffc0c0c0,0xffffffff,0xffe53935,0xfff4511e,0xfffdd835,0xff7cb342,
                            0xff00897b,0xff039be5,0xff3949ab,0xff8e24aa,0xffd81b60,0xff6d4c41,0xff3e2723,0xfff8bbd0};
static void ui_btn(int id, int x, int y, int w, int h, const char *label, int active) {
    if (nb < 96) btns[nb++] = (Btn){x, y, w, h, id};
    int hov = mx >= x && mx < x + w && my >= y && my < y + h;
    fbrect(x, y, w, h, active ? 0xff3d6fd8 : hov ? 0xff4a4f5a : 0xff333842);
    if (label) fbtext(x + (w - text_w(label, S) + S) / 2, y + (h - 7 * S) / 2, label, S, 0xffeef0f4);
}
#define TOP (22 * S)
#define LB (30 * S)
#define RB (96 * S)
#define ST (12 * S)
static void fit(void) {
    int cw = W - LB - RB, ch = H - TOP - ST; float z = fminf((float)cw / D.w, (float)ch / D.h) * 0.92f; if (z < 0.05f) z = 0.05f;
    zoom = z; offx = LB + (cw - D.w * z) / 2; offy = TOP + (ch - D.h * z) / 2;
}
static void ui_draw(void) {
    nb = 0; if (comp_dirty || !comp) rebuild_comp();
    fbrect(0, 0, W, H, 0xff1b1e24);
    /* canvas */
    int x0 = LB, y0 = TOP, x1 = W - RB, y1 = H - ST;
    for (int sy = y0; sy < y1; sy++) {
        int dy = (int)floorf((sy - offy) / zoom); u32 *row = fb + (size_t)sy * W;
        for (int sx = x0; sx < x1; sx++) {
            int dx = (int)floorf((sx - offx) / zoom);
            row[sx] = (dx >= 0 && dy >= 0 && dx < D.w && dy < D.h && comp) ? comp[(size_t)dy * D.w + dx] : 0xff262a31;
        }
    }
    /* thanh trên */
    fbrect(0, 0, W, TOP, 0xff12141a); int x = 4 * S, y = 3 * S, h = 16 * S;
    static const struct { int id; const char *l; } top[] = {{B_NEW,"NEW"},{B_OPEN,"OPEN"},{B_SAVE,"SAVE"},{B_PNG,"PNG"},{0,0},{B_UNDO,"UNDO"},{B_REDO,"REDO"},{0,0},{B_ZOUT,"-"},{B_ZIN,"+"},{B_FIT,"FIT"}};
    for (unsigned i = 0; i < sizeof top / sizeof *top; i++) {
        if (!top[i].id) { x += 6 * S; continue; }
        int w = text_w(top[i].l, S) + 10 * S; ui_btn(top[i].id, x, y, w, h, top[i].l, 0); x += w + 3 * S;
    }
    fbtext(W - text_w("GK KEYFL EDITOR", S) - 6 * S, 8 * S, "GK KEYFL EDITOR", S, 0xff7d8596);
    /* thanh trái: công cụ */
    fbrect(0, TOP, LB, H - TOP, 0xff15181e);
    static const char *tn[T_N] = {"PEN", "ERS", "FIL", "LIN", "RCT", "PIK"};
    for (int i = 0; i < T_N; i++) ui_btn(B_TOOL + i, 3 * S, TOP + 4 * S + i * 22 * S, 24 * S, 18 * S, tn[i], tool == i);
    /* bảng phải */
    int rx = W - RB; fbrect(rx, TOP, RB, H - TOP, 0xff15181e); char b[48];
    snprintf(b, sizeof b, "SIZE %d", bsize); fbtext(rx + 6 * S, TOP + 5 * S, b, S, 0xffc9cfdb);
    ui_btn(B_SZM, rx + 52 * S, TOP + 2 * S, 16 * S, 14 * S, "-", 0); ui_btn(B_SZP, rx + 72 * S, TOP + 2 * S, 16 * S, 14 * S, "+", 0);
    fbrect(rx + 6 * S, TOP + 20 * S, 84 * S, 14 * S, color | 0xff000000u);
    for (int i = 0; i < 16; i++) { int px = rx + 6 * S + (i % 4) * 21 * S, py = TOP + 38 * S + (i / 4) * 21 * S; ui_btn(B_PAL + i, px, py, 19 * S, 19 * S, NULL, 0); fbrect(px + S, py + S, 17 * S, 17 * S, PAL[i]); }
    int ly = TOP + 126 * S; fbtext(rx + 6 * S, ly + 4 * S, "LAYERS", S, 0xffc9cfdb);
    ui_btn(B_ADDL, rx + 52 * S, ly, 16 * S, 14 * S, "+", 0); ui_btn(B_DELL, rx + 72 * S, ly, 16 * S, 14 * S, "-", 0);
    for (int i = D.n - 1, r = 0; i >= 0; i--, r++) {
        int yy = ly + 18 * S + r * 15 * S; if (yy + 14 * S > H - ST) break;
        ui_btn(B_SEL + i, rx + 4 * S, yy, 70 * S, 14 * S, NULL, D.cur == i);
        char nm[16]; snprintf(nm, sizeof nm, "%.10s", D.l[i].name); fbtext(rx + 7 * S, yy + 4 * S, nm, S, 0xffeef0f4);
        ui_btn(B_EYE + i, rx + 76 * S, yy, 14 * S, 14 * S, D.l[i].vis ? "O" : "-", 0);
    }
    /* trạng thái */
    fbrect(0, H - ST, W, ST, 0xff12141a);
    int dx = (int)floorf((mx - offx) / zoom), dy = (int)floorf((my - offy) / zoom);
    snprintf(b, sizeof b, "%d/%d  Z%d  %dX%d", dx, dy, (int)(zoom * 100), D.w, D.h); fbtext(W - text_w(b, S) - 6 * S, H - ST + 3 * S, b, S, 0xff9aa3b5);
    if (msg_ttl > 0) { fbtext(6 * S, H - ST + 3 * S, msg, S, 0xffffd54f); msg_ttl--; }
}

/* ================= xử lý nhập ================= */
static int drawing, lx, ly2, sx0, sy0;
static void to_doc(int sx, int sy, int *dx, int *dy) { *dx = (int)floorf((sx - offx) / zoom); *dy = (int)floorf((sy - offy) / zoom); }
static void png_path(char *o, size_t n) { snprintf(o, n, "%s", cur_path); char *d = strrchr(o, '.'); if (d && !strchr(d, '/')) *d = 0; strncat(o, ".png", n - strlen(o) - 1); }
static void act(int id) {
    char p[560];
    if (id == B_NEW) { doc_new(800, 600); fit(); set_msg("NEW 800X600"); }
    else if (id == B_OPEN) { int r = doc_load(cur_path, key4); if (!r) { fit(); set_msg("OPENED"); } else set_msg(r == 3 ? "NEED 4KEY (GK_4KEY)" : r == 1 ? "CANNOT READ FILE" : "BAD FILE"); }
    else if (id == B_SAVE) set_msg(doc_save(cur_path, key4) ? "SAVED" : "SAVE FAILED");
    else if (id == B_PNG) { png_path(p, sizeof p); set_msg(export_png(p) ? "PNG EXPORTED" : "EXPORT FAILED"); }
    else if (id == B_UNDO) { if (!do_undo(0)) set_msg("NOTHING TO UNDO"); }
    else if (id == B_REDO) { if (!do_undo(1)) set_msg("NOTHING TO REDO"); }
    else if (id == B_ZIN || id == B_ZOUT) { float f = id == B_ZIN ? 1.25f : 0.8f, cx = (LB + W - RB) / 2.f, cy = (TOP + H - ST) / 2.f; float dx = (cx - offx) / zoom, dy = (cy - offy) / zoom; zoom *= f; if (zoom < 0.05f) zoom = 0.05f; if (zoom > 64) zoom = 64; offx = cx - dx * zoom; offy = cy - dy * zoom; }
    else if (id == B_FIT) fit();
    else if (id == B_ADDL) { undo_clear(); char nm[32]; snprintf(nm, sizeof nm, "LAYER %d", D.n); if (!layer_add(&D, nm, 0)) set_msg("MAX LAYERS"); comp_dirty = 1; }
    else if (id == B_DELL) layer_del();
    else if (id == B_SZM) { if (bsize > 1) bsize = bsize > 8 ? bsize - 4 : bsize - 1; }
    else if (id == B_SZP) { if (bsize < 64) bsize = bsize >= 8 ? bsize + 4 : bsize + 1; }
    else if (id >= B_TOOL && id < B_TOOL + T_N) tool = id - B_TOOL;
    else if (id >= B_PAL && id < B_PAL + 16) color = PAL[id - B_PAL];
    else if (id >= B_SEL && id < B_SEL + MAXL) { if (id - B_SEL < D.n) D.cur = id - B_SEL; }
    else if (id >= B_EYE && id < B_EYE + MAXL) { if (id - B_EYE < D.n) { D.l[id - B_EYE].vis ^= 1; comp_dirty = 1; } }
}
static void shape(int x1, int y1) {
    memcpy(D.l[D.cur].px, us[un - 1].px, (size_t)D.w * D.h * 4);
    if (tool == T_LINE) draw_line(sx0, sy0, x1, y1, bsize, color, 0); else draw_rect(sx0, sy0, x1, y1, bsize, color);
}
static void on_press(int x, int y, int b) {
    mx = x; my = y;
    if (b != 1) { panning = 1; pan_mx = x; pan_my = y; pan_ox = (int)offx; pan_oy = (int)offy; return; }
    for (int i = 0; i < nb; i++) if (x >= btns[i].x && x < btns[i].x + btns[i].w && y >= btns[i].y && y < btns[i].y + btns[i].h) { act(btns[i].id); return; }
    if (x < LB || x >= W - RB || y < TOP || y >= H - ST) return;
    int dx, dy; to_doc(x, y, &dx, &dy);
    if (tool == T_PICK) { if (comp && dx >= 0 && dy >= 0 && dx < D.w && dy < D.h) color = comp[(size_t)dy * D.w + dx] | 0xff000000u; return; }
    if (!D.l[D.cur].vis) { set_msg("LAYER IS HIDDEN"); return; }
    push_undo(); drawing = 1; lx = sx0 = dx; ly2 = sy0 = dy;
    if (tool == T_PEN || tool == T_ERASE) stamp(dx, dy, bsize, color, tool == T_ERASE);
    else if (tool == T_FILL) { flood(dx, dy, color); drawing = 0; }
}
static void on_motion(int x, int y) {
    mx = x; my = y;
    if (panning) { offx = pan_ox + (x - pan_mx); offy = pan_oy + (y - pan_my); return; }
    if (!drawing) return; int dx, dy; to_doc(x, y, &dx, &dy);
    if (tool == T_PEN || tool == T_ERASE) { draw_line(lx, ly2, dx, dy, bsize, color, tool == T_ERASE); lx = dx; ly2 = dy; }
    else if (tool == T_LINE || tool == T_RECT) { shape(dx, dy); }
}
static void on_release(int x, int y) { (void)x; (void)y; drawing = 0; panning = 0; }
static void on_wheel(int x, int y, float d) {
    float dx = (x - offx) / zoom, dy = (y - offy) / zoom; zoom *= d > 0 ? 1.15f : 1 / 1.15f; if (zoom < 0.05f) zoom = 0.05f; if (zoom > 64) zoom = 64;
    offx = x - dx * zoom; offy = y - dy * zoom;
}
static void on_key(int ctrl, int shift, int c) {   /* c = ký tự thường ASCII */
    if (ctrl) { if (c == 'z') act(shift ? B_REDO : B_UNDO); else if (c == 'y') act(B_REDO); else if (c == 's') act(B_SAVE); else if (c == 'o') act(B_OPEN); else if (c == 'n') act(B_NEW); else if (c == 'e') act(B_PNG); return; }
    switch (c) { case 'p': tool = T_PEN; break; case 'e': tool = T_ERASE; break; case 'f': tool = T_FILL; break; case 'l': tool = T_LINE; break;
        case 'r': tool = T_RECT; break; case 'i': tool = T_PICK; break; case '[': act(B_SZM); break; case ']': act(B_SZP); break; case '0': fit(); break; }
}

/* ================= SDL3 ================= */
#ifdef GK_SDL3
static int run_sdl(void) {
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "1");
    if (!SDL_Init(SDL_INIT_VIDEO)) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    SDL_Window *win = SDL_CreateWindow("GK KeyFL Editor", 1280, 720, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    SDL_Renderer *ren = win ? SDL_CreateRenderer(win, NULL) : NULL; if (!ren) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    SDL_SetRenderVSync(ren, 1); SDL_Texture *tex = NULL; int tw = 0, th = 0, run = 1; float sx = 1, sy = 1, fx = 1;
    fb_resize(1280, 720); fit();
    while (run) {
        int pw = 0, ph = 0, ww = 0, wh = 0; SDL_GetWindowSizeInPixels(win, &pw, &ph); SDL_GetWindowSize(win, &ww, &wh);
        if (pw > 0 && ph > 0 && (pw != W || ph != H)) fb_resize(pw, ph);
        sx = ww > 0 ? (float)pw / ww : 1; sy = wh > 0 ? (float)ph / wh : 1; (void)fx;
        SDL_Event e;
        while (SDL_PollEvent(&e)) switch (e.type) {
            case SDL_EVENT_QUIT: case SDL_EVENT_WINDOW_CLOSE_REQUESTED: run = 0; break;
            case SDL_EVENT_MOUSE_MOTION: on_motion((int)(e.motion.x * sx), (int)(e.motion.y * sy)); break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN: on_press((int)(e.button.x * sx), (int)(e.button.y * sy), e.button.button == SDL_BUTTON_LEFT ? 1 : 2); break;
            case SDL_EVENT_MOUSE_BUTTON_UP: on_release((int)(e.button.x * sx), (int)(e.button.y * sy)); break;
            case SDL_EVENT_MOUSE_WHEEL: on_wheel(mx, my, e.wheel.y); break;
            case SDL_EVENT_KEY_DOWN: { SDL_Keycode k = e.key.key; if (k > 31 && k < 127) on_key((e.key.mod & SDL_KMOD_CTRL) != 0, (e.key.mod & SDL_KMOD_SHIFT) != 0, (int)k); break; }
            default: break;
        }
        ui_draw();
        if (!tex || tw != W || th != H) { if (tex) SDL_DestroyTexture(tex); tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, W, H); tw = W; th = H; }
        if (tex) { SDL_UpdateTexture(tex, NULL, fb, W * 4); SDL_RenderClear(ren); SDL_RenderTexture(ren, tex, NULL, NULL); SDL_RenderPresent(ren); }
    }
    if (tex) SDL_DestroyTexture(tex); SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); SDL_Quit(); return 0;
}
#endif

/* ================= chế độ không màn hình ================= */
static u32 doc_hash(void) { u32 *t = malloc((size_t)D.w * D.h * 4); flatten(t, 0); u32 h = 2166136261u; for (size_t i = 0; i < (size_t)D.w * D.h; i++) { h ^= t[i]; h *= 16777619u; } free(t); return h; }
static void demo_draw(void) {
    on_press(LB + 40 * S, TOP + 40 * S, 1); on_motion(LB + 160 * S, TOP + 90 * S); on_motion(LB + 220 * S, TOP + 50 * S); on_release(0, 0);
    tool = T_RECT; color = PAL[9]; on_press(LB + 60 * S, TOP + 100 * S, 1); on_motion(LB + 140 * S, TOP + 150 * S); on_release(0, 0);
    tool = T_FILL; color = PAL[6]; on_press(LB + 100 * S, TOP + 120 * S, 1); on_release(0, 0); tool = T_PEN; color = PAL[4];
}
static int selftest(void) {
    int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
    doc_new(64, 48); D.cur = 1; push_undo(); draw_line(2, 2, 60, 40, 3, 0xffff0000u, 0); push_undo(); draw_rect(10, 10, 50, 30, 2, 0xff0000ffu); push_undo(); flood(20, 20, 0xff00ff00u);
    u32 h1 = doc_hash(); CHECK(do_undo(0)); u32 h2 = doc_hash(); CHECK(h1 != h2); CHECK(do_undo(1)); CHECK(doc_hash() == h1);
    CHECK(doc_save("/tmp/t1.gkproj", NULL)); CHECK(doc_load("/tmp/t1.gkproj", NULL) == 0); CHECK(doc_hash() == h1 && D.n == 2 && D.w == 64);
    D.ext = malloc(sizeof(Chunk)); memcpy(D.ext[0].type, "KEYF", 4); D.ext[0].len = 5; D.ext[0].data = malloc(5); memcpy(D.ext[0].data, "hello", 5); D.next = 1;
    CHECK(doc_save("/tmp/t2.gkproj", "ab12")); CHECK(doc_load("/tmp/t2.gkproj", NULL) == 3); CHECK(doc_load("/tmp/t2.gkproj", "zzzz") == 3);
    doc_new(8, 8); CHECK(doc_load("/tmp/t2.gkproj", "ab12") == 0); CHECK(doc_hash() == h1 && D.next == 1 && !memcmp(D.ext[0].data, "hello", 5));
    CHECK(doc_save("/tmp/t3.gkproj", NULL)); CHECK(doc_load("/tmp/t3.gkproj", NULL) == 0 && D.next == 1);
    CHECK(doc_load("/tmp/nonexistent.gkproj", NULL) == 1); CHECK(export_png("/tmp/t1.png"));
    FILE *f = fopen("/tmp/bad.gkproj", "wb"); fputs("GKPJ garbage garbage garbage", f); fclose(f); CHECK(doc_load("/tmp/bad.gkproj", NULL) == 2);
    printf(fails ? "SELFTEST FAILED\n" : "SELFTEST OK\n"); return fails != 0;
}
int main(int argc, char **argv) {
    const char *e = getenv("GK_4KEY"); if (e) snprintf(key4, sizeof key4, "%.4s", e);
    const char *snap = NULL; int sw = 1280, sh = 720, havepath = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--selftest")) return selftest();
        else if (!strcmp(argv[i], "--version")) { puts("GK KeyFL Editor " GK_VERSION); return 0; }
        else if (!strcmp(argv[i], "--key") && i + 1 < argc) snprintf(key4, sizeof key4, "%.4s", argv[++i]);
        else if (!strcmp(argv[i], "--snap") && i + 1 < argc) { snap = argv[++i]; if (i + 2 < argc && atoi(argv[i + 1]) > 0) { sw = atoi(argv[++i]); sh = atoi(argv[++i]); } }
        else if (!strcmp(argv[i], "--embed") && i + 4 < argc) {   /* --embed in.gkproj TYPE file out.gkproj : nhúng nguyên tệp vào một chunk */
            if (strlen(argv[i + 2]) != 4) { fprintf(stderr, "TYPE phai dung 4 ky tu\n"); return 1; }
            if (doc_load(argv[i + 1], key4)) { fprintf(stderr, "khong mo duoc %s\n", argv[i + 1]); return 1; }
            FILE *f = fopen(argv[i + 3], "rb"); if (!f) { fprintf(stderr, "khong doc duoc %s\n", argv[i + 3]); return 1; }
            fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET); u8 *d = malloc(n ? n : 1); if (fread(d, 1, n, f) != (size_t)n) return 1; fclose(f);
            D.ext = realloc(D.ext, sizeof(Chunk) * (D.next + 1)); memcpy(D.ext[D.next].type, argv[i + 2], 4); D.ext[D.next].len = (u32)n; D.ext[D.next].data = d; D.next++;
            if (!doc_save(argv[i + 4], key4)) return 1; printf("da nhung %ld byte vao chunk %s\n", n, argv[i + 2]); return 0;
        } else if (argv[i][0] != '-') { snprintf(cur_path, sizeof cur_path, "%s", argv[i]); havepath = 1; }
    }
    doc_new(800, 600);
    if (havepath) { int r = doc_load(cur_path, key4); if (r == 1) set_msg("NEW FILE"); else if (r == 3) set_msg("NEED 4KEY"); else if (r) set_msg("BAD FILE"); }
    if (snap) {
        fb_resize(sw, sh); fit(); ui_draw(); demo_draw(); ui_draw();
        FILE *f = fopen(snap, "wb"); fprintf(f, "P6\n%d %d\n255\n", W, H);
        for (size_t i = 0; i < (size_t)W * H; i++) { u8 c[3] = {(u8)(fb[i] >> 16), (u8)(fb[i] >> 8), (u8)fb[i]}; fwrite(c, 1, 3, f); } fclose(f); return 0;
    }
#ifdef GK_SDL3
    return run_sdl();
#else
    fprintf(stderr, "Ban build nay khong co cua so. Build voi SDL3: make (xem README).\n"); return 1;
#endif
}
