// wsola.c - Waveform-Similarity Overlap-Add.
//
// Output is produced in hops of H frames. Each hop is a cross-fade between the tail of the previous window and
// the head of the next one; the next window's start is searched (by minimising the sum of absolute differences on a
// 4x decimated mono copy) around a nominal analysis position that advances by H*speed per hop. That keeps the pitch
// while the tempo changes. Cost is a couple of million integer operations per second of audio.
#include "wsola.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define DEC 4

struct Wsola {
    int rate, ch, hop, seek;
    float speed;
    int16_t *in;           // FIFO, interleaved
    int cap, n;            // capacity / frames stored (frames)
    double in_ms0;         // source ms of in[0]
    double pos;            // nominal start of the next window, relative to in[0] (frames)
    double ms_per_frame;
    int16_t *tail;         // hop * ch: second half of the previous window
    bool have_tail;
    int16_t *dtail;        // decimated mono tail
    int32_t *fade;         // Q15 fade-in weights, hop entries
};

Wsola *wsola_new(int rate, int channels) {
    Wsola *w = (Wsola *)calloc(1, sizeof *w);
    if (!w) return NULL;
    w->rate = rate;
    w->ch = channels < 1 ? 1 : (channels > 2 ? 2 : channels);
    w->hop = ((int)(rate * 0.0125) / DEC) * DEC;       // ~12.5 ms hops (25 ms windows)
    if (w->hop < 128) w->hop = 128;
    w->seek = ((int)(rate * 0.0065) / DEC) * DEC;      // +/- ~6.5 ms search radius (> one pitch period of most voices)
    w->cap = w->hop * 24 + w->seek * 2 + 4096;
    w->speed = 1.0f;
    w->ms_per_frame = 1000.0 / rate;
    w->in = (int16_t *)malloc((size_t)w->cap * w->ch * sizeof(int16_t));
    w->tail = (int16_t *)malloc((size_t)w->hop * w->ch * sizeof(int16_t));
    w->dtail = (int16_t *)malloc((size_t)(w->hop / DEC + 1) * sizeof(int16_t));
    w->fade = (int32_t *)malloc((size_t)w->hop * sizeof(int32_t));
    if (!w->in || !w->tail || !w->dtail || !w->fade) { wsola_free(w); return NULL; }
    for (int i = 0; i < w->hop; i++)
        w->fade[i] = (int32_t)(32767.0 * (0.5 - 0.5 * cos(3.14159265358979 * (i + 0.5) / w->hop)));
    return w;
}

void wsola_free(Wsola *w) {
    if (!w) return;
    free(w->in); free(w->tail); free(w->dtail); free(w->fade);
    free(w);
}

void wsola_reset(Wsola *w) {
    w->n = 0;
    w->pos = 0;
    w->have_tail = false;
}

void wsola_set_speed(Wsola *w, float speed) {
    if (speed < 0.5f) speed = 0.5f;
    if (speed > 2.0f) speed = 2.0f;
    w->speed = speed;
}

int wsola_hop(const Wsola *w) { return w->hop; }

static int frames_needed(const Wsola *w) {
    return (int)w->pos + w->seek + 2 * w->hop + DEC + 2;
}

bool wsola_starved(const Wsola *w) { return w->n < frames_needed(w); }

static void drop_old(Wsola *w) {
    int keep_from = (int)w->pos - w->seek - DEC - 2;
    if (keep_from < w->cap / 8) return;                   // amortise the memmove
    if (keep_from > w->n) keep_from = w->n;
    memmove(w->in, w->in + (size_t)keep_from * w->ch, (size_t)(w->n - keep_from) * w->ch * sizeof(int16_t));
    w->n -= keep_from;
    w->pos -= keep_from;
    w->in_ms0 += keep_from * w->ms_per_frame;
}

void wsola_push(Wsola *w, const int16_t *frames, int nframes, double first_ms) {
    if (w->n == 0) w->in_ms0 = first_ms;
    if (w->n + nframes > w->cap) {
        int keep_from = (int)w->pos - w->seek - DEC - 2;
        if (keep_from < 0) keep_from = 0;
        if (keep_from > w->n) keep_from = w->n;
        memmove(w->in, w->in + (size_t)keep_from * w->ch, (size_t)(w->n - keep_from) * w->ch * sizeof(int16_t));
        w->n -= keep_from;
        w->pos -= keep_from;
        w->in_ms0 += keep_from * w->ms_per_frame;
        if (w->n + nframes > w->cap) nframes = w->cap - w->n;          // should never happen; never overflow
    }
    memcpy(w->in + (size_t)w->n * w->ch, frames, (size_t)nframes * w->ch * sizeof(int16_t));
    w->n += nframes;
}

static inline int mono_at(const Wsola *w, const int16_t *p) {
    return w->ch == 2 ? (p[0] + p[1]) >> 7 : p[0] >> 6;      // mono, scaled down so sums stay small
}

static int sad(const Wsola *w, int start) {
    int sum = 0;
    const int m = w->hop / DEC;
    for (int k = 0; k < m; k++) {
        int a = w->dtail[k];
        int b = mono_at(w, w->in + (size_t)(start + k * DEC) * w->ch);
        sum += abs(a - b);
    }
    return sum;
}

int wsola_pull(Wsola *w, int16_t *out, int max_frames, double *src_ms) {
    int produced = 0;
    const int ch = w->ch, hop = w->hop;
    while (produced + hop <= max_frames && w->n >= frames_needed(w)) {
        int s;
        double hop_pos = w->pos;
        if (!w->have_tail) {
            s = (int)w->pos;
            memcpy(out + (size_t)produced * ch, w->in + (size_t)s * ch, (size_t)hop * ch * sizeof(int16_t));
        } else {
            int center = (int)w->pos, lo = center - w->seek, hi = center + w->seek;
            if (lo < 0) lo = 0;
            int best = center < lo ? lo : center, best_cost = 0x7fffffff;
            for (int c = lo; c <= hi; c += DEC) {                      // coarse search
                int cost = sad(w, c);
                if (cost < best_cost) { best_cost = cost; best = c; }
            }
            int rlo = best - (DEC - 1), rhi = best + (DEC - 1);        // refine at single-frame resolution
            if (rlo < lo) rlo = lo;
            for (int c = rlo; c <= rhi; c++) {
                int cost = sad(w, c);
                if (cost < best_cost) { best_cost = cost; best = c; }
            }
            s = best;
            const int16_t *src = w->in + (size_t)s * ch;
            int16_t *dst = out + (size_t)produced * ch;
            for (int i = 0; i < hop; i++) {
                int32_t f = w->fade[i];
                for (int c = 0; c < ch; c++) {
                    int32_t v = ((int32_t)w->tail[i * ch + c] * (32768 - f) + (int32_t)src[i * ch + c] * f) >> 15;
                    dst[i * ch + c] = (int16_t)v;
                }
            }
        }
        // save the second half of this window for the next overlap
        memcpy(w->tail, w->in + (size_t)(s + hop) * ch, (size_t)hop * ch * sizeof(int16_t));
        for (int k = 0; k < hop / DEC; k++) w->dtail[k] = (int16_t)mono_at(w, w->tail + (size_t)k * DEC * ch);
        w->have_tail = true;

        if (produced == 0 && src_ms) *src_ms = w->in_ms0 + hop_pos * w->ms_per_frame;
        produced += hop;
        w->pos += hop * (double)w->speed;
        drop_old(w);
    }
    return produced;
}
