// m4b.c - minimal, portable MP4/M4B demuxer
//
// Understands: moov/trak/mdia/minf/stbl tables (stsd/esds, stsz, stco/co64, stsc, stts),
// Nero 'chpl' chapters, QuickTime text-track chapters (tref/chap), iTunes tags
// (title/artist/album) and cover art (covr).
#include "m4b.h"

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#define FCC(a, b, c, d) (((uint32_t)(uint8_t)(a) << 24) | ((uint32_t)(uint8_t)(b) << 16) | \
                         ((uint32_t)(uint8_t)(c) << 8) | (uint32_t)(uint8_t)(d))

#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "m4b.c expects a little-endian host"
#endif

#define FCC_SOUN FCC('s', 'o', 'u', 'n')
#define FCC_UDTA FCC('u', 'd', 't', 'a')
#define FCC_META FCC('m', 'e', 't', 'a')
#define FCC_MOOV FCC('m', 'o', 'o', 'v')
#define FCC_MDIA FCC('m', 'd', 'i', 'a')
#define FCC_STBL FCC('s', 't', 'b', 'l')
#define FCC_TREF FCC('t', 'r', 'e', 'f')

/* ------------------------------------------------------------------ io */

static uint32_t bs32(uint32_t x) {
    return (x >> 24) | ((x >> 8) & 0xFF00u) | ((x << 8) & 0xFF0000u) | (x << 24);
}

static int rd(M4b *m, void *p, size_t n) {
    if (n == 0) return 1;
    if (fread(p, 1, n, m->f) != n) return 0;
    m->fpos += n;
    return 1;
}

static int sk(M4b *m, uint64_t pos) {
    if (m->fpos == pos) return 1;
    if (fseeko(m->f, (off_t)pos, SEEK_SET) != 0) return 0;
    m->fpos = pos;
    return 1;
}

static int r8(M4b *m, uint8_t *v) { return rd(m, v, 1); }

static int r16(M4b *m, uint16_t *v) {
    uint8_t b[2];
    if (!rd(m, b, 2)) return 0;
    *v = (uint16_t)((b[0] << 8) | b[1]);
    return 1;
}

static int r32(M4b *m, uint32_t *v) {
    uint8_t b[4];
    if (!rd(m, b, 4)) return 0;
    *v = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
    return 1;
}

static int r64(M4b *m, uint64_t *v) {
    uint32_t hi, lo;
    if (!r32(m, &hi) || !r32(m, &lo)) return 0;
    *v = ((uint64_t)hi << 32) | lo;
    return 1;
}

static int skip(M4b *m, uint64_t n) { return sk(m, m->fpos + n); }

static int rd_arr32(M4b *m, uint32_t *dst, uint32_t n) {
    if (!rd(m, dst, (size_t)n * 4)) return 0;
    for (uint32_t i = 0; i < n; i++) dst[i] = bs32(dst[i]);
    return 1;
}

/* --------------------------------------------------------------- helpers */

static void trim(char *s) {
    size_t n = strlen(s);
    while (n && ((unsigned char)s[n - 1] <= ' ')) s[--n] = 0;
    size_t i = 0;
    while (s[i] && (unsigned char)s[i] <= ' ') i++;
    if (i) memmove(s, s + i, strlen(s + i) + 1);
}

static void utf16be_to_utf8(const uint8_t *s, size_t n, char *out, size_t cap) {
    size_t o = 0;
    for (size_t i = 0; i + 1 < n; i += 2) {
        uint32_t c = ((uint32_t)s[i] << 8) | s[i + 1];
        if (c >= 0xD800 && c < 0xDC00 && i + 3 < n) {
            uint32_t d = ((uint32_t)s[i + 2] << 8) | s[i + 3];
            if (d >= 0xDC00 && d < 0xE000) { c = 0x10000 + ((c - 0xD800) << 10) + (d - 0xDC00); i += 2; }
        }
        if (c < 0x80) {
            if (o + 1 >= cap) break;
            out[o++] = (char)c;
        } else if (c < 0x800) {
            if (o + 2 >= cap) break;
            out[o++] = (char)(0xC0 | (c >> 6));
            out[o++] = (char)(0x80 | (c & 63));
        } else if (c < 0x10000) {
            if (o + 3 >= cap) break;
            out[o++] = (char)(0xE0 | (c >> 12));
            out[o++] = (char)(0x80 | ((c >> 6) & 63));
            out[o++] = (char)(0x80 | (c & 63));
        } else {
            if (o + 4 >= cap) break;
            out[o++] = (char)(0xF0 | (c >> 18));
            out[o++] = (char)(0x80 | ((c >> 12) & 63));
            out[o++] = (char)(0x80 | ((c >> 6) & 63));
            out[o++] = (char)(0x80 | (c & 63));
        }
    }
    out[o] = 0;
}

