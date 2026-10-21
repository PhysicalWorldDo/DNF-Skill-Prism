/*
 * dnflib.c - Public API implementation
 *
 * Connects NPK / IMG / pixel / DXT modules to the public C API
 * declared in include/dnflib.h.
 */
#include "dnflib.h"
#include "io_helper.h"
#include "npk.h"
#include "img.h"
#include "pixel.h"
#include "dxt.h"
#include "palette.h"
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "miniz.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>

/* ------------------------------------------------------------------ */
/* NPK API                                                             */
/* ------------------------------------------------------------------ */

DNFLIB_API DnfError dnf_npk_open_file(const char *path, NpkFile **out) {
    FILE *fp = dnf_fopen_utf8(path, "rb");
    if (!fp) return DNF_ERR_IO;
    DnfIO io;
    dnf_io_from_file(&io, fp);
    DnfError e = npk_open(&io, out, 1 /* owns_fp */);
    if (e) fclose(fp);
    return e;
}

DNFLIB_API DnfError dnf_npk_open_mem(const uint8_t *buf, size_t len, NpkFile **out) {
    if ((!buf && len > 0) || !out) return DNF_ERR_INVALID_FORMAT;
    DnfIO io;
    dnf_io_from_mem(&io, buf, len);
    return npk_open(&io, out, 0);
}

DNFLIB_API DnfError dnf_npk_read_img(NpkFile *npk, int index, ImgFile **out) {
    if (!npk || index < 0 || index >= npk->count) return DNF_ERR_INVALID_INDEX;
    NpkEntry *ent = &npk->entries[index];
    NpkPriv  *priv = (NpkPriv *)npk->_priv;
    if (!priv) return DNF_ERR_IO;
    if (ent->offset < 0 || ent->size <= 0 || ent->offset > LONG_MAX)
        return DNF_ERR_INVALID_FORMAT;

    /* Seek to IMG data region in NPK */
    DnfError e = dnf_io_seek(&priv->io, (long)ent->offset, SEEK_SET);
    if (e) return e;

    /* Wrap a sub-view of the NPK IO as an IMG memory buffer */
    /* Read the IMG bytes into a temp buffer, then parse */
    uint8_t *img_buf = (uint8_t *)malloc((size_t)ent->size);
    if (!img_buf) return DNF_ERR_OOM;

    e = dnf_io_read(&priv->io, img_buf, (size_t)ent->size);
    if (e) { free(img_buf); return e; }

    DnfIO img_io;
    dnf_io_from_mem(&img_io, img_buf, (size_t)ent->size);
    e = img_open(&img_io, out);
    free(img_buf);
    return e;
}

DNFLIB_API DnfError dnf_npk_save_file(const char *path, NpkFile *npk, int group_by_md5) {
    return npk_save(path, npk, group_by_md5);
}

DNFLIB_API DnfError dnf_npk_create(NpkFile **out) {
    return npk_create(out);
}

DNFLIB_API DnfError dnf_npk_add_entry(NpkFile *npk, const char *name,
                                       const uint8_t *data, int32_t size) {
    return npk_add_entry(npk, name, data, size);
}

DNFLIB_API void dnf_npk_free(NpkFile *npk) {
    if (!npk) return;
    NpkPriv *priv = (NpkPriv *)npk->_priv;
    if (priv) {
        if (priv->owns_fp && priv->io.type == DNF_IO_FILE && priv->io.u.fp) {
            fclose(priv->io.u.fp);
        }
        if (priv->entry_bufs) {
            int i;
            for (i = 0; i < priv->entry_bufs_count; i++) {
                if (priv->entry_bufs[i]) free(priv->entry_bufs[i]);
            }
            free(priv->entry_bufs);
        }
        free(priv);
    }
    if (npk->entries) free(npk->entries);
    free(npk);
}

/* ------------------------------------------------------------------ */
/* IMG API                                                             */
/* ------------------------------------------------------------------ */

DNFLIB_API DnfError dnf_img_open_file(const char *path, ImgFile **out) {
    FILE *fp = dnf_fopen_utf8(path, "rb");
    if (!fp) return DNF_ERR_IO;
    DnfIO io;
    dnf_io_from_file(&io, fp);
    DnfError e = img_open(&io, out);
    /* All frame data is eagerly loaded by img_open; we can close the file now.
     * If img_open fails, fp was never stored anywhere — also safe to close. */
    fclose(fp);
    if (!e) {
        /* Nullify the now-closed fp inside ImgPriv to prevent accidental reuse */
        ImgPriv *priv = (ImgPriv *)(*out)->_priv;
        if (priv && priv->io.type == DNF_IO_FILE) {
            priv->io.u.fp = NULL;
        }
    }
    return e;
}

