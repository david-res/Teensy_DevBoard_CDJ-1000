#include "Arduino.h"
#include "globals.h"
#include "letter_renderer.h"
#include <stdlib.h>
#include <string.h>

#if defined(USE_LETTER_BOXES)


// ---------------------------------------------------------------------------
// Fixed-point + swap macros — available to all functions in this file
// ---------------------------------------------------------------------------

#define FP_SHIFT 16
#define FP_ONE   (1 << FP_SHIFT)

#define SWAP16(a, b) do { int16_t _t = (a); (a) = (b); (b) = _t; } while(0)

// ---------------------------------------------------------------------------
// Internal types
// ---------------------------------------------------------------------------

// Glyph alpha mask — one uint8_t per pixel (0=box colour, 255=white).
// LVGL renders antialiased glyphs so we capture full brightness for blending.
// Memory: 8 * 11 * 8 letters = 704 bytes total.

#define MASK_STRIDE  LR_GLYPH_WIDTH
#define MASK_SIZE    (LR_GLYPH_WIDTH * LR_GLYPH_HEIGHT)

typedef struct {
    uint8_t alpha[MASK_SIZE];  // per-pixel brightness 0-255
} glyph_mask_t;

static glyph_mask_t s_glyphs[LR_NUM_LETTERS];
static bool         s_glyphs_ready = false;

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

// RGB565 -> 8-bit luminance for sampling the LVGL canvas
static inline uint8_t _rgb565_to_luma(uint16_t px) {
    uint16_t r8 = ((px >> 11) & 0x1Fu) << 3;
    uint16_t g8 = ((px >>  5) & 0x3Fu) << 2;
    uint16_t b8 =  (px        & 0x1Fu) << 3;
    return (uint8_t)((r8 + g8 + b8) / 3u);
}

// Blend two RGB565 colours by 8-bit alpha (0=a, 255=b), integer only
static inline uint16_t _blend565(uint16_t a, uint16_t b, uint8_t alpha) {
    int16_t ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
    int16_t br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
    uint16_t or_ = (uint16_t)(ar + (((br - ar) * alpha) >> 8));
    uint16_t og  = (uint16_t)(ag + (((bg - ag) * alpha) >> 8));
    uint16_t ob  = (uint16_t)(ab + (((bb - ab) * alpha) >> 8));
    return (or_ << 11) | (og << 5) | ob;
}

// ---------------------------------------------------------------------------
// Pre-render  (FLASHMEM — slow path, called once at startup)
// ---------------------------------------------------------------------------

