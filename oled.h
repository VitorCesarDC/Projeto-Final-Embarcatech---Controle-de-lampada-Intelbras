#ifndef OLED_H
#define OLED_H
/*
 * oled.h — Driver SSD1306 128×64 via I2C (BitDogLab)
 */
#include <stdint.h>
#include <stdbool.h>
#include "config.h"

/* ── API pública ──────────────────────────────────────────────────────── */
void oled_init(void);
void oled_clear(void);
void oled_update(void);            /* flush buffer → display          */

/* Pixel */
void oled_draw_pixel(int x, int y, bool on);

/* Texto */
void oled_draw_char(int x, int y, char c);
void oled_draw_string(int x, int y, const char *str);

/* Formas */
void oled_draw_rect(int x, int y, int w, int h, bool fill);
void oled_draw_hline(int x, int y, int len);
void oled_draw_vline(int x, int y, int len);

/* Barra de progresso 0-100 */
void oled_progress_bar(int x, int y, int w, int h, uint8_t pct);

/* Layouts prontos para cada tela */
void oled_screen_connecting(void);
void oled_screen_idle(bool lamp_on);
void oled_screen_brightness(uint8_t pct);
void oled_screen_color(uint8_t cursor_x, uint8_t cursor_y,
                       uint16_t hue, uint8_t sat);

#endif /* OLED_H */
