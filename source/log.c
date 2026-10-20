#include "log.h"
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
