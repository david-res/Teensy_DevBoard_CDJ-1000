#ifndef LETTER_RENDERER_H
#define LETTER_RENDERER_H

#include "lvgl.h"
#include <stdint.h>
#include <stdbool.h>
#include "globals.h"
#define USE_LETTER_BOXES

#if defined(USE_LETTER_BOXES)

// ---------------------------------------------------------------------------
// Configuration — adjust to match your font / tile size
// ---------------------------------------------------------------------------

#define LR_BYTES_PER_PIXEL  2           // RGB565
#define LR_GLYPH_WIDTH      8           // rendered glyph area (px)
#define LR_GLYPH_HEIGHT     11          // rendered glyph area (px)

// Box dimensions — may be larger than the glyph so the letter is centred
#define LR_BOX_WIDTH        12          // total box width  (px)
#define LR_BOX_HEIGHT       14          // total box height (px)

// Derived padding (centred)
#define LR_PAD_X  ((LR_BOX_WIDTH  - LR_GLYPH_WIDTH)  / 2)
#define LR_PAD_Y  ((LR_BOX_HEIGHT - LR_GLYPH_HEIGHT) / 2)

#define LR_NUM_LETTERS      8           // A-H

// ---------------------------------------------------------------------------
// Initialisation
// ---------------------------------------------------------------------------

/**
 * Pre-render white glyph masks for letters A-H using the supplied LVGL font.
 * Call once at startup (runs from FLASH — slow path is fine here).
 *
 * @param font      LVGL font pointer
 * @return true on success, false if any allocation failed
 *
 * NOTE: text colour is always white; the box colour is applied at blit time,
 *       so only one set of glyph masks is needed regardless of box colour.
 */
bool lr_prerender_glyphs(const lv_font_t *font);

// ---------------------------------------------------------------------------
// Draw calls  (FASTRUN — called from tight render loops)
// ---------------------------------------------------------------------------

/**
 * Draw letter 'A'+index (0=A … 7=H) into canvas_buf.
 * The box is filled with box_color; the glyph is composited in white on top.
 *
 * @param canvas_buf    Raw RGB565 canvas buffer
 * @param canvas_width  Canvas stride in pixels
 * @param x             Top-left x of the box
 * @param y             Top-left y of the box
 * @param letter_index  0 = 'A', 1 = 'B', … 7 = 'H'
 * @param box_color     RGB565 fill colour for the box background
 */
void lr_blit_letter_box(uint16_t *canvas_buf, uint16_t canvas_width,
                        int16_t x, int16_t y,
                        uint8_t letter_index, uint16_t box_color);

/**
 * Draw a plain coloured box with no letter — same dimensions as a letter box.
 * Use this when you want a solid colour block marker without any glyph.
 *
 * @param canvas_buf    Raw RGB565 canvas buffer
 * @param canvas_width  Canvas stride in pixels
 * @param x             Top-left x of the box
 * @param y             Top-left y of the box
 * @param box_color     RGB565 fill colour
 */
void lr_blit_box(uint16_t *canvas_buf, uint16_t canvas_width,
                 int16_t x, int16_t y,
                 uint16_t box_color);

// ---------------------------------------------------------------------------
// Triangle shape descriptor + pre-baked blit
// ---------------------------------------------------------------------------

// Maximum bounding box for any triangle shape.
// All vertex coordinates in a triangle_shape_t must fit within [0, TR_MAX_DIM).
#define TR_MAX_DIM  32

// Stride / size of the pre-baked 1-bpp mask inside triangle_shape_t
#define TR_MASK_STRIDE  ((TR_MAX_DIM + 7) / 8)
#define TR_MASK_SIZE    (TR_MASK_STRIDE * TR_MAX_DIM)

