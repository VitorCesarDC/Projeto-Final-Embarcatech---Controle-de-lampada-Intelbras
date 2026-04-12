/*
 * oled.c — Driver SSD1306 128×64 I2C (BitDogLab / RP2040)
 */
#include "oled.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include <string.h>
#include <stdio.h>

/* ── Frame-buffer ─────────────────────────────────────────────────────── */
#define BUF_SIZE  (OLED_WIDTH * OLED_HEIGHT / 8)
#define OLED_CHUNK 16
static uint8_t fb[BUF_SIZE];   /* 1 bit por pixel, organizado por páginas */

/* ── Fonte 5×8 (ASCII 32–127) ─────────────────────────────────────────── */
static const uint8_t FONT5x8[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, /* 32  ' ' */
    {0x00,0x00,0x5F,0x00,0x00}, /* 33  '!' */
    {0x00,0x07,0x00,0x07,0x00}, /* 34  '"' */
    {0x14,0x7F,0x14,0x7F,0x14}, /* 35  '#' */
    {0x24,0x2A,0x7F,0x2A,0x12}, /* 36  '$' */
    {0x23,0x13,0x08,0x64,0x62}, /* 37  '%' */
    {0x36,0x49,0x55,0x22,0x50}, /* 38  '&' */
    {0x00,0x05,0x03,0x00,0x00}, /* 39  ''' */
    {0x00,0x1C,0x22,0x41,0x00}, /* 40  '(' */
    {0x00,0x41,0x22,0x1C,0x00}, /* 41  ')' */
    {0x14,0x08,0x3E,0x08,0x14}, /* 42  '*' */
    {0x08,0x08,0x3E,0x08,0x08}, /* 43  '+' */
    {0x00,0x50,0x30,0x00,0x00}, /* 44  ',' */
    {0x08,0x08,0x08,0x08,0x08}, /* 45  '-' */
    {0x00,0x60,0x60,0x00,0x00}, /* 46  '.' */
    {0x20,0x10,0x08,0x04,0x02}, /* 47  '/' */
    {0x3E,0x51,0x49,0x45,0x3E}, /* 48  '0' */
    {0x00,0x42,0x7F,0x40,0x00}, /* 49  '1' */
    {0x42,0x61,0x51,0x49,0x46}, /* 50  '2' */
    {0x21,0x41,0x45,0x4B,0x31}, /* 51  '3' */
    {0x18,0x14,0x12,0x7F,0x10}, /* 52  '4' */
    {0x27,0x45,0x45,0x45,0x39}, /* 53  '5' */
    {0x3C,0x4A,0x49,0x49,0x30}, /* 54  '6' */
    {0x01,0x71,0x09,0x05,0x03}, /* 55  '7' */
    {0x36,0x49,0x49,0x49,0x36}, /* 56  '8' */
    {0x06,0x49,0x49,0x29,0x1E}, /* 57  '9' */
    {0x00,0x36,0x36,0x00,0x00}, /* 58  ':' */
    {0x00,0x56,0x36,0x00,0x00}, /* 59  ';' */
    {0x08,0x14,0x22,0x41,0x00}, /* 60  '<' */
    {0x14,0x14,0x14,0x14,0x14}, /* 61  '=' */
    {0x00,0x41,0x22,0x14,0x08}, /* 62  '>' */
    {0x02,0x01,0x51,0x09,0x06}, /* 63  '?' */
    {0x32,0x49,0x79,0x41,0x3E}, /* 64  '@' */
    {0x7E,0x11,0x11,0x11,0x7E}, /* 65  'A' */
    {0x7F,0x49,0x49,0x49,0x36}, /* 66  'B' */
    {0x3E,0x41,0x41,0x41,0x22}, /* 67  'C' */
    {0x7F,0x41,0x41,0x22,0x1C}, /* 68  'D' */
    {0x7F,0x49,0x49,0x49,0x41}, /* 69  'E' */
    {0x7F,0x09,0x09,0x09,0x01}, /* 70  'F' */
    {0x3E,0x41,0x49,0x49,0x7A}, /* 71  'G' */
    {0x7F,0x08,0x08,0x08,0x7F}, /* 72  'H' */
    {0x00,0x41,0x7F,0x41,0x00}, /* 73  'I' */
    {0x20,0x40,0x41,0x3F,0x01}, /* 74  'J' */
    {0x7F,0x08,0x14,0x22,0x41}, /* 75  'K' */
    {0x7F,0x40,0x40,0x40,0x40}, /* 76  'L' */
    {0x7F,0x02,0x0C,0x02,0x7F}, /* 77  'M' */
    {0x7F,0x04,0x08,0x10,0x7F}, /* 78  'N' */
    {0x3E,0x41,0x41,0x41,0x3E}, /* 79  'O' */
    {0x7F,0x09,0x09,0x09,0x06}, /* 80  'P' */
    {0x3E,0x41,0x51,0x21,0x5E}, /* 81  'Q' */
    {0x7F,0x09,0x19,0x29,0x46}, /* 82  'R' */
    {0x46,0x49,0x49,0x49,0x31}, /* 83  'S' */
    {0x01,0x01,0x7F,0x01,0x01}, /* 84  'T' */
    {0x3F,0x40,0x40,0x40,0x3F}, /* 85  'U' */
    {0x1F,0x20,0x40,0x20,0x1F}, /* 86  'V' */
    {0x3F,0x40,0x38,0x40,0x3F}, /* 87  'W' */
    {0x63,0x14,0x08,0x14,0x63}, /* 88  'X' */
    {0x07,0x08,0x70,0x08,0x07}, /* 89  'Y' */
    {0x61,0x51,0x49,0x45,0x43}, /* 90  'Z' */
    {0x00,0x7F,0x41,0x41,0x00}, /* 91  '[' */
    {0x02,0x04,0x08,0x10,0x20}, /* 92  '\' */
    {0x00,0x41,0x41,0x7F,0x00}, /* 93  ']' */
    {0x04,0x02,0x01,0x02,0x04}, /* 94  '^' */
    {0x40,0x40,0x40,0x40,0x40}, /* 95  '_' */
    {0x00,0x01,0x02,0x04,0x00}, /* 96  '`' */
    {0x20,0x54,0x54,0x54,0x78}, /* 97  'a' */
    {0x7F,0x48,0x44,0x44,0x38}, /* 98  'b' */
    {0x38,0x44,0x44,0x44,0x20}, /* 99  'c' */
    {0x38,0x44,0x44,0x48,0x7F}, /* 100 'd' */
    {0x38,0x54,0x54,0x54,0x18}, /* 101 'e' */
    {0x08,0x7E,0x09,0x01,0x02}, /* 102 'f' */
    {0x0C,0x52,0x52,0x52,0x3E}, /* 103 'g' */
    {0x7F,0x08,0x04,0x04,0x78}, /* 104 'h' */
    {0x00,0x44,0x7D,0x40,0x00}, /* 105 'i' */
    {0x20,0x40,0x44,0x3D,0x00}, /* 106 'j' */
    {0x7F,0x10,0x28,0x44,0x00}, /* 107 'k' */
    {0x00,0x41,0x7F,0x40,0x00}, /* 108 'l' */
    {0x7C,0x04,0x18,0x04,0x78}, /* 109 'm' */
    {0x7C,0x08,0x04,0x04,0x78}, /* 110 'n' */
    {0x38,0x44,0x44,0x44,0x38}, /* 111 'o' */
    {0x7C,0x14,0x14,0x14,0x08}, /* 112 'p' */
    {0x08,0x14,0x14,0x18,0x7C}, /* 113 'q' */
    {0x7C,0x08,0x04,0x04,0x08}, /* 114 'r' */
    {0x48,0x54,0x54,0x54,0x20}, /* 115 's' */
    {0x04,0x3F,0x44,0x40,0x20}, /* 116 't' */
    {0x3C,0x40,0x40,0x20,0x7C}, /* 117 'u' */
    {0x1C,0x20,0x40,0x20,0x1C}, /* 118 'v' */
    {0x3C,0x40,0x30,0x40,0x3C}, /* 119 'w' */
    {0x44,0x28,0x10,0x28,0x44}, /* 120 'x' */
    {0x0C,0x50,0x50,0x50,0x3C}, /* 121 'y' */
    {0x44,0x64,0x54,0x4C,0x44}, /* 122 'z' */
    {0x00,0x08,0x36,0x41,0x00}, /* 123 '{' */
    {0x00,0x00,0x7F,0x00,0x00}, /* 124 '|' */
    {0x00,0x41,0x36,0x08,0x00}, /* 125 '}' */
    {0x10,0x08,0x08,0x10,0x08}, /* 126 '~' */
    {0x00,0x00,0x00,0x00,0x00}, /* 127 DEL */
};

