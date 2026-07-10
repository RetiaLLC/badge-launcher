// ILI9341 driver distilled from the doom-badge port's spi_lcd.c (init table and
// window-write sequence proven on this panel at 40 MHz).

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include "lcd.h"
#include "font8x8_basic.h"

#define PIN_MOSI 11
#define PIN_MISO 12
#define PIN_SCK  13
#define PIN_CS   47
#define PIN_DC   40
#define PIN_RST  41

#define CHUNK_PX (LCD_W * 24) // 24 rows per DMA transaction (15360 B)

static const char *TAG = "lcd";
static spi_device_handle_t s_dev;
static uint16_t *s_fb;

typedef struct {
    uint8_t cmd;
    uint8_t data[16];
    uint8_t databytes; // bit 7 = delay after; 0xFF = end of list
} ili_init_cmd_t;

static const ili_init_cmd_t ili_init_cmds[] = {
    {0xCF, {0x00, 0x83, 0x30}, 3},
    {0xED, {0x64, 0x03, 0x12, 0x81}, 4},
    {0xE8, {0x85, 0x01, 0x79}, 3},
    {0xCB, {0x39, 0x2C, 0x00, 0x34, 0x02}, 5},
    {0xF7, {0x20}, 1},
    {0xEA, {0x00, 0x00}, 2},
    {0xC0, {0x26}, 1},
    {0xC1, {0x11}, 1},
    {0xC5, {0x35, 0x3E}, 2},
    {0xC7, {0xBE}, 1},
    {0x36, {0x28}, 1}, // MADCTL: landscape, BGR
    {0x3A, {0x55}, 1}, // 16bpp
    {0xB1, {0x00, 0x1B}, 2},
    {0xF2, {0x08}, 1},
    {0x26, {0x01}, 1},
    {0xE0, {0x1F, 0x1A, 0x18, 0x0A, 0x0F, 0x06, 0x45, 0x87, 0x32, 0x0A, 0x07, 0x02, 0x07, 0x05, 0x00}, 15},
    {0xE1, {0x00, 0x25, 0x27, 0x05, 0x10, 0x09, 0x3A, 0x78, 0x4D, 0x05, 0x18, 0x0D, 0x38, 0x3A, 0x1F}, 15},
    {0x2A, {0x00, 0x00, 0x00, 0xEF}, 4},
    {0x2B, {0x00, 0x00, 0x01, 0x3F}, 4},
    {0x2C, {0}, 0},
    {0xB7, {0x07}, 1},
    {0xB6, {0x0A, 0x82, 0x27, 0x00}, 4},
    {0x11, {0}, 0x80},
    {0x29, {0}, 0x80},
    {0, {0}, 0xFF},
};

static void pre_cb(spi_transaction_t *t)
{
    gpio_set_level(PIN_DC, (int)t->user);
}

static void send_cmd(uint8_t cmd)
{
    spi_transaction_t t = {.length = 8, .tx_buffer = &cmd, .user = (void *)0};
    ESP_ERROR_CHECK(spi_device_transmit(s_dev, &t));
}

static void send_data(const uint8_t *data, int len)
{
    if (len == 0) return;
    spi_transaction_t t = {.length = len * 8, .tx_buffer = data, .user = (void *)1};
    ESP_ERROR_CHECK(spi_device_transmit(s_dev, &t));
}

void lcd_init(void)
{
    spi_bus_config_t buscfg = {
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = PIN_MISO,
        .sclk_io_num = PIN_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = CHUNK_PX * 2 + 16,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 40000000,
        .mode = 0,
        .spics_io_num = PIN_CS,
        .queue_size = 4,
        .pre_cb = pre_cb,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &devcfg, &s_dev));

    gpio_set_direction(PIN_DC, GPIO_MODE_OUTPUT);
    gpio_set_direction(PIN_RST, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(100));

    for (int i = 0; ili_init_cmds[i].databytes != 0xFF; i++) {
        uint8_t dmdata[16];
        send_cmd(ili_init_cmds[i].cmd);
        memcpy(dmdata, ili_init_cmds[i].data, 16);
        send_data(dmdata, ili_init_cmds[i].databytes & 0x1F);
        if (ili_init_cmds[i].databytes & 0x80) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

    s_fb = heap_caps_malloc(LCD_W * LCD_H * 2, MALLOC_CAP_DMA);
    assert(s_fb);
    lcd_clear(C_BLACK);
    ESP_LOGI(TAG, "ILI9341 up, fb %d bytes internal DMA", LCD_W * LCD_H * 2);
}

