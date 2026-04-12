/*
 * joystick.c - Joystick analogico via ADC (BitDogLab / RP2040)
 */
#include "joystick.h"
#include "config.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"
#include <stdlib.h>

#define ADC_MAX   4095
#define ADC_MID   2048

void joystick_init(void) {
    adc_init();
    adc_gpio_init(JOY_X_PIN);   /* ADC0 */
    adc_gpio_init(JOY_Y_PIN);   /* ADC1 */
}

uint16_t joystick_x(void) {
    adc_select_input(0);         /* ADC0 = GPIO26 */
    return adc_read();
}

uint16_t joystick_y(void) {
    adc_select_input(1);         /* ADC1 = GPIO27 */
    return adc_read();
}

uint8_t joystick_y_pct(void) {
    uint16_t raw = joystick_y();
    if (raw > ADC_MAX) raw = ADC_MAX;
    /* Cima = maior brilho; nesta placa o eixo Y ja esta nesse sentido. */
    {
        int pct = 100 - (int)((uint32_t)raw * 100 / ADC_MAX);
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        return (uint8_t)pct;
    }
}

uint8_t joystick_x_pct(void) {
    uint16_t raw = joystick_x();
    /* Corrige esquerda/direita invertidos para o seletor de brilho. */
    raw = (uint16_t)(ADC_MAX - raw);
    if (raw > ADC_MAX) raw = ADC_MAX;
    return (uint8_t)((uint32_t)raw * 100 / ADC_MAX);
}

joy_dir_t joystick_dir(void) {
    uint16_t x = joystick_x();
    uint16_t y = joystick_y();

    /* Na placa, os eixos fisicos chegam trocados no ADC:
     * - horizontal vem de Y
     * - vertical vem de X
     * Alem disso, os sinais chegam invertidos, por isso ADC_MID - valor.
     */
    int dx = ADC_MID - (int)y;  /* direita positivo */
    int dy = ADC_MID - (int)x;  /* cima positivo    */
    int dead = JOY_DEADZONE;

    if (abs(dx) >= abs(dy)) {
        if (dx > dead)  return JOY_RIGHT;
        if (dx < -dead) return JOY_LEFT;
    } else {
        if (dy > dead)  return JOY_UP;
        if (dy < -dead) return JOY_DOWN;
    }
    return JOY_CENTER;
}