FLASHMEM bool lr_prerender_glyphs(const lv_font_t *font) {
    // We render each letter onto a small temporary LVGL canvas (white on black),
    // then sample the pixels to build the compact 1-bpp mask.  The canvas pixel
    // format is RGB565 so "white" == 0xFFFF and "black" == 0x0000.

    const uint32_t canvas_bytes = (uint32_t)LR_GLYPH_WIDTH *
                                  (uint32_t)LR_GLYPH_HEIGHT *
                                  LR_BYTES_PER_PIXEL;

    uint16_t *canvas_pixels = (uint16_t *)malloc(canvas_bytes);
    if (!canvas_pixels) return false;

    lv_obj_t *canvas = lv_canvas_create(lv_scr_act());
    if (!canvas) {
        free(canvas_pixels);
        return false;
    }

#if (LVGL_VERSION_MAJOR == 8)
    lv_canvas_set_buffer(canvas, canvas_pixels,
                         LR_GLYPH_WIDTH, LR_GLYPH_HEIGHT,
                         LV_IMG_CF_TRUE_COLOR);
#endif
#if (LVGL_VERSION_MAJOR == 9)
    lv_canvas_set_buffer(canvas, canvas_pixels,
                         LR_GLYPH_WIDTH, LR_GLYPH_HEIGHT,
                         LV_COLOR_FORMAT_RGB565);
#endif

    // Colours used during rendering: white text on black background so the
    // contrast is maximum and easy to threshold.
    const lv_color_t WHITE = lv_color_white();
    const lv_color_t BLACK = lv_color_black();

    // Draw label descriptor — reused for every letter
    lv_draw_label_dsc_t label_dsc;
    lv_draw_label_dsc_init(&label_dsc);
    label_dsc.color = WHITE;
    label_dsc.font  = font;

    for (uint8_t i = 0; i < LR_NUM_LETTERS; i++) {
        // Build a single-character string without any string library:
        // just two bytes on the stack — the character and a null terminator.
        char letter_str[2];
        letter_str[0] = (char)('A' + i);
        letter_str[1] = '\0';

        // Clear the canvas to black
        lv_canvas_fill_bg(canvas, BLACK, LV_OPA_COVER);

#if (LVGL_VERSION_MAJOR == 8)
        lv_point_t text_size;
        lv_txt_get_size(&text_size, letter_str, font, 0, 0,
                        LV_COORD_MAX, LV_TEXT_FLAG_NONE);

        int16_t ox = (LR_GLYPH_WIDTH  - text_size.x) / 2;
        int16_t oy = (LR_GLYPH_HEIGHT - text_size.y) / 2;

        lv_canvas_draw_text(canvas, ox, oy, LR_GLYPH_WIDTH, &label_dsc, letter_str);
#endif

#if (LVGL_VERSION_MAJOR == 9)
        lv_point_t text_size;
        lv_text_get_size(&text_size, letter_str, font, 0, 0,
                         LV_COORD_MAX, LV_TEXT_FLAG_NONE);

        int16_t ox = (LR_GLYPH_WIDTH  - text_size.x) / 2;
        int16_t oy = (LR_GLYPH_HEIGHT - text_size.y) / 2;

        lv_layer_t layer;
        lv_canvas_init_layer(canvas, &layer);

        label_dsc.text = letter_str;

        lv_area_t coords = {
            .x1 = ox,
            .y1 = oy,
            .x2 = (lv_coord_t)(ox + text_size.x - 1),
            .y2 = (lv_coord_t)(oy + text_size.y - 1)
        };
        lv_draw_label(&layer, &label_dsc, &coords);
        lv_canvas_finish_layer(canvas, &layer);
#endif

        // Sample the canvas into the 8-bit alpha mask.
        // Convert each RGB565 pixel to a luminance value 0-255.
        // Black (0x0000) -> 0, white (0xFFFF) -> 255, AA grey -> mid value.
        for (uint16_t row = 0; row < LR_GLYPH_HEIGHT; row++) {
            for (uint16_t col = 0; col < LR_GLYPH_WIDTH; col++) {
                uint16_t px = canvas_pixels[row * LR_GLYPH_WIDTH + col];
                s_glyphs[i].alpha[row * MASK_STRIDE + col] = _rgb565_to_luma(px);
            }
        }

        Serial.printf("lr_prerender: '%c' rendered, glyph %dx%d offset (%d,%d)\n",
                      letter_str[0], text_size.x, text_size.y, ox, oy);
    }

    lv_obj_del(canvas);
    free(canvas_pixels);

    s_glyphs_ready = true;
    return true;
}

// ---------------------------------------------------------------------------
// Blit letter box  (FASTRUN — called from tight render loops)
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Shared clip helper
// ---------------------------------------------------------------------------
// Given a rect [x, x+w) against canvas [0, canvas_width), fills:
//   *out_x0  — first canvas column to write  (0 … canvas_width-1)
//   *out_x1  — one-past-last canvas column   (0 … canvas_width)
//   *out_src — offset into the rect's own row where writing starts (≥0)
// Returns false when the rect is entirely outside (nothing to draw).
static inline bool _clip_x(int16_t x, uint16_t w, uint16_t canvas_width,
                            int16_t *out_x0, int16_t *out_x1,
                            int16_t *out_src_off) {
    int16_t x0 = x;
    int16_t x1 = x + (int16_t)w;           // exclusive

    if (x1 <= 0 || x0 >= (int16_t)canvas_width) return false;  // fully off

    *out_src_off = (x0 < 0) ? -x0 : 0;     // how many left pixels we skip
    if (x0 < 0)                     x0 = 0;
    if (x1 > (int16_t)canvas_width) x1 = (int16_t)canvas_width;

    *out_x0 = x0;
    *out_x1 = x1;
    return true;
}

// ---------------------------------------------------------------------------
// Blit letter box  (FASTRUN — called from tight render loops)
// ---------------------------------------------------------------------------

