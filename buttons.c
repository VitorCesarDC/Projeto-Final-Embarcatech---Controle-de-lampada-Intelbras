/*
 * buttons.c — Detecção de press curto/longo via GPIO IRQ (BitDogLab)
 */
#include "buttons.h"
#include "config.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include <stdbool.h>

/* ── Estado interno ─────────────────────────────────────────────────── */
typedef struct {
    bool     pressed;
    uint32_t press_time_ms;
} btn_state_t;

static volatile btn_state_t state_a = {false, 0};
static volatile btn_state_t state_b = {false, 0};
#define BTN_QUEUE_SIZE 8
static volatile btn_event_t event_queue[BTN_QUEUE_SIZE];
static volatile uint8_t event_head = 0;
static volatile uint8_t event_tail = 0;

static void queue_event(btn_event_t ev) {
    uint8_t next = (uint8_t)((event_head + 1u) % BTN_QUEUE_SIZE);
    if (next == event_tail) return;
    event_queue[event_head] = ev;
    event_head = next;
}

/* ── ISR ──────────────────────────────────────────────────────────────
 * Chamada em qualquer borda dos botões A e B.
 */
static void gpio_isr(uint gpio, uint32_t events) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    volatile btn_state_t *st = (gpio == BTN_A_PIN) ? &state_a : &state_b;

    if (events & GPIO_IRQ_EDGE_FALL) {
        /* Borda de descida: botão pressionado */
        if (!st->pressed) {
            st->pressed      = true;
            st->press_time_ms = now;
        }
    }
    if (events & GPIO_IRQ_EDGE_RISE) {
        /* Borda de subida: botão solto */
        if (st->pressed) {
            uint32_t dur = now - st->press_time_ms;
            st->pressed = false;
            if (dur < DEBOUNCE_MS) return;   /* ruído, ignora */

            btn_event_t ev;
            if (gpio == BTN_A_PIN)
                ev = (dur >= LONG_PRESS_MS) ? BTN_A_LONG : BTN_A_SHORT;
            else
                ev = (dur >= LONG_PRESS_MS) ? BTN_B_LONG : BTN_B_SHORT;

            queue_event(ev);
        }
    }
}

/* ── API pública ────────────────────────────────────────────────────── */
void buttons_init(void) {
    /* Botão A */
    gpio_init(BTN_A_PIN);
    gpio_set_dir(BTN_A_PIN, GPIO_IN);
    gpio_pull_up(BTN_A_PIN);
    gpio_set_irq_enabled_with_callback(BTN_A_PIN,
        GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true, gpio_isr);

    /* Botão B — compartilha o mesmo callback */
    gpio_init(BTN_B_PIN);
    gpio_set_dir(BTN_B_PIN, GPIO_IN);
    gpio_pull_up(BTN_B_PIN);
    gpio_set_irq_enabled(BTN_B_PIN,
        GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true);
}

btn_event_t buttons_get_event(void) {
    if (event_tail == event_head) return BTN_NONE;
    btn_event_t ev = event_queue[event_tail];
    event_tail = (uint8_t)((event_tail + 1u) % BTN_QUEUE_SIZE);
    return ev;
}

bool button_is_pressed(uint gpio) {
    return !gpio_get(gpio);   /* pull-up: LOW = pressionado */
}
