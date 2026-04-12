/*
 * main.c — Controle de Lâmpada Inteligente Intelbras via BitDogLab (RP2040)
 *
 * Máquina de estados:
 *
 *   [CONNECTING] ──conectou──> [IDLE]
 *       |                        |
 *       └── animação loading     ├── BTN_A: toggle power
 *                                └── BTN_B: → BRIGHTNESS
 *
 *   [BRIGHTNESS]
 *       ├── Joystick Y: ajusta brilho em tempo real
 *       ├── BTN_A: toggle power
 *       └── BTN_B: → COLOR
 *
 *   [COLOR]
 *       ├── Joystick: move cursor na grade 5×5
 *       ├── BTN_A: aplica cor selecionada
 *       └── BTN_B: → IDLE
 *
 * Botão A: sempre liga/desliga (exceto em COLOR, onde confirma a cor)
 * Botão B: cicla entre modos IDLE → BRIGHTNESS → COLOR → IDLE
 */

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/dns.h"
#include "lwip/pbuf.h"

#include "config.h"
#include "buttons.h"
#include "joystick.h"
#include "led_matrix.h"
#include "oled.h"
#include "tuya.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Estados da aplicação ──────────────────────────────────────────── */
typedef enum {
    STATE_CONNECTING,
    STATE_IDLE,
    STATE_BRIGHTNESS,
    STATE_COLOR,
} app_state_t;

/* ── Estado global ─────────────────────────────────────────────────── */
typedef struct {
    app_state_t mode;

    bool     lamp_on;
    /* true quando a cor foi confirmada pelo usuario no modo COLOR.
     * Enquanto estiver true, ajustes de brilho reaplicam COLOR (nao WHITE). */
    bool     color_locked;
    uint8_t  brightness;    /* 0-100 % */

    /* Cor atual */
    uint16_t hue;           /* 0-359  */
    uint8_t  sat;           /* 0-100% */

    /* Cursor do seletor de cor */
    uint8_t  cx, cy;        /* 0-4    */

    /* Controle de tempo */
    uint32_t last_joy_ms;
    uint32_t last_hb_ms;
    uint32_t last_oled_ms;
    uint32_t last_matrix_ms;

    int      loading_frame;
} app_ctx_t;

static app_ctx_t ctx;

/* ── Helpers de estado ─────────────────────────────────────────────── */

static void apply_brightness(void) {
    if (!ctx.lamp_on) return;
    tuya_err_t r = tuya_set_brightness(ctx.brightness);
    if (r != TUYA_OK) printf("[Main] ERRO brilho: %d\n", r);
}

static void apply_color(void) {
    if (!ctx.lamp_on) return;
    uint8_t val = ctx.brightness;
    tuya_err_t r = tuya_set_color(ctx.hue, ctx.sat, val);
    if (r != TUYA_OK) printf("[Main] ERRO cor: %d\n", r);
}

static void toggle_power(void) {
    ctx.lamp_on = !ctx.lamp_on;
    tuya_err_t r = tuya_set_power(ctx.lamp_on);
    printf("[Main] Lâmpada: %s  result=%d\n", ctx.lamp_on ? "ON" : "OFF", r);

    /* Mostra erro no OLED por 2 s */
    if (r != TUYA_OK) {
        char buf[22];
        oled_clear();
        oled_draw_string(4,  8, "ERRO Tuya:");
        snprintf(buf, sizeof(buf), "cod=%d (TCP:%s)", r,
                 r == TUYA_ERR_CONNECT ? "sem conn" :
                 r == TUYA_ERR_SEND    ? "send fail" : "?");
        oled_draw_string(0, 24, buf);
        oled_draw_string(0, 40, "Veja serial USB");
        oled_update();
        sleep_ms(2000);
        ctx.last_oled_ms = 0;   /* força refresh da tela normal */
        /* Reverte estado local pois o comando não chegou */
        ctx.lamp_on = !ctx.lamp_on;
        return;
    }

    if (ctx.lamp_on) {
        if (ctx.mode == STATE_IDLE) {
            /* Regra de UX: no modo liga/desliga, OFF->ON volta para branco. */
            tuya_err_t rw = tuya_set_white(ctx.brightness, 50);
            if (rw != TUYA_OK) printf("[Main] ERRO white on power-on: %d\n", rw);
            ctx.color_locked = false;
        } else if (ctx.mode == STATE_COLOR) {
            apply_color();
        } else {
            apply_brightness();
        }
    }
}

/* Entra num novo modo e atualiza exibição imediatamente */
static void enter_mode(app_state_t new_mode) {
    ctx.mode = new_mode;
    ctx.last_oled_ms   = 0;   /* força refresh imediato */
    ctx.last_matrix_ms = 0;
    printf("[Main] Modo: %d\n", new_mode);
}