static void free_chapters(M4b *m) {
    for (int i = 0; i < m->nchapters; i++) free(m->chapters[i].title);
    free(m->chapters);
    m->chapters = NULL;
    m->nchapters = 0;
}

static void free_track(M4bTrack *t) {
    free(t->sizes); free(t->chunk_off); free(t->stsc); free(t->stts); free(t->asc);
    memset(t, 0, sizeof *t);
}

static int new_track(M4b *m) {
    if (m->ntracks == m->captracks) {
        int nc = m->captracks ? m->captracks * 2 : 4;
        M4bTrack *p = (M4bTrack *)realloc(m->tracks, (size_t)nc * sizeof *p);
        if (!p) return -1;
        m->tracks = p;
        m->captracks = nc;
    }
    memset(&m->tracks[m->ntracks], 0, sizeof(M4bTrack));
    return m->ntracks++;
}

/* ------------------------------------------------------------ esds / stsd */

static int parse_esds(const uint8_t *p, size_t n, M4bTrack *t) {
    size_t i = 4; // version + flags
    while (i < n) {
        uint8_t tag = p[i++];
        uint32_t len = 0;
        int k = 0;
        uint8_t b;
        do {
            if (i >= n) return 0;
            b = p[i++];
            len = (len << 7) | (b & 0x7F);
        } while ((b & 0x80) && ++k < 4);

        if (tag == 0x03) {          // ES_Descriptor
            if (i + 3 > n) return 0;
            uint8_t fl = p[i + 2];
            i += 3;
            if (fl & 0x80) i += 2;
            if (fl & 0x40) { if (i >= n) return 0; i += 1 + p[i]; }
            if (fl & 0x20) i += 2;
        } else if (tag == 0x04) {   // DecoderConfigDescriptor
            if (i + 13 > n) return 0;
            t->object_type = p[i];
            i += 13;
        } else if (tag == 0x05) {   // DecoderSpecificInfo (AudioSpecificConfig)
            if (len > n - i) len = (uint32_t)(n - i);
            if (len == 0) return 0;
            free(t->asc);
            t->asc = (uint8_t *)malloc(len);
            if (!t->asc) return 0;
            memcpy(t->asc, p + i, len);
            t->asc_len = len;
            return 1;
        } else {
            i += len;
        }
    }
    return 0;
}

static void find_esds(M4b *m, M4bTrack *t, uint64_t pos, uint64_t end, int depth) {
    while (pos + 8 <= end) {
        uint32_t sz, type;
        if (!sk(m, pos) || !r32(m, &sz) || !r32(m, &type)) break;
        if (sz < 8 || pos + sz > end) break;
        if (type == FCC('e', 's', 'd', 's')) {
            uint32_t n = sz - 8;
            if (n > 0 && n < 2048) {
                uint8_t buf[2048];
                if (rd(m, buf, n)) parse_esds(buf, n, t);
            }
        } else if (type == FCC('w', 'a', 'v', 'e') && depth < 2) {
            find_esds(m, t, pos + 8, pos + sz, depth + 1);
        }
        pos += sz;
    }
}

static void parse_stsd(M4b *m, M4bTrack *t, uint64_t body, uint64_t end) {
    uint32_t vf, cnt, esz, etype;
    if (!sk(m, body) || !r32(m, &vf) || !r32(m, &cnt) || cnt == 0) return;
    uint64_t epos = body + 8;
    if (!r32(m, &esz) || !r32(m, &etype)) return;
    t->codec = etype;
    if (t->handler != FCC_SOUN || etype != FCC('m', 'p', '4', 'a')) return;
    if (esz < 36 || epos + esz > end) esz = (uint32_t)(end - epos);
    uint16_t qtver = 0, ch = 0;
    uint32_t rate = 0;
    if (!skip(m, 8) || !r16(m, &qtver) || !skip(m, 6) || !r16(m, &ch) || !skip(m, 6) || !r32(m, &rate)) return;
    t->channels = ch;
    t->samplerate = rate >> 16;
    uint64_t cpos = epos + 8 + 28;
    if (qtver == 1) cpos += 16;
    if (cpos < epos + esz) find_esds(m, t, cpos, epos + esz, 0);
}

