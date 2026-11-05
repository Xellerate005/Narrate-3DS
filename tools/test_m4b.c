#define _FILE_OFFSET_BITS 64
#include "../source/m4b.h"
#include <assert.h>
#include <string.h>
int main(int argc, char **argv) {
    M4b m; char err[128] = "";
    if (m4b_open(&m, argv[1], err, sizeof err) != 0) { printf("open failed: %s\n", err); return 1; }
    M4bTrack *t = m4b_audio(&m);
    printf("title='%s' artist='%s' cover=%u bytes\n", m.title, m.artist, m.cover_len);
    printf("samples=%u chunks=%u asc=%02x%02x dur=%llu ms obj=0x%x ch=%u rate=%u\n", t->nsamples, t->nchunks, t->asc[0], t->asc[1],
           (unsigned long long)m.duration_ms, t->object_type, t->channels, t->samplerate);
    for (int i = 0; i < m.nchapters; i++) printf("  ch%d @%llu ms '%s'\n", i, (unsigned long long)m.chapters[i].start_ms, m.chapters[i].title);
    M4bCursor c; uint8_t buf[64]; uint32_t len;
    assert(m4b_seek(&m, t, 0, &c) == 0);
    for (int k = 0; k < 100; k++) { assert(m4b_read(&m, t, &c, buf, 64, &len) == 0); assert(len == 10 && buf[0] == k && buf[9] == k); }
    assert(m4b_read(&m, t, &c, buf, 64, &len) == -1);
    for (int s = 0; s < 100; s += 7) { assert(m4b_seek(&m, t, s, &c) == 0); for (int k = s; k < 100 && k < s + 60; k++) { assert(m4b_read(&m, t, &c, buf, 64, &len) == 0); assert(buf[0] == k); } }
    assert(m4b_seek(&m, t, 77, &c) == 0 && m4b_read(&m, t, &c, buf, 64, &len) == 0 && buf[3] == 77);
    printf("time_to_sample(1000ms)=%u sample_to_ms(43)=%llu chapter_at(900)=%d chapter_at(1700)=%d chapter_at(2100)=%d\n",
           m4b_time_to_sample(t, 1000), (unsigned long long)m4b_sample_to_ms(t, 43), m4b_chapter_at(&m, 900), m4b_chapter_at(&m, 1700), m4b_chapter_at(&m, 2100));
    m4b_close(&m);
    printf("ALL OK\n");
    return 0;
}