/* ── Conexão Wi-Fi ─────────────────────────────────────────────────── */
static bool wifi_connect(void) {
    printf("[WiFi] Conectando a '%s'...\n", WIFI_SSID);

    if (cyw43_arch_wifi_connect_timeout_ms(
            WIFI_SSID, WIFI_PASSWORD,
            CYW43_AUTH_WPA2_AES_PSK, 15000) != 0) {
        printf("[WiFi] Falha na conexao!\n");
        return false;
    }

    printf("[WiFi] Conectado! IP: %s\n",
           ip4addr_ntoa(netif_ip4_addr(netif_list)));
    return true;
}

/* ── Setup ─────────────────────────────────────────────────────────── */
static void app_init(void) {
    stdio_init_all();
    sleep_ms(200);
    printf("\n=== BitDogLab Smart Lamp Controller ===\n");

    /* Periféricos */
    buttons_init();
    joystick_init();
    matrix_init();
    oled_init();

    /* Estado inicial */
    ctx.mode       = STATE_CONNECTING;
    ctx.lamp_on    = false;
    ctx.color_locked = false;
    ctx.brightness = 70;
    ctx.hue        = 30;     /* laranja por padrão */
    ctx.sat        = 100;
    ctx.cx         = 0;
    ctx.cy         = 0;
    ctx.last_hb_ms = 0;
    ctx.loading_frame = 0;

    /* Mostra tela de conexão */
    oled_screen_connecting();
}

/* ── Loop: modo CONNECTING ─────────────────────────────────────────── */
static void loop_connecting(void) {
    uint32_t now = to_ms_since_boot(get_absolute_time());

    if (now - ctx.last_matrix_ms > 500) {
        ctx.last_matrix_ms = now;
        matrix_off();
    }
}

/* ── Loop: modo IDLE ───────────────────────────────────────────────── */
static void loop_idle(void) {
    uint32_t now = to_ms_since_boot(get_absolute_time());

    /* OLED: atualiza a cada 300ms */
    if (now - ctx.last_oled_ms > 300) {
        ctx.last_oled_ms = now;
        oled_screen_idle(ctx.lamp_on);
    }

    /* Matriz: mantem apagada em IDLE */
    if (now - ctx.last_matrix_ms > 500) {
        ctx.last_matrix_ms = now;
        matrix_off();
    }
}

/* ── Loop: modo BRIGHTNESS ─────────────────────────────────────────── */
static void loop_brightness(void) {
    uint32_t now = to_ms_since_boot(get_absolute_time());

    /* Ajuste por passos: direita aumenta, esquerda diminui.
     * O envio para Tuya so acontece no BTN_A (fixar), evitando spam. */
    if (now - ctx.last_joy_ms > 140) {
        ctx.last_joy_ms = now;
        joy_dir_t dir = joystick_dir();
        if (dir == JOY_RIGHT) {
            if (ctx.brightness < 100) {
                uint8_t step = (ctx.brightness > 94) ? (uint8_t)(100 - ctx.brightness) : 6;
                ctx.brightness = (uint8_t)(ctx.brightness + step);
            }
        } else if (dir == JOY_LEFT) {
            if (ctx.brightness > 0) {
                uint8_t step = (ctx.brightness < 6) ? ctx.brightness : 6;
                ctx.brightness = (uint8_t)(ctx.brightness - step);
            }
        }
    }

    /* OLED: atualiza a cada 100ms */
    if (now - ctx.last_oled_ms > 100) {
        ctx.last_oled_ms = now;
        oled_screen_brightness(ctx.brightness);
    }

    /* Matriz permanece apagada fora do modo de selecao de cor */
    if (now - ctx.last_matrix_ms > 150) {
        ctx.last_matrix_ms = now;
        matrix_off();
    }
}

/* ── Loop: modo COLOR ──────────────────────────────────────────────── */
static void loop_color(void) {
    uint32_t now = to_ms_since_boot(get_absolute_time());

    /* Lê joystick para mover o cursor */
    if (now - ctx.last_joy_ms > 200) {
        ctx.last_joy_ms = now;
        joy_dir_t dir = joystick_dir();
        bool moved = false;

        switch (dir) {
            case JOY_UP:
                if (ctx.cy > 0) { ctx.cy--; moved = true; } break;
            case JOY_DOWN:
                if (ctx.cy < MATRIX_ROWS - 1) { ctx.cy++; moved = true; } break;
            case JOY_LEFT:
                if (ctx.cx > 0) { ctx.cx--; moved = true; } break;
            case JOY_RIGHT:
                if (ctx.cx < MATRIX_COLS - 1) { ctx.cx++; moved = true; } break;
            default: break;
        }

        if (moved) {
            /* Atualiza cor de preview baseada na célula sob o cursor */
            matrix_cell_hsv(ctx.cy, ctx.cx, &ctx.hue, &ctx.sat);
            ctx.last_oled_ms   = 0;
            ctx.last_matrix_ms = 0;
        }
    }

    /* OLED */
    if (now - ctx.last_oled_ms > 120) {
        ctx.last_oled_ms = now;
        oled_screen_color(ctx.cx, ctx.cy, ctx.hue, ctx.sat);
    }

    /* Matriz fisica da placa deve permanecer apagada */
    if (now - ctx.last_matrix_ms > 120) {
        ctx.last_matrix_ms = now;
        matrix_off();
    }
}

