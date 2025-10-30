#ifndef DIGIT_RENDERER_H
#define DIGIT_RENDERER_H

#include "lvgl.h"
#include <stdint.h>
#include <stdbool.h>
#include "globals.h"

#if defined(USE_BEAT_NUMBERS)

// Configuration - adjust these to match your needs

// Requires 2 * 10 * 14 * 10 bytes, 2800 bytes  
#define BYTES_PER_PIXEL 2   // 16-bit color (RGB565)
#define DIGIT_WIDTH 8      // Width of each digit in pixels
#define DIGIT_HEIGHT 11     // Height of each digit in pixels
#define NUM_DIGITS 10       // 0-9

/**
 * Initialize and pre-render all digit buffers
 * Call this once during initialization
 * 
 * @param font Pointer to the LVGL font to use
 * @param text_color Color of the digit text
 * @param bg_color Background color of the digit
 * @return true if successful, false on error
 */
bool prerender_digit_buffers(const lv_font_t *font, lv_color_t text_color, lv_color_t bg_color);

/**
 * Copy a pre-rendered digit to your canvas buffer at specified position
 * 
 * @param canvas_buf Pointer to your canvas buffer (as uint16_t*)
 * @param canvas_width Width of your canvas in pixels
 * @param x X position where to draw the digit
 * @param y Y position where to draw the digit
 * @param digit Which digit to draw (0-9)
 */
void blit_digit_to_canvas(uint16_t *canvas_buf, uint16_t canvas_width, int16_t x, int16_t y, uint8_t digit);

/**
 * Draw a multi-digit number (e.g., beat number 42)
 * 
 * @param canvas_buf Pointer to your canvas buffer (as uint16_t*)
 * @param canvas_width Width of your canvas in pixels
 * @param x X position where to start drawing the number
 * @param y Y position where to draw the number
 * @param number The number to draw
 */
void blit_number_to_canvas(uint16_t *canvas_buf, uint16_t canvas_width, int16_t x, int16_t y, uint32_t number);

/**
 * Free all digit buffers - call during cleanup
 */
void free_digit_buffers(void);

/**
 * Get the width of a digit in pixels (useful for positioning)
 */
inline uint16_t get_digit_width(void) { return DIGIT_WIDTH; }

/**
 * Get the height of a digit in pixels (useful for positioning)
 */
inline uint16_t get_digit_height(void) { return DIGIT_HEIGHT; }

#endif // USE_BEAT_NUMBERS
#endif // DIGIT_RENDERER_H