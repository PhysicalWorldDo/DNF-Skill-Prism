/*
 * io_helper.c - IO abstraction implementation
 */
#include "io_helper.h"
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#include <wchar.h>
#endif

FILE *dnf_fopen_utf8(const char *path, const char *mode) {
#ifdef _WIN32
    int path_len;
    int mode_len;
    wchar_t *wide_path;
    wchar_t *wide_mode;
    FILE *fp = NULL;

    if (!path || !mode) return NULL;
    path_len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    mode_len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, mode, -1, NULL, 0);
    if (path_len <= 0 || mode_len <= 0) return NULL;

    wide_path = (wchar_t *)malloc((size_t)path_len * sizeof(wchar_t));
    wide_mode = (wchar_t *)malloc((size_t)mode_len * sizeof(wchar_t));
    if (!wide_path || !wide_mode) {
        free(wide_path);
        free(wide_mode);
        return NULL;
    }
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide_path, path_len) ||
        !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, mode, -1, wide_mode, mode_len)) {
        free(wide_path);
        free(wide_mode);
        return NULL;
    }
#ifdef _MSC_VER
    if (_wfopen_s(&fp, wide_path, wide_mode) != 0) fp = NULL;
#else
    fp = _wfopen(wide_path, wide_mode);
#endif
    free(wide_path);
    free(wide_mode);
    return fp;
#else
    return fopen(path, mode);
#endif
}

void dnf_io_from_file(DnfIO *io, FILE *fp) {
    io->type  = DNF_IO_FILE;
    io->u.fp  = fp;
}

void dnf_io_from_mem(DnfIO *io, const uint8_t *buf, size_t len) {
    io->type        = DNF_IO_MEM;
    io->u.mem.buf   = buf;
    io->u.mem.len   = len;
    io->u.mem.pos   = 0;
}

void dnf_io_from_mem_write(DnfIO *io) {
    io->type = DNF_IO_MEM_WRITE;
    io->u.mem_write.buf = NULL;
    io->u.mem_write.len = 0;
    io->u.mem_write.capacity = 0;
    io->u.mem_write.pos = 0;
}

DnfError dnf_io_take_mem(DnfIO *io, uint8_t **out, size_t *out_len) {
    if (!io || io->type != DNF_IO_MEM_WRITE || !out || !out_len)
        return DNF_ERR_INVALID_FORMAT;
    *out = io->u.mem_write.buf;
    *out_len = io->u.mem_write.len;
    io->u.mem_write.buf = NULL;
    io->u.mem_write.len = 0;
    io->u.mem_write.capacity = 0;
    io->u.mem_write.pos = 0;
    return DNF_OK;
}

DnfError dnf_io_read(DnfIO *io, void *out, size_t n) {
    if (!io || (!out && n > 0)) return DNF_ERR_INVALID_FORMAT;
    if (n == 0) return DNF_OK;
    if (io->type == DNF_IO_FILE) {
        if (fread(out, 1, n, io->u.fp) != n) return DNF_ERR_IO;
    } else if (io->type == DNF_IO_MEM) {
        if (io->u.mem.pos > io->u.mem.len ||
            n > io->u.mem.len - io->u.mem.pos) return DNF_ERR_IO;
        memcpy(out, io->u.mem.buf + io->u.mem.pos, n);
        io->u.mem.pos += n;
    } else {
        if (io->u.mem_write.pos > io->u.mem_write.len ||
            n > io->u.mem_write.len - io->u.mem_write.pos)
            return DNF_ERR_IO;
        memcpy(out, io->u.mem_write.buf + io->u.mem_write.pos, n);
        io->u.mem_write.pos += n;
    }
    return DNF_OK;
}

DnfError dnf_io_write(DnfIO *io, const void *data, size_t n) {
    if (n == 0) return DNF_OK;
    if (io->type == DNF_IO_FILE) {
        if (fwrite(data, 1, n, io->u.fp) != n) return DNF_ERR_IO;
    } else if (io->type == DNF_IO_MEM) {
        return DNF_ERR_IO;
    } else {
        if (n > SIZE_MAX - io->u.mem_write.pos) return DNF_ERR_OOM;
        size_t required = io->u.mem_write.pos + n;
        if (required > io->u.mem_write.capacity) {
            size_t capacity = io->u.mem_write.capacity ? io->u.mem_write.capacity : 256u;
            while (capacity < required) {
                if (capacity > SIZE_MAX / 2u) { capacity = required; break; }
                capacity *= 2u;
            }
            uint8_t *grown = (uint8_t *)realloc(io->u.mem_write.buf, capacity);
            if (!grown) return DNF_ERR_OOM;
            io->u.mem_write.buf = grown;
            io->u.mem_write.capacity = capacity;
        }
        memcpy(io->u.mem_write.buf + io->u.mem_write.pos, data, n);
        io->u.mem_write.pos = required;
        if (required > io->u.mem_write.len) io->u.mem_write.len = required;
    }
    return DNF_OK;
}

