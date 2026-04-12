#ifndef BUTTONS_H
#define BUTTONS_H
/*
 * buttons.h — Leitura de botões com interrupção + debounce (BitDogLab)
 *
 * Botão A (GPIO 5): liga/desliga a lâmpada
 * Botão B (GPIO 6): troca entre modos
 */
#include <stdint.h>
#include <stdbool.h>
#include "pico/types.h"

/* Tipo de evento de botão */
typedef enum {
    BTN_NONE       = 0,
    BTN_A_SHORT,       /* Pressão curta  em A */
    BTN_A_LONG,        /* Pressão longa  em A */
    BTN_B_SHORT,       /* Pressão curta  em B */
    BTN_B_LONG,        /* Pressão longa  em B */
} btn_event_t;

void buttons_init(void);

/* Retorna o evento mais recente (consome o evento — retorna NONE na próxima chamada) */
btn_event_t buttons_get_event(void);

/* Retorna true se o botão especificado está pressionado agora */
bool button_is_pressed(uint gpio);

#endif /* BUTTONS_H */