FASTRUN void lr_blit_letter_box(uint16_t *canvas_buf, uint16_t canvas_width,
                                int16_t x, int16_t y,
                                uint8_t letter_index, uint16_t box_color) {
    if (!s_glyphs_ready)               return;
    if (letter_index >= LR_NUM_LETTERS) return;

    const glyph_mask_t *glyph = &s_glyphs[letter_index];

    // --- Clip box rect to canvas X range ------------------------------------
    int16_t bx0, bx1, bsrc;
    if (!_clip_x(x, LR_BOX_WIDTH, canvas_width, &bx0, &bx1, &bsrc)) return;
    const int16_t vis_w = bx1 - bx0;   // number of visible columns (≥1)

    // --- 1. Fill the visible portion of each box row ------------------------
    //
    // Build a prototype run of `vis_w` pixels at bx0, then memcpy it to the
    // remaining rows.  This is the same prototype-row trick as before but
    // working only within the clipped width — no writes past canvas_width.

    uint16_t *proto = canvas_buf + (y * canvas_width + bx0);
    for (int16_t px = 0; px < vis_w; px++) proto[px] = box_color;

    for (uint16_t row = 1; row < LR_BOX_HEIGHT; row++) {
        memcpy(canvas_buf + ((y + row) * canvas_width + bx0),
               proto,
               vis_w * sizeof(uint16_t));
    }

    // --- 2. Composite white glyph pixels — only within clipped X range ------
    //
    // The glyph lives at (x + LR_PAD_X, y + LR_PAD_Y) in canvas space.
    // For each mask row we walk the bytes but only write pixels whose canvas X
    // coordinate falls inside [bx0, bx1).

    const int16_t gx = x + LR_PAD_X;
    const int16_t gy = y + LR_PAD_Y;

    for (uint16_t row = 0; row < LR_GLYPH_HEIGHT; row++) {
        uint16_t *dst_row = canvas_buf + ((gy + row) * canvas_width);
        const uint8_t *alpha_row = glyph->alpha + row * MASK_STRIDE;

        for (uint16_t col = 0; col < LR_GLYPH_WIDTH; col++) {
            uint8_t a = alpha_row[col];
            if (a == 0) continue;   // fully transparent — skip

            int16_t cx = gx + (int16_t)col;
            if (cx < bx0 || cx >= bx1) continue;   // outside clip rect

            // Blend: box_color -> white by alpha
            dst_row[cx] = (a == 255) ? 0x0000u : _blend565(box_color, 0x0000u, a);
        }
    }
}


// ---------------------------------------------------------------------------
// Blit plain box  (FASTRUN)
// ---------------------------------------------------------------------------

FASTRUN void lr_blit_box(uint16_t *canvas_buf, uint16_t canvas_width,
                         int16_t x, int16_t y,
                         uint16_t box_color) {
    int16_t bx0, bx1, bsrc;
    if (!_clip_x(x, LR_BOX_WIDTH, canvas_width, &bx0, &bx1, &bsrc)) return;
    const int16_t vis_w = bx1 - bx0;

    // Fill prototype row then memcpy to remaining rows — same as letter box.
    uint16_t *proto = canvas_buf + (y * canvas_width + bx0);
    for (int16_t px = 0; px < vis_w; px++) proto[px] = box_color;
    for (uint16_t row = 1; row < LR_BOX_HEIGHT; row++) {
        memcpy(canvas_buf + ((y + row) * canvas_width + bx0),
               proto, vis_w * sizeof(uint16_t));
    }
}
// ---------------------------------------------------------------------------
// Triangle shape: build (FLASHMEM) + blit (FASTRUN)
// ---------------------------------------------------------------------------

// Internal scanline fill into a 1-bpp mask buffer.
// Same edge-walking logic as lr_draw_triangle but writes bits, not pixels.

#define TR_MASK_STRIDE_  ((TR_MAX_DIM + 7) / 8)

static inline void _tr_mask_set(uint8_t *mask, int16_t x, int16_t y) {
    if (x < 0 || x >= TR_MAX_DIM || y < 0 || y >= TR_MAX_DIM) return;
    mask[y * TR_MASK_STRIDE_ + (x >> 3)] |= (uint8_t)(0x80u >> (x & 7u));
}

static FLASHMEM void _tr_fill_hspan_mask(uint8_t *mask,
                                         uint16_t w, uint16_t h,
                                         int16_t y,
                                         int32_t xl_fp, int32_t xr_fp) {
    if (y < 0 || y >= (int16_t)h) return;
    int16_t xl = (int16_t)(xl_fp >> FP_SHIFT);
    int16_t xr = (int16_t)(xr_fp >> FP_SHIFT);
    if (xl > xr) { int16_t t = xl; xl = xr; xr = t; }
    if (xr < 0 || xl >= (int16_t)w) return;
    if (xl < 0)            xl = 0;
    if (xr >= (int16_t)w)  xr = (int16_t)w - 1;
    for (int16_t x = xl; x <= xr; x++) _tr_mask_set(mask, x, y);
}

