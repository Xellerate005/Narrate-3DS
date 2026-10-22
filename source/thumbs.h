// thumbs.h - small cover thumbnails for the library shelf (saved when a book is opened, cached on the SD card)
#ifndef THUMBS_H
#define THUMBS_H

#include <3ds.h>
#include <citro2d.h>
#include <stdbool.h>
#include <stdint.h>

void thumbs_init(void);
void thumbs_exit(void);
void thumb_save(const char *book_path, const uint8_t *rgba256);   // rgba256: 256x256 RGBA, top-down
const C2D_Image *thumb_get(const char *book_path);                // NULL when there is no thumbnail (yet)
void thumbs_new_frame(void);                                      // call once per drawn frame
bool thumbs_pending(void);                                        // true if loads were deferred: draw again soon

#endif
