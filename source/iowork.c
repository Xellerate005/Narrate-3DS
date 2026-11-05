// iowork.c
#include "iowork.h"

#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define QN 8

typedef struct { char path[160]; char mode[4]; char *data; size_t len; } Job;

static Job g_q[QN];
static volatile int g_qn;
static int g_qh;
static LightLock g_lock;
static LightEvent g_ev;
static Thread g_th;
static volatile bool g_quit, g_busy;

static void do_write(const char *path, const char *mode, const char *data, size_t len) {
    FILE *f = fopen(path, mode);
    if (!f) return;
    if (len) fwrite(data, 1, len, f);
    fclose(f);
}

static void worker(void *arg) {
    (void)arg;
    for (;;) {
        LightEvent_Wait(&g_ev);
        for (;;) {
            LightLock_Lock(&g_lock);
            if (g_qn == 0) { LightLock_Unlock(&g_lock); break; }
            Job j = g_q[g_qh];
            g_qh = (g_qh + 1) % QN;
            g_qn--;
            g_busy = true;
            LightLock_Unlock(&g_lock);
            do_write(j.path, j.mode, j.data, j.len);
            free(j.data);
            g_busy = false;
        }
        if (g_quit) break;
    }
}

void iowork_init(void) {
    LightLock_Init(&g_lock);
    LightEvent_Init(&g_ev, RESET_ONESHOT);
    g_quit = false;
    s32 prio = 0x30;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    prio += 6;                                    // lower priority than the UI thread
    if (prio > 0x3F) prio = 0x3F;
    g_th = threadCreate(worker, NULL, 32 * 1024, prio, -1, false);
}

void iowork_flush(void) {
    if (!g_th) return;
    while (g_qn > 0 || g_busy) svcSleepThread(2 * 1000 * 1000LL);
}

void iowork_exit(void) {
    if (!g_th) return;
    iowork_flush();
    g_quit = true;
    LightEvent_Signal(&g_ev);
    threadJoin(g_th, U64_MAX);
    threadFree(g_th);
    g_th = NULL;
}

void iowork_write(const char *path, const char *mode, char *data, size_t len) {
    if (!g_th) {                                  // no worker: write right here
        do_write(path, mode, data, len);
        free(data);
        return;
    }
    LightLock_Lock(&g_lock);
    if (mode[0] == 'w') {
        for (int i = 0; i < g_qn; i++) {
            Job *j = &g_q[(g_qh + i) % QN];
            if (j->mode[0] == 'w' && strcmp(j->path, path) == 0) {   // newer save supersedes the one still waiting
                free(j->data);
                j->data = data;
                j->len = len;
                LightLock_Unlock(&g_lock);
                LightEvent_Signal(&g_ev);
                return;
            }
        }
    }
    if (g_qn == QN) {                             // queue full: fall back to a direct write
        LightLock_Unlock(&g_lock);
        do_write(path, mode, data, len);
        free(data);
        return;
    }
    Job *j = &g_q[(g_qh + g_qn) % QN];
    snprintf(j->path, sizeof j->path, "%s", path);
    snprintf(j->mode, sizeof j->mode, "%s", mode);
    j->data = data;
    j->len = len;
    g_qn++;
    LightLock_Unlock(&g_lock);
    LightEvent_Signal(&g_ev);
}
