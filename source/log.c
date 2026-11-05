#include "log.h"
#include "iowork.h"
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>

#define LOG_FILE "sdmc:/3ds/narrate/log.txt"

void log_reset(void) {
    FILE *f = fopen(LOG_FILE, "w");
    if (f) fclose(f);
}

// Opens, writes and closes every time so the line is on the card even if we crash right after.
void nlog(const char *fmt, ...) {
    FILE *f = fopen(LOG_FILE, "a");
    if (!f) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

void nlog_async(const char *fmt, ...) {
    char *buf = (char *)malloc(300);
    if (!buf) return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, 299, fmt, ap);
    va_end(ap);
    if (n < 0) { free(buf); return; }
    if (n > 298) n = 298;
    buf[n++] = '\n';
    iowork_write(LOG_FILE, "a", buf, (size_t)n);
}