/* ------------------------------------------------------------- sample tables */

static int parse_stsz(M4b *m, M4bTrack *t, uint64_t body) {
    uint32_t vf, size, count;
    if (!sk(m, body) || !r32(m, &vf) || !r32(m, &size) || !r32(m, &count)) return 0;
    if (count > 50u * 1000 * 1000) return 0;
    free(t->sizes);
    t->sizes = NULL;
    t->nsamples = count;
    t->const_size = size;
    if (size == 0 && count) {
        t->sizes = (uint32_t *)malloc((size_t)count * 4);
        if (!t->sizes) return -1;
        if (!rd_arr32(m, t->sizes, count)) { free(t->sizes); t->sizes = NULL; t->nsamples = 0; return 0; }
    }
    return 1;
}

static int parse_stco(M4b *m, M4bTrack *t, uint64_t body, int is64) {
    uint32_t vf, count;
    if (!sk(m, body) || !r32(m, &vf) || !r32(m, &count)) return 0;
    if (count > 50u * 1000 * 1000) return 0;
    free(t->chunk_off);
    t->chunk_off = (uint64_t *)malloc((size_t)(count ? count : 1) * 8);
    if (!t->chunk_off) return -1;
    t->nchunks = 0;
    if (is64) {
        for (uint32_t i = 0; i < count; i++) {
            uint64_t v;
            if (!r64(m, &v)) return 0;
            t->chunk_off[i] = v;
            t->nchunks++;
        }
    } else {
        uint32_t tmp[1024];
        uint32_t done = 0;
        while (done < count) {
            uint32_t n = count - done > 1024 ? 1024 : count - done;
            if (!rd_arr32(m, tmp, n)) return 0;
            for (uint32_t i = 0; i < n; i++) t->chunk_off[done + i] = tmp[i];
            done += n;
            t->nchunks = done;
        }
    }
    return 1;
}

static int parse_stsc(M4b *m, M4bTrack *t, uint64_t body) {
    uint32_t vf, count;
    if (!sk(m, body) || !r32(m, &vf) || !r32(m, &count)) return 0;
    if (count > 10u * 1000 * 1000) return 0;
    free(t->stsc);
    t->stsc = (M4bStsc *)malloc((size_t)(count ? count : 1) * sizeof(M4bStsc));
    if (!t->stsc) return -1;
    t->nstsc = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t a, b, c;
        if (!r32(m, &a) || !r32(m, &b) || !r32(m, &c)) return 0;
        t->stsc[i].first_chunk = a;
        t->stsc[i].spc = b;
        t->nstsc++;
    }
    return 1;
}

static int parse_stts(M4b *m, M4bTrack *t, uint64_t body) {
    uint32_t vf, count;
    if (!sk(m, body) || !r32(m, &vf) || !r32(m, &count)) return 0;
    if (count > 10u * 1000 * 1000) return 0;
    free(t->stts);
    t->stts = (M4bStts *)malloc((size_t)(count ? count : 1) * sizeof(M4bStts));
    if (!t->stts) return -1;
    t->nstts = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t a, b;
        if (!r32(m, &a) || !r32(m, &b)) return 0;
        t->stts[i].count = a;
        t->stts[i].delta = b;
        t->nstts++;
    }
    return 1;
}

/* ----------------------------------------------------------------- tags */

static void parse_chpl(M4b *m, uint64_t body) {
    uint8_t ver, fl[3], cnt;
    uint32_t unk;
    if (!sk(m, body) || !r8(m, &ver) || !rd(m, fl, 3) || !r32(m, &unk) || !r8(m, &cnt) || cnt == 0) return;
    M4bChapter *ch = (M4bChapter *)calloc(cnt, sizeof *ch);
    if (!ch) return;
    int n = 0;
    for (int i = 0; i < cnt; i++) {
        uint64_t st;
        uint8_t len;
        char buf[256];
        if (!r64(m, &st) || !r8(m, &len)) break;
        if (len && !rd(m, buf, len)) break;
        buf[len] = 0;
        trim(buf);
        ch[n].start_ms = st / 10000;
        ch[n].title = strdup(buf);
        n++;
    }
    if (n > m->nchapters) {
        free_chapters(m);
        m->chapters = ch;
        m->nchapters = n;
    } else {
        for (int i = 0; i < n; i++) free(ch[i].title);
        free(ch);
    }
}

