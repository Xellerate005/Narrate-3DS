// gimage.h - PNG -> GPU texture with sprite-sheet frames, used by custom themes
#ifndef GIMAGE_H
#define GIMAGE_H

#include <3ds.h>
#include <citro2d.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    C3D_Tex tex;
    int w, h;           // image size in pixels (after any down-scaling)
    int tw, th;         // texture size (powers of two)
    int frames;         // sprite-sheet frames, laid out left to right
    float fps;
    bool ok;
} GImage;

bool gimage_load(GImage *g, const char *png_path, int frames, float fps);
void gimage_free(GImage *g);
int  gimage_frame_w(const GImage *g);
// Draws frame 'frame' scaled into the box (x, y, w, h), rotated 'rot_deg' about the box centre.
// flip: 1 = vertical, 2 = horizontal (the theme's image_flip fix). tint: 0 = none, otherwise an RGBA colour (C2D_Color32 packing).
void gimage_draw(const GImage *g, int frame, float x, float y, float w, float h, float rot_deg, float alpha, int flip, uint32_t tint);
size_t gimage_bytes(void);          // texture memory currently used by theme images

#endif
