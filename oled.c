/*
 * ============================================================
 * oled.c - PRE-WRITTEN OLED DRIVER
 * ============================================================
 * SSD1306 128x64 OLED driver over I2C + framebuffer drawing
 * primitives. No external library needed.
 *
 * DO NOT MODIFY THIS FILE. Just #include "game.h" and call
 * the functions declared there (oled_init, fb_clear, etc.)
 * ============================================================
 */

#include "game.h"

/* SSD1306 command constants */
#define SSD1306_CMD         0x00
#define SSD1306_DATA        0x40
#define SSD1306_DISPLAY_ON  0xAF
#define SSD1306_DISPLAY_OFF 0xAE
#define SSD1306_SET_MUX     0xA8
#define SSD1306_SET_OFFSET  0xD3
#define SSD1306_SET_START   0x40
#define SSD1306_SET_REMAP   0xA0
#define SSD1306_COM_SCAN    0xC8
#define SSD1306_COM_PINS    0xDA
#define SSD1306_CONTRAST    0x81
#define SSD1306_RESUME      0xA4
#define SSD1306_NORMAL      0xA6
#define SSD1306_CLK_DIV     0xD5
#define SSD1306_CHARGE_PUMP 0x8D
#define SSD1306_ADDR_MODE   0x20
#define SSD1306_COL_ADDR    0x21
#define SSD1306_PAGE_ADDR   0x22

/* Framebuffer: 128x64 / 8 = 1024 bytes */
static uint8_t framebuf[SCREEN_W * SCREEN_H / 8];

/* 5x7 bitmap font (ASCII 32-90) */
static const uint8_t font5x7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, // Space
    {0x00,0x00,0x5F,0x00,0x00}, // !
    {0x00,0x00,0x00,0x00,0x00}, // "
    {0x00,0x00,0x00,0x00,0x00}, // #
    {0x00,0x00,0x00,0x00,0x00}, // $
    {0x23,0x13,0x08,0x64,0x62}, // %
    {0x00,0x00,0x00,0x00,0x00}, // &
    {0x00,0x00,0x00,0x00,0x00}, // '
    {0x00,0x00,0x00,0x00,0x00}, // (
    {0x00,0x00,0x00,0x00,0x00}, // )
    {0x00,0x00,0x00,0x00,0x00}, // *
    {0x00,0x00,0x00,0x00,0x00}, // +
    {0x00,0x00,0x00,0x00,0x00}, // ,
    {0x00,0x00,0x00,0x00,0x00}, // -
    {0x00,0x00,0x00,0x00,0x00}, // .
    {0x00,0x00,0x00,0x00,0x00}, // /
    {0x3E,0x51,0x49,0x45,0x3E}, // 0
    {0x00,0x42,0x7F,0x40,0x00}, // 1
    {0x42,0x61,0x51,0x49,0x46}, // 2
    {0x21,0x41,0x45,0x4B,0x31}, // 3
    {0x18,0x14,0x12,0x7F,0x10}, // 4
    {0x27,0x45,0x45,0x45,0x39}, // 5
    {0x3C,0x4A,0x49,0x49,0x30}, // 6
    {0x01,0x71,0x09,0x05,0x03}, // 7
    {0x36,0x49,0x49,0x49,0x36}, // 8
    {0x06,0x49,0x49,0x29,0x1E}, // 9
    {0x00,0x36,0x36,0x00,0x00}, // :
    {0x00,0x00,0x00,0x00,0x00}, // ;
    {0x00,0x00,0x00,0x00,0x00}, // <
    {0x00,0x00,0x00,0x00,0x00}, // =
    {0x00,0x00,0x00,0x00,0x00}, // >
    {0x00,0x00,0x00,0x00,0x00}, // ?
    {0x00,0x00,0x00,0x00,0x00}, // @
    {0x7E,0x11,0x11,0x11,0x7E}, // A
    {0x7F,0x49,0x49,0x49,0x36}, // B
    {0x3E,0x41,0x41,0x41,0x22}, // C
    {0x7F,0x41,0x41,0x22,0x1C}, // D
    {0x7F,0x49,0x49,0x49,0x41}, // E
    {0x7F,0x09,0x09,0x09,0x01}, // F
    {0x3E,0x41,0x49,0x49,0x3A}, // G
    {0x7F,0x08,0x08,0x08,0x7F}, // H
    {0x00,0x41,0x7F,0x41,0x00}, // I
    {0x20,0x40,0x41,0x3F,0x01}, // J
    {0x7F,0x08,0x14,0x22,0x41}, // K
    {0x7F,0x40,0x40,0x40,0x40}, // L
    {0x7F,0x02,0x0C,0x02,0x7F}, // M
    {0x7F,0x04,0x08,0x10,0x7F}, // N
    {0x3E,0x41,0x41,0x41,0x3E}, // O
    {0x7F,0x09,0x09,0x09,0x06}, // P
    {0x3E,0x41,0x51,0x21,0x5E}, // Q
    {0x7F,0x09,0x19,0x29,0x46}, // R
    {0x46,0x49,0x49,0x49,0x31}, // S
    {0x01,0x01,0x7F,0x01,0x01}, // T
    {0x3F,0x40,0x40,0x40,0x3F}, // U
    {0x1F,0x20,0x40,0x20,0x1F}, // V
    {0x3F,0x40,0x38,0x40,0x3F}, // W
    {0x63,0x14,0x08,0x14,0x63}, // X
    {0x07,0x08,0x70,0x08,0x07}, // Y
    {0x61,0x51,0x49,0x45,0x43}, // Z
};

