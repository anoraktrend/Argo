#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#ifdef __SSE2__
#include <emmintrin.h>
#endif

#if defined(_WIN32) && !defined(__MINGW32__) && !defined(_POSIX_C_SOURCE)
static char *strdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}
#endif

#define MINM 3
#define MAXM 131074
#define MXF  64
#define WIN2 8192
#define WIN1 16384
#define HB 15
#define HS (1<<HB)
#define HC 64

typedef struct { unsigned char *d; size_t sz, cp; } Bf;

static void bp(Bf *b, unsigned char c) {
    if (b->sz >= b->cp) {
        size_t ncp = b->cp ? b->cp * 2 : 65536;
        if (ncp < b->cp) { fprintf(stderr, "OOM\n"); exit(1); }
        b->d = realloc(b->d, ncp);
        if (!b->d) { fprintf(stderr, "OOM\n"); exit(1); }
        b->cp = ncp;
    }
    b->d[b->sz++] = c;
}

static size_t vle(unsigned char *b, size_t v) {
    size_t n = 0;
    do { b[n++] = (unsigned char)(v & 0x7F) | (v >= 0x80 ? 0x80 : 0); v >>= 7; } while (v);
    return n;
}

static size_t vld(const unsigned char **p) {
    size_t v = 0, s = 0; unsigned char c;
    do { c = *(*p)++; v |= (size_t)(c & 0x7F) << s; s += 7; } while (c & 0x80);
    return v;
}

typedef struct { unsigned char *d; size_t cap; int pos; } Bbuf;

static void bb_init(Bbuf *b) { memset(b, 0, sizeof(*b)); }
static void bb_free(Bbuf *b) { free(b->d); b->d = NULL; }
static void bb_grow(Bbuf *b, size_t need) {
    size_t ncap = b->cap ? b->cap : 4096;
    while (ncap < need) {
        if (ncap > (size_t)-1 / 2) { fprintf(stderr, "OOM\n"); exit(1); }
        ncap *= 2;
    }
    b->d = realloc(b->d, ncap);
    if (!b->d) { fprintf(stderr, "OOM\n"); exit(1); }
    if (ncap > b->cap) memset(b->d + b->cap, 0, ncap - b->cap);
    b->cap = ncap;
}
static void bb_bit(Bbuf *b, int bit) {
    if ((size_t)b->pos / 8 >= b->cap) bb_grow(b, (size_t)b->pos / 8 + 1);
    if (bit) b->d[b->pos / 8] |= (1 << (b->pos % 8));
    b->pos++;
}
static void bb_bits(Bbuf *b, unsigned v, int n) {
    for (int i = n - 1; i >= 0; i--) bb_bit(b, (v >> i) & 1);
}
static void bb_vli(Bbuf *b, size_t v) {
    do { unsigned char c = v & 0x7F; v >>= 7; if (v) c |= 0x80; bb_bits(b, c, 8); } while (v);
}

static size_t hh[HS];
static size_t hn[WIN2];

static void hr(void) { memset(hh, 0xFF, sizeof(hh)); }

static unsigned hsh(const unsigned char *d, size_t p) {
    return ((unsigned)d[p]*773u + (unsigned)d[p+1]*97u + (unsigned)d[p+2]) & (HS-1);
}

static int mtch(const unsigned char *d, size_t p, size_t n, int *l) {
    if (p + MINM > n) return -1;
    unsigned h = hsh(d, p);
    int bl = 0, bo = 0;
    size_t lim = p > WIN2 ? p - WIN2 : 0;
    int cnt = 0;
    size_t i = hh[h];
    while (i >= lim && i < p && cnt < HC) {
        if (d[i] == d[p] && d[i+1] == d[p+1] && d[i+2] == d[p+2]) {
            int m = MINM;
#if defined(__SSE2__) && defined(__GNUC__)
            while (m + 16 <= MAXM && p + m + 16 <= n && i + m + 16 <= n) {
                int eq = _mm_movemask_epi8(_mm_cmpeq_epi8(
                    _mm_loadu_si128((const __m128i*)(d + i + m)),
                    _mm_loadu_si128((const __m128i*)(d + p + m))));
                if (eq != 0xFFFF) { m += __builtin_ctz(~eq & 0xFFFFu); break; }
                m += 16;
            }
#endif
            while (p + m < n && d[i+m] == d[p+m] && m < MAXM) m++;
            if (m > bl) { bl = m; bo = (int)(p - i); if (m == MAXM) break; }
        }
        i = hn[i & (WIN2-1)];
        cnt++;
    }
    hn[p & (WIN2-1)] = hh[h];
    hh[h] = p;
    if (bl >= MINM) { *l = bl; return bo; }
    return -1;
}

