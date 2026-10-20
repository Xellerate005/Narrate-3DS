// player.c - AAC decoding (FAAD2) on a worker thread, streamed to NDSP channel 0.
#include "player.h"
#include "log.h"

#include <3ds.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#if defined(__has_include)
#  if __has_include(<neaacdec.h>)
#    include <neaacdec.h>
#  else
#    include <faad.h>
#  endif
#else
#  include <faad.h>
#endif

#define NBUF         12       // ~1.1 s of audio queued at 44.1 kHz: rides out SD-card / OS hiccups
#define FILL_FRAMES  4096     // target PCM frames per NDSP buffer
#define FILL_FIRST   1024     // the first buffer after open/seek is short so sound starts quickly
#define BUF_CAP      8192     // hard capacity per buffer (HE-AAC frames can be 2048 frames)
#define SBUF_SIZE    32768    // max encoded AAC frame we accept
#define THREAD_STACK (256 * 1024)

static LightLock     g_lock;
static Thread        g_thread;
static volatile bool g_quit;
static bool          g_ready;

static M4b           g_mp4;
static bool          g_open;
static M4bCursor     g_cur;
static NeAACDecHandle g_dec;
static u8           *g_sbuf;

static volatile bool g_playing;
static volatile bool g_eof;
static int           g_discard;
static int           g_errs;
static int           g_logged;
static int           g_need_format;
static volatile int  g_want;          // main thread is waiting for g_lock: the audio thread should yield soon
static int           g_warm;          // warm-up buffers queued since open/seek (the first two are short)
static int           g_started;       // at least one buffer queued since open/seek
static bool          g_was_empty;
static bool          g_is_new;
static u64           g_load_us, g_win_ms;
static u32           g_peak_pct, g_underruns, g_last_load_pct, g_last_peak_pct;

#ifndef SYSCLOCK_ARM11
#define SYSCLOCK_ARM11 268111856ULL
#endif

// Take the player lock; the audio thread notices g_want and finishes its current frame early.
static void lock_player(void) {
    g_want++;
    LightLock_Lock(&g_lock);
    g_want--;
}


static u32           g_rate = 44100;
static u32           g_channels = 2;

static ndspWaveBuf   g_wb[NBUF];
static s16          *g_pcm[NBUF];
static u64           g_buf_ms[NBUF];
static u32           g_buf_dur[NBUF];

static volatile u64  g_last_ms;       // end of the most recently queued audio / seek target
static u64           g_anchor_ms = (u64)-1;
static u64           g_anchor_time;
static u64           g_frozen_elapsed;

static bool buffers_active(void) {
    for (int i = 0; i < NBUF; i++) {
        u8 st = g_wb[i].status;
        if (st == NDSP_WBUF_QUEUED || st == NDSP_WBUF_PLAYING) return true;
    }
    return false;
}

/* ------------------------------------------------------------- channel setup */

static void setup_channel(void) {
    ndspChnReset(0);
    ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
    ndspChnSetRate(0, (float)g_rate);
    ndspChnSetFormat(0, g_channels == 1 ? NDSP_FORMAT_MONO_PCM16 : NDSP_FORMAT_STEREO_PCM16);
    float mix[12] = {0};
    mix[0] = 1.0f;
    mix[1] = 1.0f;
    ndspChnSetMix(0, mix);
    for (int i = 0; i < NBUF; i++) memset(&g_wb[i], 0, sizeof g_wb[i]);
}

static void reset_position_tracking(void) {
    g_anchor_ms = (u64)-1;
    g_frozen_elapsed = 0;
}

/* ------------------------------------------------------------------ decoding */

static u32 append_pcm(s16 *out, u32 have, const s16 *in, u32 nf, u32 in_ch) {
    if (have + nf > BUF_CAP) nf = BUF_CAP - have;
    s16 *o = out + (size_t)have * g_channels;
    if (in_ch == g_channels) {
        memcpy(o, in, (size_t)nf * g_channels * sizeof(s16));
    } else {
        for (u32 i = 0; i < nf; i++) {
            s16 l = in[i * in_ch];
            s16 r = in_ch > 1 ? in[i * in_ch + 1] : l;
            if (g_channels == 2) { o[2 * i] = l; o[2 * i + 1] = r; }
            else o[i] = (s16)(((int)l + (int)r) / 2);
        }
    }
    return nf;
}