static FLASHMEM void _tr_fill_half_mask(uint8_t *mask,
                                        uint16_t w, uint16_t h,
                                        int16_t y_top, int16_t y_bot,
                                        int32_t xl_fp, int32_t dxl,
                                        int32_t xr_fp, int32_t dxr) {
    for (int16_t y = y_top; y <= y_bot; y++) {
        _tr_fill_hspan_mask(mask, w, h, y, xl_fp, xr_fp);
        xl_fp += dxl;
        xr_fp += dxr;
    }
}

FLASHMEM void lr_build_triangle_shape(triangle_shape_t *shape,
                                      int16_t x0, int16_t y0,
                                      int16_t x1, int16_t y1,
                                      int16_t x2, int16_t y2,
                                      uint16_t bbox_w, uint16_t bbox_h) {
    // Zero the mask
    for (uint16_t i = 0; i < TR_MASK_SIZE; i++) shape->mask[i] = 0;
    shape->w = bbox_w;
    shape->h = bbox_h;

    // Sort by Y
    if (y0 > y1) { SWAP16(y0,y1); SWAP16(x0,x1); }
    if (y1 > y2) { SWAP16(y1,y2); SWAP16(x1,x2); }
    if (y0 > y1) { SWAP16(y0,y1); SWAP16(x0,x1); }

    int16_t dy_full = y2 - y0;
    int16_t dy_top  = y1 - y0;
    int16_t dy_bot  = y2 - y1;

    if (dy_full == 0) {
        _tr_fill_hspan_mask(shape->mask, bbox_w, bbox_h, y0,
                            (int32_t)x0 << FP_SHIFT,
                            (int32_t)x2 << FP_SHIFT);
        return;
    }

    int32_t dx_long      = (int32_t)(x2 - x0) * FP_ONE / dy_full;
    int32_t xl_fp        = (int32_t)x0 << FP_SHIFT;
    int32_t x_long_at_y1 = ((int32_t)x0 << FP_SHIFT) + dx_long * dy_top;

    if (dy_top > 0) {
        int32_t dx_short = (int32_t)(x1 - x0) * FP_ONE / dy_top;
        int32_t xs_fp    = (int32_t)x0 << FP_SHIFT;
        if ((int32_t)x1 < (x_long_at_y1 >> FP_SHIFT))
            _tr_fill_half_mask(shape->mask, bbox_w, bbox_h,
                               y0, y1, xs_fp, dx_short, xl_fp, dx_long);
        else
            _tr_fill_half_mask(shape->mask, bbox_w, bbox_h,
                               y0, y1, xl_fp, dx_long, xs_fp, dx_short);
    } else {
        _tr_fill_hspan_mask(shape->mask, bbox_w, bbox_h, y0,
                            (int32_t)x0 << FP_SHIFT,
                            (int32_t)x1 << FP_SHIFT);
    }

    if (dy_bot > 0) {
        int32_t dx_short2 = (int32_t)(x2 - x1) * FP_ONE / dy_bot;
        int32_t xl_mid    = (int32_t)x0 * FP_ONE + dx_long * dy_top;
        int32_t xs2_fp    = (int32_t)x1 << FP_SHIFT;
        if ((int32_t)x1 < (xl_mid >> FP_SHIFT))
            _tr_fill_half_mask(shape->mask, bbox_w, bbox_h,
                               y1, y2, xs2_fp, dx_short2, xl_mid, dx_long);
        else
            _tr_fill_half_mask(shape->mask, bbox_w, bbox_h,
                               y1, y2, xl_mid, dx_long, xs2_fp, dx_short2);
    } else {
        _tr_fill_hspan_mask(shape->mask, bbox_w, bbox_h, y2,
                            (int32_t)x1 << FP_SHIFT,
                            (int32_t)x2 << FP_SHIFT);
    }
}