DNFLIB_API DnfError dnf_img_open_mem(const uint8_t *buf, size_t len, ImgFile **out) {
    if ((!buf && len > 0) || !out) return DNF_ERR_INVALID_FORMAT;
    DnfIO io;
    dnf_io_from_mem(&io, buf, len);
    return img_open(&io, out);
}

DNFLIB_API DnfError dnf_img_create(ImgFile **out) {
    if (!out) return DNF_ERR_INVALID_FORMAT;
    *out = NULL;

    ImgFile *img = (ImgFile *)calloc(1, sizeof(ImgFile));
    if (!img) return DNF_ERR_OOM;
    ImgPriv *priv = (ImgPriv *)calloc(1, sizeof(ImgPriv));
    if (!priv) {
        free(img);
        return DNF_ERR_OOM;
    }

    img->version = IMG_VER2;
    img->_priv = priv;
    *out = img;
    return DNF_OK;
}

DNFLIB_API DnfError dnf_img_create_paletted(const uint8_t *rgba,
                                            int palette_count, int color_count,
                                            ImgFile **out) {
    if (!out) return DNF_ERR_INVALID_FORMAT;
    *out = NULL;
    if (!rgba || palette_count < 1 || palette_count > IMG_MAX_PALETTES ||
        color_count < 1 || color_count > DNF_PALETTE_MAX)
        return DNF_ERR_INVALID_FORMAT;
    ImgFile *img = NULL;
    DnfError e = dnf_img_create(&img);
    if (e) return e;
    ImgPriv *priv = (ImgPriv *)img->_priv;
    priv->palettes = (ColorBoard *)calloc((size_t)palette_count, sizeof(ColorBoard));
    if (!priv->palettes) { dnf_img_free(img); return DNF_ERR_OOM; }
    priv->palette_count = palette_count;
    for (int p = 0; p < palette_count; p++) {
        ColorBoard *cb = &priv->palettes[p]; cb->count = color_count;
        for (int c = 0; c < color_count; c++) {
            const uint8_t *color = rgba + ((size_t)p * color_count + c) * 4;
            cb->r[c] = color[0]; cb->g[c] = color[1];
            cb->b[c] = color[2]; cb->a[c] = color[3];
        }
    }
    img->version = IMG_VER6; *out = img; return DNF_OK;
}

DNFLIB_API DnfError dnf_img_add_frame_indices(ImgFile *img,
                                               const uint8_t *indices,
                                               int w, int h,
                                               int x, int y, int mw, int mh) {
    if (!img || !indices || img->version != IMG_VER6 || !img->_priv ||
        w <= 0 || h <= 0 || mw <= 0 || mh <= 0 ||
        img->frame_count < 0 || img->frame_count >= IMG_MAX_SPRITES ||
        (size_t)w > (size_t)INT_MAX / (size_t)h)
        return DNF_ERR_INVALID_FORMAT;
    ImgPriv *priv = (ImgPriv *)img->_priv;
    if (!priv->palettes || priv->palette_count <= 0) return DNF_ERR_INVALID_FORMAT;
    const size_t count = (size_t)w * h;
    for (size_t i = 0; i < count; i++)
        if (indices[i] >= priv->palettes[0].count) return DNF_ERR_INVALID_INDEX;
    mz_ulong length = mz_compressBound((mz_ulong)count);
    uint8_t *data = (uint8_t *)malloc(length);
    if (!data) return DNF_ERR_OOM;
    if (mz_compress2(data, &length, indices, (mz_ulong)count, 6) != MZ_OK || length > INT32_MAX) {
        free(data); return DNF_ERR_INVALID_FORMAT;
    }
    ImageFrame *frames = (ImageFrame *)realloc(img->frames, ((size_t)img->frame_count + 1) * sizeof(ImageFrame));
    if (!frames) { free(data); return DNF_ERR_OOM; }
    img->frames = frames;
    ImageFrame *frame = &frames[img->frame_count++]; memset(frame, 0, sizeof(*frame));
    frame->format = DNF_FMT_ARGB_1555; frame->compress = DNF_COMPRESS_ZLIB;
    frame->w = w; frame->h = h; frame->x = x; frame->y = y;
    frame->mw = mw; frame->mh = mh; frame->data = data;
    frame->data_size = (int32_t)length; frame->link_index = -1;
    return DNF_OK;
}