/* ── Comandos SSD1306 ─────────────────────────────────────────────────── */
static void oled_cmd(uint8_t cmd) {
    uint8_t buf[2] = {0x00, cmd};   /* 0x00 = Co=0, D/C=0 (comando) */
    i2c_write_blocking(OLED_I2C_PORT, OLED_ADDR, buf, 2, false);
}

void oled_init(void) {
    i2c_init(OLED_I2C_PORT, 400 * 1000);
    gpio_set_function(OLED_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(OLED_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(OLED_SDA_PIN);
    gpio_pull_up(OLED_SCL_PIN);

    sleep_ms(100);

    static const uint8_t init_cmds[] = {
        0xAE,       /* Display OFF                 */
        0xD5,0x80,  /* Set display clock div       */
        0xA8,0x3F,  /* Multiplex ratio (64-1)      */
        0xD3,0x00,  /* Display offset = 0          */
        0x40,       /* Start line = 0              */
        0x8D,0x14,  /* Charge pump ON              */
        0x20,0x00,  /* Horizontal addressing mode  */
        0xA1,       /* Segment remap               */
        0xC8,       /* COM scan direction          */
        0xDA,0x12,  /* COM pins config             */
        0x81,0xCF,  /* Contrast                    */
        0xD9,0xF1,  /* Pre-charge period           */
        0xDB,0x40,  /* VCOMH deselect              */
        0xA4,       /* Resume display from RAM     */
        0xA6,       /* Normal display              */
        0xAF,       /* Display ON                  */
    };
    for (size_t i = 0; i < sizeof(init_cmds); i++)
        oled_cmd(init_cmds[i]);

    oled_clear();
    oled_update();
}

void oled_clear(void) {
    memset(fb, 0, BUF_SIZE);
}

void oled_update(void) {
    oled_cmd(0x21); oled_cmd(0); oled_cmd(127); /* column 0-127 */
    oled_cmd(0x22); oled_cmd(0); oled_cmd(7);   /* page   0-7   */

    uint8_t packet[OLED_CHUNK + 1];
    packet[0] = 0x40;  /* Co=0, D/C=1 = dados */

    for (int i = 0; i < BUF_SIZE; i += OLED_CHUNK) {
        int n = BUF_SIZE - i;
        if (n > OLED_CHUNK) n = OLED_CHUNK;
        memcpy(&packet[1], &fb[i], (size_t)n);
        i2c_write_blocking(OLED_I2C_PORT, OLED_ADDR, packet, (size_t)(n + 1), false);
    }
}

/* ── Primitivas ───────────────────────────────────────────────────────── */
void oled_draw_pixel(int x, int y, bool on) {
    if (x < 0 || x >= OLED_WIDTH || y < 0 || y >= OLED_HEIGHT) return;
    int byte_idx = x + (y / 8) * OLED_WIDTH;
    if (on) fb[byte_idx] |=  (1 << (y & 7));
    else    fb[byte_idx] &= ~(1 << (y & 7));
}

void oled_draw_char(int x, int y, char c) {
    if (c < 32 || c > 127) c = '?';
    const uint8_t *col = FONT5x8[(uint8_t)c - 32];
    for (int cx = 0; cx < 5; cx++) {
        uint8_t bits = col[cx];
        for (int cy = 0; cy < 8; cy++)
            oled_draw_pixel(x + cx, y + cy, (bits >> cy) & 1);
    }
}

void oled_draw_string(int x, int y, const char *str) {
    while (*str) {
        oled_draw_char(x, y, *str++);
        x += 6;   /* 5px char + 1px spacing */
        if (x > OLED_WIDTH - 6) break;
    }
}

void oled_draw_rect(int x, int y, int w, int h, bool fill) {
    if (fill) {
        for (int row = y; row < y + h; row++)
            for (int col = x; col < x + w; col++)
                oled_draw_pixel(col, row, true);
    } else {
        oled_draw_hline(x, y,       w);
        oled_draw_hline(x, y+h-1,   w);
        oled_draw_vline(x,   y, h);
        oled_draw_vline(x+w-1,y, h);
    }
}

void oled_draw_hline(int x, int y, int len) {
    for (int i = 0; i < len; i++) oled_draw_pixel(x+i, y, true);
}

void oled_draw_vline(int x, int y, int len) {
    for (int i = 0; i < len; i++) oled_draw_pixel(x, y+i, true);
}

void oled_progress_bar(int x, int y, int w, int h, uint8_t pct) {
    oled_draw_rect(x, y, w, h, false);
    int fill = (int)(pct * (w - 2)) / 100;
    if (fill > 0)
        oled_draw_rect(x + 1, y + 1, fill, h - 2, true);
}

/* ── Telas prontas ────────────────────────────────────────────────────── */

/* Tela de conexão Wi-Fi */
void oled_screen_connecting(void) {
    oled_clear();
    oled_draw_string(10, 8,  "BitDogLab");
    oled_draw_hline(0, 18, 128);
    oled_draw_string(4,  28, "Conectando Wi-Fi");
    oled_draw_string(20, 44, "Aguarde...");
    oled_update();
}

/* Tela principal: estado on/off */
void oled_screen_idle(bool lamp_on) {
    oled_clear();
    oled_draw_string(10, 2,  "LAMPADA SMART");
    oled_draw_hline(0, 12, 128);

    if (lamp_on) {
        /* Ícone de lâmpada ligada (círculo + base) */
        oled_draw_rect(54, 18, 18, 16, false);
        oled_draw_rect(58, 34, 10,  4, true);
        oled_draw_string(24, 42, "LIGADA [ON]");
    } else {
        oled_draw_rect(54, 18, 18, 16, false);
        oled_draw_rect(58, 34, 10,  4, false);
        oled_draw_string(18, 42, "DESLIGADA [OFF]");
    }

    oled_draw_hline(0, 54, 128);
    oled_draw_string(0, 56, "A:Ligar  B:Modo");
    oled_update();
}

/* Tela de brilho */
void oled_screen_brightness(uint8_t pct) {
    char buf[24];
    oled_clear();
    oled_draw_string(14, 2,  "MODO: BRILHO");
    oled_draw_hline(0, 12, 128);

    snprintf(buf, sizeof(buf), "Intensidade: %3d%%", pct);
    oled_draw_string(2, 22, buf);

    oled_progress_bar(4, 36, 120, 10, pct);

    oled_draw_hline(0, 54, 128);
    oled_draw_string(0, 56, "Joy:Ajusta B:Cor");
    oled_update();
}

/* Tela do seletor de cor */
void oled_screen_color(uint8_t cx, uint8_t cy, uint16_t hue, uint8_t sat) {
    char buf[24];
    oled_clear();
    oled_draw_string(12, 2,  "MODO: COR");
    oled_draw_hline(0, 12, 128);

    /* Grade 5x5 menor para caber inteira sem cortar no painel */
    for (int r = 0; r < 5; r++)
        for (int c = 0; c < 5; c++) {
            int px = 4 + c * 8;
            int py = 16 + r * 8;
            bool sel = (r == cy && c == cx);
            if (sel)
                oled_draw_rect(px, py, 7, 7, true);   /* celula selecionada */
            else
                oled_draw_rect(px, py, 7, 7, false);  /* celula normal */
        }

    /* Info da cor selecionada */
    snprintf(buf, sizeof(buf), "H:%3d S:%3d%%", hue, sat);
    oled_draw_string(48, 18, buf);

    /* Nome aproximado da cor */
    const char *nome;
    if (sat < 20)            nome = "Branco";
    else if (hue < 15)       nome = "Vermelho";
    else if (hue < 45)       nome = "Laranja";
    else if (hue < 75)       nome = "Amarelo";
    else if (hue < 150)      nome = "Verde";
    else if (hue < 200)      nome = "Ciano";
    else if (hue < 260)      nome = "Azul";
    else if (hue < 300)      nome = "Violeta";
    else                     nome = "Rosa";
    oled_draw_string(48, 30, nome);

    oled_draw_hline(0, 54, 128);
    oled_draw_string(0, 56, "Joy:Mover A:OK");
    oled_update();
}