// Called with g_lock held. Returns 1 if a buffer was queued.
static int fill_one_buffer(void) {
    int idx = -1;
    for (int i = 0; i < NBUF; i++) {
        u8 st = g_wb[i].status;
        if (st == NDSP_WBUF_FREE || st == NDSP_WBUF_DONE) { idx = i; break; }
    }
    if (idx < 0) return 0;

    M4bTrack *t = m4b_audio(&g_mp4);
    u32 frames = 0;
    static const u32 WARM[2] = {FILL_FIRST, 2048};
    u32 target = g_warm < 2 ? WARM[g_warm] : FILL_FRAMES;
    bool first = g_warm < 2;
    u64 t0 = svcGetSystemTick();
    if (g_logged == 0) { g_logged = 1; nlog("audio thread: first decode starting"); }
    while (frames < target && !g_eof && !(g_want && frames > 0)) {
        if (g_cur.sample >= t->nsamples) { g_eof = true; break; }
        u32 smp = g_cur.sample, len = 0;
        int rc = m4b_read(&g_mp4, t, &g_cur, g_sbuf, SBUF_SIZE, &len);
        if (rc == -1) { g_eof = true; break; }
        if (rc == -2) continue;

        NeAACDecFrameInfo fi;
        memset(&fi, 0, sizeof fi);
        void *pcm = NeAACDecDecode(g_dec, &fi, g_sbuf, len);
        if (fi.error > 0 || !pcm || fi.channels == 0) {
            if (++g_errs > 200) g_eof = true;
            continue;
        }
        g_errs = 0;
        if (g_need_format) {
            g_need_format = 0;
            u32 r = (u32)fi.samplerate, c = fi.channels >= 2 ? 2 : 1;
            if (r < 8000) r = 8000;
            if (r > 48000) r = 48000;
            nlog("audio thread: first frame decoded: rate=%u channels=%u", (unsigned)r, (unsigned)c);
            if (r != g_rate || c != g_channels) {
                g_rate = r;
                g_channels = c;
                setup_channel();             // nothing is queued yet, so a reset is safe
                ndspChnSetPaused(0, false);
            }
        }
        if (g_discard > 0) { g_discard--; continue; }
        u32 nf = (u32)fi.samples / fi.channels;
        if (nf == 0) continue;
        if (frames == 0) g_buf_ms[idx] = m4b_sample_to_ms(t, smp);
        frames += append_pcm(g_pcm[idx], frames, (const s16 *)pcm, nf, fi.channels);
    }
    if (frames == 0) return 0;

    g_buf_dur[idx] = (u32)((u64)frames * 1000 / g_rate);
    g_last_ms = g_buf_ms[idx] + g_buf_dur[idx];

    // decode-load statistics (what fraction of real time we spend decoding)
    u64 us = (svcGetSystemTick() - t0) * 1000000ULL / SYSCLOCK_ARM11;
    if (!first && g_buf_dur[idx] >= 20) {
        u32 pct = (u32)(us / ((u64)g_buf_dur[idx] * 10));
        if (pct > g_peak_pct) g_peak_pct = pct;
        g_load_us += us;
        g_win_ms += g_buf_dur[idx];
        if (g_win_ms >= 10000) {
            g_last_load_pct = (u32)(g_load_us / (g_win_ms * 10));
            g_last_peak_pct = g_peak_pct;
            g_load_us = g_win_ms = 0;
            g_peak_pct = 0;
        }
    }
    if (g_warm < 2) g_warm++;
    g_started = 1;

    ndspWaveBuf *wb = &g_wb[idx];
    memset(wb, 0, sizeof *wb);
    wb->data_vaddr = g_pcm[idx];
    wb->nsamples = frames;
    wb->looping = false;
    DSP_FlushDataCache(g_pcm[idx], (u32)frames * g_channels * sizeof(s16));
    ndspChnWaveBufAdd(0, wb);
    if (g_logged == 1) { g_logged = 2; nlog("audio thread: first buffer queued (%u frames)", (unsigned)frames); }
    return 1;
}

static void audio_thread(void *arg) {
    (void)arg;
    while (!g_quit) {
        int did = 0;
        LightLock_Lock(&g_lock);
        if (g_open && g_playing && !g_eof) {
            if (g_started) {
                bool empty = !buffers_active();
                if (empty && !g_was_empty) g_underruns++;
                g_was_empty = empty;
            }
            did = fill_one_buffer();
        }
        LightLock_Unlock(&g_lock);
        if (!did) svcSleepThread(6 * 1000 * 1000LL);
    }
}

/* ------------------------------------------------------------------- public */