DNFLIB_API DnfError dnf_img_add_frame_rgba(ImgFile *img,
                                            const uint8_t *rgba,
                                            int w, int h,
                                            int x, int y, int mw, int mh,
                                            DnfImageFormat format,
                                            DnfCompressMode compress) {
    if (!img || !rgba || (img->version != IMG_VER2 && img->version != IMG_VER6) ||
        w <= 0 || h <= 0 || mw <= 0 || mh <= 0 ||
        img->frame_count < 0 || img->frame_count == INT_MAX)
        return DNF_ERR_INVALID_FORMAT;
    if (img->version == IMG_VER6 && format != DNF_FMT_ARGB_8888)
        return DNF_ERR_INVALID_FORMAT;
    if (format != DNF_FMT_ARGB_1555 &&
        format != DNF_FMT_ARGB_4444 &&
        format != DNF_FMT_ARGB_8888)
        return DNF_ERR_UNSUPPORTED_VERSION;
    if (compress != DNF_COMPRESS_NONE && compress != DNF_COMPRESS_ZLIB)
        return DNF_ERR_UNSUPPORTED_VERSION;
    if ((size_t)w > SIZE_MAX / (size_t)h ||
        (size_t)w * (size_t)h > SIZE_MAX / 4u)
        return DNF_ERR_INVALID_FORMAT;
    size_t pixel_size = format == DNF_FMT_ARGB_8888 ? 4u : 2u;
    if ((size_t)w * (size_t)h > (size_t)INT_MAX / 4u ||
        (size_t)w * (size_t)h > (size_t)INT32_MAX / pixel_size)
        return DNF_ERR_INVALID_FORMAT;

    uint8_t *data = NULL;
    int32_t data_size = 0;
    DnfError e = dnf_image_encode(rgba, w, h, format, compress,
                                  &data, &data_size);
    if (e) return e;

    size_t new_count = (size_t)img->frame_count + 1u;
    if (new_count > SIZE_MAX / sizeof(ImageFrame)) {
        free(data);
        return DNF_ERR_OOM;
    }
    ImageFrame *frames = (ImageFrame *)realloc(
        img->frames, new_count * sizeof(ImageFrame));
    if (!frames) {
        free(data);
        return DNF_ERR_OOM;
    }
    img->frames = frames;

    ImageFrame *frame = &img->frames[img->frame_count];
    memset(frame, 0, sizeof(*frame));
    frame->format = format;
    frame->compress = compress;
    frame->w = w;
    frame->h = h;
    frame->x = x;
    frame->y = y;
    frame->mw = mw;
    frame->mh = mh;
    frame->data = data;
    frame->data_size = data_size;
    frame->link_index = -1;
    img->frame_count++;
    return DNF_OK;
}

DNFLIB_API DnfError dnf_img_add_link_frame(ImgFile *img, int link_index) {
    if (!img || (img->version != IMG_VER2 && img->version != IMG_VER6) ||
        link_index < 0 || link_index >= img->frame_count ||
        img->frame_count == INT_MAX)
        return DNF_ERR_INVALID_INDEX;

    size_t new_count = (size_t)img->frame_count + 1u;
    if (new_count > SIZE_MAX / sizeof(ImageFrame)) return DNF_ERR_OOM;
    ImageFrame *frames = (ImageFrame *)realloc(
        img->frames, new_count * sizeof(ImageFrame));
    if (!frames) return DNF_ERR_OOM;
    img->frames = frames;

    ImageFrame *source = &img->frames[link_index];
    ImageFrame *frame = &img->frames[img->frame_count];
    memset(frame, 0, sizeof(*frame));
    frame->format = DNF_FMT_LINK;
    frame->link_index = link_index;
    frame->w = source->w;
    frame->h = source->h;
    frame->x = source->x;
    frame->y = source->y;
    frame->mw = source->mw;
    frame->mh = source->mh;
    img->frame_count++;
    return DNF_OK;
}

DNFLIB_API DnfError dnf_img_save_file(const char *path, ImgFile *img) {
    if (!path || !img) return DNF_ERR_INVALID_FORMAT;
    FILE *fp = dnf_fopen_utf8(path, "wb");
    if (!fp) return DNF_ERR_IO;
    DnfIO io;
    dnf_io_from_file(&io, fp);
    DnfError e = img_save(&io, img);
    if (!e && fflush(fp) != 0) e = DNF_ERR_IO;
    if (fclose(fp) != 0 && !e) e = DNF_ERR_IO;
    return e;
}