FASTRUN void lr_blit_triangle(uint16_t *canvas_buf,
                              uint16_t canvas_width, uint16_t canvas_height,
                              int16_t x, int16_t y,
                              const triangle_shape_t *shape,
                              uint16_t color) {
    // Clip bounding box to canvas X range once — shared helper above.
    int16_t bx0, bx1, bsrc;
    if (!_clip_x(x, shape->w, canvas_width, &bx0, &bx1, &bsrc)) return;

    for (uint16_t row = 0; row < shape->h; row++) {
        int16_t cy = y + (int16_t)row;
        if (cy < 0 || cy >= (int16_t)canvas_height) continue;

        uint16_t *dst_row        = canvas_buf + cy * canvas_width;
        const uint8_t *mask_row = shape->mask + row * TR_MASK_STRIDE_;

        for (uint16_t byte_idx = 0; byte_idx < TR_MASK_STRIDE_; byte_idx++) {
            uint8_t mb = mask_row[byte_idx];
            if (!mb) continue;

            uint16_t bit_base = byte_idx << 3;
#define TR_WRITE_IF_VIS(BIT, OFF) \
            if (mb & (BIT)) { \
                int16_t cx = x + (int16_t)(bit_base + (OFF)); \
                if (cx >= bx0 && cx < bx1) dst_row[cx] = color; \
            }
            TR_WRITE_IF_VIS(0x80u, 0)
            TR_WRITE_IF_VIS(0x40u, 1)
            TR_WRITE_IF_VIS(0x20u, 2)
            TR_WRITE_IF_VIS(0x10u, 3)
            TR_WRITE_IF_VIS(0x08u, 4)
            TR_WRITE_IF_VIS(0x04u, 5)
            TR_WRITE_IF_VIS(0x02u, 6)
            TR_WRITE_IF_VIS(0x01u, 7)
#undef TR_WRITE_IF_VIS
        }
    }
}

// ---------------------------------------------------------------------------
// Triangle rasteriser  (FASTRUN)
// ---------------------------------------------------------------------------
//
// Classic scanline fill using fixed-point edge walking.
// No floating point, no stdlib, no divisions in the inner loop.
//
// Algorithm:
//   1. Sort vertices by Y so v0.y <= v1.y <= v2.y.
//   2. Split triangle at the middle vertex into a flat-bottom and a flat-top
//      half (or handle degenerate cases).
//   3. For each half, walk the two active edges using Bresenham-style
//      fixed-point accumulators and fill the horizontal span.

// Fill a horizontal span [x_left, x_right] on row y.
// Clamps to [0, canvas_width) automatically.
static FASTRUN inline void _fill_hspan(uint16_t *canvas_buf,
                                       uint16_t canvas_width,
                                       uint16_t canvas_height,
                                       int16_t y,
                                       int32_t xl_fp, int32_t xr_fp,
                                       uint16_t color) {
    if (y < 0 || y >= (int16_t)canvas_height) return;

    // Convert fixed-point edges to integer pixel coordinates
    int16_t xl = (int16_t)(xl_fp >> FP_SHIFT);
    int16_t xr = (int16_t)(xr_fp >> FP_SHIFT);

    // Ensure left <= right
    if (xl > xr) { int16_t t = xl; xl = xr; xr = t; }

    // Clamp to canvas
    if (xr < 0 || xl >= (int16_t)canvas_width) return;
    if (xl < 0)                     xl = 0;
    if (xr >= (int16_t)canvas_width) xr = (int16_t)canvas_width - 1;

    uint16_t *row = canvas_buf + (y * canvas_width);

    // Fill first pixel to create a prototype word, then use memset-style fill.
    // For uint16_t we can't use memset directly; use a small loop — the
    // compiler will auto-vectorise this on Cortex-M7 (Teensy 4.x).
    int16_t span = xr - xl + 1;
    uint16_t *p = row + xl;
    for (int16_t i = 0; i < span; i++) {
        p[i] = color;
    }
}

// Rasterise one trapezoid half:
//   y from y_top to y_bot (inclusive),
//   left edge starts at xl_fp and steps by dxl per row,
//   right edge starts at xr_fp and steps by dxr per row.
static FASTRUN void _fill_half(uint16_t *buf,
                               uint16_t cw, uint16_t ch,
                               int16_t y_top, int16_t y_bot,
                               int32_t xl_fp, int32_t dxl,
                               int32_t xr_fp, int32_t dxr,
                               uint16_t color) {
    for (int16_t y = y_top; y <= y_bot; y++) {
        _fill_hspan(buf, cw, ch, y, xl_fp, xr_fp, color);
        xl_fp += dxl;
        xr_fp += dxr;
    }
}