int player_init(void) {
    LightLock_Init(&g_lock);
    Result rc = ndspInit();
    nlog("ndspInit rc=0x%08lX", (unsigned long)rc);
    if (R_FAILED(rc)) return (int)rc ? (int)rc : -1;
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);

    g_sbuf = (u8 *)malloc(SBUF_SIZE);
    for (int i = 0; i < NBUF; i++) {
        g_pcm[i] = (s16 *)linearAlloc((size_t)BUF_CAP * 2 * sizeof(s16));
        if (!g_pcm[i]) return -2;
    }
    if (!g_sbuf) return -2;

    // Old 3DS: ask the OS for more of the system core; New 3DS: use the spare core.
    bool is_new = false;
    APT_CheckNew3DS(&is_new);
    g_is_new = is_new;
    int core = -2;
    if (is_new) {
        core = 2;
    } else {
        // Old 3DS / 2DS: the second core is shared with the OS; ask for as much of it as allowed.
        Result lr = APT_SetAppCpuTimeLimit(80);
        if (R_FAILED(lr)) lr = APT_SetAppCpuTimeLimit(70);
        u32 lim = 0;
        APT_GetAppCpuTimeLimit(&lim);
        nlog("cpu time limit: rc=0x%08lX effective=%u%%", (unsigned long)lr, (unsigned)lim);
        core = 1;
    }
    nlog("new3ds=%d, audio thread core=%d", (int)is_new, core);
    s32 prio = 0x30;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    s32 aprio = prio - 3;
    if (aprio < 0x19) aprio = 0x19;
    g_quit = false;
    g_thread = threadCreate(audio_thread, NULL, THREAD_STACK, aprio, core, false);
    if (!g_thread) g_thread = threadCreate(audio_thread, NULL, THREAD_STACK, aprio, -1, false);
    nlog("audio thread %s", g_thread ? "started" : "FAILED");
    if (!g_thread) return -3;
    g_ready = true;
    return 0;
}

void player_exit(void) {
    if (!g_ready) return;
    player_close();
    g_quit = true;
    threadJoin(g_thread, U64_MAX);
    threadFree(g_thread);
    for (int i = 0; i < NBUF; i++) if (g_pcm[i]) linearFree(g_pcm[i]);
    free(g_sbuf);
    ndspExit();
    g_ready = false;
}

void player_close(void) {
    if (!g_ready) return;
    lock_player();
    if (g_open) {
        g_playing = false;
        ndspChnSetPaused(0, true);
        ndspChnWaveBufClear(0);
        ndspChnReset(0);
        if (g_dec) { NeAACDecClose(g_dec); g_dec = NULL; }
        m4b_close(&g_mp4);
        g_open = false;
    }
    LightLock_Unlock(&g_lock);
}

int player_open(const char *path, char *err, size_t errlen) {
    if (!g_ready) { snprintf(err, errlen, "Audio engine unavailable"); return -1; }
    player_close();
    lock_player();

    if (m4b_open(&g_mp4, path, err, errlen) != 0) goto fail;
    M4bTrack *t = m4b_audio(&g_mp4);

    nlog("m4b ok: samples=%u chapters=%d asc_len=%u obj=0x%02X timescale=%u tag-channels=%u",
         (unsigned)t->nsamples, g_mp4.nchapters, (unsigned)t->asc_len, (unsigned)t->object_type, (unsigned)t->timescale, (unsigned)t->channels);
    g_dec = NeAACDecOpen();
    if (!g_dec) { snprintf(err, errlen, "Could not create AAC decoder"); goto fail_close; }
    NeAACDecConfigurationPtr cfg = NeAACDecGetCurrentConfiguration(g_dec);
    cfg->outputFormat = FAAD_FMT_16BIT;
    cfg->downMatrix = 1;
    cfg->dontUpSampleImplicitSBR = 1;
    NeAACDecSetConfiguration(g_dec, cfg);

    unsigned long sr = 0;
    unsigned char ch = 0;
    if (NeAACDecInit2(g_dec, t->asc, t->asc_len, &sr, &ch) < 0) {
        snprintf(err, errlen, "AAC profile not supported by decoder");
        goto fail_dec;
    }
    nlog("faad init ok: rate=%lu ch=%u", sr, (unsigned)ch);
    g_rate = sr ? (u32)sr : 44100;
    g_channels = ch >= 2 ? 2 : 1;

    // NOTE: FAAD2 needs ~35 KB of stack per frame, more than the 3DS main thread has.
    // All decoding therefore happens on the audio thread; it fixes up the real output
    // rate / channel layout (SBR can double it) when it decodes the first frame.
    g_need_format = 1;
    if (g_rate < 8000) g_rate = 8000;
    if (g_rate > 48000) g_rate = 48000;

    m4b_seek(&g_mp4, t, 0, &g_cur);
    NeAACDecPostSeekReset(g_dec, -1);
    g_logged = 0;
    g_warm = 0;
    g_started = 0;
    g_was_empty = false;
    g_load_us = g_win_ms = 0;
    g_peak_pct = g_last_load_pct = g_last_peak_pct = 0;
    g_underruns = 0;
    g_open = true;
    g_playing = false;
    g_eof = false;
    g_errs = 0;
    g_discard = 0;
    g_last_ms = 0;
    setup_channel();
    ndspChnSetPaused(0, true);
    reset_position_tracking();
    LightLock_Unlock(&g_lock);
    return 0;

fail_dec:
    NeAACDecClose(g_dec);
    g_dec = NULL;
fail_close:
    m4b_close(&g_mp4);
fail:
    LightLock_Unlock(&g_lock);
    return -1;
}