static int xor86(const unsigned char *d, size_t n) {
    if (n < 8) return 0;
    int c = 0;
    for (size_t i = 0; i + 4 < n; i++)
        if ((d[i] == 0xE8 || d[i] == 0xE9) && (d[i+4] == 0x00 || d[i+4] == 0xFF))
            if (++c > 4) return 1;
    return 0;
}

static unsigned char *e8e9(const unsigned char *d, size_t n) {
    if (!xor86(d, n)) return NULL;
    unsigned char *p = malloc(n);
    if (!p) return NULL;
    memcpy(p, d, n);
    for (size_t i = 0; i + 4 < n; i++)
        if ((p[i] == 0xE8 || p[i] == 0xE9) && (p[i+4] == 0x00 || p[i+4] == 0xFF)) {
            unsigned a = (unsigned)p[i+1] | (unsigned)p[i+2]<<8 | (unsigned)p[i+3]<<16;
            a += (unsigned)i;
            p[i+1] = (unsigned char)a;
            p[i+2] = (unsigned char)(a>>8);
            p[i+3] = (unsigned char)(a>>16);
            i += 4;
        }
    return p;
}

static int cmpbl(const unsigned char *d, size_t n) {
    if (n < 16) return 1;
    int l = 0;
    for (size_t i = 0; i + MINM < n && i < 4096; i++) {
        int ml; int mo = mtch(d, i, n, &ml);
        if (mo > 0) { l += ml; hr(); }
    }
    return (size_t)l >= n / 8;
}

static size_t rle_size(const unsigned char *d, size_t n) {
    size_t s = 0;
    for (size_t i = 0; i < n;) {
        unsigned char b = d[i]; size_t r = 1;
        while (i + r < n && d[i + r] == b && r < 65536) r++;
        s += 1; size_t t = r;
        do { s++; t >>= 7; } while (t);
        i += r;
    }
    return s;
}

static void rle_enc(const unsigned char *d, size_t n, Bf *out) {
    bp(out, 3);
    unsigned char v[16]; size_t vn = vle(v, n);
    for (size_t i = 0; i < vn; i++) bp(out, v[i]);
    for (size_t i = 0; i < n;) {
        unsigned char b = d[i]; size_t r = 1;
        while (i + r < n && d[i + r] == b && r < 65536) r++;
        bp(out, b); vn = vle(v, r);
        for (size_t j = 0; j < vn; j++) bp(out, v[j]);
        i += r;
    }
}

static void huf_build(int *freq, int nsym, unsigned char *len) {
    int nz[256], nn = 0;
    for (int i = 0; i < nsym; i++) if (freq[i] > 0) nz[nn++] = i;
    if (nn <= 1) { if (nn == 1) len[nz[0]] = 1; return; }
    int val[256], id[256];
    for (int i = 0; i < nn; i++) { val[i] = freq[nz[i]]; id[i] = i; }
    int par[512], ni = nn;
    for (int i = 0; i < nn; i++) par[i] = -1;
    int na = nn;
    while (na > 1) {
        int a = 0, b = 1;
        if (val[b] < val[a]) { int t = a; a = b; b = t; }
        for (int i = 2; i < na; i++) {
            if (val[i] < val[a]) { b = a; a = i; }
            else if (val[i] < val[b]) b = i;
        }
        int p = ni++;
        par[id[a]] = p; par[id[b]] = p; par[p] = -1;
        val[a] += val[b]; id[a] = p;
        val[b] = val[--na]; id[b] = id[na];
    }
    for (int i = 0; i < nn; i++) {
        int d = 0, n = i;
        while (par[n] >= 0) { d++; n = par[n]; }
        len[nz[i]] = (unsigned char)(d ? d : 1);
    }
}

static void huf_canon(const unsigned char *len, int nsym, unsigned short *code) {
    int cnt[16] = {0};
    int ml = 0;
    for (int i = 0; i < nsym; i++) { int l = len[i]; if (l > 0) { cnt[l]++; if (l > ml) ml = l; } }
    unsigned short fc[17] = {0};
    for (int l = 1; l <= ml; l++) fc[l] = (fc[l-1] + cnt[l-1]) << 1;
    for (int i = 0; i < nsym; i++) if (len[i] > 0) { code[i] = fc[len[i]]++; }
}

static void huf_store(Bf *out, const unsigned char *len, int nsym) {
    int cnt[16] = {0}, ml = 0;
    for (int i = 0; i < nsym; i++) if (len[i] > 0) { int l = len[i]; if (l > ml) ml = l; cnt[l-1]++; }
    bp(out, (unsigned char)ml);
    for (int i = 0; i < ml; i++) bp(out, (unsigned char)cnt[i]);
    for (int l = 1; l <= ml; l++) for (int i = 0; i < nsym; i++) if (len[i] == l) bp(out, (unsigned char)i);
}