static void set_tag(char *dst, size_t cap, M4b *m, uint32_t plen) {
    uint32_t n = plen < cap - 1 ? plen : (uint32_t)(cap - 1);
    if (!rd(m, dst, n)) { dst[0] = 0; return; }
    dst[n] = 0;
    trim(dst);
}

static void parse_ilst(M4b *m, uint64_t pos, uint64_t end) {
    while (pos + 8 <= end) {
        uint32_t sz, type;
        if (!sk(m, pos) || !r32(m, &sz) || !r32(m, &type)) break;
        if (sz < 8 || pos + sz > end) break;
        uint64_t ipos = pos + 8, iend = pos + sz;
        while (ipos + 16 <= iend) {
            uint32_t dsz, dtype;
            if (!sk(m, ipos) || !r32(m, &dsz) || !r32(m, &dtype)) break;
            if (dsz < 16 || ipos + dsz > iend) break;
            if (dtype == FCC('d', 'a', 't', 'a')) {
                uint32_t flags, loc;
                if (!r32(m, &flags) || !r32(m, &loc)) break;
                uint32_t plen = dsz - 16;
                if (type == FCC(0xA9, 'n', 'a', 'm')) set_tag(m->title, sizeof m->title, m, plen);
                else if (type == FCC(0xA9, 'A', 'R', 'T')) set_tag(m->artist, sizeof m->artist, m, plen);
                else if (type == FCC('a', 'A', 'R', 'T')) { if (!m->artist[0]) set_tag(m->artist, sizeof m->artist, m, plen); }
                else if (type == FCC(0xA9, 'a', 'l', 'b')) set_tag(m->album, sizeof m->album, m, plen);
                else if (type == FCC('c', 'o', 'v', 'r')) {
                    if (!m->cover && plen > 16 && plen <= 12u * 1024 * 1024) {
                        uint8_t *c = (uint8_t *)malloc(plen);
                        if (c && rd(m, c, plen)) { m->cover = c; m->cover_len = plen; }
                        else free(c);
                    }
                }
            }
            ipos += dsz;
        }
        pos += sz;
    }
}

/* ------------------------------------------------------------------ walk */

