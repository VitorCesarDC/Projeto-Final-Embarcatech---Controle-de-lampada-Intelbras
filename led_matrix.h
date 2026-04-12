#ifndef LED_MATRIX_H
#define LED_MATRIX_H
/*
 * led_matrix.h — Controle da matriz WS2812B 5×5 (BitDogLab)
 */
#include <stdint.h>
#include "config.h"

/* Estrutura de cor em RGB; a conversao para a ordem do LED e feita no driver. */
typedef struct { uint8_t r, g, b; } rgb_t;

/* ── API pública ────────────────────────────────────────────────────── */
void matrix_init(void);

/* Define cor de um LED (linha 0-4, coluna 0-4) */
void matrix_set(int row, int col, rgb_t color);

/* Apaga todos os LEDs */
void matrix_clear(void);

/* Envia o buffer para os LEDs */
void matrix_show(void);

/* Converte HSV → RGB (h: 0-359, s: 0-100, v: 0-100) */
rgb_t hsv_to_rgb(uint16_t h, uint8_t s, uint8_t v);

/* Escala brilho de uma cor (0-255) */
rgb_t rgb_scale(rgb_t c, uint8_t brightness);

/* ── Modos de exibição ──────────────────────────────────────────────── */

/* Roda de cores para seleção — destaca célula (cx, cy) */
void matrix_show_color_wheel(uint8_t cursor_x, uint8_t cursor_y);

/* Barra de brilho (pct 0-100) — colunas acendem de baixo para cima */
void matrix_show_brightness(uint8_t pct, uint16_t hue);

/* Cor sólida (feedback da cor atual da lâmpada) */
void matrix_show_solid(uint16_t hue, uint8_t sat, uint8_t val);

/* Animação de carregamento (um pixel girando) */
void matrix_show_loading(int frame);

/* Apaga tudo */
void matrix_off(void);

/* ── Paleta de cores da grade 5×5 ────────────────────────────────────── */
/* Retorna HSV de uma célula da grade */
void matrix_cell_hsv(uint8_t row, uint8_t col,
                     uint16_t *out_h, uint8_t *out_s);

#endif /* LED_MATRIX_H */
