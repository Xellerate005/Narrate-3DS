// iowork.h - background SD-card writer, so saving never stalls the UI / animations
#ifndef IOWORK_H
#define IOWORK_H
#include <stddef.h>

void iowork_init(void);
void iowork_exit(void);                                                       // writes everything still queued, then stops
void iowork_flush(void);                                                      // blocks until the queue is empty
// Queues a write of 'data' (len bytes) to 'path' ("w" = replace, "a" = append). Takes ownership of 'data' (must be
// malloc'd). A newer queued "w" job for the same path replaces an older one that hasn't started yet.
void iowork_write(const char *path, const char *mode, char *data, size_t len);

#endif
