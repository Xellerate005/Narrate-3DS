// cover.h - decode embedded JPEG/PNG cover art into a GPU texture
#ifndef COVER_H
#define COVER_H

#include <3ds.h>
#include <citro2d.h>
#include <stdbool.h>
#include <stdint.h>

bool cover_load(const uint8_t *data, uint32_t len);   // true on success
void cover_free(void);
bool cover_ready(void);
void cover_draw(float x, float y, float size);
void cover_set_orient(int flip, int rot);              // flip 0..3 (none/vertical/horizontal/both), rot 0..3 (x90 degrees)        // draws the (square) cover scaled to 'size'
bool cover_accent(u8 *r, u8 *g, u8 *b);               // dominant colour, boosted; false if cover is grey

#endif