static void lz77_std(const unsigned char *src, size_t n, Bf *out) {
    hr();
    size_t pos = 0, fp = 0, rp0 = 0, rp1 = 0, rp2 = 0;
    unsigned char f = 0; int bit = 0;
    bp(out, 0); fp = out->sz - 1;
    while (pos < n) {
        int ml, mo = mtch(src, pos, n, &ml);
        if (mo > 0) {
            int rp = -1;
            if ((size_t)mo == rp0) rp = 0;
            else if ((size_t)mo == rp1) rp = 1;
            else if ((size_t)mo == rp2) rp = 2;
            unsigned char lb[16]; size_t ln;
            if (rp >= 0) {
                f |= (unsigned char)(3 << (bit*2));
                unsigned lv = (unsigned)(ml - MINM);
                if (lv <= 63) { bp(out, (unsigned char)((rp<<6)|lv)); }
                else { bp(out, (unsigned char)((rp<<6)|63));
                    ln = vle(lb, lv - 63); for (size_t i = 0; i < ln; i++) bp(out, lb[i]); }
                if (rp == 1) { size_t t = rp0; rp0 = rp1; rp1 = t; }
                else if (rp == 2) { size_t t = rp0; rp0 = rp2; rp2 = rp1; rp1 = t; }
            } else {
                unsigned tok = (size_t)mo < 256 ? 1 : 2;
                f |= (unsigned char)(tok << (bit*2));
                unsigned lv = (unsigned)(ml - MINM);
                if (lv <= 254) { bp(out, (unsigned char)lv); }
                else { bp(out, 255); ln = vle(lb, lv - 255); for (size_t i = 0; i < ln; i++) bp(out, lb[i]); }
                if ((size_t)mo < 256) bp(out, (unsigned char)mo);
                else { bp(out, (unsigned char)(mo >> 8)); bp(out, (unsigned char)mo); }
                rp2 = rp1; rp1 = rp0; rp0 = (size_t)mo;
            }
            pos += (size_t)ml;
        } else {
            bp(out, src[pos]); pos++;
        }
        if (++bit == 4) {
            out->d[fp] = f; f = 0; bit = 0;
            bp(out, 0); fp = out->sz - 1;
        }
    }
    if (bit) { out->d[fp] = f; } else out->sz--;
}