DNFLIB_API DnfError dnf_img_save_mem(ImgFile *img,
                                     uint8_t **out_data, size_t *out_size) {
    if (!img || !out_data || !out_size) return DNF_ERR_INVALID_FORMAT;
    *out_data = NULL;
    *out_size = 0;

    DnfIO io;
    dnf_io_from_mem_write(&io);
    DnfError e = img_save(&io, img);
    if (e) {
        uint8_t *discard = NULL;
        size_t discard_len = 0;
        dnf_io_take_mem(&io, &discard, &discard_len);
        free(discard);
        return e;
    }
    return dnf_io_take_mem(&io, out_data, out_size);
}

DNFLIB_API void dnf_img_free(ImgFile *img) {
    if (!img) return;
    ImgPriv *priv = (ImgPriv *)img->_priv;
    if (img->frames) {
        int i;
        for (i = 0; i < img->frame_count; i++) {
            if (img->frames[i].data) free(img->frames[i].data);
        }
        free(img->frames);
    }
    img_priv_free(priv, img->frame_count);
    free(img);
}

DNFLIB_API int dnf_img_palette_count(ImgFile *img) {
    if (!img) return 0;
    ImgPriv *priv = (ImgPriv *)img->_priv;
    if (!priv || priv->palette_count <= 0) return 0;
    return priv->palette_count;
}

/* ------------------------------------------------------------------ */
/* Image decode                                                        */
/* ------------------------------------------------------------------ */

static int is_canvas_placeholder_frame(const ImageFrame *f) {
    if (!f || f->w != 1 || f->h != 1 || f->mw <= 0 || f->mh <= 0)
        return 0;

    return f->x >= f->mw || f->y >= f->mh ||
           (int64_t)f->x + (int64_t)f->w <= 0 ||
           (int64_t)f->y + (int64_t)f->h <= 0;
}

static DnfError checked_pixel_lengths(int w, int h,
                                      size_t *out_pixels,
                                      size_t *out_rgba_len) {
    if (w <= 0 || h <= 0 || (size_t)w > SIZE_MAX / (size_t)h)
        return DNF_ERR_INVALID_FORMAT;
    size_t pixels = (size_t)w * (size_t)h;
    if (pixels > (size_t)INT_MAX / 4u || pixels > SIZE_MAX / 4u)
        return DNF_ERR_INVALID_FORMAT;
    if (out_pixels) *out_pixels = pixels;
    if (out_rgba_len) *out_rgba_len = pixels * 4u;
    return DNF_OK;
}

static DnfError decode_transparent_frame(const ImageFrame *f,
                                         uint8_t **out_rgba,
                                         int *out_w,
                                         int *out_h) {
    if (!f) return DNF_ERR_INVALID_FORMAT;
    size_t len = 0;
    DnfError e = checked_pixel_lengths(f->w, f->h, NULL, &len);
    if (e) return e;
    uint8_t *rgba = (uint8_t *)calloc(1, len);
    if (!rgba) return DNF_ERR_OOM;

    if (out_rgba) *out_rgba = rgba; else free(rgba);
    if (out_w) *out_w = f->w;
    if (out_h) *out_h = f->h;
    return DNF_OK;
}

DNFLIB_API DnfError dnf_image_decode(ImgFile *img, int frame_index,
                                     uint8_t **out_rgba, int *out_w, int *out_h) {
    return dnf_image_decode_with_palette(img, frame_index, 0, out_rgba, out_w, out_h);
}

