// zipx.c
#include "zipx.h"

#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <zlib.h>

#define MAX_FILES   400
#define MAX_FILE    (12u * 1024 * 1024)
#define MAX_TOTAL   (40u * 1024 * 1024)

static void seterr(char *err, size_t n, const char *m) { if (err && n) snprintf(err, n, "%s", m); }

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

void zipx_mkdir_p(const char *path) {
    char tmp[512];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            if (!(tmp[strlen(tmp) - 1] == ':')) mkdir(tmp, 0777);
            *p = '/';
        }
    }
    mkdir(tmp, 0777);
}

void zipx_rmtree(const char *path) {
    DIR *d = opendir(path);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            char full[640];
            snprintf(full, sizeof full, "%s/%s", path, e->d_name);
            struct stat st;
            if (stat(full, &st) == 0 && S_ISDIR(st.st_mode)) zipx_rmtree(full);
            else remove(full);
        }
        closedir(d);
    }
    rmdir(path);
}

// "dir/../x", "/abs", "C:\x", backslashes... -> a safe relative path, or 0 if unusable
static int sanitize(const char *in, char *out, size_t n) {
    size_t o = 0;
    const char *p = in;
    while (*p == '/' || *p == '\\') p++;
    for (; *p && o + 1 < n; p++) out[o++] = (*p == '\\') ? '/' : *p;
    out[o] = 0;
    if (!o || strstr(out, "..") || strchr(out, ':')) return 0;
    return 1;
}

static uint8_t g_in[8192], g_out[16384];

static int extract_one(FILE *f, uint32_t lho, uint16_t method, uint32_t csize, uint32_t usize, uint32_t crc,
                       const char *dest, char *err, size_t errlen) {
    uint8_t lh[30];
    if (fseek(f, (long)lho, SEEK_SET) != 0 || fread(lh, 1, 30, f) != 30 || rd32(lh) != 0x04034b50) { seterr(err, errlen, "bad local header"); return -1; }
    long data = (long)lho + 30 + rd16(lh + 26) + rd16(lh + 28);
    if (fseek(f, data, SEEK_SET) != 0) { seterr(err, errlen, "seek failed"); return -1; }

    FILE *o = fopen(dest, "wb");
    if (!o) { seterr(err, errlen, "cannot write file"); return -1; }
    uLong c = crc32(0L, Z_NULL, 0);
    uint32_t written = 0;
    int rc = 0;

    if (method == 0) {
        uint32_t left = csize;
        while (left) {
            size_t want = left > sizeof g_in ? sizeof g_in : left;
            if (fread(g_in, 1, want, f) != want) { rc = -1; break; }
            fwrite(g_in, 1, want, o);
            c = crc32(c, g_in, (uInt)want);
            written += (uint32_t)want;
            left -= (uint32_t)want;
        }
    } else {
        z_stream z;
        memset(&z, 0, sizeof z);
        if (inflateInit2(&z, -MAX_WBITS) != Z_OK) { fclose(o); seterr(err, errlen, "inflate init failed"); return -1; }
        uint32_t left = csize;
        int zr = Z_OK;
        while (zr != Z_STREAM_END) {
            if (z.avail_in == 0) {
                size_t want = left > sizeof g_in ? sizeof g_in : left;
                if (!want) { rc = -1; break; }
                if (fread(g_in, 1, want, f) != want) { rc = -1; break; }
                left -= (uint32_t)want;
                z.next_in = g_in;
                z.avail_in = (uInt)want;
            }
            z.next_out = g_out;
            z.avail_out = sizeof g_out;
            zr = inflate(&z, Z_NO_FLUSH);
            if (zr != Z_OK && zr != Z_STREAM_END) { rc = -1; break; }
            size_t have = sizeof g_out - z.avail_out;
            if (have) {
                fwrite(g_out, 1, have, o);
                c = crc32(c, g_out, (uInt)have);
                written += (uint32_t)have;
                if (written > MAX_FILE) { rc = -1; break; }
            }
        }
        inflateEnd(&z);
    }
    fclose(o);
    if (rc == 0 && (written != usize || (uint32_t)c != crc)) rc = -1;
    if (rc != 0) { remove(dest); seterr(err, errlen, "corrupt or truncated data in archive"); }
    return rc;
}

