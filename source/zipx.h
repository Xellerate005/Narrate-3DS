// zipx.h - minimal ZIP reader (stored + deflate) for .pix theme archives, plus small file-system helpers
#ifndef ZIPX_H
#define ZIPX_H
#include <stddef.h>

// Extracts every file of a .zip into dest_dir (created if needed). Paths are sanitised (no "..", no absolute paths),
// sizes are capped and every file's CRC is verified. Returns 0 on success, else -1 with a message in err.
int  zipx_extract(const char *zip_path, const char *dest_dir, char *err, size_t errlen);
void zipx_mkdir_p(const char *path);
void zipx_rmtree(const char *path);          // deletes a folder and everything inside it

#endif
