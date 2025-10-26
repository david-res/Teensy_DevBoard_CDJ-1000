#include "Arduino.h"
#include "globals.h"
#include "digit_renderer.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#if defined(USE_BEAT_NUMBERS)
// Storage for pre-rendered digit buffers
typedef struct {
    uint16_t *buffer;
    uint32_t buffer_size;
    uint16_t width;
    uint16_t height;
} digit_buffer_t;

static digit_buffer_t digit_buffers[NUM_DIGITS];

FLASHMEM bool prerender_digit_buffers(const lv_font_t *font, lv_color_t text_color, lv_color_t bg_color) {
    uint32_t buffer_size = DIGIT_WIDTH * DIGIT_HEIGHT * BYTES_PER_PIXEL;
    
    // Create a temporary canvas for rendering
    lv_obj_t *temp_canvas = lv_canvas_create(lv_scr_act());
    if (!temp_canvas) {
        return false;
    }
    
    // Allocate a temporary buffer for the canvas
    uint8_t *temp_buffer = (uint8_t *)malloc(buffer_size);
    if (!temp_buffer) {
        lv_obj_del(temp_canvas);
        return false;
    }
    
#if (LVGL_VERSION_MAJOR == 8)
    lv_canvas_set_buffer(temp_canvas, temp_buffer, DIGIT_WIDTH, DIGIT_HEIGHT, LV_IMG_CF_TRUE_COLOR);
#endif
#if (LVGL_VERSION_MAJOR == 9)
    lv_canvas_set_buffer(temp_canvas, temp_buffer, DIGIT_WIDTH, DIGIT_HEIGHT, LV_COLOR_FORMAT_RGB565);
#endif
    
    // Pre-render each digit
    for (int i = 0; i < NUM_DIGITS; i++) {
        // Allocate buffer for this digit (as uint16_t array)
        digit_buffers[i].buffer = (uint16_t *)malloc(buffer_size);
        if (!digit_buffers[i].buffer) {
            // Cleanup on failure
            for (int j = 0; j < i; j++) {
                free(digit_buffers[j].buffer);
            }
            free(temp_buffer);
            lv_obj_del(temp_canvas);
            return false;
        }
        
        digit_buffers[i].buffer_size = buffer_size;
        digit_buffers[i].width = DIGIT_WIDTH;
        digit_buffers[i].height = DIGIT_HEIGHT;
        
        // Fill canvas with background color
        lv_canvas_fill_bg(temp_canvas, bg_color, LV_OPA_COVER);
        
        // Draw the digit
        char digit_str[2] = {0};
        digit_str[0] = '0' + i;
        
#if (LVGL_VERSION_MAJOR == 8)
        // Get text size for centering
        uint32_t start = micros();
        lv_point_t text_size;
        lv_txt_get_size(&text_size, digit_str, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        
        int16_t x_offset = (DIGIT_WIDTH - text_size.x) / 2;
        int16_t y_offset = (DIGIT_HEIGHT - text_size.y) / 2;
        
        // Draw using the label descriptor
        lv_draw_label_dsc_t label_dsc;
        lv_draw_label_dsc_init(&label_dsc);
        label_dsc.color = text_color;
        label_dsc.font = font;
        
        lv_canvas_draw_text(temp_canvas, x_offset, y_offset, DIGIT_WIDTH, &label_dsc, digit_str);
        Serial.printf("LVGL draw label to canvas: %d - x: %d, y: %d, %lduS\n", i, text_size.x, text_size.y, micros()-start);
#endif

#if (LVGL_VERSION_MAJOR == 9)
        // Get text size for centering
        uint32_t start = micros();
        lv_point_t text_size;
        lv_text_get_size(&text_size, digit_str, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        
        int16_t x_offset = (DIGIT_WIDTH - text_size.x) / 2;
        int16_t y_offset = (DIGIT_HEIGHT - text_size.y) / 2;
        
        // Use layer to draw text
        lv_layer_t layer;
        lv_canvas_init_layer(temp_canvas, &layer);
        
        lv_draw_label_dsc_t label_dsc;
        lv_draw_label_dsc_init(&label_dsc);
        label_dsc.color = text_color;
        label_dsc.font = font;
        label_dsc.text = digit_str;
        
        lv_area_t coords;
        coords.x1 = x_offset;
        coords.y1 = y_offset;
        coords.x2 = x_offset + text_size.x - 1;
        coords.y2 = y_offset + text_size.y - 1;
        
        lv_draw_label(&layer, &label_dsc, &coords);
        lv_canvas_finish_layer(temp_canvas, &layer);
        Serial.printf("LVGL draw label to canvas: %lduS\n", micros()-start);
#endif
        
        // Copy the rendered digit to permanent storage
        memcpy(digit_buffers[i].buffer, temp_buffer, buffer_size);
    }
    
    // Cleanup temporary resources
    free(temp_buffer);
    lv_obj_del(temp_canvas);
    
    return true;
}

FASTRUN void blit_digit_to_canvas(uint16_t *canvas_buf, uint16_t canvas_width, int16_t x, int16_t y, uint8_t digit) {
    if (digit >= NUM_DIGITS) return;
    
    digit_buffer_t *db = &digit_buffers[digit];
    
    // Fast line-by-line copy using memcpy
    // This is more efficient than pixel-by-pixel copying
    for (uint16_t row = 0; row < db->height; row++) {
        uint16_t *src = db->buffer + (row * db->width);
        uint16_t *dst = canvas_buf + ((y + row) * canvas_width + x);
        memcpy(dst, src, db->width * 2);
    }
}

FASTRUN void blit_number_to_canvas(uint16_t *canvas_buf, uint16_t canvas_width, int16_t x, int16_t y, uint32_t number) {
    // Handle single digit
    if (number < 10) {
        blit_digit_to_canvas(canvas_buf, canvas_width, x, y, number);
        return;
    }
    
    // Convert number to digits
    char num_str[16];
    snprintf(num_str, sizeof(num_str), "%u", number);
    
    int16_t current_x = x;
    for (int i = 0; num_str[i] != '\0'; i++) {
        uint8_t digit = num_str[i] - '0';
        blit_digit_to_canvas(canvas_buf, canvas_width, current_x, y, digit);
        current_x += DIGIT_WIDTH;
    }
}

FLASHMEM void free_digit_buffers(void) {
    for (int i = 0; i < NUM_DIGITS; i++) {
        if (digit_buffers[i].buffer) {
            free(digit_buffers[i].buffer);
            digit_buffers[i].buffer = NULL;
        }
    }
}
#endif