int zipx_extract(const char *zip_path, const char *dest_dir, char *err, size_t errlen) {
    FILE *f = fopen(zip_path, "rb");
    if (!f) { seterr(err, errlen, "cannot open archive"); return -1; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    if (size < 22) { fclose(f); seterr(err, errlen, "not a zip archive"); return -1; }

    // end-of-central-directory record
    long scan = size < 66000 ? size : 66000;
    uint8_t *tail = (uint8_t *)malloc((size_t)scan);
    if (!tail) { fclose(f); seterr(err, errlen, "out of memory"); return -1; }
    fseek(f, size - scan, SEEK_SET);
    if (fread(tail, 1, (size_t)scan, f) != (size_t)scan) { free(tail); fclose(f); seterr(err, errlen, "read error"); return -1; }
    long eocd = -1;
    for (long i = scan - 22; i >= 0; i--) if (rd32(tail + i) == 0x06054b50) { eocd = i; break; }
    if (eocd < 0) { free(tail); fclose(f); seterr(err, errlen, "not a zip archive"); return -1; }
    uint16_t count = rd16(tail + eocd + 10);
    uint32_t cd_size = rd32(tail + eocd + 12), cd_off = rd32(tail + eocd + 16);
    free(tail);
    if (count > MAX_FILES || cd_size > 256 * 1024 || (long)cd_off + (long)cd_size > size) { fclose(f); seterr(err, errlen, "archive too large or damaged"); return -1; }

    uint8_t *cd = (uint8_t *)malloc(cd_size ? cd_size : 1);
    if (!cd) { fclose(f); seterr(err, errlen, "out of memory"); return -1; }
    fseek(f, (long)cd_off, SEEK_SET);
    if (fread(cd, 1, cd_size, f) != cd_size) { free(cd); fclose(f); seterr(err, errlen, "read error"); return -1; }

    zipx_mkdir_p(dest_dir);
    uint32_t total = 0, pos = 0;
    int rc = 0;
    for (int i = 0; i < count && rc == 0; i++) {
        if (pos + 46 > cd_size || rd32(cd + pos) != 0x02014b50) { seterr(err, errlen, "damaged directory"); rc = -1; break; }
        uint16_t flags = rd16(cd + pos + 8), method = rd16(cd + pos + 10);
        uint32_t crc = rd32(cd + pos + 16), csize = rd32(cd + pos + 20), usize = rd32(cd + pos + 24);
        uint16_t fn = rd16(cd + pos + 28), ex = rd16(cd + pos + 30), cm = rd16(cd + pos + 32);
        uint32_t lho = rd32(cd + pos + 42);
        char raw[300], rel[300], full[640];
        size_t nl = fn < sizeof raw - 1 ? fn : sizeof raw - 1;
        if (pos + 46 + fn > cd_size) { seterr(err, errlen, "damaged directory"); rc = -1; break; }
        memcpy(raw, cd + pos + 46, nl);
        raw[nl] = 0;
        pos += 46u + fn + ex + cm;

        if (nl && (raw[nl - 1] == '/' || raw[nl - 1] == '\\')) continue;            // directory entry
        if (!sanitize(raw, rel, sizeof rel)) continue;                               // unsafe name: skip
        if (flags & 1) { seterr(err, errlen, "encrypted archives are not supported"); rc = -1; break; }
        if (method != 0 && method != 8) { seterr(err, errlen, "unsupported compression"); rc = -1; break; }
        if (usize > MAX_FILE || total + usize > MAX_TOTAL) { seterr(err, errlen, "theme is too large"); rc = -1; break; }
        total += usize;

        snprintf(full, sizeof full, "%s/%s", dest_dir, rel);
        char dir[640];
        snprintf(dir, sizeof dir, "%s", full);
        char *slash = strrchr(dir, '/');
        if (slash) { *slash = 0; zipx_mkdir_p(dir); }
        rc = extract_one(f, lho, method, csize, usize, crc, full, err, errlen);
    }
    free(cd);
    fclose(f);
    return rc;
}