/* ── Processamento de eventos de botão ─────────────────────────────── */
static void handle_button(btn_event_t ev) {
    switch (ev) {
        case BTN_A_SHORT:
            if (ctx.mode == STATE_COLOR) {
                /* Em modo cor: BTN_A aplica a cor selecionada */
                printf("[Main] Cor aplicada: H=%d S=%d\n", ctx.hue, ctx.sat);
                apply_color();
                ctx.color_locked = true;
                enter_mode(STATE_IDLE);
            } else if (ctx.mode == STATE_BRIGHTNESS) {
                /* Em brilho: A confirma/fixa o nivel atual.
                 * Se houver cor travada, reaplica COLOR com o novo brilho. */
                if (ctx.lamp_on) {
                    if (ctx.color_locked) apply_color();
                    else apply_brightness();
                }
                printf("[Main] Brilho fixado: %u%%\n", ctx.brightness);
            } else {
                /* Nos outros modos: BTN_A liga/desliga */
                toggle_power();
            }
            break;

        case BTN_A_LONG:
            /* Long press sempre desliga */
            if (ctx.lamp_on) {
                ctx.lamp_on = false;
                tuya_set_power(false);
            }
            break;

        case BTN_B_SHORT:
            /* Cicla entre modos */
            switch (ctx.mode) {
                case STATE_IDLE:       enter_mode(STATE_BRIGHTNESS); break;
                case STATE_BRIGHTNESS: enter_mode(STATE_COLOR);      break;
                case STATE_COLOR:      enter_mode(STATE_IDLE);       break;
                default: break;
            }
            break;

        case BTN_B_LONG:
            /* Long press em B: volta sempre ao IDLE */
            enter_mode(STATE_IDLE);
            break;

        default:
            break;
    }
}

/* ── Main ──────────────────────────────────────────────────────────── */
int main(void) {
    app_init();

    /* Inicializa Wi-Fi */
    if (cyw43_arch_init() != 0) {
        printf("[WiFi] Falha ao iniciar cyw43!\n");
        oled_clear();
        oled_draw_string(4, 24, "ERRO: Wi-Fi init");
        oled_update();
        while (true) tight_loop_contents();
    }

    cyw43_arch_enable_sta_mode();

    /* Tenta conectar */
    while (!wifi_connect()) {
        oled_clear();
        oled_draw_string(4, 20, "Wi-Fi falhou!");
        oled_draw_string(4, 34, "Tentando de novo");
        oled_update();
        sleep_ms(3000);
        oled_screen_connecting();
    }

    /* Wi-Fi conectado: sincroniza estado real da lampada e entra em IDLE */
    enter_mode(STATE_IDLE);
    {
        bool remote_on = false;
        tuya_err_t qr = tuya_get_power_state(&remote_on);
        if (qr == TUYA_OK) {
            ctx.lamp_on = remote_on;
            printf("[Main] Estado inicial lampada: %s\n", ctx.lamp_on ? "ON" : "OFF");
        } else {
            ctx.lamp_on = false;
            printf("[Main] Nao foi possivel ler estado inicial (err=%d). Assumindo OFF.\n", qr);
        }
    }
    matrix_off();

    printf("[Main] Pronto! Entrando no loop principal.\n");

    /* ── Loop principal ─────────────────────────────────────────────── */
    while (true) {
        uint32_t now = to_ms_since_boot(get_absolute_time());
        cyw43_arch_poll();

        /* Processa eventos de botão */
        btn_event_t ev = buttons_get_event();
        if (ev != BTN_NONE && ctx.mode != STATE_CONNECTING) {
            handle_button(ev);
        }

        /* Executa loop do modo atual */
        switch (ctx.mode) {
            case STATE_CONNECTING: loop_connecting(); break;
            case STATE_IDLE:       loop_idle();       break;
            case STATE_BRIGHTNESS: loop_brightness(); break;
            case STATE_COLOR:      loop_color();      break;
        }

        /* Heartbeat Tuya a cada HEARTBEAT_MS */
        if (ctx.mode != STATE_CONNECTING &&
            now - ctx.last_hb_ms > HEARTBEAT_MS) {
            ctx.last_hb_ms = now;
            tuya_heartbeat();
        }

        sleep_ms(10);   /* cede CPU ao lwIP */
    }

    cyw43_arch_deinit();
    return 0;
}