void lcd_clear(uint16_t color)
{
    for (int i = 0; i < LCD_W * LCD_H; i++) {
        s_fb[i] = color;
    }
}

void lcd_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > LCD_W) w = LCD_W - x;
    if (y + h > LCD_H) h = LCD_H - y;
    for (int yy = y; yy < y + h; yy++) {
        uint16_t *row = &s_fb[yy * LCD_W + x];
        for (int xx = 0; xx < w; xx++) {
            row[xx] = color;
        }
    }
}

void lcd_text(int col, int row, const char *s, uint16_t fg, uint16_t bg)
{
    int x0 = col * 16, y0 = row * 16;
    for (; *s && x0 <= LCD_W - 16; s++, x0 += 16) {
        unsigned char c = (unsigned char)*s;
        if (c > 127) c = '?';
        const char *glyph = font8x8_basic[c];
        for (int gy = 0; gy < 8; gy++) {
            uint16_t *r0 = &s_fb[(y0 + gy * 2) * LCD_W + x0];
            uint16_t *r1 = r0 + LCD_W;
            for (int gx = 0; gx < 8; gx++) {
                uint16_t px = (glyph[gy] >> gx) & 1 ? fg : bg;
                r0[gx * 2] = px; r0[gx * 2 + 1] = px;
                r1[gx * 2] = px; r1[gx * 2 + 1] = px;
            }
        }
    }
}

// 8x16 cells (1x wide, 2x tall): 40 columns on the same 16px rows as
// lcd_text. Narrow enough that full firmware filenames fit in the menu.
void lcd_text_tall(int col, int row, const char *s, uint16_t fg, uint16_t bg)
{
    int x0 = col * 8, y0 = row * 16;
    for (; *s && x0 <= LCD_W - 8; s++, x0 += 8) {
        unsigned char c = (unsigned char)*s;
        if (c > 127) c = '?';
        const char *glyph = font8x8_basic[c];
        for (int gy = 0; gy < 8; gy++) {
            uint16_t *r0 = &s_fb[(y0 + gy * 2) * LCD_W + x0];
            uint16_t *r1 = r0 + LCD_W;
            for (int gx = 0; gx < 8; gx++) {
                uint16_t px = (glyph[gy] >> gx) & 1 ? fg : bg;
                r0[gx] = px;
                r1[gx] = px;
            }
        }
    }
}

void lcd_text_small(int col, int row, const char *s, uint16_t fg, uint16_t bg)
{
    int x0 = col * 8, y0 = row * 8;
    for (; *s && x0 <= LCD_W - 8; s++, x0 += 8) {
        unsigned char c = (unsigned char)*s;
        if (c > 127) c = '?';
        const char *glyph = font8x8_basic[c];
        for (int gy = 0; gy < 8; gy++) {
            uint16_t *r = &s_fb[(y0 + gy) * LCD_W + x0];
            for (int gx = 0; gx < 8; gx++) {
                r[gx] = (glyph[gy] >> gx) & 1 ? fg : bg;
            }
        }
    }
}

void lcd_flush(void)
{
    // Set the full-frame window once, then stream the framebuffer; the panel's
    // write pointer auto-advances across the whole frame.
    // Stack copies: SPI DMA can't read from flash .rodata.
    uint8_t ca[] = {0, 0, (LCD_W - 1) >> 8, (LCD_W - 1) & 0xFF};
    uint8_t pa[] = {0, 0, (LCD_H - 1) >> 8, (LCD_H - 1) & 0xFF};
    send_cmd(0x2A); send_data(ca, 4);
    send_cmd(0x2B); send_data(pa, 4);
    send_cmd(0x2C);
    for (int px = 0; px < LCD_W * LCD_H; px += CHUNK_PX) {
        send_data((const uint8_t *)&s_fb[px], CHUNK_PX * 2);
    }
}