static void lz77_huf(const unsigned char *src, size_t n, Bf *out) {
    int tok_freq[3] = {0};
    int lit_freq[256] = {0};
    hr();
    {
        size_t pos = 0; size_t rp0 = 0, rp1 = 0, rp2 = 0;
        while (pos < n) {
            int ml, mo = mtch(src, pos, n, &ml);
            if (mo > 0) {
                int rp = -1;
                if ((size_t)mo == rp0) rp = 0;
                else if ((size_t)mo == rp1) rp = 1;
                else if ((size_t)mo == rp2) rp = 2;
                if (rp >= 0) { tok_freq[2]++;
                    if (rp == 1) { size_t t = rp0; rp0 = rp1; rp1 = t; }
                    else if (rp == 2) { size_t t = rp0; rp0 = rp2; rp2 = rp1; rp1 = t; }
                } else { tok_freq[1]++; rp2 = rp1; rp1 = rp0; rp0 = (size_t)mo; }
                pos += (size_t)ml;
            } else { tok_freq[0]++; lit_freq[src[pos]]++; pos++; }
        }
    }

    unsigned char tok_len[3] = {0}; unsigned short tok_code[3] = {0};
    unsigned char lit_len[256] = {0}; unsigned short lit_code[256] = {0};
    huf_build(tok_freq, 3, tok_len);
    huf_canon(tok_len, 3, tok_code);
    huf_build(lit_freq, 256, lit_len);
    huf_canon(lit_len, 256, lit_code);

    unsigned char vlb[16]; size_t vn;
    vn = vle(vlb, n); for (size_t vi = 0; vi < vn; vi++) bp(out, vlb[vi]);
    huf_store(out, tok_len, 3);
    huf_store(out, lit_len, 256);

    Bbuf bb; bb_init(&bb);
    hr();
    {
        size_t pos = 0; size_t rp0 = 0, rp1 = 0, rp2 = 0;
        while (pos < n) {
            int ml, mo = mtch(src, pos, n, &ml);
            if (mo > 0) {
                int rp = -1;
                if ((size_t)mo == rp0) rp = 0;
                else if ((size_t)mo == rp1) rp = 1;
                else if ((size_t)mo == rp2) rp = 2;
                if (rp >= 0) {
                    bb_bits(&bb, tok_code[2], tok_len[2]);
                    bb_bits(&bb, (unsigned)rp, 2);
                    unsigned lv = (unsigned)(ml - MINM);
                    if (lv < 255) { bb_bits(&bb, lv, 8); }
                    else { bb_bits(&bb, 255, 8); bb_vli(&bb, lv - 255); }
                    if (rp == 1) { size_t t = rp0; rp0 = rp1; rp1 = t; }
                    else if (rp == 2) { size_t t = rp0; rp0 = rp2; rp2 = rp1; rp1 = t; }
                } else {
                    bb_bits(&bb, tok_code[1], tok_len[1]);
                    unsigned lv = (unsigned)(ml - MINM);
                    if (lv < 255) { bb_bits(&bb, lv, 8); }
                    else { bb_bits(&bb, 255, 8); bb_vli(&bb, lv - 255); }
                    if ((size_t)mo < 128) { bb_bits(&bb, (unsigned)mo, 8); }
                    else { bb_bits(&bb, ((unsigned)mo | 0x8000u), 16); }
                    rp2 = rp1; rp1 = rp0; rp0 = (size_t)mo;
                }
                pos += (size_t)ml;
            } else {
                bb_bits(&bb, tok_code[0], tok_len[0]);
                unsigned char b = src[pos];
                bb_bits(&bb, lit_code[b], lit_len[b]);
                pos++;
            }
        }
    }
    size_t nb = ((size_t)bb.pos + 7) / 8;
#ifdef SELFTEST
    fprintf(stderr,"SELFTEST n=%zu nb=%zu bp=%zu vn=",n,nb,bb.pos);
    {
        /* The generated decoder reads from `i` which starts at the flag byte.
           out->d[0] is the flag byte (set by cmp(), not by us).
           out->d[1..] has the VLE size, then tables, then bitstream (bb.d).
           But wait: we write bb.d to out BELOW this point (at `for(i=0;i<nb;i++) bp(out,bb.d[i])`).
           So out->d doesn't have the bitstream yet! 
           We need to read the bitstream from bb.d, and the tables from out->d.
           The decoder reads everything from one buffer because the bitstream is
           appended after the tables during flush. We're reading before the flush,
           so we need to read tables from out->d and bitstream from bb.d. */
        const unsigned char *rp = out->d;
        /* Skip flag byte (out->d[0]) */
        rp++;
        size_t us = vld(&rp);
        fprintf(stderr,"%zu us=%zu\\n",us,us);
        
        int tc[16] = {0};
        int tml = *rp++;
        for (int j = 0; j < tml; j++) tc[j] = *rp++;
        unsigned char ts[4]; int tt = 0;
        for (int j = 0; j < tml; j++) tt += tc[j];
        for (int j = 0; j < tt; j++) ts[j] = *rp++;
        while (tml > 0 && !tc[tml-1]) tml--;
        
        int lc[16] = {0};
        int lml = *rp++;
        for (int j = 0; j < lml; j++) lc[j] = *rp++;
        unsigned char ls[256]; int lt = 0;
        for (int j = 0; j < lml; j++) lt += lc[j];
        for (int j = 0; j < lt; j++) ls[j] = *rp++;
        while (lml > 0 && !lc[lml-1]) lml--;
        
        int tbstart = (int)(rp - out->d);
        fprintf(stderr,"tbstart=%zu tml=%d lml=%d tt=%d lt=%d\\n",(size_t)tbstart,tml,lml,tt,lt);
        
        unsigned char *de = malloc(n);
        size_t bp = 0, q = 0; unsigned c;
        int r0 = 0, r1 = 0, r2 = 0, iter = 0;
        while (q < n && iter < 100000) {
            iter++; c = 0; int fi = 0, fc = 0, t;
            for (int l = 0; l < tml; l++) {
                c = (c << 1) | ((bb.d[bp/8] >> (bp%8)) & 1); bp++;
                if (tc[l] > 0 && (int)(c - fc) < tc[l]) { t = ts[fi + (c - fc)]; goto tdone; }
                fi += tc[l]; fc = (fc + tc[l]) << 1;
            } t = 0; tdone:
            if (t == 0) {
                c = 0; fi = 0; fc = 0;
                for (int l = 0; l < lml; l++) {
                    c = (c << 1) | ((bb.d[bp/8] >> (bp%8)) & 1); bp++;
                    if (lc[l] > 0 && (int)(c - fc) < lc[l]) { de[q++] = ls[fi + (c - fc)]; break; }
                    fi += lc[l]; fc = (fc + lc[l]) << 1;
                }
            } else if (t == 2) {
                int ri = 0; for (int i = 0; i < 2; i++) { ri = (ri << 1) | ((bb.d[bp/8] >> (bp%8)) & 1); bp++; }
                int L = 0; for (int i = 0; i < 8; i++) { L = (L << 1) | ((bb.d[bp/8] >> (bp%8)) & 1); bp++; }
                if (L == 255) { unsigned char cb; size_t v = 0, s = 0; do { cb = 0; for (int i = 0; i < 8; i++) { cb = (cb << 1) | ((bb.d[bp/8] >> (bp%8)) & 1); bp++; } v |= (size_t)(cb & 0x7F) << s; s += 7; } while (cb & 0x80); L = (int)(v + 255); }
                L += 3; int o;
                if (ri == 0) o = r0;
                else if (ri == 1) { o = r1; r1 = r0; r0 = o; }
                else { o = r2; r2 = r1; r1 = r0; r0 = o; }
                int j; for (j=0;j<L;j++) de[q+j]=de[q+j-o]; q+=L;
            } else {
                int L = 0; for (int i = 0; i < 8; i++) { L = (L << 1) | ((bb.d[bp/8] >> (bp%8)) & 1); bp++; }
                if (L == 255) { unsigned char cb; size_t v = 0, s = 0; do { cb = 0; for (int i = 0; i < 8; i++) { cb = (cb << 1) | ((bb.d[bp/8] >> (bp%8)) & 1); bp++; } v |= (size_t)(cb & 0x7F) << s; s += 7; } while (cb & 0x80); L = (int)(v + 255); }
                L += 3;
                int o = 0; for (int i = 0; i < 8; i++) { o = (o << 1) | ((bb.d[bp/8] >> (bp%8)) & 1); bp++; }
                if (o & 128) { int o2 = 0; for (int i = 0; i < 8; i++) { o2 = (o2 << 1) | ((bb.d[bp/8] >> (bp%8)) & 1); bp++; } o = ((o & 127) << 8) | o2; }
                int j; for (j=0;j<L;j++) de[q+j]=de[q+j-o]; q+=L;
                r2=r1; r1=r0; r0=o;
            }
        }
        if (q != n || memcmp(de, src, n)) {
            size_t ep = 0; while (ep < n && ep < q && de[ep] == src[ep]) ep++;
            fprintf(stderr,"FAIL iter=%d q=%zu n=%zu ep=%zu exp=%02x got=%02x bp=%zu",iter,q,n,ep,src[ep],de[ep],bp);
            if (q > ep) fprintf(stderr," prev: exp=%02x got=%02x",src[ep-1],de[ep-1]);
            fputc('\n',stderr); exit(1);
        } else {
            fprintf(stderr,"OK q=%zu\n",q);
        }
        free(de);
    }
#endif
    for (size_t i = 0; i < nb; i++) bp(out, bb.d[i]);
    bb_free(&bb);
}