DNFLIB_API DnfError dnf_image_decode_with_palette(ImgFile *img, int frame_index,
                                                  int palette_index,
                                                  uint8_t **out_rgba,
                                                  int *out_w,
                                                  int *out_h) {
    if (!img || frame_index < 0 || frame_index >= img->frame_count)
        return DNF_ERR_INVALID_INDEX;
    if (palette_index < 0)
        return DNF_ERR_INVALID_INDEX;

    int resolved_index = frame_index;
    int link_hops = 0;
    while (img->frames[resolved_index].format == DNF_FMT_LINK) {
        if (link_hops++ >= img->frame_count)
            return DNF_ERR_INVALID_FORMAT;
        int next = img->frames[resolved_index].link_index;
        if (next < 0 || next >= img->frame_count)
            return DNF_ERR_INVALID_INDEX;
        resolved_index = next;
    }
    ImageFrame *f = &img->frames[resolved_index];
    frame_index = resolved_index;

    size_t pixel_count = 0;
    size_t rgba_len = 0;
    DnfError dimension_error = checked_pixel_lengths(
        f->w, f->h, &pixel_count, &rgba_len);
    if (dimension_error) return dimension_error;

    /* Ver5 Sprite Atlas frames: data == NULL, data comes from ImgPriv.sprites[] */
    ImgPriv *priv = (ImgPriv *)img->_priv;
    if (!f->data || f->data_size <= 0) {
        if (f->compress != DNF_COMPRESS_DDS_ZLIB || !priv ||
            !priv->sprite_exts || !priv->sprites) {
            return DNF_ERR_INVALID_FORMAT;
        }
        SpriteFrameExt *ext = &priv->sprite_exts[frame_index];
        if (ext->sprite_index < 0 || ext->sprite_index >= priv->sprite_count)
            return DNF_ERR_INVALID_INDEX;
        ImgSprite *sp = &priv->sprites[ext->sprite_index];

        /* Decompress sprite DDS data if not yet cached */
        if (!sp->raw_data) {
            if (!sp->zip_data || sp->data_size <= 0) return DNF_ERR_INVALID_FORMAT;
            DnfError e2 = dnf_zlib_decompress(sp->zip_data, (size_t)sp->data_size,
                                               &sp->raw_data, &sp->raw_len);
            if (e2) return e2;
        }

        /* Decode full sprite texture */
        uint8_t *full_rgba = NULL;
        int sw = 0, sh = 0;
        DnfError e2 = dxt_decode_dds(sp->raw_data, sp->raw_len,
                                      &full_rgba, &sw, &sh);
        if (e2) return e2;

        /* Crop the frame region from full texture */
        int fl = ext->left, ft = ext->top, fr = ext->right, fb = ext->bottom;
        if (fl < 0) fl = 0;
        if (fl > sw) fl = sw;
        if (ft < 0) ft = 0;
        if (ft > sh) ft = sh;
        if (fr < 0) fr = 0;
        if (fr > sw) fr = sw;
        if (fb < 0) fb = 0;
        if (fb > sh) fb = sh;
        if (fr <= fl || fb <= ft) {
            free(full_rgba);
            return DNF_ERR_INVALID_FORMAT;
        }
        int fw = fr - fl;
        int fh = fb - ft;

        size_t crop_len = 0;
        e2 = checked_pixel_lengths(fw, fh, NULL, &crop_len);
        if (e2) { free(full_rgba); return e2; }
        uint8_t *crop = (uint8_t *)malloc(crop_len);
        if (!crop) { free(full_rgba); return DNF_ERR_OOM; }
        int row;
        for (row = 0; row < fh; row++) {
            memcpy(crop + (size_t)row * (size_t)fw * 4u,
                   full_rgba + ((size_t)(ft + row) * (size_t)sw + (size_t)fl) * 4u,
                   (size_t)fw * 4u);
        }
        free(full_rgba);

        if (out_rgba) *out_rgba = crop; else free(crop);
        if (out_w) *out_w = fw;
        if (out_h) *out_h = fh;
        return DNF_OK;
    }

    int w = f->w, h = f->h;
    if (w <= 0 || h <= 0) return DNF_ERR_INVALID_FORMAT;
    DnfError e;

    /* Step 1: decompress if needed */
    uint8_t *raw = NULL;
    size_t   raw_len = 0;
    int      raw_owned = 0;

    if (f->compress == DNF_COMPRESS_ZLIB || f->compress == DNF_COMPRESS_DDS_ZLIB) {
        e = dnf_zlib_decompress(f->data, (size_t)f->data_size, &raw, &raw_len);
        if (e) {
            if (is_canvas_placeholder_frame(f))
                return decode_transparent_frame(f, out_rgba, out_w, out_h);
            return e;
        }
        raw_owned = 1;
    } else {
        raw = f->data;
        raw_len = (size_t)f->data_size;
    }

    /* Step 2: DDS formats → use dxt decoder (raw is a DDS file) */
    if (f->format == DNF_FMT_DXT_1 ||
        f->format == DNF_FMT_DXT_3 ||
        f->format == DNF_FMT_DXT_5) {
        int dw = 0, dh = 0;
        e = dxt_decode_dds(raw, raw_len, out_rgba, &dw, &dh);
        if (raw_owned) free(raw);
        if (e) return e;
        if (out_w) *out_w = dw;
        if (out_h) *out_h = dh;
        return DNF_OK;
    }

    /* Step 3: palette-indexed formats (Ver4/5/6).
     * ARGB1555 frames with NONE compression store native 16-bit pixels even
     * when the IMG has a ColorBoard; only ZLIB payloads store byte indices. */
    if (f->format == DNF_FMT_ARGB_1555 &&
        f->compress == DNF_COMPRESS_ZLIB &&
        priv && priv->palette_count > 0) {
        if (palette_index >= priv->palette_count) {
            if (raw_owned) free(raw);
            return DNF_ERR_INVALID_INDEX;
        }
        /* palette indexed: raw bytes are 1-byte indices */
        if (raw_len < pixel_count) {
            if (raw_owned) free(raw);
            return DNF_ERR_INVALID_FORMAT;
        }
        uint8_t *rgba = (uint8_t *)malloc(rgba_len);
        if (!rgba) { if (raw_owned) free(raw); return DNF_ERR_OOM; }
        e = palette_decode(&priv->palettes[palette_index], raw, (int)pixel_count, rgba);
        if (raw_owned) free(raw);
        if (e) { free(rgba); return e; }
        if (out_rgba) *out_rgba = rgba;
        if (out_w) *out_w = w;
        if (out_h) *out_h = h;
        return DNF_OK;
    }

    /* Step 4: direct pixel format conversion */
    int ps = pixel_size(f->format);
    if (ps <= 0 || pixel_count > SIZE_MAX / (size_t)ps ||
        raw_len < pixel_count * (size_t)ps) {
        if (raw_owned) free(raw);
        return DNF_ERR_INVALID_FORMAT;
    }
    uint8_t *rgba = (uint8_t *)malloc(rgba_len);
    if (!rgba) { if (raw_owned) free(raw); return DNF_ERR_OOM; }

    e = pixel_to_rgba(raw, w, h, f->format, rgba);
    if (raw_owned) free(raw);
    if (e) { free(rgba); return e; }

    if (out_rgba) *out_rgba = rgba;
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
    return DNF_OK;
}

