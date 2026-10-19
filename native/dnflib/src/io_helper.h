/*
 * io_helper.h - Internal IO abstraction layer
 * Supports both FILE* (file mode) and memory buffer mode.
 */
#ifndef DNF_IO_HELPER_H
#define DNF_IO_HELPER_H

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include "dnflib.h"  /* DnfError */

/* ------------------------------------------------------------------ */
/* DnfIO: unified IO handle                                            */
/* ------------------------------------------------------------------ */
typedef enum { DNF_IO_FILE = 0, DNF_IO_MEM = 1, DNF_IO_MEM_WRITE = 2 } DnfIOType;

typedef struct {
    DnfIOType type;
    union {
        FILE *fp;
        struct {
            const uint8_t *buf;
            size_t         len;
            size_t         pos;
        } mem;
        struct {
            uint8_t *buf;
            size_t   len;
            size_t   capacity;
            size_t   pos;
        } mem_write;
    } u;
} DnfIO;

/* ---- constructors ---- */
void dnf_io_from_file(DnfIO *io, FILE *fp);
void dnf_io_from_mem(DnfIO *io, const uint8_t *buf, size_t len);
void dnf_io_from_mem_write(DnfIO *io);
FILE *dnf_fopen_utf8(const char *path, const char *mode);
DnfError dnf_io_take_mem(DnfIO *io, uint8_t **out, size_t *out_len);

/* ---- primitives ---- */
DnfError dnf_io_read(DnfIO *io, void *out, size_t n);
DnfError dnf_io_write(DnfIO *io, const void *data, size_t n);
DnfError dnf_io_seek(DnfIO *io, long offset, int whence);
long     dnf_io_tell(DnfIO *io);
DnfError dnf_io_length(DnfIO *io, size_t *out_len);

/* ---- little-endian typed reads ---- */
DnfError dnf_io_read_u8 (DnfIO *io, uint8_t  *v);
DnfError dnf_io_read_u16(DnfIO *io, uint16_t *v);
DnfError dnf_io_read_u32(DnfIO *io, uint32_t *v);
DnfError dnf_io_read_i32(DnfIO *io, int32_t  *v);
DnfError dnf_io_read_i16(DnfIO *io, int16_t  *v);

/* ---- little-endian typed writes ---- */
DnfError dnf_io_write_u8 (DnfIO *io, uint8_t  v);
DnfError dnf_io_write_u16(DnfIO *io, uint16_t v);
DnfError dnf_io_write_u32(DnfIO *io, uint32_t v);
DnfError dnf_io_write_i32(DnfIO *io, int32_t  v);

/* ---- helpers ---- */
/* Read a fixed-length ASCII string (reads exactly n bytes, stops at NUL) */
DnfError dnf_io_read_ascii(DnfIO *io, char *out, size_t n);
/* Write a NUL-terminated ASCII string (writes str + NUL) */
DnfError dnf_io_write_ascii(DnfIO *io, const char *str);

#endif /* DNF_IO_HELPER_H */
