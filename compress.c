#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) && !defined(__MINGW32__)
static char *strdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}
#endif

#define MINM 3
#define MAXM 258
#define MXF  64
#define WIN2 8192

typedef struct { unsigned char *d; size_t sz, cp; } Bf;

static int mtch(const unsigned char *d, size_t p, size_t n, int *l) {
    int bl = 0, bo = 0;
    size_t end = p > WIN2 ? p - WIN2 : 0;
    for (size_t i = p; i-- > end; ) {
        int m = 0;
        while (m < MAXM && p+m < n && d[i+m] == d[p+m]) m++;
        if (m > bl) { bl = m; bo = (int)(p - i); if (m == MAXM) break; }
    }
    if (bl >= MINM) { *l = bl; return bo; }
    return -1;
}

static void bp(Bf *b, unsigned char c) {
    if (b->sz >= b->cp) {
        b->cp = b->cp ? b->cp*2 : 65536;
        b->d = realloc(b->d, b->cp);
        if (!b->d) { fprintf(stderr, "OOM\n"); exit(1); }
    }
    b->d[b->sz++] = c;
}

static void cmp(const unsigned char *in, size_t n, Bf *out) {
    if (!n) return;
    size_t pos = 0, fp = 0;
    unsigned char f = 0; int bit = 0;
    bp(out, 0); fp = out->sz - 1;
    while (pos < n) {
        int ml, mo = mtch(in, pos, n, &ml);
        if (mo > 0) {
            f |= (unsigned char)(1 << bit);
            bp(out, (unsigned char)(ml - MINM));
            bp(out, (unsigned char)(mo >> 8));
            bp(out, (unsigned char)mo);
            pos += (size_t)ml;
        } else { bp(out, in[pos]); pos++; }
        if (++bit == 8) {
            out->d[fp] = f; f = 0; bit = 0;
            bp(out, 0); fp = out->sz - 1;
        }
    }
    if (bit) out->d[fp] = f; else out->sz--;
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

static int gen(const char *path, const char **fn, size_t *fl,
               const unsigned char *cd, size_t *cs, size_t *os, int nf) {
    FILE *o = fopen(path, "w");
    if (!o) return 1;
    fputs("#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n", o);
    fputs("#define M 3\n", o);
    fputs("static const unsigned char ", o);
    for (int i = 0; i < nf; i++) {
        if (i) fputs(";\nstatic const unsigned char ", o);
        fprintf(o, "D%d[%zu]=\"", i, (cs[i]+3)/4*5);
        b85e(o, cd, cs[i]); cd += cs[i];
        fputs("\"", o);
    }
    fputs(";\n", o);
    fputs("static const char*N[]={", o);
    for (int i = 0; i < nf; i++) { esc(o, fn[i], fl[i]); fputs(",", o); }
    fputs("};\n", o);
    fputs("static const size_t S[]={", o);
    for (int i = 0; i < nf; i++) fprintf(o, "%zu,", os[i]);
    fputs("};\n", o);
    fputs("static const size_t C[]={", o);
    for (int i = 0; i < nf; i++) fprintf(o, "%zu,", cs[i]);
    fputs("};\n", o);
    fputs("static const unsigned char*P[]={", o);
    for (int i = 0; i < nf; i++) fprintf(o, "D%d,", i);
    fputs("};\n", o);
    fprintf(o, "static int F=%d;\n", nf);
    fputs("static void lz(const unsigned char*i,size_t n,unsigned char*e){\n", o);
    fputs("size_t p=0,q=0;while(p<n){unsigned char f=i[p++];\n", o);
    fputs("for(int b=0;b<8&&p<n;b++){if(f&(1<<b)){\n", o);
    fputs("int L=i[p]+M;int off=(i[p+1]<<8)|i[p+2];p+=3;\n", o);
    fputs("for(int j=0;j<L;j++)e[q+j]=e[q+j-off];q+=L;\n", o);
    fputs("}else e[q++]=i[p++];}}}\n", o);
    fputs("static int v85(unsigned char c){return c=='!'?0:c<='['?c-34:c-35;}\n", o);
    fputs("static void b8(const unsigned char*i,size_t n,unsigned char*e){\n", o);
    fputs("size_t p=0,q=0,w;while(p+4<n){\n", o);
    fputs("w=0;for(int j=0;j<5;j++)w=w*85+v85(i[p++]);\n", o);
    fputs("e[q++]=w>>24;e[q++]=w>>16;e[q++]=w>>8;e[q++]=w;}\n", o);
    fputs("if(p<n){w=0;int j;for(j=0;p+j<n;j++)w=w*85+v85(i[p+j]);\n", o);
    fputs("for(;j<5;j++)w=w*85+84;for(j=3-(n-p)%5;j<4;j++)e[q++]=w>>(24-j*8);}}\n", o);
    fputs("int main(){\n", o);
    fputs("for(int i=0;i<F;i++){\n", o);
    fputs("size_t cl=(C[i]+3)/4*4;\n", o);
    fputs("unsigned char*c=malloc(cl);if(!c)return 1;\n", o);
    fputs("b8(P[i],(C[i]+3)/4*5,c);\n", o);
    fputs("unsigned char*b=malloc(S[i]);if(!b){free(c);return 1;}\n", o);
    fputs("lz(c,C[i],b);free(c);\n", o);
    fputs("FILE*f=fopen(N[i],\"wb\");if(!f){free(b);return 1;}\n", o);
    fputs("fwrite(b,1,S[i],f);fclose(f);free(b);puts(N[i]);}\n", o);
    fputs("return 0;}\n", o);
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

    unsigned char *db[MXF];
    size_t osz[MXF], csz[MXF];
    Bf cb = {0};
    for (int i = 0; i < ni; i++) {
        FILE *f = fopen(in[i], "rb");
        if (!f) return 1;
        fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f);
        db[i] = malloc((size_t)n+1);
        if (!db[i]) return 1;
        fread(db[i], 1, (size_t)n, f); fclose(f);
        osz[i] = (size_t)n;
    }
    for (int i = 0; i < ni; i++) {
        size_t s = cb.sz;
        if (osz[i]) cmp(db[i], osz[i], &cb);
        csz[i] = cb.sz - s;
    }
    for (int i = 0; i < ni; i++) {
        printf("%s: %zu -> %zu -> %zu b85\n", in[i], osz[i], csz[i], (csz[i]+3)/4*5);
    }
    size_t fl[MXF];
    for (int i = 0; i < ni; i++) fl[i] = strlen(in[i]);
    gen(out, in, fl, cb.d, csz, osz, ni);
    printf("%s\n", out);
    for (int i = 0; i < ni; i++) free(db[i]);
    free(cb.d);
    return 0;
}