static void cmp(const unsigned char *in, size_t n, Bf *out) {
    if (!n) { bp(out, 0); return; }
    size_t sv = out->sz;
    if (n > 8 && rle_size(in, n) < n) { rle_enc(in, n, out); return; }
    int doit = cmpbl(in, n) ? 1 : 0;
    if (doit) {
        unsigned char *tx = e8e9(in, n);
        const unsigned char *src = tx ? tx : in;
        int e8 = tx ? 1 : 0;
        if (n > 4000) {
            size_t pre = out->sz;
            bp(out, (unsigned char)(e8 ? 5 : 4));
            lz77_huf(src, n, out);
            if (out->sz - pre < n) { free(tx); return; }
            out->sz = pre;
        }
        size_t pre = out->sz;
        bp(out, (unsigned char)(e8 ? 2 : 1));
        lz77_std(src, n, out);
        if (out->sz - pre < n) { free(tx); return; }
        out->sz = pre;
        free(tx);
    }
    out->sz = sv; bp(out, 0);
    for (size_t i = 0; i < n; i++) bp(out, in[i]);
}

static void esc(FILE *o, const char *s, size_t n) {
    fputc('"', o);
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c >= 32 && c < 127 && c != '"' && c != '\\') fputc((char)c, o);
        else if (c == '"') fputs("\\\"", o);
        else if (c == '\\') fputs("\\\\", o);
        else fprintf(o, "\\x%02x", c);
    }
    fputc('"', o);
}

static void b85e(FILE *o, const unsigned char *d, size_t n) {
    size_t i = 0;
    for (; i + 4 <= n; i += 4) {
        unsigned v = (unsigned)d[i]<<24 | d[i+1]<<16 | d[i+2]<<8 | d[i+3];
        unsigned t = v, c[5];
        for (int j = 0; j < 5; j++, t /= 85) c[j] = t % 85;
        for (int j = 4; j >= 0; j--) {
            unsigned x = c[j];
            fputc(x == 0 ? '!' : (char)(x <= 57 ? x + 34 : x + 35), o);
        }
    }
    if (i < n) {
        unsigned v = 0; int r = n - i;
        for (int j = 0; j < r; j++) v |= (unsigned)d[i+j] << (24 - j*8);
        unsigned t = v, c[5];
        for (int j = 0; j < 5; j++, t /= 85) c[j] = t % 85;
        for (int j = 4; j >= 0; j--) {
            unsigned x = c[j];
            fputc(x == 0 ? '!' : (char)(x <= 57 ? x + 34 : x + 35), o);
        }
    }
}