FASTRUN void lr_draw_triangle(uint16_t *canvas_buf,
                              uint16_t canvas_width, uint16_t canvas_height,
                              int16_t x0, int16_t y0,
                              int16_t x1, int16_t y1,
                              int16_t x2, int16_t y2,
                              uint16_t color) {
    // --- Sort vertices by Y (bubble sort — 3 elements, always O(1)) ----------
    if (y0 > y1) { SWAP16(y0, y1); SWAP16(x0, x1); }
    if (y1 > y2) { SWAP16(y1, y2); SWAP16(x1, x2); }
    if (y0 > y1) { SWAP16(y0, y1); SWAP16(x0, x1); }
    // Now: y0 <= y1 <= y2

    int16_t dy_full = y2 - y0;
    int16_t dy_top  = y1 - y0;
    int16_t dy_bot  = y2 - y1;

    // Degenerate: all points on the same horizontal line
    if (dy_full == 0) return;

    // --- Compute fixed-point edge slopes -------------------------------------
    //
    // Long edge (v0 -> v2): always spans the full height
    // Top half:  short edge v0 -> v1
    // Bot half:  short edge v1 -> v2
    //
    // slope = delta_x / delta_y, stored as FP 16.16.
    // We avoid division by using the reciprocal form:
    //   dx_per_row = (dx << FP_SHIFT) / dy
    // Division is done once per edge, never inside the scanline loop.

    // Long edge slope
    int32_t dx_long = (int32_t)(x2 - x0) * FP_ONE / dy_full;

    // X on the long edge at v1's Y, for deciding left/right
    int32_t x_long_at_y1_fp = ((int32_t)x0 << FP_SHIFT) +
                               dx_long * dy_top;

    // Starting fixed-point X for the long edge at y0
    int32_t xl_fp = (int32_t)x0 << FP_SHIFT;

    // --- Top half: y0 to y1 --------------------------------------------------
    if (dy_top > 0) {
        int32_t dx_short = (int32_t)(x1 - x0) * FP_ONE / dy_top;

        // Determine which is the left edge for this half
        int32_t x_short_fp = (int32_t)x0 << FP_SHIFT;

        // At y1 the short edge reaches x1; compare to the long edge there
        if ((int32_t)x1 < (x_long_at_y1_fp >> FP_SHIFT)) {
            // short edge is on the left
            _fill_half(canvas_buf, canvas_width, canvas_height,
                       y0, y1,
                       x_short_fp, dx_short,   // left
                       xl_fp,      dx_long,    // right
                       color);
        } else {
            // short edge is on the right
            _fill_half(canvas_buf, canvas_width, canvas_height,
                       y0, y1,
                       xl_fp,      dx_long,    // left
                       x_short_fp, dx_short,   // right
                       color);
        }
    } else {
        // Flat top: draw the single horizontal edge at y0
        _fill_hspan(canvas_buf, canvas_width, canvas_height,
                    y0,
                    (int32_t)x0 << FP_SHIFT,
                    (int32_t)x1 << FP_SHIFT,
                    color);
    }

    // --- Bottom half: y1 to y2 -----------------------------------------------
    if (dy_bot > 0) {
        int32_t dx_short2 = (int32_t)(x2 - x1) * FP_ONE / dy_bot;

        // Long edge X at y1
        int32_t xl_mid_fp = (int32_t)x0 * FP_ONE + dx_long * dy_top;
        int32_t x_short2_fp = (int32_t)x1 << FP_SHIFT;

        if ((int32_t)x1 < (xl_mid_fp >> FP_SHIFT)) {
            // x1 is left of long edge at mid-point
            _fill_half(canvas_buf, canvas_width, canvas_height,
                       y1, y2,
                       x_short2_fp, dx_short2,   // left
                       xl_mid_fp,   dx_long,      // right
                       color);
        } else {
            _fill_half(canvas_buf, canvas_width, canvas_height,
                       y1, y2,
                       xl_mid_fp,   dx_long,      // left
                       x_short2_fp, dx_short2,    // right
                       color);
        }
    } else {
        // Flat bottom: draw the single horizontal edge at y2
        _fill_hspan(canvas_buf, canvas_width, canvas_height,
                    y2,
                    (int32_t)x1 << FP_SHIFT,
                    (int32_t)x2 << FP_SHIFT,
                    color);
    }
}

// ---------------------------------------------------------------------------
// Cleanup
// ---------------------------------------------------------------------------

FLASHMEM void lr_free_glyphs(void) {
    // No heap allocations in the masks themselves (they are static storage),
    // so this is a simple flag reset.
    s_glyphs_ready = false;
}

#endif // USE_LETTER_BOXES