static int walk(M4b *m, uint64_t pos, uint64_t end, int trak, uint32_t parent, int depth) {
    if (depth > 12) return 0;
    while (pos + 8 <= end) {
        uint32_t s32, type;
        if (!sk(m, pos) || !r32(m, &s32) || !r32(m, &type)) break;
        uint64_t size = s32, hdr = 8;
        if (s32 == 1) {
            uint64_t ls;
            if (!r64(m, &ls)) break;
            size = ls;
            hdr = 16;
        } else if (s32 == 0) {
            size = end - pos;
        }
        if (size < hdr) break;
        if (pos + size > end) size = end - pos;
        uint64_t body = pos + hdr, bend = pos + size;
        M4bTrack *t = trak >= 0 ? &m->tracks[trak] : NULL;

        switch (type) {
        case FCC_MOOV: case FCC_MDIA: case FCC('m', 'i', 'n', 'f'): case FCC_STBL:
        case FCC_UDTA: case FCC_TREF:
            if (walk(m, body, bend, trak, type, depth + 1) < 0) return -1;
            break;
        case FCC('t', 'r', 'a', 'k'): {
            int idx = new_track(m);
            if (idx < 0) return -1;
            if (walk(m, body, bend, idx, type, depth + 1) < 0) return -1;
            break;
        }
        case FCC('t', 'k', 'h', 'd'):
            if (t) {
                uint8_t ver;
                uint32_t id;
                if (sk(m, body) && r8(m, &ver) && skip(m, 3) && skip(m, ver == 1 ? 16 : 8) && r32(m, &id)) t->id = id;
            }
            break;
        case FCC('m', 'd', 'h', 'd'):
            if (t && parent == FCC_MDIA) {
                uint8_t ver;
                uint32_t ts, d32;
                uint64_t d64;
                if (sk(m, body) && r8(m, &ver) && skip(m, 3)) {
                    if (ver == 1) {
                        if (skip(m, 16) && r32(m, &ts) && r64(m, &d64)) { t->timescale = ts; t->duration = d64; }
                    } else {
                        if (skip(m, 8) && r32(m, &ts) && r32(m, &d32)) { t->timescale = ts; t->duration = d32; }
                    }
                }
            }
            break;
        case FCC('h', 'd', 'l', 'r'):
            if (t && parent == FCC_MDIA) {
                uint32_t h;
                if (sk(m, body) && skip(m, 8) && r32(m, &h)) t->handler = h;
            }
            break;
        case FCC('c', 'h', 'a', 'p'):
            if (t && parent == FCC_TREF) {
                uint32_t id;
                if (sk(m, body) && r32(m, &id)) t->chap_ref = id;
            }
            break;
        case FCC('s', 't', 's', 'd'): if (t && parent == FCC_STBL) parse_stsd(m, t, body, bend); break;
        case FCC('s', 't', 's', 'z'): if (t && parent == FCC_STBL) { if (parse_stsz(m, t, body) < 0) return -1; } break;
        case FCC('s', 't', 'c', 'o'): if (t && parent == FCC_STBL) { if (parse_stco(m, t, body, 0) < 0) return -1; } break;
        case FCC('c', 'o', '6', '4'): if (t && parent == FCC_STBL) { if (parse_stco(m, t, body, 1) < 0) return -1; } break;
        case FCC('s', 't', 's', 'c'): if (t && parent == FCC_STBL) { if (parse_stsc(m, t, body) < 0) return -1; } break;
        case FCC('s', 't', 't', 's'): if (t && parent == FCC_STBL) { if (parse_stts(m, t, body) < 0) return -1; } break;
        case FCC('c', 'h', 'p', 'l'):
            if (parent == FCC_UDTA) parse_chpl(m, body);
            break;
        case FCC_META:
            if (parent == FCC_UDTA || parent == FCC_MOOV) {
                uint32_t v;
                if (sk(m, body) && r32(m, &v)) {
                    uint64_t c = (v == 0) ? body + 4 : body;   // ISO 'meta' is a full box, QuickTime's is not
                    if (walk(m, c, bend, trak, type, depth + 1) < 0) return -1;
                }
            }
            break;
        case FCC('i', 'l', 's', 't'):
            if (parent == FCC_META) parse_ilst(m, body, bend);
            break;
        default:
            break;
        }
        pos = bend;
    }
    return 0;
}

/* ----------------------------------------------------- sample addressing */

#define SZ(t, i) ((t)->sizes ? (t)->sizes[i] : (t)->const_size)

uint32_t m4b_time_to_sample(const M4bTrack *t, uint64_t ms) {
    if (!t->nsamples || !t->timescale) return 0;
    uint64_t target = ms * t->timescale / 1000;
    uint64_t acc_t = 0, acc_s = 0;
    for (uint32_t i = 0; i < t->nstts; i++) {
        uint64_t run_t = (uint64_t)t->stts[i].count * t->stts[i].delta;
        if (t->stts[i].delta && target < acc_t + run_t) {
            uint64_t s = acc_s + (target - acc_t) / t->stts[i].delta;
            return s >= t->nsamples ? t->nsamples - 1 : (uint32_t)s;
        }
        acc_t += run_t;
        acc_s += t->stts[i].count;
    }
    return t->nsamples - 1;
}

uint64_t m4b_sample_to_ms(const M4bTrack *t, uint32_t sample) {
    if (!t->timescale) return 0;
    uint64_t acc_t = 0, remaining = sample;
    for (uint32_t i = 0; i < t->nstts && remaining; i++) {
        uint64_t n = t->stts[i].count < remaining ? t->stts[i].count : remaining;
        acc_t += n * t->stts[i].delta;
        remaining -= n;
    }
    return acc_t * 1000 / t->timescale;
}