static void b85e_arr(FILE *o, const unsigned char *d, size_t n) {
    size_t i = 0;
    int first = 1;
    for (; i + 4 <= n; i += 4) {
        unsigned v = (unsigned)d[i]<<24 | d[i+1]<<16 | d[i+2]<<8 | d[i+3];
        unsigned t = v; unsigned c[5];
        for (int j = 0; j < 5; j++, t /= 85) c[j] = t % 85;
        for (int j = 4; j >= 0; j--) {
            unsigned x = c[j];
            unsigned char out = (x == 0) ? '!' : (x <= 57 ? x + 34 : x + 35);
            if (!first) fprintf(o, ",");
            fprintf(o, "%d", out);
            first = 0;
        }
    }
    if (i < n) {
        unsigned v = 0; int r = n - i;
        for (int j = 0; j < r; j++) v |= (unsigned)d[i+j] << (24 - j*8);
        unsigned t = v; unsigned c[5];
        for (int j = 0; j < 5; j++, t /= 85) c[j] = t % 85;
        for (int j = 4; j >= 0; j--) {
            unsigned x = c[j];
            unsigned char out = (x == 0) ? '!' : (x <= 57 ? x + 34 : x + 35);
            if (!first) fprintf(o, ",");
            fprintf(o, "%d", out);
            first = 0;
        }
    }
}