/* ------------------------------------------------------------------ */
/* Image encode                                                        */
/* ------------------------------------------------------------------ */

DNFLIB_API DnfError dnf_image_encode(const uint8_t *rgba, int w, int h,
                                     DnfImageFormat fmt, DnfCompressMode compress,
                                     uint8_t **out_data, int32_t *out_size) {
    if (!rgba || w <= 0 || h <= 0 || !out_data || !out_size)
        return DNF_ERR_INVALID_FORMAT;

    /* DXT encoding not supported (write-back only for uncompressed formats) */
    if (fmt == DNF_FMT_DXT_1 || fmt == DNF_FMT_DXT_3 || fmt == DNF_FMT_DXT_5)
        return DNF_ERR_UNSUPPORTED_VERSION;

    int ps = pixel_size(fmt);
    if (ps <= 0) return DNF_ERR_INVALID_FORMAT;

    size_t pixel_count = 0;
    DnfError dimension_error = checked_pixel_lengths(w, h, &pixel_count, NULL);
    if (dimension_error || pixel_count > (size_t)INT32_MAX / (size_t)ps)
        return DNF_ERR_INVALID_FORMAT;
    if (compress != DNF_COMPRESS_NONE && compress != DNF_COMPRESS_ZLIB)
        return DNF_ERR_UNSUPPORTED_VERSION;

    size_t raw_len = pixel_count * (size_t)ps;
    uint8_t *raw = (uint8_t *)malloc(raw_len);
    if (!raw) return DNF_ERR_OOM;

    DnfError e = pixel_from_rgba(rgba, w, h, fmt, raw);
    if (e) { free(raw); return e; }

    if (compress == DNF_COMPRESS_ZLIB) {
        uint8_t *zlib_data = NULL;
        size_t   zlib_len  = 0;
        e = dnf_zlib_compress(raw, raw_len, &zlib_data, &zlib_len);
        free(raw);
        if (e) return e;
        if (zlib_len > (size_t)INT32_MAX) {
            free(zlib_data);
            return DNF_ERR_INVALID_FORMAT;
        }
        *out_data = zlib_data;
        *out_size = (int32_t)zlib_len;
    } else {
        *out_data = raw;
        *out_size = (int32_t)raw_len;
    }
    return DNF_OK;
}

/* ------------------------------------------------------------------ */
/* Generic free                                                        */
/* ------------------------------------------------------------------ */

DNFLIB_API void dnf_free(void *ptr) {
    free(ptr);
}
