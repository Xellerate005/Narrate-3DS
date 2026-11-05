// wsola.h - pitch-preserving time stretch (WSOLA) for 16-bit interleaved PCM, used for playback speed
#ifndef WSOLA_H
#define WSOLA_H

#include <stdbool.h>
#include <stdint.h>

typedef struct Wsola Wsola;

Wsola *wsola_new(int rate, int channels);
void   wsola_free(Wsola *w);
void   wsola_reset(Wsola *w);                    // forget all buffered audio (after a seek)
void   wsola_set_speed(Wsola *w, float speed);   // 0.5 .. 2.0 (1.0 = unchanged); may change at any time
// Feed decoded audio. first_ms = source time (ms) of the first frame of this block.
void   wsola_push(Wsola *w, const int16_t *frames, int nframes, double first_ms);
// Produce output in whole hops. Returns frames written (0 = needs more input). *src_ms = source time of the first
// output frame, so the caller can keep an exact book position while the audio plays faster or slower.
int    wsola_pull(Wsola *w, int16_t *out, int max_frames, double *src_ms);
int    wsola_hop(const Wsola *w);
bool   wsola_starved(const Wsola *w);

#endif