DnfError dnf_io_seek(DnfIO *io, long offset, int whence) {
    if (io->type == DNF_IO_FILE) {
        if (fseek(io->u.fp, offset, whence) != 0) return DNF_ERR_IO;
    } else if (io->type == DNF_IO_MEM) {
        long newpos;
        switch (whence) {
            case SEEK_SET: newpos = offset; break;
            case SEEK_CUR: newpos = (long)io->u.mem.pos + offset; break;
            case SEEK_END: newpos = (long)io->u.mem.len + offset; break;
            default: return DNF_ERR_IO;
        }
        if (newpos < 0 || (size_t)newpos > io->u.mem.len) return DNF_ERR_IO;
        io->u.mem.pos = (size_t)newpos;
    } else {
        long newpos;
        switch (whence) {
            case SEEK_SET: newpos = offset; break;
            case SEEK_CUR: newpos = (long)io->u.mem_write.pos + offset; break;
            case SEEK_END: newpos = (long)io->u.mem_write.len + offset; break;
            default: return DNF_ERR_IO;
        }
        if (newpos < 0 || (size_t)newpos > io->u.mem_write.len) return DNF_ERR_IO;
        io->u.mem_write.pos = (size_t)newpos;
    }
    return DNF_OK;
}

long dnf_io_tell(DnfIO *io) {
    if (io->type == DNF_IO_FILE)
        return ftell(io->u.fp);
    if (io->type == DNF_IO_MEM) return (long)io->u.mem.pos;
    if (io->u.mem_write.pos > (size_t)LONG_MAX) return -1;
    return (long)io->u.mem_write.pos;
}

DnfError dnf_io_length(DnfIO *io, size_t *out_len) {
    if (!io || !out_len) return DNF_ERR_INVALID_FORMAT;
    if (io->type == DNF_IO_MEM) {
        *out_len = io->u.mem.len;
        return DNF_OK;
    }
    if (io->type == DNF_IO_MEM_WRITE) {
        *out_len = io->u.mem_write.len;
        return DNF_OK;
    }
    if (io->type == DNF_IO_FILE) {
#ifdef _WIN32
        __int64 current = _ftelli64(io->u.fp);
        __int64 end;
        if (current < 0 || _fseeki64(io->u.fp, 0, SEEK_END) != 0) return DNF_ERR_IO;
        end = _ftelli64(io->u.fp);
        if (_fseeki64(io->u.fp, current, SEEK_SET) != 0) return DNF_ERR_IO;
        if (end < 0 || (uint64_t)end > (uint64_t)SIZE_MAX) return DNF_ERR_IO;
        *out_len = (size_t)end;
#else
        long current = ftell(io->u.fp);
        long end;
        if (current < 0 || fseek(io->u.fp, 0, SEEK_END) != 0) return DNF_ERR_IO;
        end = ftell(io->u.fp);
        if (fseek(io->u.fp, current, SEEK_SET) != 0) return DNF_ERR_IO;
        if (end < 0) return DNF_ERR_IO;
        *out_len = (size_t)end;
#endif
        return DNF_OK;
    }
    return DNF_ERR_IO;
}

/* ---- little-endian typed reads ---- */

DnfError dnf_io_read_u8(DnfIO *io, uint8_t *v) {
    return dnf_io_read(io, v, 1);
}

DnfError dnf_io_read_u16(DnfIO *io, uint16_t *v) {
    uint8_t b[2];
    DnfError e = dnf_io_read(io, b, 2);
    if (e) return e;
    *v = (uint16_t)(b[0] | ((uint16_t)b[1] << 8));
    return DNF_OK;
}

DnfError dnf_io_read_u32(DnfIO *io, uint32_t *v) {
    uint8_t b[4];
    DnfError e = dnf_io_read(io, b, 4);
    if (e) return e;
    *v = (uint32_t)b[0] | ((uint32_t)b[1]<<8) | ((uint32_t)b[2]<<16) | ((uint32_t)b[3]<<24);
    return DNF_OK;
}

DnfError dnf_io_read_i32(DnfIO *io, int32_t *v) {
    uint32_t u;
    DnfError e = dnf_io_read_u32(io, &u);
    if (e) return e;
    *v = (int32_t)u;
    return DNF_OK;
}

DnfError dnf_io_read_i16(DnfIO *io, int16_t *v) {
    uint16_t u;
    DnfError e = dnf_io_read_u16(io, &u);
    if (e) return e;
    *v = (int16_t)u;
    return DNF_OK;
}

/* ---- little-endian typed writes ---- */

DnfError dnf_io_write_u8(DnfIO *io, uint8_t v) {
    return dnf_io_write(io, &v, 1);
}

DnfError dnf_io_write_u16(DnfIO *io, uint16_t v) {
    uint8_t b[2] = { (uint8_t)(v), (uint8_t)(v>>8) };
    return dnf_io_write(io, b, 2);
}

DnfError dnf_io_write_u32(DnfIO *io, uint32_t v) {
    uint8_t b[4] = { (uint8_t)(v), (uint8_t)(v>>8), (uint8_t)(v>>16), (uint8_t)(v>>24) };
    return dnf_io_write(io, b, 4);
}

DnfError dnf_io_write_i32(DnfIO *io, int32_t v) {
    return dnf_io_write_u32(io, (uint32_t)v);
}

/* ---- string helpers ---- */

DnfError dnf_io_read_ascii(DnfIO *io, char *out, size_t n) {
    DnfError e = dnf_io_read(io, out, n);
    if (e) return e;
    out[n] = '\0'; /* caller must ensure out has n+1 bytes */
    return DNF_OK;
}

DnfError dnf_io_write_ascii(DnfIO *io, const char *str) {
    size_t len = strlen(str);
    DnfError e = dnf_io_write(io, str, len);
    if (e) return e;
    return dnf_io_write_u8(io, 0);
}