int m4b_seek(M4b *m, M4bTrack *t, uint32_t sample, M4bCursor *c) {
    (void)m;
    if (sample >= t->nsamples || !t->nstsc || !t->nchunks) return -1;
    uint64_t base = 0;
    for (uint32_t i = 0; i < t->nstsc; i++) {
        uint32_t first = t->stsc[i].first_chunk, spc = t->stsc[i].spc;
        uint32_t next = (i + 1 < t->nstsc) ? t->stsc[i + 1].first_chunk : t->nchunks + 1;
        if (next <= first || spc == 0 || first == 0) continue;
        uint64_t run_samples = (uint64_t)(next - first) * spc;
        if (sample < base + run_samples) {
            uint32_t rel = (uint32_t)(sample - base);
            uint32_t chunk = first - 1 + rel / spc, in = rel % spc;
            if (chunk >= t->nchunks) return -1;
            uint64_t off = t->chunk_off[chunk];
            for (uint32_t k = sample - in; k < sample; k++) off += SZ(t, k);
            c->sample = sample; c->chunk = chunk; c->in_chunk = in; c->run = i; c->off = off;
            return 0;
        }
        base += run_samples;
    }
    return -1;
}

int m4b_read(M4b *m, M4bTrack *t, M4bCursor *c, uint8_t *buf, uint32_t cap, uint32_t *len) {
    if (c->sample >= t->nsamples) return -1;
    uint32_t sz = SZ(t, c->sample);
    int rc = 0;
    if (sz > cap) {
        rc = -2;
    } else {
        if (!sk(m, c->off) || !rd(m, buf, sz)) return -1;
    }
    *len = sz;
    c->off += sz;
    c->sample++;
    c->in_chunk++;
    uint32_t spc = t->stsc[c->run].spc;
    if (c->in_chunk >= spc) {
        c->chunk++;
        c->in_chunk = 0;
        if (c->chunk < t->nchunks) {
            c->off = t->chunk_off[c->chunk];
            while (c->run + 1 < t->nstsc && c->chunk + 1 >= t->stsc[c->run + 1].first_chunk) c->run++;
        }
    }
    return rc;
}

int m4b_chapter_at(const M4b *m, uint64_t ms) {
    if (m->nchapters <= 1) return 0;
    int lo = 0, hi = m->nchapters - 1;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (m->chapters[mid].start_ms <= ms) lo = mid; else hi = mid - 1;
    }
    return lo;
}

/* --------------------------------------------------- text-track chapters */

static void load_text_chapters(M4b *m, M4bTrack *ct) {
    uint32_t n = ct->nsamples;
    if (n == 0 || n > 20000 || !ct->timescale || !ct->nstts) return;
    M4bChapter *ch = (M4bChapter *)calloc(n, sizeof *ch);
    if (!ch) return;
    M4bCursor c;
    if (m4b_seek(m, ct, 0, &c) != 0) { free(ch); return; }

    uint64_t t_units = 0;
    uint32_t run = 0, in_run = 0;
    int cnt = 0;
    uint8_t buf[1024];
    for (uint32_t i = 0; i < n; i++) {
        uint32_t len = 0;
        int rc = m4b_read(m, ct, &c, buf, sizeof buf, &len);
        if (rc == -1) break;
        char title[256];
        title[0] = 0;
        if (rc == 0 && len >= 2) {
            uint32_t tl = ((uint32_t)buf[0] << 8) | buf[1];
            if (tl > len - 2) tl = len - 2;
            const uint8_t *tx = buf + 2;
            if (tl >= 2 && tx[0] == 0xFE && tx[1] == 0xFF) utf16be_to_utf8(tx + 2, tl - 2, title, sizeof title);
            else { if (tl > sizeof title - 1) tl = sizeof title - 1; memcpy(title, tx, tl); title[tl] = 0; }
            trim(title);
        }
        if (!title[0]) snprintf(title, sizeof title, "Chapter %d", cnt + 1);
        ch[cnt].start_ms = t_units * 1000 / ct->timescale;
        ch[cnt].title = strdup(title);
        cnt++;

        while (run < ct->nstts && in_run >= ct->stts[run].count) { run++; in_run = 0; }
        uint32_t delta = run < ct->nstts ? ct->stts[run].delta : 0;
        in_run++;
        t_units += delta;
    }
    if (cnt > m->nchapters) {
        free_chapters(m);
        m->chapters = ch;
        m->nchapters = cnt;
    } else {
        for (int i = 0; i < cnt; i++) free(ch[i].title);
        free(ch);
    }
}

/* ------------------------------------------------------------ open/close */

M4bTrack *m4b_audio(M4b *m) { return m->audio >= 0 ? &m->tracks[m->audio] : NULL; }

