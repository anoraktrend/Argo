#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <pthread.h>
#include <unistd.h>

static int hw_threads(void) {
#ifdef _SC_NPROCESSORS_ONLN
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int)n : 1;
#else
    return 1;
#endif
}
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
#define MAXM 273
#define MXF 64
#define WIN2 (1<<20)
#define WIN1 (1<<21)
#define HB 15
#define HS (1<<HB)
#define HC 64

typedef struct { size_t *hh, *hn; } MState;

static MState *ms_new(void) {
    MState *ms = calloc(1, sizeof(MState));
    ms->hh = malloc(HS * sizeof(size_t));
    ms->hn = calloc(WIN2, sizeof(size_t));
    memset(ms->hh, 0xFF, HS * sizeof(size_t));
    return ms;
}

static void ms_free(MState *ms) {
    free(ms->hh); free(ms->hn); free(ms);
}

static void ms_reset(MState *ms) {
    if (!ms->hh) {
        ms->hh = malloc(HS * sizeof(size_t));
        ms->hn = calloc(WIN2, sizeof(size_t));
    }
    memset(ms->hh, 0xFF, HS * sizeof(size_t));
}

static MState g_ms; /* single-thread fallback */

static unsigned ms_hsh(const unsigned char *d, size_t p) {
    return ((unsigned)d[p]*773u + (unsigned)d[p+1]*97u + (unsigned)d[p+2]) & (HS-1);
}

#define kNumBitModelTotalBits 11
#define kNumMoveBits 5
#define PROB_INIT_VAL ((1 << kNumBitModelTotalBits) / 2)
#define kTopValue ((uint32_t)1 << 24)
#define kNumStates 12
#define kNumPosBitsMax 4
#define kNumLenToPosStates 4
#define kEndPosModelIndex 14
#define kNumFullDistances (1 << (kEndPosModelIndex >> 1))
#define kNumAlignBits 4
#define kMatchMinLen 2

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

