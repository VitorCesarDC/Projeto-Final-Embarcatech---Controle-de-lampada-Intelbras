#ifndef JOYSTICK_H
#define JOYSTICK_H
/*
 * joystick.h — Leitura do joystick analógico (ADC0, ADC1)
 */
#include <stdint.h>
#include <stdbool.h>

/* Direções detectadas */
typedef enum {
    JOY_CENTER = 0,
    JOY_UP,
    JOY_DOWN,
    JOY_LEFT,
    JOY_RIGHT,
} joy_dir_t;

void joystick_init(void);

/* Lê valor bruto (0-4095) dos eixos */
uint16_t joystick_x(void);
uint16_t joystick_y(void);

/* Retorna percentual 0-100 baseado no eixo Y (100 = cima, 0 = baixo) */
uint8_t joystick_y_pct(void);
/* Retorna percentual 0-100 baseado no eixo X (0 = esquerda, 100 = direita) */
uint8_t joystick_x_pct(void);

/* Retorna a direção predominante (com zona morta) */
joy_dir_t joystick_dir(void);

#endif /* JOYSTICK_H */
