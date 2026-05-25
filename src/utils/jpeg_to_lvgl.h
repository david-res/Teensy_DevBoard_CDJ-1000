
#include "lvgl.h"
#include <USBHost_t36.h>

struct JpegImageHandle {
    lv_image_dsc_t  dsc;
    uint8_t        *buf;
};

// Pass the USBFilesystem reference you want to read from
bool jpeg_load(USBFilesystem &fs, const char *path, JpegImageHandle &handle);
void jpeg_image_free(JpegImageHandle &handle);