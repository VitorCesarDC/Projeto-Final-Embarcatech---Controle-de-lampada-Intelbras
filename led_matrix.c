/*
 * led_matrix.c — Matriz 5×5 WS2812B via PIO (BitDogLab / RP2040)
 *
 * Paleta de cores da grade 5×5:
 *   Linha 0: Vermelhos / Laranjas
 *   Linha 1: Amarelos  / Verdes claros
 *   Linha 2: Verdes    / Cianos
 *   Linha 3: Azuis     / Roxos
 *   Linha 4: Magentas  / Brancos
 */
#include "led_matrix.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include "ws2812.pio.h"
#include <string.h>

/* ── Buffer de pixels ───────────────────────────────────────────────── */
static rgb_t pixels[MATRIX_LEDS];
static PIO  pio_inst;
static uint sm_inst;
static bool matrix_hw_enabled = false;

/* Mapeamento: índice físico do LED na fita para (row, col)
 * A BitDogLab serpenteia a fita: linha par → esquerda-direita,
 * linha ímpar → direita-esquerda.
 */
static int led_index(int row, int col) {
    if (row % 2 == 0)
        return row * MATRIX_COLS + col;
    else
        return row * MATRIX_COLS + (MATRIX_COLS - 1 - col);
}

/* ── Paleta HSV da grade 5×5 ───────────────────────────────────────── */
/*
 * Grade de cores (Hue em graus, Saturação %)
 *
 *       Col0   Col1   Col2   Col3   Col4
 * Row0:   0°    15°    30°    45°    60°   (Vermelho → Amarelo)
 * Row1:  75°    90°   105°   120°   150°   (Verde-amarelo → Verde)
 * Row2: 165°   180°   195°   210°   225°   (Verde-ciano → Azul-ciano)
 * Row3: 240°   255°   270°   285°   300°   (Azul → Roxo)
 * Row4: 315°   330°   345°   360°   Branco (Magenta → Rosa → Branco)
 */
static const uint16_t PALETTE_H[MATRIX_ROWS][MATRIX_COLS] = {
    {  0,  15,  30,  45,  60},
    { 75,  90, 105, 120, 150},
    {165, 180, 195, 210, 225},
    {240, 255, 270, 285, 300},
    {315, 330, 345,   0,   0},   /* última coluna = branco (s=0) */
};
static const uint8_t PALETTE_S[MATRIX_ROWS][MATRIX_COLS] = {
    {100,100,100,100,100},
    {100,100,100,100,100},
    {100,100,100,100,100},
    {100,100,100,100,100},
    {100,100,100,  0,  0},       /* (4,3) e (4,4) = branco */
};

void matrix_cell_hsv(uint8_t row, uint8_t col,
                     uint16_t *out_h, uint8_t *out_s) {
    *out_h = PALETTE_H[row][col];
    *out_s = PALETTE_S[row][col];
}

/* ── Conversão HSV → RGB ───────────────────────────────────────────── */
rgb_t hsv_to_rgb(uint16_t h, uint8_t s, uint8_t v) {
    if (h >= 360) h = 0;
    uint8_t V  = (uint8_t)((uint32_t)v * 255 / 100);
    uint8_t S  = (uint8_t)((uint32_t)s * 255 / 100);

    if (S == 0) return (rgb_t){V, V, V};

    uint8_t region = h / 60;
    uint8_t rem    = (h % 60) * 255 / 60;
    uint8_t p = (uint32_t)V * (255 - S) / 255;
    uint8_t q = (uint32_t)V * (255 - (uint32_t)S * rem / 255) / 255;
    uint8_t t = (uint32_t)V * (255 - (uint32_t)S * (255 - rem) / 255) / 255;

    switch (region) {
        case 0: return (rgb_t){.r=V, .g=t, .b=p};
        case 1: return (rgb_t){.r=q, .g=V, .b=p};
        case 2: return (rgb_t){.r=p, .g=V, .b=t};
        case 3: return (rgb_t){.r=p, .g=q, .b=V};
        case 4: return (rgb_t){.r=t, .g=p, .b=V};
        default:return (rgb_t){.r=V, .g=p, .b=q};
    }
}

rgb_t rgb_scale(rgb_t c, uint8_t br) {
    return (rgb_t){
        .r = (uint8_t)((uint16_t)c.r * br / 255),
        .g = (uint8_t)((uint16_t)c.g * br / 255),
        .b = (uint8_t)((uint16_t)c.b * br / 255),
    };
}

/* ── Inicialização do PIO ──────────────────────────────────────────── */
void matrix_init(void) {
    /* Requisito do projeto: manter a matriz fisica sempre apagada.
     * Nao inicializamos PIO/WS2812 para eliminar qualquer risco de acender. */
    gpio_init(LED_MATRIX_PIN);
    gpio_set_dir(LED_MATRIX_PIN, GPIO_OUT);
    gpio_put(LED_MATRIX_PIN, 0);
    matrix_hw_enabled = false;
    matrix_clear();
}

/* ── Buffer e envio ────────────────────────────────────────────────── */
void matrix_clear(void) {
    memset(pixels, 0, sizeof(pixels));
}

void matrix_set(int row, int col, rgb_t color) {
    if (row < 0 || row >= MATRIX_ROWS || col < 0 || col >= MATRIX_COLS) return;
    pixels[led_index(row, col)] = color;
}

void matrix_show(void) {
    /* Driver intencionalmente desabilitado para a matriz fisica. */
    if (!matrix_hw_enabled) return;
    for (int i = 0; i < MATRIX_LEDS; i++) {
        /* WS2812B espera GRB na ordem: G, R, B nos 24 bits MSB */
        uint32_t grb = ((uint32_t)pixels[i].g << 24) |
                       ((uint32_t)pixels[i].r << 16) |
                       ((uint32_t)pixels[i].b <<  8);
        pio_sm_put_blocking(pio_inst, sm_inst, grb);
    }
    sleep_us(60); /* reset pulse */
}

/* ── Modos de exibição ─────────────────────────────────────────────── */

void matrix_show_color_wheel(uint8_t cursor_x, uint8_t cursor_y) {
    (void)cursor_x;
    (void)cursor_y;
    matrix_off();
}

void matrix_show_brightness(uint8_t pct, uint16_t hue) {
    (void)pct;
    (void)hue;
    matrix_off();
}

void matrix_show_solid(uint16_t hue, uint8_t sat, uint8_t val) {
    (void)hue;
    (void)sat;
    (void)val;
    matrix_off();
}

void matrix_show_loading(int frame) {
    (void)frame;
    matrix_off();
}

/* Todas as funções acima mantêm a matriz física desativada por requisito do projeto. */
void matrix_off(void) {
    matrix_clear();
    if (matrix_hw_enabled) matrix_show();
    gpio_put(LED_MATRIX_PIN, 0);
}
