#include "jpeg_to_lvgl.h"
#include <JPEGDEC.h>

// ---------------------------------------------------------------------------
// File I/O callbacks
// ---------------------------------------------------------------------------

struct JPEGFileCtx {
    File           file;
    USBFilesystem *fs;
};

static void *jpegOpen(const char *filename, int32_t *pSize)
{
    extern USBFilesystem *g_jpeg_fs;
    JPEGFileCtx *ctx = new JPEGFileCtx();
    ctx->fs   = g_jpeg_fs;
    ctx->file = g_jpeg_fs->open(filename);
    if (!ctx->file) {
        delete ctx;
        return nullptr;
    }
    *pSize = ctx->file.size();
    return (void *)ctx;
}

static void jpegClose(void *pHandle)
{
    JPEGFileCtx *ctx = static_cast<JPEGFileCtx *>(pHandle);
    if (ctx) { ctx->file.close(); delete ctx; }
}

static int32_t jpegRead(JPEGFILE *pFile, uint8_t *pBuf, int32_t iLen)
{
    JPEGFileCtx *ctx = static_cast<JPEGFileCtx *>(pFile->fHandle);
    return (int32_t)ctx->file.read(pBuf, iLen);
}

static int32_t jpegSeek(JPEGFILE *pFile, int32_t iPosition)
{
    JPEGFileCtx *ctx = static_cast<JPEGFileCtx *>(pFile->fHandle);
    return ctx->file.seek(iPosition) ? iPosition : -1;
}

// ---------------------------------------------------------------------------
// Draw callback — int return type to match this version of JPEGDEC
// pUser not available in decode(), so we use a file-scope pointer
// ---------------------------------------------------------------------------

static JpegImageHandle *g_current_handle = nullptr;

static int jpegDraw(JPEGDRAW *pDraw)
{
    if (!g_current_handle) return 0;
    const uint16_t img_width = g_current_handle->dsc.header.w;

    for (int row = 0; row < pDraw->iHeight; row++) {
        uint16_t *dst = reinterpret_cast<uint16_t *>(g_current_handle->buf)
                        + (uint32_t)(pDraw->y + row) * img_width + pDraw->x;
        const uint16_t *src = pDraw->pPixels + (uint32_t)row * pDraw->iWidth;
        memcpy(dst, src, pDraw->iWidth * sizeof(uint16_t));
    }
    return 1;
}

// ---------------------------------------------------------------------------

USBFilesystem *g_jpeg_fs = nullptr;

bool jpeg_load(USBFilesystem &fs, const char *path, JpegImageHandle &handle)
{
    handle.buf = nullptr;
    memset(&handle.dsc, 0, sizeof(handle.dsc));

    g_jpeg_fs = &fs;

    JPEGDEC jpeg;

    // First open: get dimensions
    if (!jpeg.open(path, jpegOpen, jpegClose, jpegRead, jpegSeek, jpegDraw)) {
        Serial.printf("jpeg_load: open failed for %s\n", path);
        g_jpeg_fs = nullptr;
        return false;
    }

    const uint16_t w = jpeg.getWidth();
    const uint16_t h = jpeg.getHeight();
    jpeg.close();

    const size_t buf_bytes = (size_t)w * h * 2;
    handle.buf = (uint8_t *)malloc(buf_bytes);
    if (!handle.buf) {
        Serial.printf("jpeg_load: malloc failed (%u bytes)\n", buf_bytes);
        g_jpeg_fs = nullptr;
        return false;
    }

    handle.dsc.header.magic  = LV_IMAGE_HEADER_MAGIC;
    handle.dsc.header.cf     = LV_COLOR_FORMAT_RGB565;
    handle.dsc.header.w      = w;
    handle.dsc.header.h      = h;
    handle.dsc.header.stride = w * 2;
    handle.dsc.data_size     = buf_bytes;
    handle.dsc.data          = handle.buf;

    // Second open: decode pixels
    if (!jpeg.open(path, jpegOpen, jpegClose, jpegRead, jpegSeek, jpegDraw)) {
        Serial.println("jpeg_load: second open failed");
        free(handle.buf);
        handle.buf = nullptr;
        g_jpeg_fs  = nullptr;
        return false;
    }

    jpeg.setPixelType(RGB565_LITTLE_ENDIAN);
    g_current_handle = &handle;
    jpeg.decode(0, 0, 0);
    g_current_handle = nullptr;
    jpeg.close();

    g_jpeg_fs = nullptr;
    return true;
}

void jpeg_image_free(JpegImageHandle &handle)
{
    free(handle.buf);
    handle.buf       = nullptr;
    handle.dsc.data  = nullptr;
}