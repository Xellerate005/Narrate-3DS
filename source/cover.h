// cover.h - decode embedded JPEG/PNG cover art into a GPU texture; shared flip/rotate orientation
#ifndef COVER_H
#define COVER_H

#include <3ds.h>
#include <citro2d.h>
#include <stdbool.h>
#include <stdint.h>

bool cover_load(const uint8_t *data, uint32_t len, const char *book_path);   // also stores a shelf thumbnail
void cover_free(void);
bool cover_ready(void);
void cover_draw(float x, float y, float size);                        // top-left, square
void cover_draw_centered(float cx, float cy, float size, float extra_rad);   // centred, with an extra rotation (vinyl label)
void cover_draw_image(const C2D_Image *img, float x, float y, float size);   // any image (thumbnails), same orientation
void cover_set_orient(int flip, int rot);                             // flip 0..3 (none/vertical/horizontal/both), rot 0..3 (x90 degrees)
bool cover_accent(u8 *r, u8 *g, u8 *b);                               // dominant colour, boosted; false if cover is grey
bool cover_tex_from_rgba(C3D_Tex *tex, int size, const uint8_t *rgba);

#endif
