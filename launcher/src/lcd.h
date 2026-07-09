// Minimal ILI9341 framebuffer driver for the 2024 DEF CON badge.
// 320x240 landscape, RGB565 framebuffer in internal RAM, full-frame flush.
// Text is the public-domain 8x8 VGA font drawn at 2x (16px cells, 20 cols x 15 rows).
#pragma once

#include <stdint.h>

#define LCD_W 320
#define LCD_H 240
#define TEXT_COLS 20
#define TEXT_ROWS 15

// RGB565, pre-swapped for SPI byte order (panel wants high byte first).
#define LCD_RGB(r, g, b) \
    ((uint16_t)(((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)) >> 8 | \
                ((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)) << 8))

#define C_BLACK  LCD_RGB(0, 0, 0)
#define C_WHITE  LCD_RGB(255, 255, 255)
#define C_TITLE  LCD_RGB(0, 200, 255)
#define C_HILITE LCD_RGB(255, 210, 0)
#define C_DIM    LCD_RGB(130, 130, 130)
#define C_OK     LCD_RGB(60, 220, 60)
#define C_ERR    LCD_RGB(255, 70, 70)

// Initializes the shared SPI bus (SCK13/MOSI11/MISO12) and the panel.
// Must be called before the SD card is mounted (the bus is init'd here).
void lcd_init(void);

void lcd_clear(uint16_t color);
void lcd_fill_rect(int x, int y, int w, int h, uint16_t color);
// Draws one line of text at a character cell position (16px grid).
void lcd_text(int col, int row, const char *s, uint16_t fg, uint16_t bg);
// Small text at 1x (8px grid, 40 cols x 30 rows) — used for the splash art.
void lcd_text_small(int col, int row, const char *s, uint16_t fg, uint16_t bg);
// Pushes the framebuffer to the panel (blocking, ~35 ms).
void lcd_flush(void);
