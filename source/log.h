// log.h - breadcrumb log on the SD card (sdmc:/3ds/narrate/log.txt); survives crashes
#ifndef LOG_H
#define LOG_H
void log_reset(void);
void nlog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#endif