static int mtch(MState *ms, const unsigned char *d, size_t sz, size_t p, int *l) {
    if (p + MINM > sz) return -1;
    int c = 0;
    size_t i = ms->hh[ms_hsh(d, p)];
    size_t lim = p > WIN2 ? p - WIN2 : 0;
    int bl = 0, bo = 0;
    while (i < sz && i >= lim && i < p && c < HC) {
        if (d[i] == d[p] && d[i+1] == d[p+1] && d[i+2] == d[p+2]) {
            int m = MINM;
#if defined(__SSE2__) && defined(__GNUC__)
            while (m + 16 <= MAXM && p + m + 16 <= sz && i + m + 16 <= sz) {
                int eq = _mm_movemask_epi8(_mm_cmpeq_epi8(
                    _mm_loadu_si128((const __m128i*)(d + i + m)),
                    _mm_loadu_si128((const __m128i*)(d + p + m))));
                if (eq != 0xFFFF) { m += __builtin_ctz(~eq & 0xFFFFu); break; }
                m += 16;
            }
#endif
            while (p + m < sz && d[i+m] == d[p+m] && m < MAXM) m++;
            if (m > bl) { bl = m; bo = (int)(p - i); if (m == MAXM) break; }
        }
        i = ms->hn[i & (WIN2-1)];
        c++;
    }
    ms->hn[p & (WIN2-1)] = ms->hh[ms_hsh(d, p)];
    ms->hh[ms_hsh(d, p)] = p;
    if (bl >= MINM) {
        *l = bl;
        return bo;
    }
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

typedef struct {
    uint32_t low;
    uint32_t range;
    uint32_t cache_size;
    uint8_t cache;
    Bf *out;
} RangeEnc;

static void rc_init(RangeEnc *rc, Bf *out) {
    rc->low = 0;
    rc->range = 0xFFFFFFFF;
    rc->cache_size = 1;
    rc->cache = 0;
    rc->out = out;
}

static void rc_shift(RangeEnc *rc) {
    uint32_t low = rc->low;
    if (low < 0xFF000000 || (low >> 24) == 0xFF) {
        uint8_t temp = rc->cache;
        do {
            bp(rc->out, (uint8_t)(temp + (uint8_t)(low >> 24)));
            temp = 0xFF;
        } while (--rc->cache_size);
        rc->cache = (uint8_t)(low >> 24);
    }
    rc->cache_size++;
    rc->low = low << 8;
}

static void rc_norm(RangeEnc *rc) {
    while (rc->range < kTopValue) {
        rc_shift(rc);
        rc->range <<= 8;
    }
}

static void rc_encode_bit(RangeEnc *rc, uint16_t *prob, int symbol) {
    uint32_t bound = (rc->range >> kNumBitModelTotalBits) * (*prob);
    if (symbol == 0) {
        rc->range = bound;
        *prob += ((1 << kNumBitModelTotalBits) - *prob) >> kNumMoveBits;
    } else {
        rc->low += bound;
        rc->range -= bound;
        *prob -= *prob >> kNumMoveBits;
    }
    rc_norm(rc);
}

static void rc_encode_direct(RangeEnc *rc, uint32_t value, int num) {
    for (int i = num - 1; i >= 0; i--) {
        rc->range >>= 1;
        if ((value >> i) & 1)
            rc->low += rc->range;
        rc_norm(rc);
    }
}

static void rc_flush(RangeEnc *rc) {
    for (int i = 0; i < 5; i++)
        rc_shift(rc);
}

static void rc_encode_bit_tree(RangeEnc *rc, uint16_t *probs, int num_bits, int symbol) {
    int m = 1;
    for (int i = num_bits - 1; i >= 0; i--) {
        int bit = (symbol >> i) & 1;
        rc_encode_bit(rc, &probs[m], bit);
        m = (m << 1) | bit;
    }
}

static void rc_encode_bit_tree_rev(RangeEnc *rc, uint16_t *probs, int num_bits, int symbol) {
    int m = 1;
    for (int i = 0; i < num_bits; i++) {
        int bit = (symbol >> i) & 1;
        rc_encode_bit(rc, &probs[m], bit);
        m = (m << 1) | bit;
    }
}

static unsigned update_state_literal(unsigned state) {
    if (state < 4) return 0;
    else if (state < 10) return state - 3;
    else return state - 6;
}
static unsigned update_state_match(unsigned state) { return state < 7 ? 7 : 10; }
static unsigned update_state_rep(unsigned state) { return state < 7 ? 8 : 11; }
static unsigned update_state_shortrep(unsigned state) { return state < 7 ? 9 : 11; }

static int lzma_encode(const unsigned char *src, size_t n, Bf *out,
                       int lc, int lp, int pb) {
    if (n == 0) return 0;

    size_t prob_count = 1846 + 768 * (1 << (lp + lc));
    uint16_t *probs = calloc(prob_count, sizeof(uint16_t));
    if (!probs) { fprintf(stderr, "OOM\n"); return -1; }
    for (size_t i = 0; i < prob_count; i++) probs[i] = PROB_INIT_VAL;

    uint16_t *is_match = probs;
    uint16_t *is_rep = is_match + (kNumStates << kNumPosBitsMax);
    uint16_t *is_rep_g0 = is_rep + kNumStates;
    uint16_t *is_rep_g1 = is_rep_g0 + kNumStates;
    uint16_t *is_rep_g2 = is_rep_g1 + kNumStates;
    uint16_t *is_rep0_long = is_rep_g2 + kNumStates;
    uint16_t *pos_slot_decoder = is_rep0_long + (kNumStates << kNumPosBitsMax);
    uint16_t *pos_decoders = pos_slot_decoder + (kNumLenToPosStates << 6);
    uint16_t *align_decoder = pos_decoders + (1 + kNumFullDistances - kEndPosModelIndex);
    uint16_t *len_choice = align_decoder + (1 << kNumAlignBits);
    uint16_t *len_choice2 = len_choice + 1;
    uint16_t *len_low_coder = len_choice2 + 1;
    uint16_t *len_mid_coder = len_low_coder + (1 << kNumPosBitsMax) * (1 << 3);
    uint16_t *len_high_coder = len_mid_coder + (1 << kNumPosBitsMax) * (1 << 3);
    uint16_t *rep_len_choice = len_high_coder + (1 << 8);
    uint16_t *rep_len_choice2 = rep_len_choice + 1;
    uint16_t *rep_len_low_coder = rep_len_choice2 + 1;
    uint16_t *rep_len_mid_coder = rep_len_low_coder + (1 << kNumPosBitsMax) * (1 << 3);
    uint16_t *rep_len_high_coder = rep_len_mid_coder + (1 << kNumPosBitsMax) * (1 << 3);
    uint16_t *lit_probs = rep_len_high_coder + (1 << 8);

    MState *ms = ms_new();

    RangeEnc rc;
    rc_init(&rc, out);
    bp(out, 0);

    unsigned state = 0;
    uint32_t rep0 = 0, rep1 = 0, rep2 = 0, rep3 = 0;
    size_t pos = 0;

    while (pos < n) {
        unsigned pos_state = pos & ((1 << pb) - 1);
        unsigned state2 = (state << kNumPosBitsMax) + pos_state;

        int ml, mo = mtch(ms, src, n, pos, &ml);
        int is_match_bit = mo > 0 ? 1 : 0;
        rc_encode_bit(&rc, &is_match[state2], is_match_bit);

        if (is_match_bit == 0) {
            unsigned prev_byte = pos > 0 ? src[pos - 1] : 0;
            unsigned lit_state = ((pos & ((1 << lp) - 1)) << lc) + (prev_byte >> (8 - lc));
            uint16_t *lp_probs = lit_probs + 0x300 * lit_state;

            if (state >= 7) {
                unsigned match_byte = src[pos - rep0 - 1];
                int symbol = 1;
                while (symbol < 0x100) {
                    unsigned match_bit = (match_byte >> 7) & 1;
                    match_byte <<= 1;
                    int bit = (src[pos] >> (7 - (symbol - 1))) & 1;
                    rc_encode_bit(&rc, &lp_probs[((1 + match_bit) << 8) + symbol], bit);
                    symbol = (symbol << 1) | bit;
                    if ((int)match_bit != bit) break;
                }
                while (symbol < 0x100) {
                    int bit = (src[pos] >> (7 - (symbol - 1))) & 1;
                    rc_encode_bit(&rc, &lp_probs[symbol], bit);
                    symbol = (symbol << 1) | bit;
                }
            } else {
                int symbol = 1;
                while (symbol < 0x100) {
                    int bit = (src[pos] >> (7 - (symbol - 1))) & 1;
                    rc_encode_bit(&rc, &lp_probs[symbol], bit);
                    symbol = (symbol << 1) | bit;
                }
            }
            pos++;
            state = update_state_literal(state);
        } else {
            rc_encode_bit(&rc, &is_rep[state], 0);

            int len;
            uint32_t dist = (uint32_t)mo;
            int rp = -1;
            if (dist == rep0) rp = 0;
            else if (dist == rep1) rp = 1;
            else if (dist == rep2) rp = 2;
            else if (dist == rep3) rp = 3;

            if (rp >= 0) {
                rc_encode_bit(&rc, &is_rep_g0[state], rp > 0 ? 1 : 0);
                if (rp > 0) {
                    rc_encode_bit(&rc, &is_rep_g1[state], rp > 1 ? 1 : 0);
                    if (rp > 1) {
                        rc_encode_bit(&rc, &is_rep_g2[state], rp > 2 ? 1 : 0);
                    }
                }

                if (rp == 0) {
                    rc_encode_bit(&rc, &is_rep0_long[state2], ml > 1 ? 1 : 0);
                    if (ml == 1) {
                        state = update_state_shortrep(state);
                        pos++;
                        continue;
                    }
                }

                len = ml - kMatchMinLen;
                uint16_t *rep_len_choice_ptr = rep_len_choice;
                uint16_t *rep_len_choice2_ptr = rep_len_choice2;
                rc_encode_bit(&rc, rep_len_choice_ptr, len >= 8 ? 1 : 0);
                if (len < 8) {
                    rc_encode_bit_tree(&rc, rep_len_low_coder + (pos_state << 3), 3, len);
                } else {
                    len -= 8;
                    rc_encode_bit(&rc, rep_len_choice2_ptr, len >= 8 ? 1 : 0);
                    if (len < 8) {
                        rc_encode_bit_tree(&rc, rep_len_mid_coder + (pos_state << 3), 3, len);
                    } else {
                        len -= 8;
                        rc_encode_bit_tree(&rc, rep_len_high_coder, 8, len);
                    }
                }

                if (rp == 1) { uint32_t t = rep0; rep0 = rep1; rep1 = t; }
                else if (rp == 2) { uint32_t t = rep0; rep0 = rep2; rep2 = rep1; rep1 = t; }
                else if (rp == 3) { uint32_t t = rep0; rep0 = rep3; rep3 = rep2; rep2 = rep1; rep1 = t; }

                len = ml - kMatchMinLen;
                state = update_state_rep(state);
                pos += ml;
            } else {
                len = ml - kMatchMinLen;
                rc_encode_bit(&rc, &len_choice[0], len >= 8 ? 1 : 0);
                if (len < 8) {
                    rc_encode_bit_tree(&rc, len_low_coder + (pos_state << 3), 3, len);
                } else {
                    len -= 8;
                    rc_encode_bit(&rc, &len_choice2[0], len >= 8 ? 1 : 0);
                    if (len < 8) {
                        rc_encode_bit_tree(&rc, len_mid_coder + (pos_state << 3), 3, len);
                    } else {
                        len -= 8;
                        rc_encode_bit_tree(&rc, len_high_coder, 8, len);
                    }
                }

                rep3 = rep2;
                rep2 = rep1;
                rep1 = rep0;
                rep0 = dist;

                state = update_state_match(state);
                len = ml - kMatchMinLen;

                unsigned len_state = len;
                if (len_state > kNumLenToPosStates - 1)
                    len_state = kNumLenToPosStates - 1;

                unsigned pos_slot;
                if (dist == 0) pos_slot = 0;
                else if (dist == 1) pos_slot = 1;
                else if (dist == 2) pos_slot = 2;
                else if (dist == 3) pos_slot = 3;
                else if (dist < 6) pos_slot = 4;
                else if (dist < 8) pos_slot = 5;
                else if (dist < 12) pos_slot = 6;
                else if (dist < 16) pos_slot = 7;
                else if (dist < 24) pos_slot = 8;
                else if (dist < 32) pos_slot = 9;
                else if (dist < 48) pos_slot = 10;
                else if (dist < 64) pos_slot = 11;
                else if (dist < 96) pos_slot = 12;
                else if (dist < 128) pos_slot = 13;
                else {
                    unsigned bits = 0;
                    uint32_t t = dist;
                    while (t >= 128) { t >>= 1; bits++; }
                    pos_slot = (bits + 1) * 2 + (t >= 2 ? 1 : 0);
                }

                rc_encode_bit_tree(&rc, pos_slot_decoder + (len_state << 6), 6, pos_slot);

                if (pos_slot >= 4) {
                    unsigned num_direct_bits = (pos_slot >> 1) - 1;
                    uint32_t dist_out = ((2 | (pos_slot & 1)) << num_direct_bits);
                    uint32_t dist_low = dist - dist_out;
                    if (pos_slot < kEndPosModelIndex) {
                        rc_encode_bit_tree_rev(&rc, pos_decoders + dist_out - pos_slot, num_direct_bits, dist_low);
                    } else {
                        rc_encode_direct(&rc, dist_low >> kNumAlignBits, num_direct_bits - kNumAlignBits);
                        rc_encode_bit_tree_rev(&rc, align_decoder, kNumAlignBits, dist_low & ((1 << kNumAlignBits) - 1));
                    }
                }

                pos += ml;
            }
        }
    }

    rc_encode_bit(&rc, &is_match[(state << kNumPosBitsMax) + (pos & ((1 << pb) - 1))], 1);
    rc_encode_bit(&rc, &is_rep[state], 0);
    rep3 = rep2; rep2 = rep1; rep1 = rep0;
    rc_encode_bit(&rc, &len_choice[0], 0);
    rc_encode_direct(&rc, 0xFFFFFFFF, 4);
    rc_flush(&rc);

    ms_free(ms);
    free(probs);
    return 0;
}

static void encode_lzma2_props(unsigned char *props, uint32_t dict_size) {
    unsigned p;
    if (dict_size == 0xFFFFFFFF)
        p = 40;
    else {
        unsigned bits = 0;
        uint32_t t = dict_size;
        while (t > 1) { t >>= 1; bits++; }
        p = (unsigned)((bits - 11) * 2 + ((dict_size >> (bits - 1)) & 1));
    }
    props[0] = (unsigned char)p;
    props[1] = (unsigned char)((2 * 5 + 3) * 9 + 3);
}

typedef struct {
    const unsigned char *src;
    size_t n;
    int lc, lp, pb;
    int store; /* 1 = store uncompressed */
    Bf result;
    int ok;
} LZMAWork;

static void *lzma_thread(void *arg) {
    LZMAWork *w = (LZMAWork *)arg;
    w->result = (Bf){0};
    Bf *o = &w->result;
    if (w->store || w->n < 32) {
        bp(o, 0);
        for (size_t i = 0; i < w->n; i++) bp(o, w->src[i]);
        w->ok = 1;
        return NULL;
    }
    Bf raw = {0};
    if (lzma_encode(w->src, w->n, &raw, w->lc, w->lp, w->pb) != 0) {
        free(raw.d);
        w->ok = 0;
        return NULL;
    }
    if (raw.sz >= w->n) {
        free(raw.d);
        bp(o, 0);
        for (size_t i = 0; i < w->n; i++) bp(o, w->src[i]);
        w->ok = 1;
        return NULL;
    }
    for (size_t i = 0; i < raw.sz; i++) bp(o, raw.d[i]);
    free(raw.d);
    w->ok = 1;
    return NULL;
}

static int lzma2_encode(const unsigned char *src, size_t n, Bf *out, uint32_t dict_size) {
    if (n == 0) { bp(out, 0); bp(out, 0); return 0; }

    unsigned char props_buf[2];
    encode_lzma2_props(props_buf, dict_size);
    int lc = 3, lp = 0, pb = 2;
    int lzma_props = (unsigned char)((pb * 5 + lp) * 9 + lc);
    size_t chunk_max = (1 << 18);

    int nchunks = (int)((n + chunk_max - 1) / chunk_max);
    if (nchunks < 1) nchunks = 1;
    int nthreads = hw_threads();
    if (nthreads < 1) nthreads = 1;

    LZMAWork *works = calloc((size_t)nchunks, sizeof(LZMAWork));
    pthread_t *threads = calloc((size_t)nchunks, sizeof(pthread_t));
    size_t off = 0;

    for (int i = 0; i < nchunks; i++) {
        works[i].src = src + off;
        works[i].n = n - off;
        if (works[i].n > chunk_max) works[i].n = chunk_max;
        works[i].lc = lc; works[i].lp = lp; works[i].pb = pb;
        works[i].ok = 0;
        off += works[i].n;
    }

    int launched = 0;
    for (int i = 0; i < nchunks; i++) {
        if (i < nthreads && nchunks > 1) {
            if (pthread_create(&threads[i], NULL, lzma_thread, &works[i]) == 0)
                launched++;
            else
                lzma_thread(&works[i]);
        } else {
            lzma_thread(&works[i]);
        }
    }

    for (int i = 0; i < launched; i++)
        pthread_join(threads[i], NULL);

    int first = 1;
    for (int i = 0; i < nchunks; i++) {
        size_t csz = works[i].result.sz;
        size_t uc = works[i].n;

        if (csz == 0) {
            free(works[i].result.d);
            continue;
        }

        int is_store = (works[i].result.d[0] == 0);

        if (is_store) {
            if (first) bp(out, 1); else bp(out, 2);
            bp(out, (unsigned char)(uc >> 8));
            bp(out, (unsigned char)uc);
            for (size_t j = 0; j < uc; j++) bp(out, works[i].src[j]);
        } else {
            unsigned cc = (unsigned)(csz);
            unsigned char ctrl;
            if (first) {
                ctrl = (unsigned char)(0x80 | ((uc >> 16) & 0x1F));
                bp(out, ctrl);
                bp(out, (unsigned char)lzma_props);
            } else {
                ctrl = (unsigned char)(0x40 | ((uc >> 16) & 0x1F));
                bp(out, ctrl);
            }
            bp(out, (unsigned char)(uc >> 8));
            bp(out, (unsigned char)uc);
            bp(out, (unsigned char)(cc >> 8));
            bp(out, (unsigned char)cc);
            for (size_t j = 0; j < csz; j++) bp(out, works[i].result.d[j]);
        }
        free(works[i].result.d);
        first = 0;
    }

    bp(out, 0); bp(out, 0);
    free(works); free(threads);
    return 0;
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

static void cmp(const unsigned char *in, size_t n, Bf *out) {
    if (!n) { bp(out, 0); return; }
    size_t sv = out->sz;

    if (n > 8 && rle_size(in, n) < n) { rle_enc(in, n, out); return; }

    unsigned char *tx = e8e9(in, n);
    const unsigned char *src = tx ? tx : in;
    int e8 = tx ? 1 : 0;

    uint32_t dict_size = n < (1 << 18) ? (uint32_t)(n + n/2) : (1 << 23);
    if (dict_size < (1 << 12)) dict_size = 1 << 12;
    if (dict_size > (1 << 23)) dict_size = 1 << 23;

    Bf lzma2_data = {0};
    lzma2_encode(src, n, &lzma2_data, dict_size);

    if (lzma2_data.sz < n) {
        bp(out, (unsigned char)(e8 ? 7 : 6));
        for (size_t i = 0; i < lzma2_data.sz; i++) bp(out, lzma2_data.d[i]);
        free(lzma2_data.d);
        if (tx) free(tx);
        return;
    }
    free(lzma2_data.d);

    size_t pre = out->sz;
    bp(out, (unsigned char)(e8 ? 2 : 1));
    ms_reset(&g_ms);
    size_t pos = 0, fp = 0, rp0 = 0, rp1 = 0, rp2 = 0;
    unsigned char f = 0; int bit = 0;
    bp(out, 0); fp = out->sz - 1;
    while (pos < n) {
        int ml, mo = mtch(&g_ms, src, n, pos, &ml);
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
    if (out->sz - pre < n) { if (tx) free(tx); return; }
    out->sz = pre;
    free(tx);

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
    fputs("#define _POSIX_C_SOURCE 200809L\n#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n#include <pthread.h>\n#include <unistd.h>\n#include <sys/stat.h>\n#include <sys/types.h>\n", o);
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

    fputs("#define kTop ((unsigned)1<<24)\n", o);
    fputs("typedef struct{unsigned r,c;const unsigned char*b;size_t*p;}RC;\n", o);
    fputs("static int rc_init(RC*rc,const unsigned char*b,size_t*p){\n", o);
    fputs("rc->r=0xFFFFFFFF;rc->c=0;rc->b=b;rc->p=p;\n", o);
    fputs("unsigned char z=rc->b[(*p)++];\n", o);
    fputs("for(int i=0;i<4;i++)rc->c=(rc->c<<8)|rc->b[(*p)++];\n", o);
    fputs("return z==0;}\n", o);
    fputs("static void rc_n(RC*rc){\n", o);
    fputs("while(rc->r<kTop){rc->r<<=8;rc->c=(rc->c<<8)|rc->b[(*rc->p)++];}}\n", o);
    fputs("static int rc_b(RC*rc,unsigned short*pr){\n", o);
    fputs("unsigned v=*pr;unsigned b=(rc->r>>11)*v;\n", o);
    fputs("int s;if(rc->c<b){v+=((1<<11)-v)>>5;rc->r=b;s=0;\n", o);
    fputs("}else{v-=v>>5;rc->c-=b;rc->r-=b;s=1;}*pr=(unsigned short)v;rc_n(rc);return s;}\n", o);
    fputs("static unsigned rc_d(RC*rc,unsigned n){\n", o);
    fputs("unsigned r=0;for(unsigned i=0;i<n;i++){\n", o);
    fputs("rc->r>>=1;unsigned t=(rc->c-rc->r)>>31;rc->c-=rc->r&t;rc_n(rc);r=(r<<1)|(t+1);}return r;}\n", o);
    fputs("static int rc_bt(RC*rc,unsigned short*p,int n){\n", o);
    fputs("int m=1;for(int i=0;i<n;i++)m=(m<<1)+rc_b(rc,p+m);return m-(1<<n);}\n", o);
    fputs("static int rc_br(RC*rc,unsigned short*p,int n){\n", o);
    fputs("int m=1,s=0;for(int i=0;i<n;i++){int b=rc_b(rc,p+m);m=(m<<1)+b;s|=b<<i;}return s;}\n", o);
    fputs("static unsigned short PIV=2048/2;\n", o);
    fputs("static int rd_bit(const unsigned char*d,size_t*bp){\n", o);
    fputs("int r=(d[*bp/8]>>(*bp%8))&1;(*bp)++;return r;}\n", o);
    fputs("static int rd_bits(const unsigned char*d,size_t*bp,int n){\n", o);
    fputs("int v=0,i;for(i=0;i<n;i++)v=(v<<1)|rd_bit(d,bp);return v;}\n", o);
    fputs("static size_t rd_vlb(const unsigned char*d,size_t*bp){size_t v=0,s=0;int i;unsigned char c;\n", o);
    fputs("do{c=0;for(i=0;i<8;i++)c=(c<<1)|((d[*bp/8]>>(*bp%8))&1);(*bp)+=8;\n", o);
    fputs("v|=(size_t)(c&0x7F)<<s;s+=7;}while(c&0x80);return v;}\n", o);
    fputs("static int rd_huf(const unsigned char*d,size_t*bp,const int*cnt,const unsigned char*sym,int ml){\n", o);
    fputs("unsigned c=0;int i=0,fc=0;\n", o);
    fputs("for(int l=0;l<ml;l++){c=(c<<1)|rd_bit(d,bp);\n", o);
    fputs("int cc=cnt[l];if(cc>0&&(int)(c-fc)<cc)return sym[i+(c-fc)];\n", o);
    fputs("i+=cc;fc=(fc+cc)<<1;}return 0;}\n", o);
    fputs("static int rd_tbl(const unsigned char**p,int*cnt,unsigned char*sym){\n", o);
    fputs("int ml=*(*p)++,tot=0,j;\n", o);
    fputs("for(j=0;j<ml;j++){cnt[j]=*(*p)++;tot+=cnt[j];}\n", o);
    fputs("for(j=0;j<tot;j++)sym[j]=*(*p)++;\n", o);
    fputs("while(ml>0&&!cnt[ml-1])ml--;return ml>0?ml:1;}\n", o);

    fputs("static size_t lzma2_d(const unsigned char*in,size_t n,unsigned char*out){\n", o);
    fputs("size_t p=0,op=0;\n", o);
    fputs("while(p<n){\n", o);
    fputs("unsigned ctl=in[p++];\n", o);
    fputs("if(ctl==0)break;\n", o);
    fputs("if(ctl==1||ctl==2){\n", o);
    fputs("unsigned sz=(unsigned)in[p]<<8|in[p+1];p+=2;\n", o);
    fputs("memcpy(out+op,in+p,sz);p+=sz;op+=sz;continue;}\n", o);

    fputs("unsigned props=0,uc_hi=0;\n", o);
    fputs("if(ctl&0x80){props=in[p++];}\n", o);
    fputs("uc_hi=(ctl&0x1F)<<16;\n", o);
    fputs("unsigned uc=uc_hi|(unsigned)in[p]<<8|in[p+1];p+=2;\n", o);
    fputs("unsigned cc=(unsigned)in[p]<<8|in[p+1];p+=2;\n", o);

    fputs("unsigned lc=3,lp=0,pb=2;\n", o);
    fputs("if(ctl&0x80){unsigned d=props;lc=d%9;d/=9;pb=d/5;lp=d%5;}\n", o);

    fputs("RC rc;rc_init(&rc,in,&p);\n", o);
    fputs("size_t pc=1846+768*(1<<(lp+lc));unsigned short*pr=calloc(pc,2);\n", o);
    fputs("if(!pr)return -1;\n", o);
    fputs("for(size_t i=0;i<pc;i++)pr[i]=PIV;\n", o);

    fputs("unsigned short*im=pr;\n", o);
    fputs("unsigned short*ir=im+(12<<4);\n", o);
    fputs("unsigned short*ir0=ir+12;\n", o);
    fputs("unsigned short*ir1=ir0+12;\n", o);
    fputs("unsigned short*ir2=ir1+12;\n", o);
    fputs("unsigned short*irl=ir2+12;\n", o);
    fputs("unsigned short*ps=irl+(12<<4);\n", o);
    fputs("unsigned short*pd=ps+(4<<6);\n", o);
    fputs("unsigned short*pa=pd+(1+((1<<(14>>1))-14));\n", o);
    fputs("unsigned short*lc0=pa+(1<<4);\n", o);
    fputs("unsigned short*lc1=lc0+1;\n", o);
    fputs("unsigned short*ll=lc1+1;\n", o);
    fputs("unsigned short*lm=ll+(1<<4)*(1<<3);\n", o);
    fputs("unsigned short*lh=lm+(1<<4)*(1<<3);\n", o);
    fputs("unsigned short*rc0=lh+(1<<8);\n", o);
    fputs("unsigned short*rc1=rc0+1;\n", o);
    fputs("unsigned short*rl=rc1+1;\n", o);
    fputs("unsigned short*rm=rl+(1<<4)*(1<<3);\n", o);
    fputs("unsigned short*rh=rm+(1<<4)*(1<<3);\n", o);
    fputs("unsigned short*lp0=rh+(1<<8);\n", o);

    fputs("unsigned sr=0;\n", o);
    fputs("unsigned r0=0,r1=0,r2=0,r3=0;\n", o);
    fputs("size_t ep=0;\n", o);
    fputs("while(ep<uc){\n", o);
    fputs("unsigned ps2=ep&((1<<pb)-1);\n", o);
    fputs("unsigned st2=(sr<<4)|ps2;\n", o);
    fputs("if(!rc_b(&rc,&im[st2])){\n", o);
    fputs("unsigned lb=ep>0?out[ep-1]:0;\n", o);
    fputs("unsigned ls=((ep&((1<<lp)-1))<<lc)|(lb>>(8-lc));\n", o);
    fputs("unsigned short*lp2=lp0+0x300*ls;\n", o);
    fputs("if(sr>=7){\n", o);
    fputs("unsigned mb=out[ep-r0-1];\n", o);
    fputs("int sy=1;\n", o);
    fputs("while(sy<256){\n", o);
    fputs("int mbit=(mb>>7)&1;mb<<=1;\n", o);
    fputs("int bit=rc_b(&rc,&lp2[((1+mbit)<<8)+sy]);\n", o);
    fputs("sy=(sy<<1)|bit;if(mbit!=bit)break;}\n", o);
    fputs("while(sy<256)sy=(sy<<1)|rc_b(&rc,&lp2[sy]);\n", o);
    fputs("out[ep++]=(unsigned char)(sy-256);\n", o);
    fputs("}else{\n", o);
    fputs("int sy=1;while(sy<256)sy=(sy<<1)|rc_b(&rc,&lp2[sy]);\n", o);
    fputs("out[ep++]=(unsigned char)(sy-256);}\n", o);
    fputs("if(sr<4)sr=0;else if(sr<10)sr-=3;else sr-=6;\n", o);
    fputs("}else{\n", o);
    fputs("if(!rc_b(&rc,&ir[sr])){\n", o);
    fputs("int L=rc_b(&rc,&lc0[0]);\n", o);
    fputs("if(L==0)L=rc_bt(&rc,ll+(ps2<<3),3);\n", o);
    fputs("else{\n", o);
    fputs("L=rc_b(&rc,&lc1[0]);\n", o);
    fputs("if(L==0)L=8+rc_bt(&rc,lm+(ps2<<3),3);\n", o);
    fputs("else L=16+rc_bt(&rc,lh,8);}\n", o);
    fputs("r3=r2;r2=r1;r1=r0;\n", o);
    fputs("unsigned ls2=L;if(ls2>3)ls2=3;\n", o);
    fputs("unsigned ps2v=rc_bt(&rc,ps+(ls2<<6),6);\n", o);
    fputs("if(ps2v<4)r0=ps2v;\n", o);
    fputs("else{\n", o);
    fputs("unsigned nd=(ps2v>>1)-1;\n", o);
    fputs("unsigned d0=((2|(ps2v&1))<<nd);\n", o);
    fputs("if(ps2v<14)r0=d0+rc_br(&rc,pd+d0-ps2v,nd);\n", o);
    fputs("else r0=d0+(rc_d(&rc,nd-4)<<4)+rc_br(&rc,pa,4);}\n", o);
    fputs("sr=sr<7?7:10;\n", o);
    fputs("L+=2;\n", o);
    fputs("for(int j=0;j<L;j++)out[ep+j]=out[ep+j-r0];ep+=L;\n", o);
    fputs("}else{\n", o);
    fputs("int ri;\n", o);
    fputs("if(!rc_b(&rc,&ir0[sr])){\n", o);
    fputs("ri=0;\n", o);
    fputs("if(!rc_b(&rc,&irl[st2])){\n", o);
    fputs("out[ep]=out[ep-r0];ep++;\n", o);
    fputs("sr=sr<7?9:11;continue;}\n", o);
    fputs("}else{\n", o);
    fputs("ri=1+rc_b(&rc,&ir1[sr]);\n", o);
    fputs("if(ri==2)ri+=rc_b(&rc,&ir2[sr]);}\n", o);
    fputs("int L=rc_b(&rc,&rc0[0]);\n", o);
    fputs("if(L==0)L=rc_bt(&rc,rl+(ps2<<3),3);\n", o);
    fputs("else{\n", o);
    fputs("L=rc_b(&rc,&rc1[0]);\n", o);
    fputs("if(L==0)L=8+rc_bt(&rc,rm+(ps2<<3),3);\n", o);
    fputs("else L=16+rc_bt(&rc,rh,8);}\n", o);
    fputs("unsigned d;\n", o);
    fputs("if(ri==0)d=r0;\n", o);
    fputs("else if(ri==1){d=r1;r1=r0;r0=d;}\n", o);
    fputs("else if(ri==2){d=r2;r2=r1;r1=r0;r0=d;}\n", o);
    fputs("else{d=r3;r3=r2;r2=r1;r1=r0;r0=d;}\n", o);
    fputs("sr=sr<7?8:11;\n", o);
    fputs("L+=2;\n", o);
    fputs("for(int j=0;j<L;j++)out[ep+j]=out[ep+j-d];ep+=L;\n", o);
    fputs("}}}\n", o);

    fputs("free(pr);\n", o);
    fputs("}return op;}\n", o);

    fputs("static void lz(const unsigned char*i,size_t n,unsigned char*e){\n", o);
    fputs("int flg=i[0];\n", o);
    fputs("if(flg==0){memcpy(e,i+1,n-1);return;}\n", o);
    fputs("if(flg==3){size_t p=1;\n", o);
    fputs("size_t us=rd_vl(i,&p),e2=0;\n", o);
    fputs("while(e2<us){unsigned char b=i[p++];size_t r=rd_vl(i,&p);\n", o);
    fputs("memset(e+e2,b,r);e2+=r;}return;}\n", o);
    fputs("if(flg>=6){lzma2_d(i+1,n-1,e);if(flg&1)x86(e,n);return;}\n", o);
    fputs("int e8=flg==2||flg==5;size_t p=1,q=0,r0=0,r1=0,r2=0;\n", o);

    fputs("if(flg>=4){\n", o);
    fputs("size_t us=rd_vl(i,&p);\n", o);
    fputs("int tc[16]={0};unsigned char ts[5];\n", o);
    fputs("int lc[4][16]={{0}};unsigned char ls[4][256];\n", o);
    fputs("int tml,lml;\n", o);
    fputs("{const unsigned char*pp=i+p;tml=rd_tbl(&pp,tc,ts);p+=pp-(i+p);}\n", o);
    fputs("for(int s=0;s<4;s++){const unsigned char*pp=i+p;lml=rd_tbl(&pp,lc[s],ls[s]);p+=pp-(i+p);}\n", o);
    fputs("size_t jump[4];for(int s=0;s<4;s++)jump[s]=(size_t)i[p+s]|((size_t)i[p+4+s]<<8)|((size_t)i[p+8+s]<<16)|((size_t)i[p+12+s]<<24);p+=16;\n", o);
    fputs("size_t bp_lit[4];for(int s=0;s<4;s++)bp_lit[s]=(p+jump[s])*8;\n", o);
    fputs("size_t bp=p*8;\n", o);
    fputs("while(q<us){\n", o);
    fputs("int t=rd_huf(i,&bp,tc,ts,tml);\n", o);
    fputs("if(t==0){int s=q&3;e[q++]=(unsigned char)rd_huf(i,&bp_lit[s],lc[s],ls[s],lml);}\n", o);
    fputs("else if(t>=1 && t<=3){\n", o);
    fputs("int L=rd_bits(i,&bp,8);\n", o);
    fputs("if(L==255)L=(int)(rd_vlb(i,&bp)+(size_t)255);L+=M;int o;\n", o);
    fputs("if(t==1)o=r0;else if(t==2){o=r1;r1=r0;r0=o;}else{o=r2;r2=r1;r1=r0;r0=o;}\n", o);
    fputs("int j;for(j=0;j<L;j++)e[q+j]=e[q+j-o];q+=L;}\n", o);
    fputs("else{int L=rd_bits(i,&bp,8);\n", o);
    fputs("if(L==255)L=(int)(rd_vlb(i,&bp)+(size_t)255);L+=M;\n", o);
    fputs("int o=0,shift=0,b;\n", o);
    fputs("do{b=rd_bits(i,&bp,8);o|=(b&127)<<shift;shift+=7;}while(b&128);\n", o);
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

    fputs("typedef struct{unsigned char*d;int a;int z;}EJ;\n", o);
    fputs("static void *xt(void*p){EJ*j=p;int i;\n", o);
    fputs("size_t o=0;for(i=0;i<j->a;i++)o+=S[i];\n", o);
    fputs("for(i=j->a;i<j->z;i++){\n", o);
    fputs("FILE*f=fopen(N[i],\"wb\");if(!f)return NULL;\n", o);
    fputs("fwrite(j->d+o,1,S[i],f);fclose(f);o+=S[i];puts(N[i]);}return NULL;}\n", o);

    fprintf(o, "int main(void){\nsize_t i,ts=0;\n");
    for (int i = 0; i < nf; i++) fprintf(o, "ts+=S[%d];\n", i);
    fputs("unsigned char*b=malloc(ts);if(!b)return 1;\n", o);
    fprintf(o, "size_t cl=(C+3)/4*4;\n");
    fputs("unsigned char*c=malloc(cl);if(!c){free(b);return 1;}\n", o);
    fputs("b8(D,(C+3)/4*5,c);lz(c,C,b);free(c);\n", o);
    fputs("for(i=0;i<(size_t)F;i++)mkpath(N[i]);\n", o);
    fputs("long nh=1;\n", o);
#ifdef _SC_NPROCESSORS_ONLN
    fputs("#ifdef _SC_NPROCESSORS_ONLN\n", o);
    fputs("nh=sysconf(_SC_NPROCESSORS_ONLN);\n", o);
    fputs("#endif\n", o);
#endif
    fputs("int nt=(int)nh;if(nt<1)nt=1;\n", o);
    fputs("if(nt>F)nt=F;pthread_t*tt=malloc((size_t)nt*sizeof(pthread_t));\n", o);
    fputs("EJ*mj=malloc((size_t)nt*sizeof(EJ));\n", o);
    fputs("int cpf=F/nt;for(i=0;i<nt;i++){\n", o);
    fputs("mj[i].d=b;mj[i].a=i*cpf;mj[i].z=i==nt-1?F:(i+1)*cpf;\n", o);
    fputs("pthread_create(&tt[i],NULL,xt,&mj[i]);}\n", o);
    fputs("for(i=0;i<nt;i++)pthread_join(tt[i],NULL);\n", o);
    fputs("free(tt);free(mj);free(b);return 0;}\n", o);
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