static void seterr(char *err, size_t n, const char *s) {
    if (err && n) snprintf(err, n, "%s", s);
}

int m4b_open(M4b *m, const char *path, char *err, size_t errlen) {
    memset(m, 0, sizeof *m);
    m->audio = -1;
    m->f = fopen(path, "rb");
    if (!m->f) { seterr(err, errlen, "Could not open file"); return -1; }
    setvbuf(m->f, NULL, _IOFBF, 65536);
    if (fseeko(m->f, 0, SEEK_END) != 0) { seterr(err, errlen, "Seek failed"); goto fail; }
    m->fsize = (uint64_t)ftello(m->f);
    if (fseeko(m->f, 0, SEEK_SET) != 0) { seterr(err, errlen, "Seek failed"); goto fail; }
    m->fpos = 0;

    if (walk(m, 0, m->fsize, -1, 0, 0) < 0) { seterr(err, errlen, "Out of memory reading file index"); goto fail; }

    for (int i = 0; i < m->ntracks; i++) {
        M4bTrack *t = &m->tracks[i];
        if (t->handler == FCC_SOUN && t->nsamples && t->asc && t->nstsc && t->nchunks && t->nstts && t->timescale) {
            m->audio = i;
            break;
        }
    }
    if (m->audio < 0) {
        int has_audio = 0, other_codec = 0;
        for (int i = 0; i < m->ntracks; i++)
            if (m->tracks[i].handler == FCC_SOUN) {
                has_audio = 1;
                if (m->tracks[i].codec != FCC('m', 'p', '4', 'a')) other_codec = 1;
            }
        if (other_codec) seterr(err, errlen, "Unsupported audio codec (AAC only)");
        else if (has_audio) seterr(err, errlen, "Audio track is damaged or empty");
        else seterr(err, errlen, "No audio track found (is this an M4B?)");
        goto fail;
    }
    M4bTrack *a = &m->tracks[m->audio];
    if (a->object_type != 0x40 && a->object_type != 0x66 && a->object_type != 0x67 && a->object_type != 0x68) {
        seterr(err, errlen, "Unsupported audio codec (AAC only)");
        goto fail;
    }

    m->duration_ms = m4b_sample_to_ms(a, a->nsamples);

    if (a->chap_ref) {
        for (int i = 0; i < m->ntracks; i++)
            if (i != m->audio && m->tracks[i].id == a->chap_ref) { load_text_chapters(m, &m->tracks[i]); break; }
    }

    // drop everything except the audio track's tables
    for (int i = 0; i < m->ntracks; i++)
        if (i != m->audio) free_track(&m->tracks[i]);

    // sort + sanitize chapters
    for (int i = 1; i < m->nchapters; i++) {
        M4bChapter k = m->chapters[i];
        int j = i - 1;
        while (j >= 0 && m->chapters[j].start_ms > k.start_ms) { m->chapters[j + 1] = m->chapters[j]; j--; }
        m->chapters[j + 1] = k;
    }
    {
        int w = 0;
        for (int i = 0; i < m->nchapters; i++) {
            if (i > 0 && m->duration_ms && m->chapters[i].start_ms >= m->duration_ms) { free(m->chapters[i].title); continue; }
            m->chapters[w++] = m->chapters[i];
        }
        m->nchapters = w;
    }
    if (m->nchapters == 0) {
        m->chapters = (M4bChapter *)calloc(1, sizeof(M4bChapter));
        if (!m->chapters) { seterr(err, errlen, "Out of memory"); goto fail; }
        m->chapters[0].title = strdup("Full book");
        m->chapters[0].start_ms = 0;
        m->nchapters = 1;
    }
    if (!m->title[0]) {
        const char *s = strrchr(path, '/');
        s = s ? s + 1 : path;
        snprintf(m->title, sizeof m->title, "%s", s);
        char *dot = strrchr(m->title, '.');
        if (dot) *dot = 0;
    }
    return 0;

fail:
    m4b_close(m);
    return -1;
}

void m4b_close(M4b *m) {
    if (m->f) fclose(m->f);
    for (int i = 0; i < m->ntracks; i++) free_track(&m->tracks[i]);
    free(m->tracks);
    free_chapters(m);
    free(m->cover);
    memset(m, 0, sizeof *m);
    m->audio = -1;
}