static int gen(const char *path, const char **fn, size_t *fl,
               const unsigned char *cd, size_t *cs, size_t *os, int nf) {
    FILE *o = fopen(path, "w");
    if (!o) return 1;
    fputs("#define _POSIX_C_SOURCE 200809L\n#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n#include <sys/stat.h>\n#include <sys/types.h>\n", o);
    fputs("#define M 3\n", o);
    fputs("static const unsigned char D[]={", o);
    b85e_arr(o, cd, cs[0]);
    fputs("};\n", o);
    fputs("static const char*N[]={", o);
    for (int i = 0; i < nf; i++) { esc(o, fn[i], fl[i]); fputs(",", o); }
    fputs("};\n", o);
    fputs("static const size_t S[]={", o);
    for (int i = 0; i < nf; i++) fprintf(o, "%zu,", os[i]);
    fputs("};\n", o);
    fprintf(o, "static const size_t C=%zu;\n", cs[0]);
    fprintf(o, "static int F=%d;\n", nf);
    fputs("static void x86(unsigned char*d,size_t n){\n", o);
    fputs("for(size_t i=0;i+4<n;i++)\n", o);
    fputs("if((d[i]==0xE8||d[i]==0xE9)&&(d[i+4]==0||d[i+4]==0xFF)){\n", o);
    fputs("unsigned a=(unsigned)d[i+1]|(unsigned)d[i+2]<<8|(unsigned)d[i+3]<<16;\n", o);
    fputs("a-=i;d[i+1]=a;d[i+2]=a>>8;d[i+3]=a>>16;i+=4;}}\n", o);
    fputs("static int v85(unsigned char c){return c=='!'?0:c<='['?c-34:c-35;}\n", o);
    fputs("static void b8(const unsigned char*i,size_t n,unsigned char*e){\n", o);
    fputs("size_t p=0,q=0,w;while(p+4<n){\n", o);
    fputs("w=0;for(int j=0;j<5;j++)w=w*85+v85(i[p++]);\n", o);
    fputs("e[q++]=w>>24;e[q++]=w>>16;e[q++]=w>>8;e[q++]=w;}\n", o);
    fputs("if(p<n){w=0;int j;for(j=0;p+j<n;j++)w=w*85+v85(i[p+j]);\n", o);
    fputs("for(;j<5;j++)w=w*85+84;for(j=3-(n-p)%5;j<4;j++)e[q++]=w>>(24-j*8);}}\n", o);

    fputs("static size_t rd_vl(const unsigned char*d,size_t*p){size_t v=0,s=0;unsigned char c;\n", o);
    fputs("do{c=d[(*p)++];v|=(size_t)(c&0x7F)<<s;s+=7;}while(c&0x80);return v;}\n", o);

    fputs("static size_t rd_vlb(const unsigned char*d,size_t*bp){size_t v=0,s=0;int i;unsigned char c;\n", o);
    fputs("do{c=0;for(i=0;i<8;i++)c=(c<<1)|((d[*bp/8]>>(*bp%8))&1);(*bp)+=8;\n", o);
    fputs("v|=(size_t)(c&0x7F)<<s;s+=7;}while(c&0x80);return v;}\n", o);

    fputs("static int rd_bit(const unsigned char*d,size_t*bp){\n", o);
    fputs("int r=(d[*bp/8]>>(*bp%8))&1;(*bp)++;return r;}\n", o);
    fputs("static int rd_bits(const unsigned char*d,size_t*bp,int n){\n", o);
    fputs("int v=0,i;for(i=0;i<n;i++)v=(v<<1)|rd_bit(d,bp);return v;}\n", o);
    fputs("static int rd_huf(const unsigned char*d,size_t*bp,const int*cnt,\n", o);
    fputs("   const unsigned char*sym,int ml){\n", o);
    fputs("unsigned c=0;int i=0,fc=0;\n", o);
    fputs("for(int l=0;l<ml;l++){c=(c<<1)|rd_bit(d,bp);\n", o);
    fputs("int cc=cnt[l];if(cc>0&&(int)(c-fc)<cc)return sym[i+(c-fc)];\n", o);
    fputs("i+=cc;fc=(fc+cc)<<1;}return 0;}\n", o);
    fputs("static int rd_tbl(const unsigned char**p,int*cnt,unsigned char*sym){\n", o);
    fputs("int ml=*(*p)++,tot=0,j;\n", o);
    fputs("for(j=0;j<ml;j++){cnt[j]=*(*p)++;tot+=cnt[j];}\n", o);
    fputs("for(j=0;j<tot;j++)sym[j]=*(*p)++;\n", o);
    fputs("while(ml>0&&!cnt[ml-1])ml--;return ml>0?ml:1;}\n", o);

    fputs("static void lz(const unsigned char*i,size_t n,unsigned char*e){\n", o);
    fputs("int flg=i[0];\n", o);
    fputs("if(flg==0){memcpy(e,i+1,n-1);return;}\n", o);
    fputs("if(flg==3){size_t p=1;\n", o);
    fputs("size_t us=rd_vl(i,&p),e2=0;\n", o);
    fputs("while(e2<us){unsigned char b=i[p++];size_t r=rd_vl(i,&p);\n", o);
    fputs("memset(e+e2,b,r);e2+=r;}return;}\n", o);
    fputs("int e8=flg==2||flg==5;size_t p=1,q=0,r0=0,r1=0,r2=0;\n", o);

    fputs("if(flg>=4){\n", o);
    fputs("size_t us=rd_vl(i,&p);\n", o);
    fputs("int tc[16]={0};unsigned char ts[4];\n", o);
    fputs("int lc[16]={0};unsigned char ls[256];\n", o);
    fputs("int tml,lml;\n", o);
    fputs("{const unsigned char*pp=i+p;tml=rd_tbl(&pp,tc,ts);p+=pp-(i+p);}\n", o);
    fputs("{const unsigned char*pp=i+p;lml=rd_tbl(&pp,lc,ls);p+=pp-(i+p);}\n", o);
    fputs("size_t bp=p*8;\n", o);
    fputs("while(q<us){\n", o);
    fputs("int t=rd_huf(i,&bp,tc,ts,tml);\n", o);
    fputs("if(t==0){e[q++]=(unsigned char)rd_huf(i,&bp,lc,ls,lml);}\n", o);
    fputs("else if(t==2){\n", o);
    fputs("int ri=rd_bits(i,&bp,2);int L=rd_bits(i,&bp,8);\n", o);
    fputs("if(L==255)L=(int)(rd_vlb(i,&bp)+(size_t)255);L+=M;int o;\n", o);
    fputs("if(ri==0)o=r0;else if(ri==1){o=r1;r1=r0;r0=o;}else{o=r2;r2=r1;r1=r0;r0=o;}\n", o);
    fputs("int j;for(j=0;j<L;j++)e[q+j]=e[q+j-o];q+=L;}\n", o);
    fputs("else{int L=rd_bits(i,&bp,8);\n", o);
    fputs("if(L==255)L=(int)(rd_vlb(i,&bp)+(size_t)255);L+=M;\n", o);
    fputs("int o=rd_bits(i,&bp,8);\n", o);
    fputs("if(o&128){o=(o&127)<<8|rd_bits(i,&bp,8);}\n", o);
    fputs("int j;for(j=0;j<L;j++)e[q+j]=e[q+j-o];q+=L;\n", o);
    fputs("r2=r1;r1=r0;r0=o;}\n", o);
    fputs("}return;}\n", o);

    fputs("while(p<n){unsigned char f=i[p++];\n", o);
    fputs("int b;for(b=0;b<4&&p<n;b++){int t=f&3;f>>=2;\n", o);
    fputs("if(t==0)e[q++]=i[p++];\n", o);
    fputs("else if(t==3){int b=i[p++];int L=(b&63)+M;int o;\n", o);
    fputs("if(L==M+63){size_t pp=p;L=(int)(rd_vl(i,&pp)+(size_t)63)+M;p=pp;}\n", o);
    fputs("int ri=b>>6;\n", o);
    fputs("if(ri==0)o=r0;else if(ri==1){o=r1;r1=r0;r0=o;}else{o=r2;r2=r1;r1=r0;r0=o;}\n", o);
    fputs("int j;for(j=0;j<L;j++)e[q+j]=e[q+j-o];q+=L;}\n", o);
    fputs("else{int L=i[p++]+M;\n", o);
    fputs("if(L==M+255){size_t pp=p;L=(int)(rd_vl(i,&pp)+(size_t)255)+M;p=pp;}\n", o);
    fputs("if(t==1){int o=i[p++];int j;for(j=0;j<L;j++)e[q+j]=e[q+j-o];q+=L;r2=r1;r1=r0;r0=o;}\n", o);
    fputs("else{int o=(i[p]<<8)|i[p+1];p+=2;int j;for(j=0;j<L;j++)e[q+j]=e[q+j-o];q+=L;r2=r1;r1=r0;r0=o;}}}\n", o);
    fputs("}\n", o);
    fputs("if(e8)x86(e,q);}\n", o);

    fputs("static int mkpath(const char *path){\n", o);
    fputs("char *p=strdup(path),*s=p,*last=0;if(!p)return 1;\n", o);
    fputs("for(;*s;s++)if(*s=='/'){last=s;*s=0;mkdir(p,0777);*s='/';}\n", o);
    fputs("if(last){*last=0;mkdir(p,0777);*last='/';}\n", o);
    fputs("free(p);return 0;}\n", o);

    fprintf(o, "int main(void){\nsize_t i,ts=0;\n");
    for (int i = 0; i < nf; i++) fprintf(o, "ts+=S[%d];\n", i);
    fputs("unsigned char*b=malloc(ts);if(!b)return 1;\n", o);
    fprintf(o, "size_t cl=(C+3)/4*4;\n");
    fputs("unsigned char*c=malloc(cl);if(!c){free(b);return 1;}\n", o);
    fputs("b8(D,(C+3)/4*5,c);lz(c,C,b);free(c);\n", o);
    fputs("size_t off=0;\nfor(i=0;i<(size_t)F;i++){\n", o);
    fputs("mkpath(N[i]);\n", o);
    fputs("FILE*f=fopen(N[i],\"wb\");if(!f){free(b);return 1;}\n", o);
    fputs("fwrite(b+off,1,S[i],f);fclose(f);off+=S[i];puts(N[i]);}\n", o);
    fputs("free(b);\nreturn 0;}\n", o);
    fclose(o);
    return 0;
}