/**
 * Describes a triangle shape that can be stamped onto a canvas with any colour.
 *
 * Declare as a file-scope const and pass its address to lr_blit_triangle.
 * Call lr_build_triangle_shape() once (at startup / FLASHMEM) to rasterise the
 * three vertices into the internal 1-bpp mask.
 *
 * Example — a 12×10 right-pointing arrow head:
 *
 *   static triangle_shape_t s_arrow;
 *
 *   // in setup():
 *   lr_build_triangle_shape(&s_arrow,
 *       0, 0,    // top-left
 *       0, 9,    // bottom-left
 *      11, 4,    // right tip
 *      12, 10);  // bounding-box width, height
 *
 *   // in render loop:
 *   lr_blit_triangle(canvas, CANVAS_W, CANVAS_H, x, y, &s_arrow, 0xF800);
 */
typedef struct {
    uint8_t  mask[TR_MASK_SIZE]; // 1-bpp rasterised fill; row-major, MSB first
    uint16_t w;                  // bounding-box width  (pixels)
    uint16_t h;                  // bounding-box height (pixels)
} triangle_shape_t;

/**
 * Rasterise three vertices into a triangle_shape_t mask.
 * Call once at startup (FLASHMEM — slow path is fine).
 *
 * @param shape         Output shape to initialise
 * @param x0,y0         Vertex 0  (within [0, TR_MAX_DIM) )
 * @param x1,y1         Vertex 1
 * @param x2,y2         Vertex 2
 * @param bbox_w        Bounding-box width  — must be <= TR_MAX_DIM
 * @param bbox_h        Bounding-box height — must be <= TR_MAX_DIM
 */
void lr_build_triangle_shape(triangle_shape_t *shape,
                             int16_t x0, int16_t y0,
                             int16_t x1, int16_t y1,
                             int16_t x2, int16_t y2,
                             uint16_t bbox_w, uint16_t bbox_h);

/**
 * Stamp a pre-built triangle shape onto the canvas in the given colour.
 * The bounding box top-left is placed at (x, y).
 * Zero string functions, zero divisions in the inner loop.
 *
 * @param canvas_buf    Raw RGB565 canvas buffer
 * @param canvas_width  Canvas stride in pixels
 * @param canvas_height Canvas height in pixels (for clipping)
 * @param x             Top-left x of the bounding box
 * @param y             Top-left y of the bounding box
 * @param shape         Pointer to the pre-built triangle_shape_t
 * @param color         RGB565 fill colour
 */
void lr_blit_triangle(uint16_t *canvas_buf,
                      uint16_t canvas_width, uint16_t canvas_height,
                      int16_t x, int16_t y,
                      const triangle_shape_t *shape,
                      uint16_t color);

/**
 * Draw a filled triangle defined by three vertices into canvas_buf.
 * Uses integer scanline rasterisation — no FP, no stdlib calls.
 * Use this for one-off triangles; for repeated shapes prefer
 * lr_build_triangle_shape + lr_blit_triangle.
 *
 * Vertices may be supplied in any order (the function sorts them).
 *
 * @param canvas_buf    Raw RGB565 canvas buffer
 * @param canvas_width  Canvas stride in pixels
 * @param canvas_height Canvas height in pixels (for bounds-checking)
 * @param x0,y0         Vertex 0
 * @param x1,y1         Vertex 1
 * @param x2,y2         Vertex 2
 * @param color         RGB565 fill colour
 */
void lr_draw_triangle(uint16_t *canvas_buf,
                      uint16_t canvas_width, uint16_t canvas_height,
                      int16_t x0, int16_t y0,
                      int16_t x1, int16_t y1,
                      int16_t x2, int16_t y2,
                      uint16_t color);

// ---------------------------------------------------------------------------
// Cleanup
// ---------------------------------------------------------------------------

/** Free all pre-rendered glyph masks. */
void lr_free_glyphs(void);

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/** Box dimensions — useful for layout arithmetic in calling code. */
static inline uint16_t lr_box_width(void)  { return LR_BOX_WIDTH;  }
static inline uint16_t lr_box_height(void) { return LR_BOX_HEIGHT; }

#endif // USE_LETTER_BOXES
#endif // LETTER_RENDERER_H