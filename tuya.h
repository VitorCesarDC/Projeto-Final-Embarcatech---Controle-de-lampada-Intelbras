#ifndef TUYA_H
#define TUYA_H
/*
 * tuya.h — Protocolo Tuya Local v3.3 (TCP + AES-128-ECB)
 *
 * Permite controlar diretamente a lâmpada Intelbras (EWS 410)
 * na rede local sem passar pela nuvem.
 *
 * Pré-requisito: obter DEVICE_ID e LOCAL_KEY em iot.tuya.com
 */
#include <stdint.h>
#include <stdbool.h>

/* Resultado das operações Tuya */
typedef enum {
    TUYA_OK      =  0,
    TUYA_ERR_SOCKET = -1,
    TUYA_ERR_CONNECT = -2,
    TUYA_ERR_SEND    = -3,
    TUYA_ERR_ENCRYPT = -4,
} tuya_err_t;

/* ── API pública ───────────────────────────────────────────────────── */

/* Liga ou desliga a lâmpada */
tuya_err_t tuya_set_power(bool on);

/* Ajusta o brilho (1-100%) */
tuya_err_t tuya_set_brightness(uint8_t pct);

/*
 * Define a cor (modo "colour").
 *   hue: 0-359
 *   sat: 0-100%
 *   val: 0-100%
 */
tuya_err_t tuya_set_color(uint16_t hue, uint8_t sat, uint8_t val);

/* Volta para modo branco e define temperatura de cor (0=quente, 100=frio) */
tuya_err_t tuya_set_white(uint8_t brightness_pct, uint8_t temp_pct);

/* Le estado atual do DP de liga/desliga (true=ligada, false=desligada). */
tuya_err_t tuya_get_power_state(bool *on);

/* Envia heartbeat (keepalive) — chamar a cada ~10s */
tuya_err_t tuya_heartbeat(void);

#endif /* TUYA_H */