int main(int argc, char **argv) {
    const char *out = "Argo.c", *in[MXF];
    int ni = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-o")) { if (++i >= argc) return 1; out = argv[i]; }
        else if (argv[i][0] == '-') return 1;
        else if (ni < MXF) in[ni++] = argv[i];
    }
    if (!ni) { fprintf(stderr, "usage: %s [-o out.c] files...\n", argv[0]); return 1; }

    unsigned char *db[MXF] = {0};
    size_t osz[MXF], csz[MXF];
    size_t total = 0;
    int ret = 1;

    unsigned char *all = NULL;

    for (int i = 0; i < ni; i++) {
        FILE *f = fopen(in[i], "rb");
        if (!f) { ret = 1; goto cleanup; }
        fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f);
        if (n < 0) { fclose(f); ret = 1; goto cleanup; }
        db[i] = malloc((size_t)n + 1);
        if (!db[i]) { fclose(f); ret = 1; goto cleanup; }
        if (fread(db[i], 1, (size_t)n, f) != (size_t)n) { fclose(f); ret = 1; goto cleanup; }
        fclose(f);
        osz[i] = (size_t)n;
        total += osz[i];
    }

    all = malloc(total);
    if (!all) { ret = 1; goto cleanup; }
    size_t off = 0;
    for (int i = 0; i < ni; i++) {
        memcpy(all + off, db[i], osz[i]);
        off += osz[i];
    }

    Bf cb = {0};
    if (total) cmp(all, total, &cb);
    csz[0] = cb.sz;
    for (int i = 1; i < ni; i++) csz[i] = 0;

    if (ni == 1) {
        printf("%s: %zu -> %zu -> %zu b85\n", in[0], osz[0], csz[0], (csz[0]+3)/4*5);
    } else {
        printf("  (combined: %zu -> %zu -> %zu b85)\n", total, csz[0], (csz[0]+3)/4*5);
    }
    size_t fl[MXF];
    for (int i = 0; i < ni; i++) fl[i] = strlen(in[i]);
    if (gen(out, in, fl, cb.d, csz, osz, ni) != 0) { ret = 1; goto cleanup; }
    printf("%s\n", out);
    ret = 0;
cleanup:
    if (all) free(all);
    for (int i = 0; i < ni; i++) free(db[i]);
    free(cb.d);
    return ret;
}
