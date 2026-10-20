// m4b.h - minimal, portable MP4/M4B demuxer (AAC audio + chapters + tags + cover)
#ifndef M4B_H
#define M4B_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

typedef struct { uint32_t first_chunk, spc; } M4bStsc;
typedef struct { uint32_t count, delta; } M4bStts;
typedef struct { char *title; uint64_t start_ms; } M4bChapter;

typedef struct {
    uint32_t id, handler, codec, timescale, chap_ref;
    uint64_t duration;
    uint32_t nsamples, const_size;
    uint32_t *sizes;
    uint32_t nchunks;  uint64_t *chunk_off;
    uint32_t nstsc;    M4bStsc *stsc;
    uint32_t nstts;    M4bStts *stts;
    uint8_t *asc;      uint32_t asc_len;
    uint32_t channels, samplerate;
    uint8_t  object_type;
} M4bTrack;

typedef struct {
    FILE *f;
    uint64_t fpos, fsize;
    M4bTrack *tracks;
    int ntracks, captracks, audio;
    char title[256], artist[256], album[256];
    uint8_t *cover;  uint32_t cover_len;
    M4bChapter *chapters;  int nchapters;
    uint64_t duration_ms;
} M4b;

typedef struct { uint32_t sample, chunk, in_chunk, run; uint64_t off; } M4bCursor;

// Returns 0 on success; on failure returns -1 and writes a message into err.
int       m4b_open(M4b *m, const char *path, char *err, size_t errlen);
void      m4b_close(M4b *m);
M4bTrack *m4b_audio(M4b *m);

uint32_t  m4b_time_to_sample(const M4bTrack *t, uint64_t ms);
uint64_t  m4b_sample_to_ms(const M4bTrack *t, uint32_t sample);

int       m4b_seek(M4b *m, M4bTrack *t, uint32_t sample, M4bCursor *c);
// 0 = ok, -1 = I/O error / end, -2 = sample larger than buf (cursor still advanced)
int       m4b_read(M4b *m, M4bTrack *t, M4bCursor *c, uint8_t *buf, uint32_t cap, uint32_t *len);

int       m4b_chapter_at(const M4b *m, uint64_t ms);

#endif