const M4b *player_book(void) { return g_open ? &g_mp4 : NULL; }

uint64_t player_duration_ms(void) { return g_open ? g_mp4.duration_ms : 0; }

static void current_base(u64 *base, u32 *dur, bool *playing_buf) {
    *base = g_last_ms;
    *dur = 0;
    *playing_buf = false;
    for (int i = 0; i < NBUF; i++) {
        if (g_wb[i].status == NDSP_WBUF_PLAYING) {
            *base = g_buf_ms[i];
            *dur = g_buf_dur[i];
            *playing_buf = true;
            return;
        }
    }
    bool any = false;
    u64 lo = 0;
    for (int i = 0; i < NBUF; i++) {
        if (g_wb[i].status == NDSP_WBUF_QUEUED && (!any || g_buf_ms[i] < lo)) { lo = g_buf_ms[i]; any = true; }
    }
    if (any) *base = lo;
}

uint64_t player_position_ms(void) {
    if (!g_open) return 0;
    u64 now = osGetTime(), base;
    u32 dur;
    bool pb;
    current_base(&base, &dur, &pb);
    if (base != g_anchor_ms) { g_anchor_ms = base; g_anchor_time = now; g_frozen_elapsed = 0; }
    u64 elapsed;
    if (g_playing && pb) {
        elapsed = now - g_anchor_time;
        if (elapsed > dur) elapsed = dur;
    } else {
        elapsed = g_frozen_elapsed;
    }
    u64 pos = base + elapsed;
    return pos > g_mp4.duration_ms ? g_mp4.duration_ms : pos;
}

void player_play(void) {
    if (!g_open) return;
    lock_player();
    if (g_eof && g_cur.sample >= m4b_audio(&g_mp4)->nsamples) {
        // restart from the top if the book had finished
        LightLock_Unlock(&g_lock);
        player_seek_ms(0);
        lock_player();
    }
    g_anchor_time = osGetTime() - g_frozen_elapsed;
    g_playing = true;
    ndspChnSetPaused(0, false);
    LightLock_Unlock(&g_lock);
}

void player_pause(void) {
    if (!g_open) return;
    lock_player();
    if (g_playing) {
        u64 base;
        u32 dur;
        bool pb;
        current_base(&base, &dur, &pb);
        u64 now = osGetTime();
        u64 e = (pb && base == g_anchor_ms) ? now - g_anchor_time : g_frozen_elapsed;
        g_frozen_elapsed = e > dur ? dur : e;
    }
    g_playing = false;
    ndspChnSetPaused(0, true);
    LightLock_Unlock(&g_lock);
}

bool player_is_playing(void) { return g_open && g_playing; }

bool player_ended(void) {
    if (!g_open || !g_eof) return false;
    for (int i = 0; i < NBUF; i++) {
        u8 st = g_wb[i].status;
        if (st == NDSP_WBUF_QUEUED || st == NDSP_WBUF_PLAYING) return false;
    }
    return true;
}

void player_seek_ms(uint64_t ms) {
    if (!g_open) return;
    lock_player();
    M4bTrack *t = m4b_audio(&g_mp4);
    if (ms >= g_mp4.duration_ms) ms = g_mp4.duration_ms > 2000 ? g_mp4.duration_ms - 2000 : 0;

    u32 target = m4b_time_to_sample(t, ms);
    u32 pre = target >= 4 ? target - 4 : 0;           // decode a few frames of pre-roll and drop them
    if (m4b_seek(&g_mp4, t, pre, &g_cur) == 0) {
        NeAACDecPostSeekReset(g_dec, -1);
        g_discard = (int)(target - pre);
        g_eof = false;
        g_errs = 0;
    }
    ndspChnWaveBufClear(0);
    setup_channel();
    ndspChnSetPaused(0, !g_playing);
    g_warm = 0;
    g_started = 0;
    g_was_empty = false;
    g_last_ms = m4b_sample_to_ms(t, target);
    reset_position_tracking();
    if (g_playing) g_anchor_time = osGetTime();
    LightLock_Unlock(&g_lock);
}

void player_set_volume(float v) {
    if (v < 0) v = 0;
    if (v > 1) v = 1;
    ndspSetMasterVol(v);
}

void player_get_stats(PlayerStats *s) {
    s->load_pct = (int)g_last_load_pct;
    s->peak_pct = (int)g_last_peak_pct;
    s->underruns = g_underruns;
    s->is_new3ds = g_is_new;
}

bool player_is_new3ds(void) { return g_is_new; }
