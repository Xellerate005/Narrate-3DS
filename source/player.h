// player.h - AAC decode thread + NDSP output
#ifndef PLAYER_H
#define PLAYER_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "m4b.h"

int  player_init(void);                 // 0 on success, nonzero = NDSP/DSP firmware problem
void player_exit(void);

int  player_open(const char *path, char *err, size_t errlen);
void player_close(void);
const M4b *player_book(void);           // NULL when nothing is open

void player_play(void);
void player_pause(void);
bool player_is_playing(void);
bool player_ended(void);

void     player_seek_ms(uint64_t ms);
uint64_t player_position_ms(void);
uint64_t player_duration_ms(void);

void player_set_volume(float v);        // 0.0 - 1.0

typedef struct {
    int      load_pct;      // CPU time spent decoding, as % of real time (last ~10 s window); >100 means audio cannot keep up
    int      peak_pct;      // worst single buffer in that window
    unsigned underruns;     // times the audio queue ran completely dry while playing
    bool     is_new3ds;
} PlayerStats;
void player_get_stats(PlayerStats *s);
bool player_is_new3ds(void);

#endif