/* Send one command byte to SSD1306 */
static void oled_cmd(uint8_t cmd) {
    uint8_t buf[2] = {SSD1306_CMD, cmd};
    i2c_master_write_to_device(I2C_PORT, OLED_ADDR, buf, 2, pdMS_TO_TICKS(50));
}

void oled_init(void) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_SDA,
        .scl_io_num = I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,
    };
    i2c_param_config(I2C_PORT, &conf);
    i2c_driver_install(I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);

    vTaskDelay(pdMS_TO_TICKS(100));
    oled_cmd(SSD1306_DISPLAY_OFF);
    oled_cmd(SSD1306_CLK_DIV); oled_cmd(0x80);
    oled_cmd(SSD1306_SET_MUX); oled_cmd(63);
    oled_cmd(SSD1306_SET_OFFSET); oled_cmd(0);
    oled_cmd(SSD1306_SET_START);
    oled_cmd(SSD1306_CHARGE_PUMP); oled_cmd(0x14);
    oled_cmd(SSD1306_ADDR_MODE); oled_cmd(0x00);
    oled_cmd(SSD1306_SET_REMAP | 0x01);
    oled_cmd(SSD1306_COM_SCAN);
    oled_cmd(SSD1306_COM_PINS); oled_cmd(0x12);
    oled_cmd(SSD1306_CONTRAST); oled_cmd(0xCF);
    oled_cmd(SSD1306_RESUME);
    oled_cmd(SSD1306_NORMAL);
    oled_cmd(SSD1306_DISPLAY_ON);
}

void oled_flush(void) {
    oled_cmd(SSD1306_COL_ADDR); oled_cmd(0); oled_cmd(127);
    oled_cmd(SSD1306_PAGE_ADDR); oled_cmd(0); oled_cmd(7);
    for (int i = 0; i < 1024; i += 128) {
        uint8_t buf[129];
        buf[0] = SSD1306_DATA;
        memcpy(&buf[1], &framebuf[i], 128);
        i2c_master_write_to_device(I2C_PORT, OLED_ADDR, buf, 129, pdMS_TO_TICKS(50));
    }
}

void fb_clear(void) {
    memset(framebuf, 0, sizeof(framebuf));
}

void fb_pixel(int x, int y, bool on) {
    if (x < 0 || x >= SCREEN_W || y < 0 || y >= SCREEN_H) return;
    if (on)
        framebuf[x + (y / 8) * SCREEN_W] |= (1 << (y % 8));
    else
        framebuf[x + (y / 8) * SCREEN_W] &= ~(1 << (y % 8));
}

void fb_rect(int x, int y, int w, int h) {
    for (int dy = 0; dy < h; dy++)
        for (int dx = 0; dx < w; dx++)
            fb_pixel(x + dx, y + dy, true);
}

void fb_vline(int x, int y, int len) {
    for (int i = 0; i < len; i++) fb_pixel(x, y + i, true);
}

void fb_hline(int x, int y, int len) {
    for (int i = 0; i < len; i++) fb_pixel(x + i, y, true);
}

void fb_frame(int x, int y, int w, int h) {
    fb_hline(x, y, w);
    fb_hline(x, y + h - 1, w);
    fb_vline(x, y, h);
    fb_vline(x + w - 1, y, h);
}

void fb_char(int x, int y, char c) {
    int idx = -1;
    if (c >= 32 && c <= 90) idx = c - 32;
    else if (c >= 'a' && c <= 'z') idx = (c - 'a') + ('A' - 32);
    if (idx < 0 || idx >= (int)(sizeof(font5x7)/sizeof(font5x7[0]))) return;
    for (int col = 0; col < 5; col++) {
        uint8_t bits = font5x7[idx][col];
        for (int row = 0; row < 7; row++) {
            if (bits & (1 << row)) fb_pixel(x + col, y + row, true);
        }
    }
}

void fb_string(int x, int y, const char* s) {
    while (*s) {
        fb_char(x, y, *s);
        x += 6;
        s++;
    }
}

void fb_ship(int x, int y, int w) {
    int mid = w / 2;
    fb_pixel(x + mid - 1, y, true);
    fb_pixel(x + mid, y, true);
    for (int row = 1; row <= 3; row++) {
        int spread = (row * w) / 8;
        fb_pixel(x + mid - spread, y + row, true);
        fb_pixel(x + mid + spread, y + row, true);
    }
    fb_pixel(x, y + 4, true);
    fb_pixel(x + w - 1, y + 4, true);
    int tw = w / 3;
    if (tw < 2) tw = 2;
    fb_rect(x + mid - tw / 2, y + 5, tw, 2);
}
