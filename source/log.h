// log.h - breadcrumb log on the SD card (sdmc:/3ds/narrate/log.txt); survives crashes
#ifndef LOG_H
#define LOG_H
void log_reset(void);
// Written immediately (use for events right before something might crash).
void nlog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
// Queued to the background writer (use for periodic lines while animating, so the UI never waits for the SD card).
void nlog_async(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#endif
